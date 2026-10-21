"""ChatGPT (chatgpt.com) adapter.

Replays the site's own conversation endpoints with the signed-in cookies: the
``/api/auth/session`` access token, the sentinel gate (requirements token,
proof-of-work and turnstile — see :mod:`ai.sentinel`) and the ``f/conversation``
conduit.  A turn is answered either on the ``f/conversation`` SSE stream or,
when the site hands the turn off to its WebSocket, by reading
``GET /backend-api/conversation/<id>`` until the assistant message completes.

A conversation is continued by sending the next user message with the stored
``conversation_id`` and ``parent_message_id`` set to the previous assistant
message's id (verified: a follow-up in the same conversation answers with the
earlier context).

Any message can carry ``--file`` attachments: each is created with
``POST /backend-api/files``, written to the returned pre-signed blob URL, marked
uploaded, and then referenced from the message (images as
``image_asset_pointer`` parts, documents as ``metadata.attachments`` entries).

.. note::
   The request shapes, the token chain and the hand-off behaviour are confirmed
   with ``bin/browser.record https://chatgpt.com`` plus the site's own client
   bundle; the parsers stay tolerant so an unfamiliar frame surfaces via
   ``--debug`` instead of aborting the stream.
"""

from __future__ import annotations

import json
import sys
import time
import uuid

from .. import events as ev
from .. import files as files_mod
from .. import history
from .. import net
from .. import sentinel
from .. import session as session_mod
from . import Conversation, Provider

BASE = "https://chatgpt.com"
CONVERSATIONS_PATH = "/backend-api/conversations"
FILES_PATH = "/backend-api/files"
POLL_INTERVAL = 0.5
ROOT_MESSAGE = "client-created-root"


def _asset_pointer(file_id):
    """The pointer a message part uses for an uploaded file id."""
    scheme = "file-service://" if file_id.startswith("file-") else "sediment://"
    return scheme + file_id


