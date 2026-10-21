"""Kimi (www.kimi.ai) adapter.

Replays the signed-in kimi.ai session: the ``access_token`` and
``refresh_token`` kept in the site's ``localStorage``.  A chat is created with
``POST /api/chat`` and answered by ``POST /api/chat/<id>/completion/stream``,
an SSE stream of JSON events (``ping``, ``req``, ``resp``, ``zone_set``,
``loading``, ``rename``, ``cmpl`` …).  Only ``cmpl`` carries answer text; each
event repeats its own ``event`` field, so the stream is parsed as JSON rather
than by SSE event names.

The server threads the conversation on the chat id — a follow-up only sends the
new user message, no history array — so ``-c``/``-i`` just reuse the id.

The access token is a JWT that lives 15 minutes; when it is within a minute of
expiry it is renewed with ``GET /api/auth/token/refresh`` sent with the
*refresh* token as the bearer.  The refreshed token stays in memory only.

``--file`` uploads go to ``POST /apiv2-files/file/upload`` (multipart ``file``)
and the returned ``file.id`` values travel in the completion body's ``refs``.
There is no pre-signed blob step: the older ``/api/file/upload`` endpoint hands
back a URL whose storage signature does not verify, and the web app does not
use it.

The site rejects a ``system`` role message, so ``--system`` is prepended to the
first user message instead.

A long answer is sometimes cut short and stored with
``status == MESSAGE_STATUS_TRUNCATED`` — the web UI's ``Continue`` button.  The
SSE stream does not say so, so once ``done`` arrives the assistant message is
looked up through the Connect API (``POST /apiv2/kimi.gateway.chat.v1.ChatService/
ListMessages``) and, when it is truncated, ``ContinueChat`` is asked for the
rest.  That call is a Connect server-stream (``application/connect+json``:
one flag byte, a 4-byte big-endian length and a JSON patch per frame) whose
``{"op":"append","mask":"block.text.content"}`` patches carry the continuation.
The continuation is a new child message, so the loop repeats on its id until
the status is no longer truncated.
"""

from __future__ import annotations

import base64
import json
import time

from .. import events as ev
from .. import files as files_mod
from .. import net
from .. import session as session_mod
from . import Conversation, Provider

CHAT_PATH = "/api/chat"
STREAM_PATH = "/api/chat/{chat}/completion/stream"
UPLOAD_PATH = "/apiv2-files/file/upload"
REFRESH_PATH = "/api/auth/token/refresh"
CONNECT_BASE = "/apiv2/kimi.gateway.chat.v1.ChatService"
LIST_MESSAGES_PATH = CONNECT_BASE + "/ListMessages"
CONTINUE_PATH = CONNECT_BASE + "/ContinueChat"
ACCESS_KEYS = ("access_token", "accessToken")
REFRESH_KEYS = ("refresh_token", "refreshToken")

#: Renew the access token this many seconds before it expires.
REFRESH_MARGIN = 60

#: The status the site stores on an answer it cut short (the Continue button).
TRUNCATED_STATUS = "MESSAGE_STATUS_TRUNCATED"

#: Each continuation is a new stream, so this only bounds a pathological chain.
MAX_CONTINUATIONS = 8

#: The site titles a new chat from this much of the first prompt.
TITLE_LIMIT = 60


def _limit_message(message):
    """Translate the site's conversation-length error into a remedy."""
    if not isinstance(message, str):
        message = str(message)
    if "超过" in message and ("万字" in message or "对话" in message):
        return ("Kimi: this conversation has reached the site's "
                "200,000-character limit; start a new chat (--new) or split "
                "the prompt.")
    return message


def _expires_at(token):
    """The ``exp`` claim of a JWT, or 0 when it cannot be read."""
    try:
        payload = token.split(".")[1]
        payload += "=" * (-len(payload) % 4)
        data = json.loads(base64.urlsafe_b64decode(payload))
    except (IndexError, TypeError, ValueError):
        return 0
    expires = data.get("exp") if isinstance(data, dict) else None
    return expires if isinstance(expires, (int, float)) else 0


