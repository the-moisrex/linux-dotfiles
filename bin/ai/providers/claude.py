"""Claude (claude.ai) adapter.

Replays the signed-in claude.ai session against the web app's own endpoints:

* ``GET  /api/organizations`` — the organization that owns the chats.
* ``POST /api/organizations/<org>/chat_conversations`` — create a chat
  (``{"uuid": ..., "name": ...}``); the response carries the account's default
  ``model`` and ``current_leaf_message_uuid``.
* ``POST /api/organizations/<org>/conversations/<chat>/wiggle/upload-file`` —
  upload one attachment as multipart ``file``; the response's ``file_uuid`` and
  ``path`` are what the completion body references.
* ``POST /api/organizations/<org>/chat_conversations/<chat>/completion`` — one
  turn.  With ``rendering_mode: "messages"`` the response is the Anthropic
  Messages SSE stream (``message_start``/``content_block_delta``/…), so text and
  thinking deltas are read directly and ``message_start.message.uuid`` is the
  assistant message the next turn hangs off.  The older ``completion`` event
  (a plain delta field) is still understood as a fallback.

The server keeps the transcript, so a follow-up only sends the new message and
the previous assistant message's uuid as ``parent_message_uuid``.  ``-c``/``-i``
therefore resume with the chat uuid plus that parent uuid.

The web app rejects a ``system_prompt`` field, so ``--system`` is folded into
the first user message (like Kimi).  A file is uploaded and referenced as an
attachment with an empty ``extracted_content``; the server reads it from the
uploaded blob, which works for text and images alike.

``ai.save`` lists chats with ``GET /api/organizations/<org>/chat_conversations``
(``limit``/``offset``) and reads one with
``GET /api/organizations/<org>/chat_conversations/<uuid>``.
"""

from __future__ import annotations

import json
import os
import re
import uuid

from .. import events as ev
from .. import files as files_mod
from .. import history
from .. import net
from .. import session as session_mod
from . import Conversation, Provider

ORGS_PATH = "/api/organizations"
CHATS_PATH = "/api/organizations/{org}/chat_conversations"
CHAT_PATH = "/api/organizations/{org}/chat_conversations/{chat}"
UPLOAD_PATH = "/api/organizations/{org}/conversations/{chat}/wiggle/upload-file"
COMPLETION_PATH = "/api/organizations/{org}/chat_conversations/{chat}/completion"

#: The parent of the first message in a fresh chat.
ZERO_UUID = "00000000-0000-4000-8000-000000000000"

#: The site titles a new chat from this much of the first prompt.
TITLE_LIMIT = 60

#: Friendly names accepted by ``--model``; raw site ids also work.  ``auto``
#: means "the model the account's new chat is created with".
MODEL_ALIASES = {
    "auto": None,
    "default": None,
    "sonnet": "claude-sonnet-4-5-20250929",
    "sonnet-4-5": "claude-sonnet-4-5-20250929",
    "opus": "claude-opus-4-1-20250805",
    "opus-4-1": "claude-opus-4-1-20250805",
    "haiku": "claude-haiku-4-5-20251001",
    "haiku-4-5": "claude-haiku-4-5-20251001",
}

