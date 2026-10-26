"""LM Studio adapter — a local OpenAI-compatible server.

LM Studio's Local Server speaks the OpenAI protocol, so a turn is a plain
``POST /v1/chat/completions`` SSE request: no browser, no cookies, no API key
(unless the server was started with one) and no proof-of-work. Its REST endpoint
``/api/v0/models`` adds what the OpenAI list lacks — whether a model is an LLM, a
VLM or an embedding model, whether it is currently loaded, and its context
length — so ``-m`` can default to a model that can actually answer.

The server is frequently *not* on 1234 (LM Studio remembers the last port you
picked). Discovery therefore goes: ``--base-url`` → ``LMS_BASE_URL`` → the cached
endpoint (re-verified) → a short concurrent probe of the usual local ports →
``lms status`` → a message naming everything that was tried.

Prompts and ``-f`` files are inlined (see :mod:`ai.providers.openai_compat`);
LM Studio exposes no chat-history API, hence ``recordable = False``.
"""

from __future__ import annotations

import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import time

from .. import net
from .. import state
from .openai_compat import ApiError, OpenAICompat, normalise_base

#: Ports a local OpenAI-compatible server usually listens on: LM Studio, then
#: the llama.cpp/vLLM and Ollama defaults.
PROBE_PORTS = (1234, 1337, 8080, 8000, 11434)

#: The host spellings worth trying: IPv4 explicitly, then whatever ``localhost``
#: resolves to (which may be IPv6-only on some systems).
PROBE_HOSTS = ("127.0.0.1", "localhost")

#: A probe is a connect + one small GET, so it must fail fast.
PROBE_TIMEOUT = 0.4

#: The endpoint discovered last time, so later runs skip the probe.
CACHE_NAME = "lms-endpoint"

#: LM Studio's richer model list (loaded/unloaded, type, context length).
V0_MODELS_PATH = "/api/v0/models"

_ANSI = re.compile(r"\x1b\[[0-9;]*m")
_PORT = re.compile(r"port\s*:?\s*(\d+)", re.IGNORECASE)


class Lms(OpenAICompat):
    name = "lms"
    label = "LM Studio"
    origin = "http://localhost:1234"
    base_url_env = "LMS_BASE_URL"
    key_env = "LMS_API_KEY"
    model_env = "LMS_MODEL"
    #: ``-m`` is optional: the server is asked which chat model it has.
    discovers_model = True
    #: No fixed default: the port is whatever LM Studio (or another local
    #: server) is listening on, which discovery works out.
    default_base_url = ""

    #: Probe order: preference, not completion order (a 1234 next to a 1337
    #: server wins, as it is the documented default).
    probe_urls = tuple(f"http://{host}:{port}"
                       for port in PROBE_PORTS for host in PROBE_HOSTS)

    # -- endpoint discovery ------------------------------------------------

    def base_url(self, requested=None):
        explicit = (requested or os.environ.get(self.base_url_env) or "").strip()
        if explicit:
            return normalise_base(explicit)
        cached = _cached_endpoint()
        if cached and self._alive(cached):
            return normalise_base(cached)
        found = self._probe()
        if found is None:
            from_cli = self._cli_endpoint()
            if from_cli and self._alive(from_cli):
                found = from_cli
        if found is None:
            raise ApiError(self._unreachable())
        endpoint = normalise_base(found)
        _remember_endpoint(endpoint)
        return endpoint

    def _alive(self, base):
        """Whether *base* answers the OpenAI model list."""
        try:
            net.request_json(normalise_base(base) + "/models",
                             headers=self.headers(), impersonate=None,
                             timeout=PROBE_TIMEOUT)
            return True
        except Exception:  # noqa: BLE001 - a probe failure just means "not here"
            return False

    def _probe(self):
        urls = list(self.probe_urls)
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(urls)) as pool:
            futures = {url: pool.submit(self._alive, url) for url in urls}
            for url in urls:  # wait in preference order, not completion order
                try:
                    if futures[url].result():
                        return url
                except Exception:  # noqa: BLE001 - a broken probe is a miss
                    continue
        return None

    @staticmethod
    def _cli_endpoint():
        """The port ``lms status`` reports, for a server off the usual ports."""
        executable = shutil.which("lms")
        if not executable:
            return ""
        try:
            done = subprocess.run([executable, "status"], capture_output=True,
                                  text=True, timeout=10)
        except (OSError, subprocess.SubprocessError):
            return ""
        match = _PORT.search(_ANSI.sub("", done.stdout + done.stderr))
        if not match:
            return ""
        return f"http://127.0.0.1:{match.group(1)}/v1"

    def _unreachable(self):
        return ("no LM Studio server found: probed " + ", ".join(self.probe_urls)
                + " and `lms status`.\nStart it with `lms server start` (or the "
                  "app's Developer → Local Server), or pass --base-url / set "
                + self.base_url_env + ".")

    # -- models ------------------------------------------------------------

    def list_models(self, base=None, timeout=5):
        base = base or self.base_url()
        root = base.removesuffix("/v1")
        try:
            payload = net.request_json(root + V0_MODELS_PATH,
                                       headers=self.headers(), impersonate=None,
                                       timeout=timeout)
        except Exception:  # noqa: BLE001 - fall back to the OpenAI list
            return super().list_models(base, timeout)
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
        if not records:
            return super().list_models(base, timeout)
        return records

    def resolve_model(self, requested=None, base=None):
        wanted = (requested or os.environ.get(self.model_env) or "").strip()
        if wanted:
            return wanted
        models = self.chat_models(base)
        loaded = [model["id"] for model in models if model["state"] == "loaded"]
        if loaded:
            return loaded[0]
        if models:
            # Not loaded is fine: LM Studio loads it just in time for the turn.
            return models[0]["id"]
        raise ApiError("no chat model is available on the LM Studio server; load "
                       "one with `lms load <model>` (or in the app) or pass "
                       "-m/--model")

    def available_models(self):
        try:
            models = self.chat_models()
        except ApiError as exc:
            return [f"(unavailable: {exc})"]
        except Exception as exc:  # noqa: BLE001 - listing is informational
            return [f"(unavailable: {exc})"]
        if not models:
            return ["(no chat models loaded)"]
        return [f"{model['id']} (loaded)" if model["state"] == "loaded"
                else model["id"] for model in models]

    # -- errors ------------------------------------------------------------

    def error_hint(self, message, base):
        text = super().error_hint(message, base)
        lowered = text.lower()
        if any(marker in lowered for marker in
               ("connect", "refused", "timed out", "timeout", "unreachable")):
            text += (f"\nCheck that LM Studio's server is running (`lms server "
                     f"start`) and that {base} is the right endpoint; --base-url "
                     f"and {self.base_url_env} set it.")
        return text


def _as_int(value):
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


# -- the endpoint cache -----------------------------------------------------


def _cached_endpoint():
    try:
        with open(state.path(CACHE_NAME), encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return ""
    if not isinstance(data, dict):
        return ""
    return str(data.get("base_url") or "")


def _remember_endpoint(base):
    """Remember a discovered endpoint (best effort, like the state files)."""
    path = state.path(CACHE_NAME)
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        temporary = path + ".tmp"
        with open(temporary, "w", encoding="utf-8") as handle:
            json.dump({"base_url": base, "updated": time.time()}, handle)
        os.replace(temporary, path)
    except OSError:
        pass
