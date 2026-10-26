"""Shared engine for the OpenAI-compatible API providers.

``lms`` (a local LM Studio server) and ``openai`` (api.openai.com, or any hosted
OpenAI-compatible endpoint through ``OPENAI_BASE_URL``) are this package's first
**API** providers: instead of replaying a site's signed-in web session they
speak the OpenAI chat-completions protocol over HTTP.

That changes four things the web adapters take for granted:

* **No session.** ``needs_session = False``, so the CLI never asks
  ``browser.session`` for cookies and ``match`` stays empty.
* **No server-side chat id.** Chat completions are stateless, so ``-c``/``-i``
  replay the stored transcript (:mod:`ai.state`) as a real ``messages`` array.
* **No upload endpoint.** ``-f`` files travel *inside* the message — text files
  as fenced blocks behind a filename header, images as base64 ``image_url``
  parts — and an oversized prompt is sent inline (``--prompt-file`` is
  refused with a hint instead of silently doing nothing).
* **No sidebar history.** ``recordable = False`` keeps ``ai.save`` out of it.

Subclasses only supply configuration (base URL, key, model discovery); the
request shape, the SSE parsing and the error remedies live here once.
"""

from __future__ import annotations

import base64
import json
import mimetypes
import os
import re

from .. import events as ev
from .. import files as files_mod
from .. import net
from . import Conversation, Provider

#: Appended to the API root for a turn and for the model list.
CHAT_PATH = "/chat/completions"
MODELS_PATH = "/models"

#: Transcript turns replayed for ``-c``/``-i``. The API is stateless, so the
#: whole conversation travels every turn; this bounds a long REPL session.
MAX_REPLAY = 40

#: Inline text this large is refused: without an upload path the bytes end up
#: in one JSON body, and a clear message beats a huge request.
MAX_INLINE = 16 * 1024 * 1024

#: MIME types whose bytes are text even though they are not ``text/*``.
TEXT_TYPES = frozenset({
    "application/json", "application/xml", "application/yaml",
    "application/x-yaml", "application/toml", "application/javascript",
    "application/x-sh", "application/x-python", "application/sql",
    "application/ld+json", "image/svg+xml",
})

#: Extensions that are text even when ``mimetypes`` does not know them.
TEXT_EXTENSIONS = frozenset({
    ".c", ".h", ".hpp", ".cpp", ".cc", ".cxx", ".py", ".sh", ".bash",
    ".md", ".txt", ".rst", ".toml", ".ini", ".cfg", ".conf", ".csv", ".tsv",
    ".json", ".jsonl", ".yaml", ".yml", ".xml", ".html", ".css", ".js",
    ".ts", ".tsx", ".jsx", ".sql", ".diff", ".patch", ".log", ".env",
    ".cmake", ".mk", ".gradle", ".rs", ".go", ".java", ".kt", ".rb", ".pl",
    ".lua", ".php", ".vim", ".el", ".fish", ".zsh", ".nix", ".proto",
})


class ApiError(Exception):
    """A provider is misconfigured, unreachable or has no usable model."""


def normalise_base(url):
    """Normalise an API root: a scheme, no trailing slash, an explicit ``/v1``.

    LM Studio (and most OpenAI-compatible servers) answer on ``<root>/v1``, but
    people paste the root from the app's status line, so a URL without an
    ``/v1`` suffix gets one. ``https://openrouter.ai/api/v1`` is left alone.
    """
    text = (url or "").strip().rstrip("/")
    if not text:
        raise ApiError("no API endpoint configured")
    if "://" not in text:
        text = "http://" + text
    if not re.search(r"/v\d+$", text):
        text += "/v1"
    return text


def _as_int(value):
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def _as_text(attachment):
    """The attachment's bytes as text, or ``None`` when it is not text at all."""
    mime = (attachment.mime or "").split(";")[0].strip().lower()
    name = (attachment.name or "").lower()
    guessed = mimetypes.guess_type(name)[0] or ""
    extension = os.path.splitext(name)[1]
    if not (mime.startswith("text/") or mime in TEXT_TYPES
            or guessed in TEXT_TYPES or extension in TEXT_EXTENSIONS):
        return None
    try:
        text = attachment.data.decode("utf-8")
    except UnicodeDecodeError:
        return None
    if "\x00" in text:
        return None
    return text


def _parse_params(pairs):
    """``--param KEY=VALUE`` pairs as a request body, JSON-decoding the values."""
    params = {}
    for pair in pairs:
        name, separator, value = str(pair).partition("=")
        if not separator or not name.strip():
            raise ApiError(f"--param wants KEY=VALUE, got {pair!r}")
        text = value.strip()
        try:
            params[name.strip()] = json.loads(text)
        except ValueError:
            params[name.strip()] = text
    return params


