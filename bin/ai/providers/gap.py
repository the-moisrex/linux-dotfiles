"""GapGPT (gapgpt.app) adapter.

GapGPT is a multi-model front end whose chat runs over its **own WebSocket
protocol** rather than HTTP/SSE.  The composer sends a ``new_message`` event
carrying a ``text_message`` action; the server answers ``ack_new_message``
(which hands back the chat token for a brand-new chat plus the turn's message
token) and then pushes ``new_message`` snapshots of that message, each holding
the assistant ``response`` fragments (``text``, ``reasoning``, ``agent_turn``,
``canvas`` …).  This adapter replays that socket with the signed-in cookies and
diffs the fragments into deltas.

Continuation is by ``chat_token`` alone — the server threads the chat, so there
is no message-id bookkeeping like ChatGPT/DeepSeek.

The protocol was captured with ``bin/browser.record https://gapgpt.app``: the
socket is ``wss://gapgpt.app/ws/salam``, the envelope is
``{"event", "rid", "data", "version": 98}``, the server is kept alive with a
``heartbeat``/``heartbeat_ack`` pair, and auth is the ``accessToken`` cookie on
the handshake (the site omits the envelope's ``token`` field under cookie auth).
A turn can also be read without the socket — ``GET
/api/v1/get_chat/token/<chat_token>`` returns the same fragments — which is used
here as a fallback when the socket goes quiet or drops.

The site's edge (ArvanCloud) intermittently stalls the upgrade — a handshake or
write that times out, or a garbled response line — so the connect plus the first
frame are retried a few times, and writes get their own generous timeout
(separate from the short read slice used to poll and heartbeat).

``--file`` uploads through ``POST /api/v1/upload_file`` (multipart ``file``,
authenticated with the ``accessToken`` cookie as a bearer token) and passes the
returned ``file_token`` list in the action's ``file_tokens``.
"""

from __future__ import annotations

import json
import random
import sys
import time
import uuid
from urllib.parse import parse_qs, urlencode, urlparse, urlunparse

from .. import events as ev
from .. import files as files_mod
from .. import history
from .. import net
from .. import session as session_mod
from . import Conversation, Provider

WS_PATH = "/ws/salam"
EVENT_VERSION = 98
CHAT_PATH = "/api/v1/get_chat/token/{token}"
UPLOAD_PATH = "/api/v1/upload_file"
PROJECTS_PATH = "/api/v1/projects"
CHAT_LIST_PAGE = 20       # smaller pages localise a stalled listing request
CHAT_LIST_PATH = f"/api/v1/chat_list?limit={CHAT_LIST_PAGE}&offset=0"

#: The site tarpits individual history requests.  Retry them in place instead
#: of restarting the whole listing, then skip what still cannot be read.
HISTORY_TIMEOUT = 20.0
HISTORY_ATTEMPTS = 3
HISTORY_BACKOFF = (0.5, 1.5)

HEARTBEAT = 12.0        # send a heartbeat after this many idle seconds
RECV_TIMEOUT = 2.0      # socket read slice, so we can heartbeat/poll
SEND_TIMEOUT = 60.0     # writing one frame (large prompt + slow edge: 700 KiB ~ 17 s)
CONNECT_TIMEOUT = 20.0  # per-attempt websocket handshake timeout
CONNECT_ATTEMPTS = 3    # the site's edge is flaky; retry a failed handshake
CONNECT_BACKOFF = (0.5, 1.5)
POLL_AFTER = 4.0        # socket silence before falling back to get_chat
ACK_TIMEOUT = 90.0      # how long the server has to acknowledge a message

#: Friendly labels accepted by ``--model`` (the site's own ids are also accepted).
MODEL_ALIASES = {
    "gap": "A-GAP",
    "gpt-6-astra": "A-GPT-6-ASTRA",
    "gpt6": "A-GPT-6-ASTRA",
    "gpt-4o": "A-GPT4O",
    "gpt-4o-mini": "A-GPT4O-MINI",
    "gpt-4.1": "A-GPT-4.1",
    "gpt-5": "A-GPT-5",
    "o1": "A-O1",
    "o4-mini": "A-O4-MINI",
    "gemini": "A-GEMINI",
    "gemini-pro": "A-GEMINI-3-PRO",
    "claude": "A-CLAUDE",
    "sonnet": "A-SONNET",
    "opus": "A-OPUS",
    "deepseek": "A-DEEPSEEK",
    "deepseek-r1": "A-DEEPSEEK-R1",
    "deepseek-v3": "A-DEEPSEEK-V3",
    "grok": "A-GROK",
    "grok-3": "A-GROK-3",
    "grok-4": "A-GROK-4",
    "kimi": "A-KIMI",
}

