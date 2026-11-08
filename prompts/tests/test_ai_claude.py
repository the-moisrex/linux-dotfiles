#!/usr/bin/env python3
"""Offline tests for the Claude (``ai.claude``) web-chat adapter.

A threaded ``http.server`` stub speaks the claude.ai endpoints the adapter
uses: organization discovery, chat creation/fetch, the wiggle upload and the
``rendering_mode: "messages"`` SSE completion stream.  No browser, no network
and no real session are needed.
"""

from __future__ import annotations

import json
import os
import re
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "bin"))

from ai import events as ev  # noqa: E402
from ai import history  # noqa: E402
from ai import net  # noqa: E402
from ai.providers import all_providers  # noqa: E402
from ai.providers import claude as claude_mod  # noqa: E402

HAVE_CLIENT = net.client_name() != "none"

ORG = "11111111-1111-4111-8111-111111111111"
CHAT = "22222222-2222-4222-8222-222222222222"
ASSISTANT = "33333333-3333-4333-8333-333333333333"
HUMAN = "44444444-4444-4444-8444-444444444444"


def _sse(event, payload):
    return (f"event: {event}\n" + "data: " + json.dumps(payload) + "\n\n").encode()


class StubHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def _json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        length = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(length) if length else b""

    def do_GET(self):
        state = self.server.state
        if self.path.startswith("/api/organizations/" + ORG + "/chat_conversations?"):
            self._json(200, [{
                "uuid": CHAT,
                "name": "Hello chat",
                "created_at": "2026-11-08T06:00:00.000000Z",
                "updated_at": "2026-11-08T06:01:00.000000Z",
                "model": "claude-sonnet-5-5",
            }])
            return
        if self.path == "/api/organizations/" + ORG + "/chat_conversations/" + CHAT:
            self._json(200, {
                "uuid": CHAT,
                "name": "Hello chat",
                "created_at": "2026-11-08T06:00:00.000000Z",
                "updated_at": "2026-11-08T06:01:00.000000Z",
                "model": "claude-sonnet-5-5",
                "current_leaf_message_uuid": ASSISTANT,
                "chat_messages": [
                    {"uuid": HUMAN, "sender": "human", "text": "hey",
                     "index": 0, "parent_message_uuid": claude_mod.ZERO_UUID,
                     "created_at": "2026-11-08T06:00:00.000000Z",
                     "attachments": [], "files": []},
                    {"uuid": ASSISTANT, "sender": "assistant", "text": "Hello!",
                     "index": 1, "parent_message_uuid": HUMAN,
                     "created_at": "2026-11-08T06:01:00.000000Z",
                     "attachments": [{"file_name": "notes.txt"}], "files": []},
                ],
            })
            return
        if self.path == "/api/organizations":
            self._json(200, [{"uuid": ORG, "capabilities": ["chat"]}])
            return
        self._json(404, {"error": {"message": f"no route {self.path}"}})

    def do_POST(self):
        state = self.server.state
        raw = self._body()
        if self.path == "/api/organizations/" + ORG + "/chat_conversations":
            body = json.loads(raw or b"{}")
            state["created"] = body
            self._json(200, {
                "uuid": CHAT,
                "name": body.get("name") or "",
                "model": "claude-sonnet-5-5",
                "current_leaf_message_uuid": None,
            })
            return
        if self.path.endswith("/wiggle/upload-file"):
            match = re.search(rb'filename="([^"]*)"', raw)
            name = match.group(1).decode() if match else "file"
            state["uploads"].append({"name": name, "raw": raw})
            self._json(200, {
                "success": True,
                "file_uuid": "55555555-5555-4555-8555-555555555555",
                "file_name": name,
                "path": "/mnt/user-data/uploads/" + name,
                "media_type": "text/plain",
                "size_bytes": len(raw),
                "file_kind": "blob",
            })
            return
        if self.path.endswith("/completion"):
            body = json.loads(raw or b"{}")
            state["completions"].append(body)
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Connection", "close")
            self.close_connection = True
            self.end_headers()
            self.wfile.write(_sse("message_start", {
                "type": "message_start",
                "message": {"uuid": ASSISTANT, "model": "claude-sonnet-5-5"},
            }))
            self.wfile.write(_sse("content_block_delta", {
                "type": "content_block_delta",
                "delta": {"type": "thinking_delta", "thinking": "pondering"},
            }))
            self.wfile.write(_sse("content_block_delta", {
                "type": "content_block_delta",
                "delta": {"type": "text_delta", "text": "Hi"},
            }))
            self.wfile.write(_sse("content_block_delta", {
                "type": "content_block_delta",
                "delta": {"type": "text_delta", "text": " there"},
            }))
            self.wfile.write(_sse("message_delta", {
                "type": "message_delta", "delta": {"stop_reason": "end_turn"},
            }))
            self.wfile.write(_sse("message_stop", {"type": "message_stop"}))
            self.wfile.flush()
            return
        self._json(404, {"error": {"message": f"no route {self.path}"}})

    def do_DELETE(self):
        self.send_response(204)
        self.send_header("Content-Length", "0")
        self.end_headers()


def fake_match():
    return {
        "user_agent": "test-agent",
        "cookie_header": "sessionKey=test",
        "cookies": [{"name": "lastActiveOrg", "value": ORG}],
    }