def _error_text(error):
    """The human message inside an ``{"error": …}`` payload."""
    if isinstance(error, dict):
        message = error.get("message") or error.get("error")
        if message:
            return str(message)
        return json.dumps(error, ensure_ascii=False)
    return str(error)


class _Conversation(Conversation):
    """One stateless OpenAI-compatible chat (the transcript travels every turn)."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume, options=None):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        options = dict(options or {})
        self.provider = provider
        self.base_url = provider.base_url(options.get("base_url"))
        self.api_key = options.get("api_key")
        self.params = _parse_params(options.get("params") or [])
        try:
            self.model = provider.resolve_model(model, self.base_url)
        except ApiError:
            raise
        except Exception as exc:  # noqa: BLE001 - a lookup failure is a config error
            raise ApiError(provider.error_hint(str(exc), self.base_url)) from exc

    # -- request body ------------------------------------------------------

    def _messages(self, content):
        messages = []
        if self.turns == 0 and self.system:
            messages.append({"role": "system", "content": self.system})
        for turn in self.history[-MAX_REPLAY:]:
            role = turn.get("role")
            text = turn.get("text") or ""
            if role in ("user", "assistant") and text:
                messages.append({"role": role, "content": text})
        messages.append({"role": "user", "content": content})
        return messages

    def _content(self, prompt, attachments):
        """The user message content, plus the events that announced its files."""
        parts = []
        blocks = []
        for attachment in attachments:
            if attachment.mime.startswith("image/"):
                encoded = base64.b64encode(attachment.data).decode("ascii")
                parts.append({
                    "type": "image_url",
                    "image_url": {"url": f"data:{attachment.mime};base64,{encoded}"},
                })
            else:
                text = _as_text(attachment)
                if text is None:
                    raise ApiError(
                        f"{attachment.name}: {self.provider.label} takes text and "
                        f"image attachments inline; convert it to text, embed it "
                        f"in the chain with `.files`, or mention its path")
                if len(text.encode("utf-8")) > MAX_INLINE:
                    raise ApiError(
                        f"{attachment.name}: larger than {MAX_INLINE // (1024 * 1024)} "
                        f"MiB and this provider has no upload endpoint; split or "
                        f"trim it first")
                blocks.append(f"File: {attachment.name}\n```\n{text.rstrip()}\n```")
        message = "\n\n".join(part for part in [prompt, *blocks] if part)
        names = [attachment.name for attachment in attachments]
        if not parts:
            return message, names
        return [{"type": "text", "text": message}, *parts], names

    # -- one turn ----------------------------------------------------------

    def send(self, prompt, files=None):
        try:
            attachments = self._attachments(files)
            content, announced = self._content(prompt, attachments)
        except (files_mod.FileError, ApiError) as exc:
            yield ev.error(str(exc))
            return
        for name in announced:
            # Inline attachments are not uploaded, but the user should still see
            # what went into the message (and `--tty` renders it like an upload).
            yield ev.tool_call(name="attach", status="done", args=name)
        body = {"model": self.model, "stream": True,
                "messages": self._messages(content)}
        body.update(self.params)
        headers = self.provider.headers(self.api_key)
        answer = []
        try:
            for payload in net.sse_payloads(net.stream_lines(
                    self.provider.chat_url(self.base_url), headers=headers,
                    json_body=body, impersonate=None, timeout=self.timeout,
                    debug=self.debug)):
                if payload == "[DONE]":
                    break
                try:
                    data = json.loads(payload)
                except ValueError:
                    yield ev.raw(payload)
                    continue
                if not isinstance(data, dict):
                    continue
                if data.get("error"):
                    yield ev.error(self.provider.error_hint(
                        _error_text(data["error"]), self.base_url))
                    return
                for choice in data.get("choices") or ():
                    delta = (choice or {}).get("delta") or {}
                    reasoning = delta.get("reasoning_content") or delta.get("reasoning")
                    if reasoning:
                        yield ev.thinking(reasoning)
                    text = delta.get("content")
                    if isinstance(text, list):
                        text = "".join(part.get("text") or ""
                                       for part in text if isinstance(part, dict))
                    if text:
                        answer.append(text)
                        yield ev.text(text)
                    for call in delta.get("tool_calls") or ():
                        function = (call or {}).get("function") or {}
                        yield ev.tool_call(name=function.get("name") or "",
                                           status="delta",
                                           args=function.get("arguments") or "")
                usage = data.get("usage")
                if isinstance(usage, dict) and usage:
                    yield ev.usage(**usage)
        except Exception as exc:  # noqa: BLE001 - any transport failure is the answer
            yield ev.error(self.provider.error_hint(str(exc), self.base_url))
            return
        self.turns += 1
        self._record("user", prompt)
        self._record("assistant", "".join(answer))
        yield ev.done()


class OpenAICompat(Provider):
    """An OpenAI-compatible chat-completions endpoint.

    Subclasses set the environment variable names and either a fixed
    ``default_base_url`` or a discovery hook (:class:`ai.providers.lms.Lms`).
    """

    needs_session = False
    recordable = False
    prompt_file_upload = False
    api = True
    #: Whether ``-m`` may be omitted because the endpoint is asked for a model
    #: (:class:`ai.providers.lms.Lms` picks the loaded/first chat model).
    discovers_model = False
    #: The API root is ``<base>/v1``; the model list is ``<base>/v1/models``.
    base_url_env = ""
    key_env = ""
    model_env = ""
    default_base_url = ""

    # -- configuration -----------------------------------------------------

    def base_url(self, requested=None):
        """The API root for this invocation (``…/v1``)."""
        explicit = requested or os.environ.get(self.base_url_env) or ""
        url = explicit.strip() or self.default_base_url
        if not url:
            raise ApiError(f"no API endpoint configured; pass --base-url or set "
                           f"{self.base_url_env}")
        return normalise_base(url)

    def api_key(self, requested=None):
        return (requested or os.environ.get(self.key_env) or "").strip()

    def headers(self, key=None):
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        token = self.api_key(key)
        if token:
            headers["Authorization"] = "Bearer " + token
        return headers

    @staticmethod
    def chat_url(base):
        return base + CHAT_PATH

    # -- models ------------------------------------------------------------

    def list_models(self, base=None, timeout=5):
        """The endpoint's models as ``{id, type, state, context}`` records."""
        payload = net.request_json((base or self.base_url()) + MODELS_PATH,
                                   headers=self.headers(), impersonate=None,
                                   timeout=timeout)
        records = []
        for item in (payload or {}).get("data") or ():
            if not isinstance(item, dict) or not item.get("id"):
                continue
            records.append({
                "id": str(item["id"]),
                "type": str(item.get("type") or ""),
                "state": str(item.get("state") or ""),
                "context": _as_int(item.get("max_context_length")),
            })
        return records

    def chat_models(self, base=None, timeout=5):
        """The models that can answer a chat turn (embedding models dropped)."""
        return [model for model in self.list_models(base, timeout)
                if "embed" not in model["type"].lower()
                and "embed" not in model["id"].lower()]

    def resolve_model(self, requested=None, base=None):
        """The model id to send, or :class:`ApiError` when none is configured."""
        wanted = (requested or os.environ.get(self.model_env) or "").strip()
        if wanted:
            return wanted
        raise ApiError(f"choose a model with -m/--model or {self.model_env}=… "
                       f"(`ai.{self.name} --list` shows what the endpoint offers)")

    def available_models(self):
        """Model ids for ``--list`` (best effort: a listing must not fail)."""
        try:
            return [model["id"] for model in self.chat_models()] or ["(none)"]
        except ApiError as exc:
            return [f"(unavailable: {exc})"]
        except Exception as exc:  # noqa: BLE001 - listing is informational
            return [f"(unavailable: {exc})"]

    # -- reporting ---------------------------------------------------------

    def session_descriptor(self, options=None):
        """What ``--print-session`` shows for a session-less provider."""
        options = dict(options or {})
        report = {"provider": self.name, "base_url": "", "model": "",
                  "reachable": False, "models": []}
        try:
            base = self.base_url(options.get("base_url"))
            report["base_url"] = base
            models = self.list_models(base, timeout=2)
            report["reachable"] = True
            report["models"] = [model["id"] for model in models]
            report["model"] = self.resolve_model(options.get("model"), base)
        except ApiError as exc:
            report["error"] = str(exc)
        except Exception as exc:  # noqa: BLE001 - a diagnostic never fails
            report["error"] = str(exc)
        return report

    # -- errors ------------------------------------------------------------

    def error_hint(self, message, base):
        """Add a concrete remedy to a transport or model error."""
        text = str(message)
        lowered = text.lower()
        model_problem = any(marker in lowered for marker in (
            "not found", "not loaded", "does not exist", "invalid model",
            "no such model", "model_not_found", "unknown model"))
        if model_problem and "model" in lowered:
            try:
                names = ", ".join(model["id"] for model in self.chat_models(base, timeout=2))
            except Exception:  # noqa: BLE001 - the hint is optional
                names = ""
            if names:
                text += f"\nAvailable models: {names}"
        return text

    # -- conversation ------------------------------------------------------

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None,
                          options=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume, options)