class Kimi(Provider):
    name = "kimi"
    label = "Kimi"
    origin = "https://www.kimi.ai"
    session_target = "www.kimi.ai"
    default_model = "kimi"
    models = ("kimi",)
    #: 1 MB of ASCII was accepted inline; 2 MB hit the site's conversation
    #: limit.  The conversation cap is cumulative, so the byte budget is only
    #: about keeping a single message comfortable.
    inline_limit = 256 * 1024

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)

    # -- session -----------------------------------------------------------

    def _headers(self, match, token=None):
        headers = session_mod.build_headers(match, {
            "Accept": "application/json, text/plain, */*",
            "Referer": self.origin + "/",
            "Origin": self.origin,
        })
        if token:
            headers["Authorization"] = "Bearer " + token
        return headers

    def _token(self, match):
        return session_mod.storage_value(match, *ACCESS_KEYS)

    def _refresh(self, match):
        """Exchange the refresh token for a new access token (or ``None``)."""
        refresh = session_mod.storage_value(match, *REFRESH_KEYS)
        if not refresh:
            return None
        data = net.request_json(self.origin + REFRESH_PATH,
                                headers=self._headers(match, refresh), timeout=60)
        token = (data or {}).get("access_token")
        return token or None

    # -- api steps ---------------------------------------------------------

    def _create_chat(self, headers, prompt):
        title = " ".join((prompt or "").split())[:TITLE_LIMIT] or "New chat"
        data = net.request_json(self.origin + CHAT_PATH, method="POST",
                                headers=headers, json_body={"name": title},
                                timeout=60)
        if not isinstance(data, dict) or not data.get("id"):
            raise net.StreamError(f"could not create a chat: {data}")
        return data["id"]

    def _upload(self, headers, attachment):
        """Upload one attachment and return the site's ``file.id``."""
        body, content_type = net.encode_multipart([
            ("file", (attachment.name, attachment.data, attachment.mime)),
        ])
        request_headers = dict(headers)
        request_headers["Content-Type"] = content_type
        data = net.request_json(self.origin + UPLOAD_PATH, method="POST",
                                headers=request_headers, data=body, timeout=300)
        record = (data or {}).get("file")
        if not isinstance(record, dict) or not record.get("id"):
            raise net.StreamError(f"upload failed: {data}")
        return record["id"]

    def _connect_headers(self, headers, content_type="application/json"):
        request_headers = dict(headers)
        request_headers["Accept"] = content_type
        request_headers["Content-Type"] = content_type
        request_headers["Connect-Protocol-Version"] = "1"
        return request_headers

    def _message_status(self, headers, chat_id, message_id):
        """The stored status of one message (``""`` when it cannot be read)."""
        data = net.request_json(self.origin + LIST_MESSAGES_PATH, method="POST",
                                headers=self._connect_headers(headers),
                                json_body={"chatId": chat_id}, timeout=60)
        for message in (data or {}).get("messages") or []:
            if isinstance(message, dict) and message.get("id") == message_id:
                return message.get("status") or ""
        return ""

    @staticmethod
    def _connect_frame(payload):
        """One Connect streaming frame around a JSON payload."""
        data = json.dumps(payload).encode()
        return b"\x00" + len(data).to_bytes(4, "big") + data


