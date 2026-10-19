"""ChatGPT (chatgpt.com) adapter.

Replays the site's own conversation endpoints with the signed-in cookies: the
``/api/auth/session`` access token, the sentinel gate (requirements token,
proof-of-work and turnstile — see :mod:`ai.sentinel`) and the ``f/conversation``
conduit.  A turn is answered either on the ``f/conversation`` SSE stream or,
when the site hands the turn off to its WebSocket, by reading
``GET /backend-api/conversation/<id>`` until the assistant message completes.

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
from . import Provider

BASE = "https://chatgpt.com"
POLL_INTERVAL = 0.5


class ChatGPT(Provider):
    name = "chatgpt"
    label = "ChatGPT"
    origin = BASE
    session_target = "chatgpt.com"
    default_model = "auto"
    models = ("auto", "gpt-5", "gpt-5-mini", "gpt-4o", "gpt-4o-mini", "o3", "o4-mini")

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
        result = {"token": finalized.get("token") or "",
                  "proof_token": proof, "turnstile_token": turnstile,
                  "raw": finalized,
                  "headers": {}}
        if not result["token"]:
            raise sentinel.SentinelError(
                "the sentinel gate returned no chat-requirements token")
        result["headers"] = {
            "OpenAI-Sentinel-Chat-Requirements-Token": result["token"]}
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
            "parent_message_id": "client-created-root",
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

    def _stream_body(self, messages, model):
        offset, zone = self._timezone()
        return {
            "action": "next",
            "messages": messages,
            "parent_message_id": "client-created-root",
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

    # -------------------------------------------------------------------- stream

    def stream(self, match, prompt, model, system, debug=False, timeout=600):
        headers = self._headers(match)
        try:
            self._authorize(headers)
        except (net.StreamError, sentinel.SentinelError) as exc:
            yield ev.error(str(exc))
            return
        script, build = self._bootstrap(headers)
        fingerprint = sentinel.build_fingerprint(
            headers.get("User-Agent", ""), script, build)
        try:
            gate = self._sentinel_headers(headers, fingerprint, debug)
        except (net.StreamError, sentinel.SentinelError) as exc:
            yield ev.error(f"sentinel: {exc}")
            return

        message = prompt if not system else f"{system}\n\n{prompt}"
        try:
            conduit = self._conduit_token(headers, message, model)
        except (net.StreamError, sentinel.SentinelError) as exc:
            yield ev.error(str(exc))
            return

        path = "/backend-api/f/conversation"
        stream_headers = {
            **self._target(headers, path),
            **gate["headers"],
            "Accept": "text/event-stream",
            "X-Conduit-Token": conduit,
        }
        parser = _MessageStream()
        conversation_id = ""
        emitted = False
        try:
            lines = net.stream_lines(
                self.origin + path, headers=stream_headers,
                json_body=self._stream_body([self._message(message)], model),
                timeout=timeout, debug=debug)
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
                    conversation_id = (event.get("conversation_id")
                                       or conversation_id)
                    continue
                if event.get("conversation_id"):
                    conversation_id = event["conversation_id"]
                events = parser.feed(event)
                for item in events:
                    if item.kind in (ev.TEXT, ev.THINKING, ev.TOOL_CALL):
                        emitted = True
                    yield item
        except net.StreamError as exc:
            yield ev.error(str(exc))
            return

        if conversation_id and not emitted:
            yield from self._poll(headers, conversation_id, parser, timeout, debug)
        yield ev.done()

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
        content = message.get("content") or {}
        ctype = content.get("content_type")
        recipient = message.get("recipient") or "all"
        if recipient not in ("all", "", None) and recipient != self.tool:
            self.tool = recipient
            out.append(ev.tool_call(name=recipient, status="start"))
        if ctype in ("text", "multimodal_text"):
            full = "".join(_part_text(part) for part in content.get("parts") or [])
            if len(full) > self.text:
                out.append(ev.text(full[self.text:]))
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
