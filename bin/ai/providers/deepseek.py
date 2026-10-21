"""DeepSeek (chat.deepseek.com) adapter.

Uses the bearer token kept in the site's ``localStorage``, creates a chat
session, solves the ``DeepSeekHashV1`` proof-of-work challenge and streams the
completion.  A conversation is continued by reusing the chat-session id and
sending the next message with ``parent_message_id`` set to the previous
assistant message's id (verified: a follow-up on the same session answers with
the earlier context).

The stream is not SSE-with-JSON-deltas but the site's own **fragment patch**
protocol: an initial ``{"v":{"response":{...}}}`` snapshot carrying ``fragments``
(each ``THINK`` = reasoning, ``RESPONSE`` = answer), followed by patch frames
``{"p": "...", "o": "APPEND", "v": ...}`` and the shorthand ``{"v": "text"}``
that appends to the current fragment.  Endpoint paths, the payload shape and this
protocol were captured with ``bin/browser.record https://chat.deepseek.com``.

``--file`` uploads through ``POST /api/v0/file/upload_file`` (multipart ``file``,
behind its own ``X-DS-PoW-Response`` header) and references the returned
``file-<uuid>`` in the completion body's ``ref_file_ids``.

A long answer (or a long reasoning run) is sometimes cut short: the stream ends
without ``response/status`` reaching ``FINISHED`` and its ``close`` event names
a ``click_behavior`` other than ``none`` — the web UI's ``Continue`` button.
Rather than stopping there, ``send`` posts ``/api/v0/chat/continue`` with the
assistant ``message_id`` (its own proof-of-work) and appends the continuation,
dropping the repeated snapshot prefix so the answer is emitted once.
"""

from __future__ import annotations

import json
import time
import uuid
from urllib.parse import urlencode

from .. import events as ev
from .. import files as files_mod
from .. import history
from .. import net
from .. import session as session_mod
from ..pow import PowError, deepseek_response, solve_deepseek
from . import Conversation, Provider

COMPLETION_PATH = "/api/v0/chat/completion"
CONTINUE_PATH = "/api/v0/chat/continue"
UPLOAD_PATH = "/api/v0/file/upload_file"
FILES_PATH = "/api/v0/file/fetch_files"
SESSIONS_PATH = "/api/v0/chat_session/fetch_page"
HISTORY_PATH = "/api/v0/chat/history_messages"
CLIENT_VERSION = "2.5.0"

#: The site cuts a long answer (or a slow reasoning run) short, leaving the
#: message unfinished; the web UI then offers a ``Continue`` button.  Each
#: click is one more stream, so this only bounds a pathological chain.
MAX_CONTINUATIONS = 8

#: ``status`` values that mean the file can be referenced, and dead ends.
FILE_READY = ("SUCCESS",)
FILE_FAILED = ("FAILED", "CONTENT_EMPTY", "CONTENT_TOO_LONG")