class ChatGPT(Provider):
    name = "chatgpt"
    label = "ChatGPT"
    origin = BASE
    session_target = "chatgpt.com"
    default_model = "auto"
    models = ("auto", "gpt-5", "gpt-5-mini", "gpt-4o", "gpt-4o-mini", "o3", "o4-mini")
    #: The conversation endpoint rejects a single message at ~30k tokens
    #: (HTTP 413, empty body): 60 KB of CJK was accepted, 90 KB refused.
    #: 48 KiB keeps a margin for denser prompts.
    inline_limit = 48 * 1024

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)

    # ----------------------------------------------------------------- requests

    def _headers(self, match, extra=None):
        device = (session_mod.storage_value(match, "oai-did")
                  or str(uuid.uuid4()))
        headers = session_mod.build_headers(match, {
            "Accept": "*/*",
            "Content-Type": "application/json",
            "Origin": self.origin,
            "Referer": self.origin + "/",
            "oai-did": device,
            "OAI-Device-Id": device,
            "oai-language": "en-US",
            "OAI-Language": "en-US",
        })
        if extra:
            headers.update(extra)
        return headers

    @staticmethod
    def _target(headers, path):
        return {**headers, "X-OpenAI-Target-Path": path,
                "X-OpenAI-Target-Route": path}

    def _authorize(self, headers):
        session = net.request_json(self.origin + "/api/auth/session",
                                   headers=headers)
        token = (session or {}).get("accessToken")
        if not token:
            raise sentinel.SentinelError(
                "the session is not signed in (no access token); open "
                "chatgpt.com once in the browser")
        headers["Authorization"] = "Bearer " + token
        account = (session.get("account") or {}).get("id")
        if account:
            headers["chatgpt-account-id"] = account
            headers["ChatGPT-Account-ID"] = account

    # -- history (used by ai.save) ----------------------------------------

    def history_headers(self, match):
        """Authenticated REST headers, resolved once per sync."""
        cached = getattr(self, "_history_headers", None)
        if cached is not None and getattr(self, "_history_match", None) is match:
            return dict(cached)
        headers = self._headers(match)
        self._authorize(headers)
        self._history_headers = headers
        self._history_match = match
        return dict(headers)

    def iter_chat_summaries(self, match, limit=None):
        """Walk ``/backend-api/conversations`` newest-first."""
        headers = self.history_headers(match)
        offset = 0
        count = 0
        while True:
            url = (f"{self.origin}{CONVERSATIONS_PATH}"
                   f"?offset={offset}&limit=100&order=updated")
            data = net.request_json(url, headers=headers, timeout=120) or {}
            items = data.get("items") or []
            if not items:
                return
            for item in items:
                identifier = item.get("id") or ""
                if not identifier:
                    continue
                yield history.ChatSummary(
                    provider=self.name,
                    remote_id=identifier,
                    title=item.get("title") or "",
                    created=history.iso_from_epoch(item.get("create_time")),
                    updated=history.iso_from_epoch(item.get("update_time")),
                    url=f"{self.origin}/c/{identifier}",
                )
                count += 1
                if limit and count >= limit:
                    return
            offset += len(items)
            total = data.get("total") or 0
            if total and offset >= total:
                return

    def fetch_chat(self, match, remote_id):
        """Fetch one conversation and walk its active branch."""
        headers = self.history_headers(match)
        data = net.request_json(
            f"{self.origin}/backend-api/conversation/{remote_id}",
            headers=headers, timeout=180) or {}
        chat = history.Chat(
            provider=self.name,
            remote_id=remote_id,
            title=data.get("title") or "",
            created=history.iso_from_epoch(data.get("create_time")),
            updated=history.iso_from_epoch(data.get("update_time")),
            model=data.get("default_model_slug") or "",
            url=f"{self.origin}/c/{remote_id}",
        )
        for message in _active_messages(data):
            chat.messages.extend(_history_messages(message))
            model = (message.get("metadata") or {}).get("model_slug")
            if model and (message.get("author") or {}).get("role") == "assistant":
                chat.model = chat.model or model
        if not chat.created and chat.messages:
            chat.created = chat.messages[0].created
        if not chat.updated:
            chat.updated = history.newest(m.created for m in chat.messages)
        return chat

    def _bootstrap(self, headers):
        """Return ``(script_source, data_build)`` from the app shell."""
        try:
            html = net.request_text(self.origin + "/", headers={
                **headers, "Accept": "text/html,application/xhtml+xml"})
        except net.StreamError:
            return None, ""
        build = ""
        marker = "data-build="
        start = html.find(marker)
        if start != -1:
            value = start + len(marker)
            quote = html[value:value + 1]
            if quote in "\"'":
                end = html.find(quote, value + 1)
                if end != -1:
                    build = html[value + 1:end]
        script = None
        if "/backend-api/sentinel/sdk.js" in html:
            script = self.origin + "/backend-api/sentinel/sdk.js"
        return script, build

    def _sentinel_headers(self, headers, fingerprint, debug=False):
        p_token = sentinel.requirements_token(fingerprint)
        path = "/backend-api/sentinel/chat-requirements/prepare"
        prepared = net.request_json(
            self.origin + path, method="POST",
            headers=self._target(headers, path),
            json_body={"p": p_token})
        prepared = prepared or {}
        prepare_token = prepared.get("prepare_token") or ""

        proof = ""
        info = prepared.get("proofofwork") or {}
        if info.get("required"):
            if debug:
                print(f"[sentinel] solving proof-of-work (difficulty "
                      f"{info.get('difficulty')})", file=sys.stderr)
            proof = sentinel.solve_proof(
                info.get("seed", ""), info.get("difficulty", ""), fingerprint)
            if debug:
                print("[sentinel] proof-of-work solved", file=sys.stderr)

        turnstile = ""
        info = prepared.get("turnstile") or {}
        if info.get("required") and info.get("dx"):
            if debug:
                print("[sentinel] solving turnstile", file=sys.stderr)
            turnstile = sentinel.solve_turnstile(info["dx"], p_token) or ""

        path = "/backend-api/sentinel/chat-requirements/finalize"
        finalized = net.request_json(
            self.origin + path, method="POST",
            headers=self._target(headers, path),
            json_body={"prepare_token": prepare_token, "proofofwork": proof,
                       "turnstile": turnstile})
        finalized = finalized or {}
        token = finalized.get("token") or ""
        if not token:
            raise sentinel.SentinelError(
                "the sentinel gate returned no chat-requirements token")
        result = {"token": token,
                  "headers": {"OpenAI-Sentinel-Chat-Requirements-Token": token}}
        if proof:
            result["headers"]["OpenAI-Sentinel-Proof-Token"] = proof
        if turnstile:
            result["headers"]["OpenAI-Sentinel-Turnstile-Token"] = turnstile
        if finalized.get("so_token"):
            result["headers"]["OpenAI-Sentinel-SO-Token"] = finalized["so_token"]
        return result

    # ------------------------------------------------------------------ uploads

    def _upload(self, headers, attachment):
        """Upload one attachment; return the descriptor the message needs."""
        path = FILES_PATH
        prepared = net.request_json(
            self.origin + path, method="POST",
            headers=self._target(headers, path),
            json_body={
                "file_name": attachment.name,
                "file_size": attachment.size,
                "mime_type": attachment.mime,
                "client_resolved_mime_type": attachment.mime,
                "mime_resolution_source": "client",
                "use_case": "multimodal",
                "timezone_offset_min": self._timezone()[0],
                "reset_rate_limits": False,
            }, timeout=120) or {}
        file_id = prepared.get("file_id")
        upload_url = prepared.get("upload_url")
        if not file_id or not upload_url:
            raise net.StreamError(f"upload failed: {str(prepared)[:300]}")
        status, body = net.request_raw(
            upload_url, method="PUT",
            headers={
                "Content-Type": attachment.mime,
                "x-ms-blob-type": "BlockBlob",
                "x-ms-blob-content-type": attachment.mime,
                "x-ms-version": "2020-04-08",
            },
            data=attachment.data, timeout=600)
        if status >= 400:
            detail = body.decode("utf-8", "replace")[:200] if body else ""
            raise net.StreamError(
                f"uploading {attachment.name} failed: HTTP {status} {detail}")
        path = f"{FILES_PATH}/{file_id}/uploaded"
        net.request_json(self.origin + path, method="POST",
                         headers=self._target(headers, path), json_body={},
                         timeout=120)
        return {"id": file_id, "name": attachment.name,
                "mime": attachment.mime, "size": attachment.size,
                "image": attachment.mime.startswith("image/")}

    # ------------------------------------------------------------------ payloads

    def _timezone(self):
        return time.timezone // 60, time.tzname[0]

    @staticmethod
    def _message(text, role="user", attachments=()):
        attachments = list(attachments)
        message = {
            "id": str(uuid.uuid4()),
            "author": {"role": role},
            "create_time": time.time(),
            "content": {"content_type": "text", "parts": [text]},
            "metadata": {
                "developer_mode_connector_ids": [],
                "selected_sources": [],
                "selected_github_repos": [],
                "selected_all_github_repos": False,
                "serialization_metadata": {"custom_symbol_offsets": []},
            },
        }
        if not attachments:
            return message
        parts = [text] if text else []
        for attachment in attachments:
            if attachment["image"]:
                parts.append({
                    "content_type": "image_asset_pointer",
                    "asset_pointer": _asset_pointer(attachment["id"]),
                    "size_bytes": attachment["size"],
                })
        if parts != [text]:
            message["content"] = {"content_type": "multimodal_text", "parts": parts}
        message["metadata"]["attachments"] = [
            {"id": item["id"], "size": item["size"], "name": item["name"],
             "mime_type": item["mime"], "width": None, "height": None}
            for item in attachments
        ]
        return message

    def _conduit_token(self, headers, prompt, model):
        offset, zone = self._timezone()
        body = {
            "action": "next",
            "fork_from_shared_post": False,
            "parent_message_id": ROOT_MESSAGE,
            "model": model,
            "client_prepare_state": "none",
            "timezone_offset_min": offset,
            "timezone": zone,
            "conversation_mode": {"kind": "primary_assistant"},
            "system_hints": [],
            "partial_query": {
                "id": str(uuid.uuid4()),
                "author": {"role": "user"},
                "content": {"content_type": "text", "parts": [prompt]},
            },
            "supports_buffering": True,
            "supported_encodings": ["v1"],
            "client_contextual_info": {"app_name": "chatgpt.com"},
        }
        path = "/backend-api/f/conversation/prepare"
        headers = {**self._target(headers, path), "X-Conduit-Token": "no-token"}
        prepared = net.request_json(self.origin + path, method="POST",
                                    headers=headers, json_body=body) or {}
        conduit = prepared.get("conduit_token") or ""
        if not conduit:
            raise sentinel.SentinelError(
                f"the conduit prepare returned no token: {str(prepared)[:200]}")
        return conduit

    def _stream_body(self, messages, model, conversation_id=None,
                     parent=ROOT_MESSAGE):
        offset, zone = self._timezone()
        body = {
            "action": "next",
            "messages": messages,
            "parent_message_id": parent,
            "model": model,
            "client_prepare_state": "success",
            "timezone_offset_min": offset,
            "timezone": zone,
            "conversation_mode": {"kind": "primary_assistant"},
            "enable_message_followups": True,
            "system_hints": [],
            "supports_buffering": True,
            "supported_encodings": ["v1"],
            "client_contextual_info": {
                "is_dark_mode": False, "time_since_loaded": 120,
                "page_height": 900, "page_width": 1400, "pixel_ratio": 2,
                "screen_height": 1440, "screen_width": 2560,
                "app_name": "chatgpt.com",
            },
            "paragen_cot_summary_display_override": "allow",
            "force_parallel_switch": "auto",
        }
        if conversation_id:
            body["conversation_id"] = conversation_id
        return body

    def _poll(self, headers, conversation_id, parser, timeout, debug):
        """Stream the turn from ``GET /backend-api/conversation/<id>``."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                data = net.request_json(
                    f"{self.origin}/backend-api/conversation/{conversation_id}",
                    headers=headers)
            except net.StreamError as exc:
                yield ev.error(str(exc))
                return
            done = False
            for message in _walk_messages(data):
                for event in parser.feed_message(message):
                    yield event
                if _is_finished(message):
                    done = True
            if done and parser.emitted:
                return
            time.sleep(POLL_INTERVAL)
        if not parser.emitted:
            yield ev.error("the turn never produced a message to read")


class _Conversation(Conversation):
    """A ChatGPT conversation: one id, threaded by ``parent_message_id``."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        self.provider = provider
        self.headers = provider._headers(match)
        self.parent_message_id = (resume or {}).get("parent_message_id") or ROOT_MESSAGE
        self.fingerprint = None
        self._ready = False

    def _prepare(self):
        self.provider._authorize(self.headers)
        script, build = self.provider._bootstrap(self.headers)
        self.fingerprint = sentinel.build_fingerprint(
            self.headers.get("User-Agent", ""), script, build)
        self._ready = True

    def send(self, prompt, files=None):
        if not self._ready:
            try:
                self._prepare()
            except (net.StreamError, sentinel.SentinelError) as exc:
                yield ev.error(str(exc))
                return
        try:
            attachments = self._attachments(files)
        except files_mod.FileError as exc:
            yield ev.error(str(exc))
            return
        uploaded = []
        for attachment in attachments:
            try:
                uploaded.append(self.provider._upload(self.headers, attachment))
            except (net.StreamError, files_mod.FileError) as exc:
                yield ev.error(str(exc))
                return
            yield ev.tool_call(name="upload", status="done", args=attachment.name)
        message = (f"{self.system}\n\n{prompt}"
                   if (self.system and self.turns == 0) else prompt)
        try:
            gate = self.provider._sentinel_headers(
                self.headers, self.fingerprint, self.debug)
        except (net.StreamError, sentinel.SentinelError) as exc:
            yield ev.error(f"sentinel: {exc}")
            return
        try:
            conduit = self.provider._conduit_token(
                self.headers, message, self.model)
        except (net.StreamError, sentinel.SentinelError) as exc:
            yield ev.error(str(exc))
            return

        path = "/backend-api/f/conversation"
        stream_headers = {
            **self.provider._target(self.headers, path),
            **gate["headers"],
            "Accept": "text/event-stream",
            "X-Conduit-Token": conduit,
        }
        parser = _MessageStream()
        emitted = False
        try:
            lines = net.stream_lines(
                self.provider.origin + path, headers=stream_headers,
                json_body=self.provider._stream_body(
                    [self.provider._message(message, attachments=uploaded)],
                    self.model,
                    conversation_id=self.conversation_id,
                    parent=self.parent_message_id),
                timeout=self.timeout, debug=self.debug)
            for payload in net.sse_payloads(lines):
                if payload == "[DONE]":
                    break
                try:
                    event = json.loads(payload)
                except ValueError:
                    yield ev.raw(payload)
                    continue
                if not isinstance(event, dict):
                    continue
                kind = event.get("type")
                if kind in ("stream_handoff", "resume_conversation_token"):
                    self.conversation_id = (event.get("conversation_id")
                                            or self.conversation_id)
                    continue
                if event.get("conversation_id"):
                    self.conversation_id = event["conversation_id"]
                for item in parser.feed(event):
                    if item.kind in (ev.TEXT, ev.THINKING, ev.TOOL_CALL):
                        emitted = True
                    yield item
        except net.StreamError as exc:
            yield ev.error(str(exc))
            return

        if self.conversation_id and not emitted:
            yield from self.provider._poll(
                self.headers, self.conversation_id, parser, self.timeout,
                self.debug)
        self.turns += 1
        if parser.last_assistant_id:
            self.parent_message_id = parser.last_assistant_id
            self.resume["parent_message_id"] = parser.last_assistant_id
        self._record("user", prompt)
        self._record("assistant", parser.answer)
        yield ev.done()