#: Fragment types that carry the model's reasoning rather than the answer.
_THINKING_TYPES = ("reasoning", "agent_turn", "thinking", "thoughts")
_SKIP_TYPES = ("text-loading",)

#: ``message.extra_display.status`` / ``message.status`` values that end a turn.
_DONE_STATUS = ("completed", "stopped")
_FAILED_STATUS = ("error", "failed")


def _history_json(url, headers):
    """GET a history endpoint, retrying transient failures in place."""
    last = None
    for attempt in range(HISTORY_ATTEMPTS):
        try:
            return net.request_json(url, headers=headers, timeout=HISTORY_TIMEOUT)
        except Exception as exc:  # noqa: BLE001 - classify, then re-raise
            last = exc
            if attempt + 1 >= HISTORY_ATTEMPTS or not net.is_transient(exc):
                raise
            wait = HISTORY_BACKOFF[min(attempt, len(HISTORY_BACKOFF) - 1)]
            print(f"gap: history: retrying in {wait:.1f}s ({exc})",
                  file=sys.stderr)
            time.sleep(wait)
    raise last


def _auth_failure(exc):
    """An expired session must fail the listing, not be skipped as flaky."""
    text = str(exc).casefold()
    return any(marker in text for marker in
               ("401", "403", "unauthorized", "forbidden"))


def _chat_list_progress(url):
    """Return ``(offset, page_size)`` from a chat-list URL."""
    query = parse_qs(urlparse(url).query)
    try:
        offset = int((query.get("offset") or ["0"])[0])
        page = int((query.get("limit") or [str(CHAT_LIST_PAGE)])[0])
    except ValueError:
        return None, CHAT_LIST_PAGE
    return offset, max(1, page)


def _chat_list_url(url, offset):
    """Rebuild *url* with a new ``offset`` (used after skipping a page)."""
    parts = urlparse(url)
    query = parse_qs(parts.query)
    query["offset"] = [str(offset)]
    return urlunparse(parts._replace(query=urlencode(query, doseq=True)))