class DeepSeek(Provider):
    name = "deepseek"
    label = "DeepSeek"
    origin = "https://chat.deepseek.com"
    session_target = "chat.deepseek.com"
    default_model = "deepseek_reasoner"
    models = ("deepseek_reasoner", "deepseek_chat")
    #: At least 600 KB of ASCII was accepted inline (the site's own context
    #: window is the limit), so only genuinely huge prompts need a file.
    inline_limit = 256 * 1024

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)

    def _headers(self, match):
        device = session_mod.storage_value(match, "deepseek-device-id:chat") or str(uuid.uuid4())
        return session_mod.build_headers(match, {
            "Accept": "*/*",
            "Content-Type": "application/json",
            "Referer": self.origin + "/",
            "x-client-platform": "web",
            "x-client-bundle-id": "com.deepseek.chat",
            "x-client-version": CLIENT_VERSION,
            "x-client-locale": "en_US",
            "x-client-timezone-offset": str(-time.timezone),
            "x-device-id": device,
        })

    def _token(self, match):
        return session_mod.storage_value(
            match, "userToken", "token", "Authorization", "access_token", "ds_token")

    # -- api steps ---------------------------------------------------------

    def _create_session(self, headers):
        data = net.request_json(self.origin + "/api/v0/chat_session/create",
                                method="POST", headers=headers, json_body={})
        biz = self._biz(data)
        if isinstance(biz, dict):
            session = biz.get("chat_session")
            if isinstance(session, dict) and session.get("id"):
                return session["id"]
            return biz.get("id") or biz.get("chat_session_id")
        return None

    def _pow(self, headers, target_path=COMPLETION_PATH):
        data = net.request_json(self.origin + "/api/v0/chat/create_pow_challenge",
                                method="POST", headers=headers,
                                json_body={"target_path": target_path})
        challenge = self._biz(data)
        if isinstance(challenge, dict) and "challenge" in challenge:
            challenge = challenge["challenge"]
        if not isinstance(challenge, dict):
            return None
        answer = solve_deepseek(challenge.get("salt", ""),
                                challenge.get("expire_at", 0),
                                challenge.get("difficulty", 1),
                                challenge.get("challenge", ""))
        return deepseek_response(salt=challenge.get("salt", ""),
                                 challenge=challenge.get("challenge", ""),
                                 signature=challenge.get("signature", ""),
                                 answer=answer,
                                 target_path=target_path)

    def _upload(self, headers, attachment):
        """Upload one attachment and wait for it to become referenceable.

        Returns the server's file record; raises :class:`net.StreamError` when
        the site rejects the upload.
        """
        request_headers = dict(headers)
        pow_header = self._pow(request_headers, UPLOAD_PATH)
        if pow_header:
            request_headers["x-ds-pow-response"] = pow_header
        body, content_type = net.encode_multipart([
            ("file", (attachment.name, attachment.data, attachment.mime)),
        ])
        request_headers["Content-Type"] = content_type
        data = net.request_json(self.origin + UPLOAD_PATH, method="POST",
                                headers=request_headers, data=body, timeout=300)
        record = self._biz(data)
        if not isinstance(record, dict) or not record.get("id"):
            raise net.StreamError(f"upload failed: {data}")
        return self._await_file(request_headers, record)

    def _await_file(self, headers, record, timeout=120):
        """Poll ``fetch_files`` until the file is parsed (or clearly dead)."""
        file_id = record.get("id")
        deadline = time.monotonic() + timeout
        while record.get("status") not in FILE_READY + FILE_FAILED:
            if time.monotonic() >= deadline:
                return record
            time.sleep(1.0)
            try:
                data = net.request_json(
                    f"{self.origin}{FILES_PATH}?file_ids={file_id}",
                    headers=headers, timeout=60)
            except net.StreamError:
                return record
            found = _find_file(self._biz(data), file_id)
            if not found:
                return record
            record = found
        return record

    @staticmethod
    def _biz(data):
        biz = ((data or {}).get("data") or {})
        if isinstance(biz, dict) and "biz_data" in biz:
            biz = biz["biz_data"]
        return biz

    # -- history (used by ai.save) ----------------------------------------

    def history_headers(self, match):
        """REST headers with the ``userToken`` bearer, resolved once."""
        cached = getattr(self, "_history_headers", None)
        if cached is not None and getattr(self, "_history_match", None) is match:
            return dict(cached)
        headers = self._headers(match)
        token = self._token(match)
        if token:
            headers["Authorization"] = "Bearer " + token
        self._history_headers = headers
        self._history_match = match
        return dict(headers)

    def iter_chat_summaries(self, match, limit=None):
        """Walk the sidebar with its keyset cursor (pinned/updated_at/id)."""
        headers = self.history_headers(match)
        cursor = None
        seen = set()
        count = 0
        while True:
            params = {"count": 50}
            if cursor:
                params.update({
                    "lte_cursor.pinned": "true" if cursor["pinned"] else "false",
                    "lte_cursor.updated_at": cursor["updated_at"],
                    "lte_cursor.id": cursor["id"],
                })
            url = f"{self.origin}{SESSIONS_PATH}?{urlencode(params)}"
            data = net.request_json(url, headers=headers, timeout=120)
            biz = self._biz(data) or {}
            sessions = biz.get("chat_sessions") or []
            new = [item for item in sessions if item.get("id") not in seen]
            if not new:
                return
            for item in new:
                seen.add(item.get("id"))
                updated = history.iso_from_epoch(item.get("updated_at"))
                yield history.ChatSummary(
                    provider=self.name,
                    remote_id=item.get("id") or "",
                    title=item.get("title") or "",
                    created=updated,
                    updated=updated,
                    model=item.get("model_type") or "",
                    url=f"{self.origin}/a/chat/s/{item.get('id')}",
                )
                count += 1
                if limit and count >= limit:
                    return
            last = sessions[-1]
            cursor = {
                "pinned": bool(last.get("pinned")),
                "updated_at": last.get("updated_at"),
                "id": last.get("id"),
            }
            if not biz.get("has_more"):
                return

    def fetch_chat(self, match, remote_id):
        """Fetch one session's full message set and normalise the fragments."""
        headers = self.history_headers(match)
        data = net.request_json(
            f"{self.origin}{HISTORY_PATH}?chat_session_id={remote_id}",
            headers=headers, timeout=180)
        biz = self._biz(data) or {}
        session = biz.get("chat_session") or {}
        raw = biz.get("chat_messages") or []
        chat = history.Chat(
            provider=self.name,
            remote_id=remote_id,
            title=session.get("title") or "",
            created=history.iso_from_epoch(session.get("inserted_at")),
            updated=history.iso_from_epoch(session.get("updated_at")),
            model=session.get("model_type") or "",
            url=f"{self.origin}/a/chat/s/{remote_id}",
        )
        for message in _active_messages(raw, session.get("current_message_id")):
            chat.messages.extend(_history_messages(message))
        if not chat.created and chat.messages:
            chat.created = chat.messages[0].created
        if not chat.updated:
            chat.updated = history.newest(m.created for m in chat.messages)
        return chat