_UUID = re.compile(r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")

#: How many stored turns a fresh process replays when it has history but no
#: chat id to reattach to.
MAX_REPLAY = 40


def _timezone():
    """The IANA zone the site expects, from ``TZ`` or the system link."""
    zone = os.environ.get("TZ")
    if zone:
        return zone
    try:
        target = os.path.realpath("/etc/localtime")
    except OSError:
        return "UTC"
    marker = "/zoneinfo/"
    if marker in target:
        return target.split(marker, 1)[1]
    return "UTC"


def _title(prompt):
    text = " ".join((prompt or "").split())
    return text[:TITLE_LIMIT] or "New chat"


def _attachment(record, attachment):
    """The completion body's attachment entry for one uploaded file."""
    return {
        "file_uuid": record.get("file_uuid") or "",
        "file_name": record.get("file_name") or attachment.name,
        "file_type": record.get("media_type") or attachment.mime,
        "file_size": record.get("size_bytes", attachment.size),
        "extracted_content": "",
        "path": record.get("path") or "",
        "file_kind": record.get("file_kind") or "blob",
    }


class Claude(Provider):
    name = "claude"
    label = "Claude"
    origin = "https://claude.ai"
    session_target = "claude.ai"
    default_model = "auto"
    models = ("auto", "sonnet", "opus", "haiku")
    #: A single message comfortably takes hundreds of KiB; the site's context
    #: window is the real limit.
    inline_limit = 256 * 1024

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)

    # -- session -----------------------------------------------------------

    def _base_headers(self, match):
        return session_mod.build_headers(match, {
            "Accept": "application/json",
            "Content-Type": "application/json",
            "Origin": self.origin,
            "Referer": self.origin + "/chats",
            "anthropic-client-platform": "web_claude_ai",
            "anthropic-client-version": "1.0.0",
        })

    def _org_uuid(self, headers, match):
        """The organization that owns the chats (cached for this instance)."""
        cached = getattr(self, "_org", None)
        if cached:
            return cached
        cookie = session_mod.cookie_value(match, "lastActiveOrg")
        if isinstance(cookie, str) and _UUID.match(cookie.strip()):
            self._org = cookie.strip()
            return self._org
        data = net.request_json(self.origin + ORGS_PATH, headers=headers,
                                timeout=60)
        orgs = data if isinstance(data, list) else (data or {}).get("organizations") or []
        chosen = None
        for org in orgs:
            if not isinstance(org, dict) or not org.get("uuid"):
                continue
            capabilities = org.get("capabilities") or []
            if chosen is None:
                chosen = org
            if "chat" in capabilities:
                chosen = org
                break
        if chosen is None:
            raise net.StreamError("claude.ai returned no organization for this session")
        self._org = chosen["uuid"]
        return self._org

    # -- api steps ---------------------------------------------------------

    def _create_chat(self, headers, org, prompt):
        body = {"uuid": str(uuid.uuid4()), "name": _title(prompt)}
        data = net.request_json(self.origin + CHATS_PATH.format(org=org),
                                method="POST", headers=headers, json_body=body,
                                timeout=60)
        if isinstance(data, list):
            data = data[0] if data else None
        if not isinstance(data, dict) or not data.get("uuid"):
            raise net.StreamError(f"could not create a Claude chat: {data}")
        return data

    def _fetch_chat(self, headers, org, chat):
        data = net.request_json(self.origin + CHAT_PATH.format(org=org, chat=chat),
                                headers=headers, timeout=60)
        if not isinstance(data, dict):
            raise net.StreamError(f"could not read Claude chat {chat}: {data}")
        return data

    def _upload(self, headers, org, chat, attachment):
        """Upload one attachment and return the site's file record."""
        body, content_type = net.encode_multipart([
            ("file", (attachment.name, attachment.data, attachment.mime)),
        ])
        request_headers = dict(headers)
        request_headers["Content-Type"] = content_type
        data = net.request_json(
            self.origin + UPLOAD_PATH.format(org=org, chat=chat),
            method="POST", headers=request_headers, data=body, timeout=300)
        if not isinstance(data, dict) or not data.get("file_uuid"):
            raise net.StreamError(f"upload failed: {data}")
        return data

    # -- history (used by ai.save) ----------------------------------------

    def history_headers(self, match):
        cached = getattr(self, "_history_headers", None)
        if cached is not None and getattr(self, "_history_match", None) is match:
            return dict(cached)
        headers = self._base_headers(match)
        self._history_headers = headers
        self._history_match = match
        return dict(headers)

    def iter_chat_summaries(self, match, limit=None):
        """Walk ``GET /chat_conversations`` (limit/offset) newest-first."""
        headers = self.history_headers(match)
        org = self._org_uuid(headers, match)
        offset = 0
        count = 0
        page_size = 50
        while True:
            data = net.request_json(
                f"{self.origin}{CHATS_PATH.format(org=org)}"
                f"?limit={page_size}&offset={offset}",
                headers=headers, timeout=120)
            items = data if isinstance(data, list) else (data or {}).get("chat_conversations") or []
            if not items:
                return
            for item in items:
                if not isinstance(item, dict) or not item.get("uuid"):
                    continue
                identifier = item["uuid"]
                yield history.ChatSummary(
                    provider=self.name,
                    remote_id=identifier,
                    title=item.get("name") or "",
                    created=item.get("created_at") or "",
                    updated=item.get("updated_at") or item.get("created_at") or "",
                    model=item.get("model") or "",
                    url=f"{self.origin}/chat/{identifier}",
                )
                count += 1
                if limit and count >= limit:
                    return
            if len(items) < page_size:
                return
            offset += len(items)

    def fetch_chat(self, match, remote_id):
        """Fetch one chat and normalise its messages."""
        headers = self.history_headers(match)
        org = self._org_uuid(headers, match)
        data = self._fetch_chat(headers, org, remote_id)
        chat = history.Chat(
            provider=self.name,
            remote_id=remote_id,
            title=data.get("name") or "",
            created=data.get("created_at") or "",
            updated=data.get("updated_at") or "",
            model=data.get("model") or "",
            url=f"{self.origin}/chat/{remote_id}",
        )
        messages = [m for m in (data.get("chat_messages") or []) if isinstance(m, dict)]
        messages.sort(key=lambda item: item.get("index") or 0)
        for message in messages:
            role = history.USER if message.get("sender") == "human" else history.ASSISTANT
            created = message.get("created_at") or ""
            message_id = message.get("uuid") or ""
            parent_id = message.get("parent_message_uuid") or ""
            text = message.get("text") or ""
            if not text:
                text = _content_text(message.get("content"))
            if text:
                chat.messages.append(history.Message(
                    role=role, text=text, created=created,
                    message_id=message_id, parent_id=parent_id))
            for attachment in message.get("attachments") or []:
                name = (attachment or {}).get("file_name") or ""
                if name:
                    chat.messages.append(history.Message(
                        role=role, text=name, kind=history.ATTACHMENT,
                        created=created, message_id=message_id,
                        parent_id=parent_id))
            for file in message.get("files") or []:
                name = (file or {}).get("file_name") or ""
                if name:
                    chat.messages.append(history.Message(
                        role=role, text=name, kind=history.ATTACHMENT,
                        created=created, message_id=message_id,
                        parent_id=parent_id))
        if chat.messages:
            chat.created = chat.created or chat.messages[0].created
            chat.updated = history.newest([chat.updated] + [m.created for m in chat.messages])
        return chat