class Gap(Provider):
    name = "gap"
    label = "GapGPT"
    origin = "https://gapgpt.app"
    session_target = "gapgpt.app"
    default_model = "A-GAP"
    models = ("A-GAP", "A-GPT-6-ASTRA", "A-GPT-5.5", "A-GPT4O", "A-GPT4O-MINI",
              "A-GPT-4.1", "A-O1", "A-O4-MINI", "A-GEMINI-3-PRO", "A-CLAUDE",
              "A-SONNET-4.6", "A-OPUS-4.5", "A-DEEPSEEK-PRO", "A-DEEPSEEK-R1",
              "A-GROK-4", "A-KIMI-K3")
    #: ``A-GAP`` accepted 150 KB of ASCII but refused 200 KB, and 30 KB of CJK
    #: was accepted while 60 KB hit ``token_limit``.  The routed model's token
    #: window is the real limit; 24 KiB stays clear of it.
    inline_limit = 24 * 1024

    @classmethod
    def chosen_model(cls, requested):
        """Resolve a site id (``A-GPT4O``) or a friendly label (``gpt-4o``)."""
        if not requested:
            return cls.default_model
        key = requested.strip()
        if key.upper().startswith("A-"):
            return key.upper()
        return MODEL_ALIASES.get(key.lower().replace(" ", "-"), key)

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)

    def _ws_url(self):
        return "wss://" + self.origin.split("://", 1)[-1] + WS_PATH

    def _upload(self, match, attachment):
        """Upload one attachment and return the site's ``file_token``."""
        headers = session_mod.build_headers(match, {
            "Accept": "application/json, text/plain, */*",
            "Referer": self.origin + "/",
            "Origin": self.origin,
        })
        token = session_mod.cookie_value(match, "accessToken")
        if token:
            headers["Authorization"] = "Bearer " + token
        body, content_type = net.encode_multipart([
            ("file", (attachment.name, attachment.data, attachment.mime)),
        ])
        headers["Content-Type"] = content_type
        data = net.request_json(self.origin + UPLOAD_PATH, method="POST",
                                headers=headers, data=body, timeout=300)
        file_token = (data or {}).get("file_token")
        if not file_token:
            raise net.StreamError(f"upload failed: {data}")
        return file_token

    def _action(self, model, chat_token, text, file_tokens=()):
        """The ``text_message`` action the composer builds (captured shape)."""
        mode_extra = {}
        if model == "A-GAP":
            mode_extra = {"model_mode": "auto", "model_routing_preference": "auto",
                          "selected_simplified_model": "A-GAP",
                          "routed_chat_model": "A-GAP"}
        return {
            "type": "text_message",
            "message_token": "",
            "bot_name": model,
            "version": 4,
            "chat_token": chat_token or "",
            "text": text,
            "file_tokens": list(file_tokens),
            "allow_downgrade": False,
            "enable_user_memory": False,
            "mode": "",
            "mode_extra": mode_extra,
            "chat_extra": {},
            "is_temporary": False,
            "messenger_browser_session": str(uuid.uuid4()),
        }

    # -- history (used by ai.save) ----------------------------------------

    def history_headers(self, match):
        """REST headers for the history endpoints (cookie token as bearer)."""
        cached = getattr(self, "_history_headers", None)
        if cached is not None and getattr(self, "_history_match", None) is match:
            return dict(cached)
        headers = session_mod.build_headers(match, {
            "Accept": "application/json, text/plain, */*",
            "Referer": self.origin + "/",
        })
        token = session_mod.cookie_value(match, "accessToken")
        if token:
            headers["Authorization"] = "Bearer " + token
        self._history_headers = headers
        self._history_match = match
        return dict(headers)

    def _summary(self, item, projects):
        token = item.get("token") or ""
        return history.ChatSummary(
            provider=self.name,
            remote_id=token,
            title=item.get("title") or "",
            created=item.get("created_at") or "",
            updated=(item.get("last_message_created_at")
                     or item.get("created_at") or ""),
            model=item.get("model") or "",
            project=projects.get(item.get("project_token")) or "",
            url=f"{self.origin}/chat/{token}",
        )

    def iter_chat_summaries(self, match, limit=None):
        """Yield every chat in the sidebar (projects first, then the rest).

        A single stalled project/page is skipped (and recorded in
        ``self.list_skips``) so a transient edge failure cannot abort the whole
        listing.  Project chats come first and are deduplicated against the
        main list, which usually covers the skipped entries.
        """
        self.list_skips = []
        headers = self.history_headers(match)
        projects = {}
        try:
            data = _history_json(self.origin + PROJECTS_PATH, headers)
        except Exception as exc:  # noqa: BLE001 - the main list can still work
            if _auth_failure(exc):
                raise
            print(f"gap: history: skipping projects ({exc})", file=sys.stderr)
            self.list_skips.append(f"projects: {exc}")
        else:
            for project in (data or {}).get("results") or []:
                token = project.get("token")
                if token:
                    projects[token] = project.get("name") or ""

        seen = set()
        count = 0
        for token, name in projects.items():
            try:
                data = _history_json(
                    f"{self.origin}/api/v1/projects/{token}", headers)
            except Exception as exc:  # noqa: BLE001 - skip one project
                if _auth_failure(exc):
                    raise
                print(f"gap: history: skipping project {name or token} "
                      f"({exc})", file=sys.stderr)
                self.list_skips.append(f"project:{token}")
                continue
            for item in (data or {}).get("chats") or []:
                chat_token = item.get("token")
                if not chat_token or chat_token in seen:
                    continue
                seen.add(chat_token)
                summary = self._summary(item, {**projects, token: name})
                summary.project = summary.project or name
                yield summary
                count += 1
                if limit and count >= limit:
                    return

        url = self.origin + CHAT_LIST_PATH
        visited = set()
        while url:
            if url in visited:
                break
            visited.add(url)
            try:
                data = _history_json(url, headers)
            except Exception as exc:  # noqa: BLE001 - skip one page
                if _auth_failure(exc):
                    raise
                offset, page = _chat_list_progress(url)
                print(f"gap: history: skipping chat list offset {offset} "
                      f"({exc})", file=sys.stderr)
                self.list_skips.append(f"chat_list:{offset}")
                if offset is None:
                    break
                url = _chat_list_url(url, offset + page)
                continue
            results = (data or {}).get("results") or []
            if not results:
                break
            for item in results:
                chat_token = item.get("token")
                if not chat_token or chat_token in seen:
                    continue
                seen.add(chat_token)
                yield self._summary(item, projects)
                count += 1
                if limit and count >= limit:
                    return
            url = (data or {}).get("next") or ""
            if url.startswith("/"):
                url = self.origin + url

    def fetch_chat(self, match, remote_id):
        """Fetch one chat and normalise its user/assistant turns."""
        headers = self.history_headers(match)
        data = net.request_json(
            self.origin + CHAT_PATH.format(token=remote_id), headers=headers,
            timeout=120) or {}
        created = data.get("created_at") or ""
        chat = history.Chat(
            provider=self.name,
            remote_id=remote_id,
            title=data.get("title") or "",
            created=created,
            updated=(data.get("last_message_created_at") or created),
            model=data.get("model") or "",
            url=f"{self.origin}/chat/{remote_id}",
        )
        for item in data.get("messages") or []:
            moment = item.get("created_at") or created
            text = item.get("text") or ""
            if text:
                chat.messages.append(history.Message(
                    role=history.USER, text=text, created=moment,
                    message_id=item.get("token") or "",
                    meta={"files": item.get("file_names") or []}))
            for response in item.get("response") or []:
                content = response.get("content") or ""
                if not content:
                    continue
                rtype = response.get("type") or ""
                if rtype in ("loading", "text-loading", "after-message-ad",
                             "file_retrieval_status"):
                    continue
                if rtype == "reasoning":
                    kind, role, name = history.THINKING, history.ASSISTANT, ""
                elif rtype in ("agentic", "canvas"):
                    kind, role, name = history.TOOL_CALL, history.ASSISTANT, rtype
                elif rtype.startswith("generated-image"):
                    kind, role, name = history.ATTACHMENT, history.ASSISTANT, rtype
                else:
                    kind, role, name = history.TEXT, history.ASSISTANT, ""
                chat.messages.append(history.Message(
                    role=role, text=content, kind=kind, created=moment,
                    message_id=response.get("block_id") or "", name=name))
        return chat

    @staticmethod
    def _status_error(status, data):
        hints = {
            "unauthorized": "the session is not signed in; open gapgpt.app in a browser",
            "free_sub_upgrade": "this model needs a paid plan",
            "sub_upgrade": "this model needs a higher plan",
            "invalid_request": "the model name was not accepted; see --model",
            "token_limit": "the prompt is longer than the model's context window; "
                           "re-run with --prompt-file to upload it as a text file, "
                           "lower --prompt-limit, or shorten/split the prompt",
            "cancelled": "the request was cancelled",
        }
        message = f"GapGPT refused the message ({status})"
        hint = hints.get(status)
        return f"{message}: {hint}" if hint else message