def _walk_messages(data):
    mapping = (data or {}).get("mapping") or {}
    if not isinstance(mapping, dict):
        return

    def order(node):
        message = node.get("message") if isinstance(node, dict) else None
        if isinstance(message, dict):
            created = message.get("create_time")
            if isinstance(created, (int, float)):
                return created
        return 0

    for node in sorted(mapping.values(), key=order):
        if not isinstance(node, dict):
            continue
        message = node.get("message")
        if isinstance(message, dict):
            yield message


def _part_text(part):
    if isinstance(part, str):
        return part
    if isinstance(part, list):
        return "".join(_part_text(item) for item in part)
    if isinstance(part, dict):
        return str(part.get("text") or "")
    return ""


def _is_finished(message):
    if (message.get("author") or {}).get("role") != "assistant":
        return False
    metadata = message.get("metadata") or {}
    return (message.get("status") == "finished_successfully"
            or bool(metadata.get("finish_details")))


def _active_messages(conversation):
    """The messages on the conversation's current branch, oldest first."""
    mapping = (conversation or {}).get("mapping") or {}
    if not isinstance(mapping, dict):
        return []
    current = (conversation or {}).get("current_node")
    if current and current in mapping:
        chain = []
        seen = set()
        while current and current in mapping and current not in seen:
            seen.add(current)
            node = mapping[current]
            message = node.get("message") if isinstance(node, dict) else None
            if isinstance(message, dict):
                chain.append(message)
            current = node.get("parent") if isinstance(node, dict) else None
        chain.reverse()
        return chain
    ordered = []
    for node in mapping.values():
        message = node.get("message") if isinstance(node, dict) else None
        if isinstance(message, dict):
            ordered.append(message)
    ordered.sort(key=lambda m: m.get("create_time") or 0)
    return ordered


