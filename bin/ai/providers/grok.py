"""Grok (grok.com) adapter.

Grok no longer streams a turn over HTTP/SSE: the composer opens an application
WebSocket at ``wss://grok.com/ws/mgw/?uid=<client-id>`` and drives the turn with
``session.create`` / ``response.create`` events, receiving ``response.chunk``
frames that carry the answer, its "notetaker" status headers and, for reasoning
models, the thinking text.  We replay that socket with the signed-in cookies and
translate the frames into the shared event model.

A conversation keeps that socket open and sends a fresh ``response.create`` per
turn, so ``-i`` continues in place.  The socket (and its session id) only exists
for the lifetime of the process, so a resumed conversation from a *new* process
replays its stored transcript as context on the first turn instead of
reattaching.

The site computes a ``castle_request_token`` for each turn, but the server
accepts a request without it (verified from a captured turn replayed over the
socket), so it is deliberately omitted here.

.. note::
   Protocol confirmed with ``bin/browser.record https://grok.com`` (Phase 0) and
   verified end to end against the live site.
"""

from __future__ import annotations

import json
import sys
import time
import uuid

from .. import events as ev
from .. import session as session_mod
from . import Conversation, Provider

WS_URL = "wss://grok.com/ws/mgw/?uid={uid}"

#: How many transcript entries to replay when a conversation is resumed from a
#: new process (the socket cannot be reattached).
MAX_REPLAY = 20

# The capability block the web client advertises in ``session.create``.
CAPABILITIES = {
    "protocol_capabilities": [
        "conversation_attached", "custom_methods_v1", "workspace_servers_v1"],
    "use_chunk": True,
    "accept_interleaved_phases": True,
    "client_side_toolsets": ["connectors-v3"],
    "enable_side_by_side": True,
    "force_side_by_side": False,
    "enable_image_generation": True,
    "image_generation_count": 2,
    "disable_text_follow_ups": False,
    "disable_artifact": True,
    "force_concise": False,
}

# Channels the site stamps on a ``response.chunk``; anything unrecognised is
# shown as reasoning rather than leaking into the answer.
_CHANNEL_RESPONSE = ("RESPONSE",)
_CHANNEL_TOOL = ("TOOL", "SEARCH", "BROWSE", "CODE", "WIDGET")


def _client_id(match):
    """The persistent ``uid`` the site uses for the socket (best effort).

    Grok keeps it in the workspace's marketing-prompt key; ``__cuid`` is a
    close second.  The ``grok_device_id`` cookie is *not* the socket id — the
    server drops the connection when it is sent as ``uid``.
    """
    for key in session_mod.localstorage(match):
        if key.startswith("dismissed:marketing-opt-in-prompt:"):
            return key.rsplit(":", 1)[-1]
    return (session_mod.storage_value(match, "__cuid")
            or str(uuid.uuid4()))


