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
from .. import net
from .. import sentinel
from .. import session as session_mod
from . import Conversation, Provider

BASE = "https://chatgpt.com"
POLL_INTERVAL = 0.5
ROOT_MESSAGE = "client-created-root"


class ChatGPT(Provider):
    name = "chatgpt"
    label = "ChatGPT"
    origin = BASE
    session_target = "chatgpt.com"
    default_model = "auto"
    models = ("auto", "gpt-5", "gpt-5-mini", "gpt-4o", "gpt-4o-mini", "o3", "o4-mini")

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

    # ------------------------------------------------------------------ payloads

    def _timezone(self):
        return time.timezone // 60, time.tzname[0]

    @staticmethod
    def _message(text, role="user"):
        return {
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

    def send(self, prompt):
        if not self._ready:
            try:
                self._prepare()
            except (net.StreamError, sentinel.SentinelError) as exc:
                yield ev.error(str(exc))
                return
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
                    [self.provider._message(message)], self.model,
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
    if isinstance(part, dict):
        return str(part.get("text") or "")
    return ""


def _is_finished(message):
    if (message.get("author") or {}).get("role") != "assistant":
        return False
    metadata = message.get("metadata") or {}
    return (message.get("status") == "finished_successfully"
            or bool(metadata.get("finish_details")))


class _MessageStream:
    """Turn cumulative conversation messages into deltas (deduplicated by id)."""

    def __init__(self):
        self.text = 0
        self.thinking = 0
        self.tool = None
        self.emitted = False
        self.answer_parts = []
        self.last_assistant_id = ""

    @property
    def answer(self):
        return "".join(self.answer_parts)

    def feed(self, event):
        out = []
        message = event.get("message")
        if isinstance(message, dict):
            out += self.feed_message(message)
        if isinstance(event.get("v"), dict):
            out += self.feed(event["v"])
        return out

    def feed_message(self, message):
        out = []
        if (message.get("author") or {}).get("role") != "assistant":
            return out
        identifier = message.get("id")
        if identifier:
            self.last_assistant_id = str(identifier)
        content = message.get("content") or {}
        ctype = content.get("content_type")
        recipient = message.get("recipient") or "all"
        if recipient not in ("all", "", None) and recipient != self.tool:
            self.tool = recipient
            out.append(ev.tool_call(name=recipient, status="start"))
        if ctype in ("text", "multimodal_text"):
            full = "".join(_part_text(part) for part in content.get("parts") or [])
            if len(full) > self.text:
                delta = full[self.text:]
                out.append(ev.text(delta))
                self.answer_parts.append(delta)
                self.text = len(full)
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