@unittest.skipUnless(HAVE_CLIENT, "no HTTP client available (curl_cffi/httpx)")
class ClaudeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), StubHandler)
        cls.server.state = {"created": None, "uploads": [], "completions": []}
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_address[1]}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)

    def setUp(self):
        self.state = self.server.state
        self.state.update({"created": None, "uploads": [], "completions": []})
        self.provider = all_providers()["claude"]()
        self.provider.origin = self.base
        self.match = fake_match()

    def collect(self, conversation, prompt, files=None):
        text, thinking, errors, tools = [], [], [], []
        for event in conversation.send(prompt, files=files):
            if event.kind == ev.TEXT:
                text.append(event.text)
            elif event.kind == ev.THINKING:
                thinking.append(event.text)
            elif event.kind == ev.ERROR:
                errors.append(event.text)
            elif event.kind == ev.TOOL_CALL:
                tools.append(event)
        return "".join(text), "".join(thinking), errors, tools

    # -- the stream --------------------------------------------------------

    def test_send_reads_messages_sse(self):
        conversation = self.provider.open_conversation(self.match, "auto")
        text, thinking, errors, _ = self.collect(conversation, "hello")
        self.assertEqual(errors, [])
        self.assertEqual(text, "Hi there")
        self.assertEqual(thinking, "pondering")
        self.assertEqual(conversation.conversation_id, CHAT)
        self.assertEqual(conversation.parent_uuid, ASSISTANT)
        self.assertEqual(conversation.model, "claude-sonnet-5-5")
        self.assertEqual(len(conversation.history), 2)

    def test_send_posts_the_messages_body(self):
        conversation = self.provider.open_conversation(self.match, "auto")
        self.collect(conversation, "hello")
        body = self.state["completions"][0]
        self.assertEqual(body["rendering_mode"], "messages")
        self.assertEqual(body["parent_message_uuid"], claude_mod.ZERO_UUID)
        self.assertEqual(body["model"], "claude-sonnet-5-5")
        self.assertEqual(body["prompt"], "hello")
        self.assertEqual(body["attachments"], [])

    def test_resume_sends_the_previous_assistant_uuid(self):
        conversation = self.provider.open_conversation(self.match, "auto")
        self.collect(conversation, "first")
        self.collect(conversation, "second")
        self.assertEqual(self.state["completions"][1]["parent_message_uuid"], ASSISTANT)

    def test_system_is_folded_into_the_first_message(self):
        conversation = self.provider.open_conversation(self.match, "auto", system="Be terse.")
        self.collect(conversation, "hello")
        self.assertIn("Be terse.", self.state["completions"][0]["prompt"])
        self.assertIn("hello", self.state["completions"][0]["prompt"])

    def test_model_alias_is_resolved(self):
        self.assertEqual(claude_mod.MODEL_ALIASES["sonnet"], "claude-sonnet-4-5-20250929")
        self.assertIsNone(claude_mod.MODEL_ALIASES["auto"])

    # -- uploads -----------------------------------------------------------

    def test_file_is_uploaded_and_attached(self):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as handle:
            handle.write("The secret word is BANANA42.\n")
            path = handle.name
        try:
            conversation = self.provider.open_conversation(self.match, "auto")
            text, _, errors, tools = self.collect(
                conversation, "What is the secret?", files=[path])
        finally:
            os.unlink(path)
        self.assertEqual(errors, [])
        self.assertEqual(text, "Hi there")
        self.assertTrue(any(tool.name == "upload" for tool in tools))
        self.assertEqual(len(self.state["uploads"]), 1)
        attachment = self.state["completions"][0]["attachments"][0]
        self.assertEqual(attachment["file_name"], os.path.basename(path))
        self.assertEqual(attachment["file_uuid"], "55555555-5555-4555-8555-555555555555")
        self.assertEqual(attachment["extracted_content"], "")
        self.assertEqual(attachment["file_kind"], "blob")

    # -- history -----------------------------------------------------------

    def test_history_summaries_and_fetch(self):
        summaries = list(self.provider.iter_chat_summaries(self.match, limit=5))
        self.assertEqual(len(summaries), 1)
        self.assertEqual(summaries[0].remote_id, CHAT)
        self.assertEqual(summaries[0].title, "Hello chat")
        chat = self.provider.fetch_chat(self.match, CHAT)
        self.assertEqual(chat.title, "Hello chat")
        self.assertEqual([m.role for m in chat.messages],
                         [history.USER, history.ASSISTANT, history.ASSISTANT])
        self.assertEqual(chat.messages[0].text, "hey")
        self.assertEqual(chat.messages[1].text, "Hello!")
        self.assertEqual(chat.messages[2].kind, history.ATTACHMENT)
        self.assertEqual(chat.messages[2].text, "notes.txt")

    # -- parsing helpers ---------------------------------------------------

    def test_sse_parser_handles_multiline_and_done(self):
        lines = [
            "event: message_start",
            'data: {"type": "message_start",',
            'data:  "message": {"uuid": "u"}}',
            "",
            "data: [DONE]",
            "",
        ]
        parsed = list(claude_mod._sse_events(lines))
        self.assertEqual(parsed[0]["type"], "message_start")
        self.assertEqual(parsed[0]["message"]["uuid"], "u")

    def test_attachment_shape_has_the_required_fields(self):
        class Attachment:
            name = "a.txt"
            mime = "text/plain"
            size = 12

        record = {"file_uuid": "u", "file_name": "a.txt",
                  "path": "/p", "media_type": "text/plain",
                  "size_bytes": 12, "file_kind": "blob"}
        entry = claude_mod._attachment(record, Attachment())
        for key in ("file_uuid", "file_name", "file_type", "file_size",
                    "extracted_content", "path", "file_kind"):
            self.assertIn(key, entry)
        self.assertEqual(entry["extracted_content"], "")


if __name__ == "__main__":
    unittest.main()