class Grok(Provider):
    name = "grok"
    label = "Grok"
    origin = "https://grok.com"
    session_target = "grok.com"
    default_model = "fast"
    models = ("fast", "expert", "grok-4", "grok-3", "grok-3-mini", "grok-3-thinking")

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)

    # ----------------------------------------------------------------- socket

    def _connect(self, websocket, match, timeout):
        headers = []
        if match.get("user_agent"):
            headers.append(f"User-Agent: {match['user_agent']}")
        headers.append(f"Cookie: {match['cookie_header']}")
        socket = websocket.create_connection(
            WS_URL.format(uid=_client_id(match)), timeout=min(timeout, 120),
            origin=self.origin, header=headers, suppress_origin=False)
        socket.settimeout(timeout)
        return socket

    def _session_create(self, socket, model, debug):
        self._send(socket, {"event": {
            "type": "session.create",
            "event_id": "evt_init_" + str(uuid.uuid4()),
            "session": {"model": model, "x_grok": CAPABILITIES},
        }})
        return self._await_session(socket, debug)

    def _response_create(self, socket, session_id, prompt):
        self._send(socket, {"session_id": session_id, "event": {
            "type": "response.create",
            "event_id": "evt_resp_" + str(int(time.time() * 1000)),
            "item": {
                "type": "message",
                "role": "user",
                "x_grok": {
                    "client_message_id": str(uuid.uuid4()),
                    "input_chunks": [{"text": {"text": prompt}}],
                },
            },
        }})
        self._send(socket, {"session_id": session_id, "event": {
            "type": "presence.update", "visible": True}})

    def _send(self, socket, payload):
        socket.send(json.dumps(payload))

    def _receive(self, socket, debug):
        raw = socket.recv()
        if isinstance(raw, bytes):
            raw = raw.decode("utf-8", "replace")
        if debug:
            print(f"[ws] {raw}", file=sys.stderr)
        try:
            frame = json.loads(raw)
        except ValueError:
            return {"event": {"type": "raw", "raw": raw}}
        return frame if isinstance(frame, dict) else {"event": {"type": "raw",
                                                                "raw": raw}}

    def _await_session(self, socket, debug):
        deadline = time.time() + 60
        while time.time() < deadline:
            frame = self._receive(socket, debug)
            event = frame.get("event") or {}
            kind = event.get("type")
            if kind == "session.created":
                session_id = frame.get("session_id")
                if session_id:
                    return session_id
            elif kind in ("error", "session.error"):
                raise RuntimeError(str(event.get("error") or event)[:300])
        raise RuntimeError("the site never created a websocket session")

    def _read(self, socket, debug):
        while True:
            frame = self._receive(socket, debug)
            event = frame.get("event") or {}
            kind = event.get("type")
            if kind in ("error", "session.error"):
                yield ev.error(str(event.get("error") or event)[:300])
                return
            if kind == "response.chunk":
                chunk = event.get("chunk") or {}
                text = chunk.get("text") or {}
                body = text.get("text")
                if body:
                    yield _chunk_event(text.get("channel") or "", body)
                for call in chunk.get("tool_calls") or []:
                    if isinstance(call, dict):
                        yield ev.tool_call(name=_tool_name(call), status="delta",
                                           args=str(call.get("arguments") or "")[:400])
            elif kind == "response.output_item.added":
                item = event.get("item") or {}
                if item.get("name") or item.get("type") in ("function_call", "tool_call"):
                    yield ev.tool_call(name=item.get("name") or "tool", status="start")
            elif kind == "response.grok.output":
                output = event.get("output") or {}
                problem = output.get("stream_error") or {}
                if problem:
                    yield ev.error(problem.get("message")
                                   or "the model reported a stream error")
                    return
            elif kind == "response.done":
                usage = (event.get("response") or {}).get("usage")
                if isinstance(usage, dict) and usage:
                    yield ev.usage(**usage)
                yield ev.done()
                return
            elif kind == "raw":
                yield ev.raw(str(event.get("raw", "")))


class _Conversation(Conversation):
    """A Grok chat held open over one WebSocket session."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        self.provider = provider
        self.socket = None
        self.session_id = None
        # Seeded history means ``-c`` in a new process: the socket is gone, so
        # the next prompt carries the transcript as context.
        self._replay = bool(self.history)

    def _content(self, prompt):
        parts = []
        if self.turns == 0 and self.system:
            parts.append(self.system)
        if self._replay and self.history:
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
        return "\n\n".join(p for p in parts if p)

    def send(self, prompt):
        try:
            import websocket  # websocket-client
        except ImportError:
            yield ev.error("Grok needs the 'websocket-client' package "
                           "(pip install websocket-client)")
            return
        if not self.match.get("cookie_header"):
            yield ev.error("no cookies in the Grok session; log in to grok.com")
            return
        content = self._content(prompt)
        if self.socket is None:
            try:
                self.socket = self.provider._connect(
                    websocket, self.match, self.timeout)
                self.session_id = self.provider._session_create(
                    self.socket, self.model, self.debug)
            except Exception as exc:  # noqa: BLE001 - surface any connect failure
                yield ev.error(f"could not open the Grok websocket: {exc}")
                self.close()
                return
        self._replay = False
        answer = []
        try:
            self.provider._response_create(self.socket, self.session_id, content)
            for event in self.provider._read(self.socket, self.debug):
                if event.kind == ev.TEXT:
                    answer.append(event.text)
                yield event
        except Exception as exc:  # noqa: BLE001 - surface any stream failure
            yield ev.error(f"Grok stream failed: {exc}")
            self.close()
            return
        self.turns += 1
        if self.session_id:
            self.conversation_id = self.session_id
        self._record("user", prompt)
        self._record("assistant", "".join(answer))

    def close(self):
        if self.socket is not None:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None


def _tool_name(call):
    function = call.get("function")
    if isinstance(function, dict) and function.get("name"):
        return str(function["name"])
    return str(call.get("name") or "tool")


def _chunk_event(channel, body):
    if any(token in channel for token in _CHANNEL_RESPONSE):
        return ev.text(body)
    if any(token in channel for token in _CHANNEL_TOOL):
        return ev.tool_call(name=channel.replace("CHANNEL_", "").lower(),
                            status="delta", args=body)
    # CHANNEL_ASSISTANT_NOTETAKER_HEADER and any thinking/reasoning channel.
    return ev.thinking(body)