def _active_messages(messages, current_id=None):
    """The branch ending at *current_id* (or every message, in order)."""
    if current_id is not None:
        by_id = {message.get("message_id"): message for message in messages
                 if isinstance(message, dict)}
        chain = []
        seen = set()
        cursor = current_id
        while cursor in by_id and cursor not in seen:
            seen.add(cursor)
            message = by_id[cursor]
            chain.append(message)
            cursor = message.get("parent_id")
        if chain:
            chain.reverse()
            return chain
    return sorted(messages, key=lambda m: m.get("message_id") or 0)


def _history_messages(message):
    """Normalise one DeepSeek message (with its fragments) into messages."""
    role = (message.get("role") or "").lower()
    created = history.iso_from_epoch(message.get("inserted_at"))
    message_id = str(message.get("message_id") or "")
    parent_id = str(message.get("parent_id") or "")
    out = []
    for fragment in message.get("fragments") or []:
        text = (fragment.get("content") or "").strip()
        if not text:
            continue
        ftype = (fragment.get("type") or "").upper()
        if ftype == "THINK":
            kind = history.THINKING
        elif ftype == "REQUEST":
            kind = history.TEXT
        else:
            kind = history.TEXT
        out.append(history.Message(
            role=history.USER if role == "user" else history.ASSISTANT,
            text=text, kind=kind, created=created,
            message_id=message_id, parent_id=parent_id))
    thinking = (message.get("thinking_content") or "").strip()
    if thinking:
        out.append(history.Message(
            role=history.ASSISTANT, text=thinking, kind=history.THINKING,
            created=created, message_id=message_id, parent_id=parent_id))
    for item in message.get("files") or []:
        name = item.get("file_name") or item.get("name") or ""
        if name:
            out.append(history.Message(
                role=history.USER if role == "user" else history.ASSISTANT,
                text=name, kind=history.ATTACHMENT, created=created,
                message_id=message_id, parent_id=parent_id))
    return out