def _history_messages(message):
    """Normalise one ChatGPT message into zero or more history messages."""
    role = (message.get("author") or {}).get("role") or ""
    content = message.get("content") or {}
    ctype = content.get("content_type") or ""
    text = "".join(_part_text(part) for part in content.get("parts") or []).strip()
    created = history.iso_from_epoch(message.get("create_time"))
    message_id = str(message.get("id") or "")
    parent_id = str(message.get("parent") or "")
    out = []

    def add(kind, body, name=""):
        if body:
            out.append(history.Message(
                role=role, text=body, kind=kind, created=created,
                message_id=message_id, parent_id=parent_id, name=name))

    if role == "user":
        add(history.TEXT, text)
    elif role == "assistant":
        if ctype in ("text", "multimodal_text"):
            add(history.TEXT, text)
        elif ctype in ("thoughts", "reasoning_recap"):
            thoughts = content.get("thoughts") or []
            thinking = "\n".join(
                str(item.get("content") or "")
                for item in thoughts if isinstance(item, dict)).strip()
            add(history.THINKING, thinking or text)
        elif ctype == "code":
            add(history.TOOL_CALL, content.get("text") or "", "code")
        elif ctype == "execution_output":
            add(history.TOOL_CALL, content.get("text") or "", "execution_output")
        elif ctype == "tether_browsing_display":
            add(history.TOOL_CALL, text, "search")
        elif ctype in ("model_editable_context", "user_editable_context"):
            if text:
                out.append(history.Message(
                    role=history.SYSTEM, text=text, created=created,
                    message_id=message_id, parent_id=parent_id))
    elif role == "tool":
        add(history.TOOL_CALL, text, message.get("recipient") or "tool")
    elif role == "system":
        add(history.TEXT, text)
    return out