class _Conversation(Conversation):
    """A Kimi chat: the server keeps the transcript, keyed by the chat id."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        self.provider = provider
        self.token = provider._token(match)

    def send(self, prompt, files=None):
        self._ensure_token()
        if not self.token:
            yield ev.error("no Kimi token in web storage; log in to www.kimi.ai")
            return
        headers = self.provider._headers(self.match, self.token)
        headers["Accept"] = "text/event-stream"
        refs = []
        try:
            attachments = self._attachments(files)
        except files_mod.FileError as exc:
            yield ev.error(str(exc))
            return
        for attachment in attachments:
            try:
                file_id = self.provider._upload(headers, attachment)
            except (net.StreamError, files_mod.FileError) as exc:
                yield ev.error(str(exc))
                return
            refs.append(file_id)
            yield ev.tool_call(name="upload", status="done", args=attachment.name)

        first = self.turns == 0 and not self.conversation_id
        content = f"{self.system}\n\n{prompt}" if (self.system and first) else prompt
        body = {"messages": [{"role": "user", "content": content}],
                "refs": refs, "use_search": False}
        if self.model and self.model != self.provider.default_model:
            body["model"] = self.model

        answer = []
        message_id = ""
        try:
            if not self.conversation_id:
                self.conversation_id = self.provider._create_chat(headers, prompt)
            url = self.provider.origin + STREAM_PATH.format(chat=self.conversation_id)
            for line in net.stream_lines(url, headers=headers, json_body=body,
                                         timeout=self.timeout, debug=self.debug):
                if not line:
                    continue
                if self.debug:
                    yield ev.raw(line)
                for payload in net.sse_payloads([line]):
                    try:
                        obj = json.loads(payload)
                    except ValueError:
                        if payload:
                            yield ev.raw(payload)
                        continue
                    event = obj.get("event") if isinstance(obj, dict) else None
                    if event == "cmpl":
                        text = obj.get("text") or ""
                        if text:
                            answer.append(text)
                            yield ev.text(text)
                    elif event == "resp":
                        message_id = obj.get("id") or message_id
                    elif event == "error":
                        error = obj.get("error") or {}
                        if isinstance(error, dict):
                            message = error.get("message") or "Kimi returned an error"
                        elif isinstance(error, str):
                            message = error
                        else:
                            message = "Kimi returned an error"
                        yield ev.error(_limit_message(message))
                        return
        except net.StreamError as exc:
            yield ev.error(str(exc))
            return

        # The site stores a cut-short answer as MESSAGE_STATUS_TRUNCATED and
        # shows a "Continue" button; the SSE stream itself never says so, so
        # the message has to be looked up.  ContinueChat answers with a new
        # child message, hence the loop on the latest id.
        continuations = 0
        while (continuations < MAX_CONTINUATIONS and message_id
               and self._status(headers, message_id) == TRUNCATED_STATUS):
            continuations += 1
            yield ev.tool_call(name="continue", status="auto",
                               args=f"message {message_id}")
            try:
                message_id, status = yield from self._continue(
                    headers, message_id, answer)
            except net.StreamError as exc:
                yield ev.error(str(exc))
                break
            if status != TRUNCATED_STATUS:
                break

        self.turns += 1
        self._record("user", prompt)
        self._record("assistant", "".join(answer))
        yield ev.done()

    def _status(self, headers, message_id):
        """The site's status for one message, or ``""`` when unavailable."""
        try:
            return self.provider._message_status(
                headers, self.conversation_id, message_id)
        except net.StreamError:
            return ""

    def _continue(self, headers, message_id, answer):
        """Stream the rest of a truncated answer; return (id, final status).

        Yields the continuation's text events.  The response is Connect
        server-streaming, so each frame is a length-prefixed JSON patch rather
        than an SSE line; ``block.text.content`` patches carry the text.
        """
        request_headers = self.provider._connect_headers(
            headers, "application/connect+json")
        body = self.provider._connect_frame(
            {"chatId": self.conversation_id, "messageId": message_id})
        buffer = b""
        texts = {}
        new_id = message_id
        status = ""
        for chunk in net.stream_chunks(self.provider.origin + CONTINUE_PATH,
                                       headers=request_headers, data=body,
                                       timeout=self.timeout, debug=self.debug):
            buffer += chunk
            while len(buffer) >= 5:
                flag = buffer[0]
                length = int.from_bytes(buffer[1:5], "big")
                if len(buffer) < 5 + length:
                    break
                payload = buffer[5:5 + length]
                buffer = buffer[5 + length:]
                if flag == 2:
                    return new_id, status
                if flag != 0:
                    continue
                try:
                    patch = json.loads(payload)
                except ValueError:
                    continue
                if not isinstance(patch, dict):
                    continue
                message = patch.get("message") or {}
                if message.get("id"):
                    new_id = message["id"]
                if message.get("status"):
                    status = message["status"]
                block = patch.get("block") or {}
                text = ((block.get("text") or {}).get("content"))
                if not isinstance(text, str) or not text:
                    continue
                block_id = block.get("id") or ""
                previous = texts.get(block_id, "")
                if patch.get("op") == "append":
                    texts[block_id] = previous + text
                    answer.append(text)
                    yield ev.text(text)
                elif text.startswith(previous) and len(text) > len(previous):
                    delta = text[len(previous):]
                    texts[block_id] = text
                    answer.append(delta)
                    yield ev.text(delta)
        return new_id, status

    def _ensure_token(self):
        """Renew an expiring access token (best effort; keep the old one)."""
        if self.token and _expires_at(self.token) > time.time() + REFRESH_MARGIN:
            return
        try:
            refreshed = self.provider._refresh(self.match)
        except net.StreamError:
            return
        if refreshed:
            self.token = refreshed