def _find_file(node, file_id):
    """Find the ``{"id": file_id, ...}`` record inside a nested response."""
    if isinstance(node, dict):
        if node.get("id") == file_id and "status" in node:
            return node
        for value in node.values():
            found = _find_file(value, file_id)
            if found:
                return found
    elif isinstance(node, list):
        for value in node:
            found = _find_file(value, file_id)
            if found:
                return found
    return None


class _Conversation(Conversation):
    """A DeepSeek chat: one session, threaded by ``parent_message_id``."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        self.provider = provider
        self.headers = provider._headers(match)
        self.token = provider._token(match)
        self.thinking = model in ("deepseek_reasoner", "reasoner")
        self.parent_message_id = (resume or {}).get("parent_message_id")

    def send(self, prompt, files=None):
        if not self.token:
            yield ev.error("no DeepSeek token in web storage; log in to chat.deepseek.com")
            return
        headers = dict(self.headers)
        headers["Authorization"] = "Bearer " + self.token
        try:
            if not self.conversation_id:
                self.conversation_id = self.provider._create_session(headers) or ""
            pow_header = self.provider._pow(headers)
        except (net.StreamError, PowError) as exc:
            yield ev.error(str(exc))
            return
        if pow_header:
            headers["x-ds-pow-response"] = pow_header
        ref_ids = []
        try:
            attachments = self._attachments(files)
        except files_mod.FileError as exc:
            yield ev.error(str(exc))
            return
        for attachment in attachments:
            try:
                record = self.provider._upload(headers, attachment)
            except (net.StreamError, files_mod.FileError) as exc:
                yield ev.error(str(exc))
                return
            status = record.get("status")
            if status and status not in FILE_READY:
                yield ev.error(
                    f"{attachment.name}: the site could not read the file "
                    f"({status}); split it into smaller files or shorten the prompt")
                return
            ref_ids.append(record["id"])
            yield ev.tool_call(name="upload", status="done", args=attachment.name)
        first = self.turns == 0
        content = f"{self.system}\n\n{prompt}" if (self.system and first) else prompt
        body = {
            "chat_session_id": self.conversation_id,
            "parent_message_id": self.parent_message_id,
            "model_type": "default",
            "prompt": content,
            "ref_file_ids": ref_ids,
            "thinking_enabled": self.thinking,
            "search_enabled": False,
            "action": None,
            "preempt": False,
        }
        parser = _Parser()
        answer = []
        thinking = []
        try:
            yield from self._consume(
                self.provider.origin + COMPLETION_PATH, headers, body,
                parser, answer, thinking)
        except net.StreamError as exc:
            yield ev.error(str(exc))
            return

        # The site truncates an answer that ran long (or whose reasoning did)
        # and stops without finishing the message; the web UI then shows a
        # "Continue" button.  Ask for the rest, appending to the same answer.
        continuations = 0
        while (continuations < MAX_CONTINUATIONS and self._incomplete(parser)
               and isinstance(parser.message_id, int)):
            continuations += 1
            yield ev.tool_call(name="continue", status="auto",
                               args=f"message {parser.message_id}")
            headers = dict(self.headers)
            headers["Authorization"] = "Bearer " + self.token
            try:
                pow_header = self.provider._pow(headers, CONTINUE_PATH)
                if pow_header:
                    headers["x-ds-pow-response"] = pow_header
            except (net.StreamError, PowError) as exc:
                yield ev.error(str(exc))
                break
            body = {
                "chat_session_id": self.conversation_id,
                "message_id": parser.message_id,
            }
            previous_id = parser.message_id
            parser = _Parser(skip_response=len("".join(answer)),
                             skip_thinking=len("".join(thinking)))
            try:
                yield from self._consume(
                    self.provider.origin + CONTINUE_PATH, headers, body,
                    parser, answer, thinking)
            except net.StreamError as exc:
                yield ev.error(str(exc))
                break
            if not isinstance(parser.message_id, int):
                parser.message_id = previous_id
        self.turns += 1
        if isinstance(parser.message_id, int):
            self.parent_message_id = parser.message_id
            self.resume["parent_message_id"] = parser.message_id
        self._record("user", prompt)
        self._record("assistant", "".join(answer))
        yield ev.done()

    def _consume(self, url, headers, body, parser, answer, thinking):
        """Stream one response through *parser*, filling *answer*/*thinking*."""
        for line in net.stream_lines(url, headers=headers, json_body=body,
                                     timeout=self.timeout, debug=self.debug):
            for payload in net.sse_payloads([line]):
                try:
                    obj = json.loads(payload)
                except ValueError:
                    if payload:
                        yield ev.raw(payload)
                    continue
                for event in parser.feed(obj):
                    if event.kind == ev.TEXT:
                        answer.append(event.text)
                    elif event.kind == ev.THINKING:
                        thinking.append(event.text)
                    yield event
            if self.debug and line and not line.startswith(("data:", "event:")):
                yield ev.raw(line)

    @staticmethod
    def _incomplete(parser):
        """True when the site closed the stream before the message finished."""
        behavior = (parser.close or {}).get("click_behavior")
        if behavior and behavior != "none":
            return True
        return bool(parser.status and parser.status != "FINISHED")


class _Parser:
    """Turn DeepSeek's fragment-patch frames into deltas."""

    def __init__(self, skip_response=0, skip_thinking=0):
        self.fragments = []
        self.usage = None
        self.message_id = None
        self.status = None
        self.close = None
        # A continuation's first snapshot repeats the fragments already shown;
        # these counts drop that prefix so nothing is emitted twice.
        self.skip = {"RESPONSE": skip_response, "THINK": skip_thinking}

    def feed(self, obj):
        if not isinstance(obj, dict):
            return []
        if "click_behavior" in obj:
            self.close = obj
            return []
        value = obj.get("v")
        if isinstance(value, dict) and isinstance(value.get("response"), dict):
            return self._snapshot(value["response"])
        if isinstance(obj.get("response"), dict):
            return self._snapshot(obj["response"])
        path = obj.get("p")
        if path is None:
            if isinstance(value, str):
                return self._content(value)
            return []
        op = obj.get("o") or "APPEND"
        if path in ("response/status", "quasi_status"):
            if isinstance(value, str):
                self.status = value
            return []
        if op == "BATCH" and isinstance(value, list):
            out = []
            for sub in value:
                out += self.feed(sub)
            return out
        if path == "response/fragments" and op == "APPEND":
            out = []
            for fragment in value if isinstance(value, list) else [value]:
                if isinstance(fragment, dict):
                    self.fragments.append(fragment.get("type") or "RESPONSE")
                    out += self._content(fragment.get("content") or "")
            return out
        if path.endswith("/content") and op in ("APPEND", "SET"):
            return self._content(value if isinstance(value, str) else "")
        if path.endswith("/accumulated_token_usage") and isinstance(value, int):
            self.usage = value
        return []

    def _snapshot(self, response):
        out = []
        if isinstance(response.get("message_id"), int):
            self.message_id = response["message_id"]
        for fragment in response.get("fragments") or []:
            if not isinstance(fragment, dict):
                continue
            kind = fragment.get("type") or "RESPONSE"
            content = fragment.get("content") or ""
            skip = self.skip.get(kind) or 0
            if skip:
                drop = min(skip, len(content))
                content = content[drop:]
                self.skip[kind] = skip - drop
            self.fragments.append(kind)
            out += self._content(content)
        if isinstance(response.get("accumulated_token_usage"), int):
            self.usage = response["accumulated_token_usage"]
        return out

    def _content(self, text):
        if not text:
            return []
        kind = self.fragments[-1] if self.fragments else "RESPONSE"
        if kind == "THINK":
            return [ev.thinking(text)]
        if kind == "RESPONSE":
            return [ev.text(text)]
        return [ev.tool_call(name=kind.lower(), status="delta", args=text)]
