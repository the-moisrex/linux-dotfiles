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
"""

from __future__ import annotations

import json
import time
import uuid

from .. import events as ev
from .. import net
from .. import session as session_mod
from ..pow import PowError, deepseek_response, solve_deepseek
from . import Conversation, Provider

COMPLETION_PATH = "/api/v0/chat/completion"
CLIENT_VERSION = "2.5.0"


class DeepSeek(Provider):
    name = "deepseek"
    label = "DeepSeek"
    origin = "https://chat.deepseek.com"
    session_target = "chat.deepseek.com"
    default_model = "deepseek_reasoner"
    models = ("deepseek_reasoner", "deepseek_chat")

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

    def _pow(self, headers):
        data = net.request_json(self.origin + "/api/v0/chat/create_pow_challenge",
                                method="POST", headers=headers,
                                json_body={"target_path": COMPLETION_PATH})
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
                                 target_path=COMPLETION_PATH)

    @staticmethod
    def _biz(data):
        biz = ((data or {}).get("data") or {})
        if isinstance(biz, dict) and "biz_data" in biz:
            biz = biz["biz_data"]
        return biz


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

    def send(self, prompt):
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
        first = self.turns == 0
        content = f"{self.system}\n\n{prompt}" if (self.system and first) else prompt
        body = {
            "chat_session_id": self.conversation_id,
            "parent_message_id": self.parent_message_id,
            "model_type": "default",
            "prompt": content,
            "ref_file_ids": [],
            "thinking_enabled": self.thinking,
            "search_enabled": False,
            "action": None,
            "preempt": False,
        }
        parser = _Parser()
        answer = []
        try:
            lines = net.stream_lines(self.provider.origin + COMPLETION_PATH,
                                     headers=headers, json_body=body,
                                     timeout=self.timeout, debug=self.debug)
            for line in lines:
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
                        yield event
                if self.debug and line and not line.startswith(("data:", "event:")):
                    yield ev.raw(line)
        except net.StreamError as exc:
            yield ev.error(str(exc))
            return
        self.turns += 1
        if isinstance(parser.message_id, int):
            self.parent_message_id = parser.message_id
            self.resume["parent_message_id"] = parser.message_id
        self._record("user", prompt)
        self._record("assistant", "".join(answer))
        yield ev.done()


class _Parser:
    """Turn DeepSeek's fragment-patch frames into deltas."""

    def __init__(self):
        self.fragments = []
        self.usage = None
        self.message_id = None

    def feed(self, obj):
        if not isinstance(obj, dict):
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
            self.fragments.append(fragment.get("type") or "RESPONSE")
            out += self._content(fragment.get("content") or "")
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