class _MessageStream:
    """Turn cumulative conversation messages into deltas (deduplicated by id).

    The site interleaves whole ``message`` frames with JSON-patch frames
    (``{"o": "patch", "v": [{"p": "/message/content/parts/0", …}]}``) that carry
    the rest of a streaming answer; both are applied here, keyed by message id
    so a fresh message starts from an empty part list.
    """

    def __init__(self):
        self.text = 0
        self.thinking = 0
        self.tool = None
        self.emitted = False
        self.answer_parts = []
        self.last_assistant_id = ""
        self.current_id = ""
        self.parts = []

    @property
    def answer(self):
        return "".join(self.answer_parts)

    def feed(self, event):
        out = []
        message = event.get("message")
        if isinstance(message, dict):
            out += self.feed_message(message)
        value = event.get("v")
        if isinstance(value, dict):
            out += self.feed(value)
        elif isinstance(value, list) and event.get("o") == "patch":
            out += self.apply_patches(value)
        return out

    def apply_patches(self, patches):
        """Apply ``/message/content/parts/<i>`` patches to the live answer."""
        out = []
        for patch in patches:
            if not isinstance(patch, dict):
                continue
            path = str(patch.get("p") or "")
            if not path.startswith("/message/content/parts/"):
                continue
            try:
                index = int(path.rsplit("/", 1)[-1])
            except ValueError:
                continue
            while len(self.parts) <= index:
                self.parts.append("")
            value = patch.get("v")
            if patch.get("o") == "append" and isinstance(self.parts[index], str) \
                    and isinstance(value, str):
                self.parts[index] += value
            else:
                self.parts[index] = value
            out += self._sync_text()
        return out

    def _sync_text(self):
        full = "".join(_part_text(part) for part in self.parts)
        if len(full) <= self.text:
            return []
        delta = full[self.text:]
        self.text = len(full)
        self.answer_parts.append(delta)
        return [ev.text(delta)]

    def feed_message(self, message):
        out = []
        if (message.get("author") or {}).get("role") != "assistant":
            return out
        identifier = message.get("id")
        if identifier:
            identifier = str(identifier)
            self.last_assistant_id = identifier
            if identifier != self.current_id:
                self.current_id = identifier
                self.parts = []
                self.text = 0
        content = message.get("content") or {}
        ctype = content.get("content_type")
        recipient = message.get("recipient") or "all"
        if recipient not in ("all", "", None) and recipient != self.tool:
            self.tool = recipient
            out.append(ev.tool_call(name=recipient, status="start"))
        if ctype in ("text", "multimodal_text"):
            self.parts = list(content.get("parts") or [])
            out += self._sync_text()
        elif ctype in ("thoughts", "reasoning_recap"):
            parts = content.get("thoughts") or content.get("parts") or []
            full = "\n".join(_part_text(part) for part in parts)
            if len(full) > self.thinking:
                out.append(ev.thinking(full[self.thinking:]))
                self.thinking = len(full)
        elif ctype == "code":
            out.append(ev.tool_call(name="code", status="delta",
                                    args=content.get("text") or ""))
        elif ctype == "execution_output":
            out.append(ev.tool_call(name=self.tool or "code", status="done",
                                    args=(content.get("text") or "")[:200]))
            self.tool = None
        elif ctype == "tether_browsing_display":
            out.append(ev.tool_call(
                name="search", status="done",
                args=str(content.get("result") or content.get("summary") or "")[:200]))
        elif ctype == "tether_quote":
            out.append(ev.tool_call(
                name="source", status="delta",
                args=str(content.get("title") or content.get("url") or "")[:200]))
        if out:
            self.emitted = True
        return out