def _content_text(content):
    """Join the text blocks of a stored message's ``content`` array."""
    if not isinstance(content, list):
        return ""
    parts = []
    for block in content:
        if isinstance(block, dict) and block.get("type") == "text":
            text = block.get("text")
            if isinstance(text, str):
                parts.append(text)
    return "".join(parts)


class _Conversation(Conversation):
    """A Claude chat, continued by its uuid and the last assistant message."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        self.provider = provider
        self.headers = provider._base_headers(match)
        requested = model or provider.default_model
        self.model_preference = MODEL_ALIASES.get(requested, requested)
        self.parent_uuid = (resume or {}).get("parent_message_uuid") or ""
        self.assistant_uuid = ""

    def send(self, prompt, files=None):
        if not self.headers.get("Cookie"):
            yield ev.error(
                "no signed-in session found for claude.ai; log in once in a browser")
            return
        headers = dict(self.headers)
        headers["Accept"] = "text/event-stream"
        try:
            org = self.provider._org_uuid(headers, self.match)
            created = self._ensure_chat(headers, org, prompt)
            attachments = []
            try:
                uploaded = self._attachments(files)
            except files_mod.FileError as exc:
                yield ev.error(str(exc))
                return
            for attachment in uploaded:
                try:
                    record = self.provider._upload(headers, org, self.conversation_id,
                                                   attachment)
                except (net.StreamError, files_mod.FileError) as exc:
                    yield ev.error(str(exc))
                    return
                attachments.append(_attachment(record, attachment))
                yield ev.tool_call(name="upload", status="done",
                                   args=attachment.name)

            body = self._body(prompt, created, attachments)
        except net.StreamError as exc:
            yield ev.error(_friendly(str(exc)))
            return

        answer = []
        thinking = []
        stop_reason = None
        error = None
        try:
            for event in _sse_events(net.stream_lines(
                    self.provider.origin + COMPLETION_PATH.format(
                        org=org, chat=self.conversation_id),
                    headers=headers, json_body=body, timeout=self.timeout,
                    debug=self.debug)):
                if isinstance(event, str):
                    yield ev.raw(event)
                    continue
                for item in _feed(event, self, answer, thinking):
                    yield item
                if event.get("type") == "error":
                    error = _error_message(event)
                    break
                if event.get("type") in ("message_stop",):
                    break
                if event.get("type") == "message_delta":
                    delta = event.get("delta") or {}
                    if delta.get("stop_reason"):
                        stop_reason = delta["stop_reason"]
                if event.get("type") == "completion" and event.get("stop_reason"):
                    stop_reason = event["stop_reason"]
                    break
        except net.StreamError as exc:
            yield ev.error(_friendly(str(exc)))
            return

        if error:
            return
        text = "".join(answer)
        if not text.strip():
            yield ev.error("Claude returned no answer"
                           + (f" (chat {self.conversation_id})" if self.conversation_id else ""))
            return

        self.parent_uuid = self.assistant_uuid or self.parent_uuid
        self.resume = {
            "parent_message_uuid": self.parent_uuid,
            "model": self.model,
        }
        self.turns += 1
        self._record("user", prompt)
        self._record("assistant", text)
        yield ev.done()

    def _ensure_chat(self, headers, org, prompt):
        """Create or refresh the chat; return whether it was just created."""
        if self.conversation_id:
            if not self.model_preference or not self.parent_uuid:
                data = self.provider._fetch_chat(headers, org, self.conversation_id)
                if not self.model_preference:
                    self.model = data.get("model") or self.model
                if not self.parent_uuid:
                    self.parent_uuid = data.get("current_leaf_message_uuid") or ZERO_UUID
            return False
        data = self.provider._create_chat(headers, org, prompt)
        self.conversation_id = data["uuid"]
        if not self.model_preference:
            self.model = data.get("model") or self.model
        self.parent_uuid = data.get("current_leaf_message_uuid") or ZERO_UUID
        return True

    def _body(self, prompt, created, attachments):
        query = self._query(prompt, created)
        return {
            "prompt": query,
            "parent_message_uuid": self.parent_uuid or ZERO_UUID,
            "timezone": _timezone(),
            "model": self.model_preference or self.model,
            "attachments": attachments,
            "files": [],
            "rendering_mode": "messages",
        }

    def _query(self, prompt, created):
        """The text to send, folding in a system prompt and any replay."""
        parts = []
        if self.system and self.turns == 0:
            parts.append(self.system)
        if created and self.history:
            lines = ["This is a continuation of the conversation so far. "
                     "Reply to the final user message below.", ""]
            for turn in self.history[-MAX_REPLAY:]:
                who = "User" if turn.get("role") == "user" else "Assistant"
                lines.append(f"{who}: {turn.get('text', '')}")
            lines.append("")
            lines.append(f"User: {prompt}")
            parts.append("\n".join(lines))
        else:
            parts.append(prompt)
        return "\n\n".join(part for part in parts if part)


def _feed(event, conversation, answer, thinking):
    """Translate one SSE event into stream events and update the conversation."""
    kind = event.get("type")
    if kind == "message_start":
        message = event.get("message") or {}
        if message.get("uuid"):
            conversation.assistant_uuid = message["uuid"]
        if message.get("model"):
            conversation.model = message["model"]
        return
    if kind == "content_block_start":
        block = event.get("content_block") or {}
        if block.get("type") == "tool_use":
            yield ev.tool_call(name=block.get("name") or "tool", status="start",
                               args=json.dumps(block.get("input") or {}))
        return
    if kind == "content_block_delta":
        delta = event.get("delta") or {}
        delta_type = delta.get("type")
        if delta_type == "text_delta":
            text = delta.get("text") or ""
            if text:
                answer.append(text)
                yield ev.text(text)
        elif delta_type == "thinking_delta":
            text = delta.get("thinking") or ""
            if text:
                thinking.append(text)
                yield ev.thinking(text)
        return
    if kind == "content_block_stop":
        return
    if kind == "completion":
        text = event.get("completion") or ""
        if text:
            answer.append(text)
            yield ev.text(text)
        return
    if kind == "error":
        yield ev.error(_error_message(event))
        return
    # ping, message_limit, message_delta, message_stop … carry no text.


def _friendly(message):
    """Add the remedy to a site error that names the problem."""
    lowered = message.casefold()
    if "model_not_available" in lowered or "model not available" in lowered:
        return (f"{message} (this account cannot use that model; "
                f"try --model auto)")
    return message


def _error_message(event):
    error = event.get("error") or {}
    if isinstance(error, dict):
        return error.get("message") or "Claude returned an error"
    if isinstance(error, str):
        return error
    return "Claude returned an error"


def _sse_events(lines):
    """Yield each SSE event's JSON object from an iterable of lines."""
    data = []
    for line in lines:
        line = line.rstrip("\r")
        if line == "":
            if data:
                payload = "\n".join(data)
                data = []
                if payload.strip() == "[DONE]":
                    return
                try:
                    yield json.loads(payload)
                except ValueError:
                    yield payload
            continue
        if line.startswith("event:"):
            continue
        if line.startswith("data:"):
            data.append(line[5:].lstrip())
    if data:
        payload = "\n".join(data)
        if payload.strip() != "[DONE]":
            try:
                yield json.loads(payload)
            except ValueError:
                yield payload