class _Conversation(Conversation):
    """A GapGPT chat held open over one WebSocket session."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        self.provider = provider
        # The REST history/poll endpoints need the accessToken bearer, exactly
        # like the upload path; cookie-only headers get HTTP 401, which would
        # leave the poll fallback (``_poll``) permanently blind.
        self.headers = provider.history_headers(match)
        self.socket = None
        self.turn_token = ""
        self.title = self.resume.get("title") or ""
        self.blocks = {}

    # -- socket ------------------------------------------------------------

    def _ensure(self):
        if self.socket is not None and not getattr(self.socket, "connected", True):
            self.close()
        if self.socket is not None:
            return
        import websocket  # websocket-client
        header = []
        if self.match.get("user_agent"):
            header.append(f"User-Agent: {self.match['user_agent']}")
        header.append(f"Cookie: {self.match['cookie_header']}")
        self.socket = websocket.create_connection(
            self.provider._ws_url(), timeout=CONNECT_TIMEOUT,
            origin=self.provider.origin, header=header, suppress_origin=False)

    def _send(self, event, data):
        self.socket.settimeout(SEND_TIMEOUT)
        rid = random.randint(0, 99999999)
        self.socket.send(json.dumps({"event": event, "rid": rid, "data": data,
                                     "version": EVENT_VERSION}))
        return rid

    def _receive(self):
        """Return the next frame (or None on a socket read timeout)."""
        self.socket.settimeout(RECV_TIMEOUT)
        raw = self.socket.recv()
        if isinstance(raw, bytes):
            raw = raw.decode("utf-8", "replace")
        if self.debug:
            print(f"[ws] {raw.rstrip()}", flush=True)
        try:
            frame = json.loads(raw)
        except ValueError:
            return {}
        return frame if isinstance(frame, dict) else {}

    def close(self):
        if self.socket is not None:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None

    # -- streaming ---------------------------------------------------------

    def send(self, prompt, files=None):
        try:
            import websocket  # noqa: F401  (websocket-client)
        except ImportError:
            yield ev.error("GapGPT needs the 'websocket-client' package "
                           "(pip install websocket-client)")
            return
        if not self.match.get("cookie_header"):
            yield ev.error("no cookies in the GapGPT session; log in to gapgpt.app")
            return
        try:
            attachments = self._attachments(files)
        except files_mod.FileError as exc:
            yield ev.error(str(exc))
            return
        file_tokens = []
        for attachment in attachments:
            try:
                file_tokens.append(self.provider._upload(self.match, attachment))
            except (net.StreamError, files_mod.FileError) as exc:
                yield ev.error(str(exc))
                return
            yield ev.tool_call(name="upload", status="done", args=attachment.name)

        text = (f"{self.system}\n\n{prompt}"
                if (self.system and self.turns == 0) else prompt)
        data = {
            "action": self.provider._action(self.model, self.conversation_id, text,
                                            file_tokens),
            "chat_token": self.conversation_id,
            "chat_model": self.model,
            "message_token": "",
            "allow_downgrade": False,
            "enable_user_memory": False,
        }
        rid = None
        failure = None
        for attempt in range(CONNECT_ATTEMPTS):
            try:
                self._ensure()
                rid = self._send("new_message", data)
                break
            except Exception as exc:  # noqa: BLE001 - retried below
                failure = exc
                self.close()
                if self.debug:
                    print(f"[gap] connect attempt {attempt + 1}/{CONNECT_ATTEMPTS} "
                          f"failed: {exc}", file=sys.stderr)
                if attempt < CONNECT_ATTEMPTS - 1:
                    time.sleep(CONNECT_BACKOFF[attempt])
        if rid is None:
            yield ev.error(f"could not open the GapGPT websocket: {failure}")
            return

        self.blocks = {}
        answer = []
        try:
            ok = yield from self._read_turn(rid, answer)
        except Exception as exc:  # noqa: BLE001 - surface any stream failure
            yield ev.error(f"GapGPT stream failed: {exc}")
            self.close()
            return
        if not ok:
            return
        self.turns += 1
        if self.title:
            self.resume["title"] = self.title
        self._record("user", prompt)
        self._record("assistant", "".join(answer))
        yield ev.done()

    def _read_turn(self, rid, answer):
        """Consume one turn's frames; return True when it completed cleanly."""
        started = time.time()
        deadline = started + self.timeout
        last_beat = started
        last_seen = started
        acked = False
        while time.time() < deadline:
            frame = None
            if self.socket is not None:
                try:
                    frame = self._receive()
                except Exception as exc:  # noqa: BLE001 - timeout vs a dead socket
                    if not _timed_out(exc):
                        self.close()
            now = time.time()
            if frame is None:
                if self.socket is not None and now - last_beat >= HEARTBEAT:
                    try:
                        self._send("heartbeat", {})
                    except Exception:  # noqa: BLE001
                        self.close()
                    last_beat = now
                if not acked and now - started >= ACK_TIMEOUT:
                    yield ev.error("GapGPT never acknowledged the message")
                    return False
                if acked and now - last_seen >= POLL_AFTER:
                    # A poll must never abort the turn: the socket is the
                    # primary path, and a failed poll just means "try again
                    # after the next quiet slice".
                    try:
                        snapshot, done = self._poll()
                    except Exception as exc:  # noqa: BLE001 - never fatal
                        if self.debug:
                            print(f"[gap] poll failed: {exc}", file=sys.stderr)
                        snapshot, done = None, False
                    last_seen = now
                    if snapshot is not None:
                        for event in self._apply(snapshot, answer):
                            yield event
                    if done:
                        return True
                else:
                    time.sleep(0.1 if self.socket is not None else 0.3)
                continue

            event = frame.get("event") or ""
            data = frame.get("data") if isinstance(frame.get("data"), dict) else {}
            if event in ("heartbeat_ack", "event_ack"):
                continue

            if not acked:
                if event == "ack_new_message" and frame.get("rid") == rid:
                    status = data.get("status")
                    if status != "ok":
                        yield ev.error(self.provider._status_error(status, data))
                        return False
                    self.conversation_id = data.get("chat_token") or self.conversation_id
                    self.turn_token = (data.get("message") or {}).get("token") or ""
                    acked = True
                    last_seen = time.time()
                    for item in self._apply(data.get("message"), answer):
                        yield item
                elif event in ("error", "session.error"):
                    yield ev.error(str(data or event)[:300])
                    return False
                continue

            if event == "new_message":
                message = data.get("message") or {}
                if self.turn_token and message.get("token") != self.turn_token:
                    continue
                last_seen = time.time()
                for item in self._apply(message, answer):
                    yield item
                if _failed(message):
                    yield ev.error(str(message.get("error") or "the model reported an error")[:300])
                    return False
                if _completed(message):
                    return True
            elif event == "chat_title_update":
                if data.get("chat_token") == self.conversation_id and data.get("title"):
                    self.title = str(data["title"])
            elif event in ("error", "session.error"):
                yield ev.error(str(data or event)[:300])
                return False
        yield ev.error("GapGPT timed out waiting for the answer")
        return False

    def _apply(self, message, answer):
        """Diff one message snapshot's fragments into text/thinking/tool deltas."""
        if not isinstance(message, dict):
            return
        for block in message.get("response") or []:
            if not isinstance(block, dict):
                continue
            kind = block.get("type") or ""
            content = block.get("content")
            if content is None or kind in _SKIP_TYPES:
                continue
            identifier = block.get("block_id") or kind
            seen = self.blocks.get(identifier, 0)
            if len(content) <= seen:
                continue
            self.blocks[identifier] = len(content)
            delta = content[seen:]
            if kind == "text":
                answer.append(delta)
                yield ev.text(delta)
            elif kind in _THINKING_TYPES:
                yield ev.thinking(delta)
            elif kind == "canvas":
                yield ev.tool_call(name="canvas", status="delta", args=delta[:400])
            else:
                yield ev.tool_call(name=kind or "tool", status="delta", args=delta[:400])

    def _poll(self):
        """Fallback: read the turn from ``GET /api/v1/get_chat/token/<token>``."""
        if not self.conversation_id:
            return None, False
        try:
            data = net.request_json(
                self.provider.origin + CHAT_PATH.format(token=self.conversation_id),
                headers=self.headers, timeout=30)
        except Exception as exc:  # noqa: BLE001 - the socket is the primary path
            if self.debug:
                print(f"[gap] poll failed: {exc}", file=sys.stderr)
            return None, False
        messages = (data or {}).get("messages") or []
        chosen = None
        for message in messages:
            if not isinstance(message, dict):
                continue
            if self.turn_token and message.get("token") == self.turn_token:
                chosen = message
            elif not self.turn_token and message.get("response"):
                chosen = message
        if chosen is not None and not self.turn_token:
            self.turn_token = chosen.get("token") or ""
        if chosen is None:
            return None, False
        return chosen, _completed(chosen)


def _completed(message):
    if not isinstance(message, dict):
        return False
    if (message.get("extra_display") or {}).get("status") in _DONE_STATUS:
        return True
    return message.get("status") in _DONE_STATUS


def _failed(message):
    if not isinstance(message, dict):
        return False
    if (message.get("extra_display") or {}).get("status") in _FAILED_STATUS:
        return True
    return message.get("status") in _FAILED_STATUS


def _timed_out(exc):
    return isinstance(exc, TimeoutError) or type(exc).__name__ in (
        "WebSocketTimeoutException", "timeout")
