#!/usr/bin/env python3
"""Offline tests for the API providers (``ai.lms`` / ``ai.openai``).

A threaded ``http.server`` stub speaks the OpenAI-compatible protocol, so the
tests prove the SSE mapping (text, reasoning, usage, done), model discovery and
defaulting, ``--param`` forwarding, inline attachments, the ``-c`` transcript
replay, the prompt-file refusal and the error remedies — with no LM Studio, no
API key and no network.
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "bin"))

from ai import net  # noqa: E402

HAVE_CLIENT = net.client_name() != "none"

CHAT_MODELS = [
    {"id": "stub-chat", "type": "llm", "state": "loaded",
     "max_context_length": 8192},
    {"id": "stub-vlm", "type": "vlm", "state": "not-loaded"},
    {"id": "stub-embed", "type": "embeddings", "state": "not-loaded"},
]


class StubHandler(BaseHTTPRequestHandler):
    """The smallest OpenAI-compatible LM Studio: models + a canned SSE turn."""

    protocol_version = "HTTP/1.1"

    def log_message(self, *args):  # keep the test output clean
        pass

    def _json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/v0/models":
            self._json(200, {"data": CHAT_MODELS})
        elif self.path == "/v1/models":
            self._json(200, {"data": [{"id": model["id"]} for model in CHAT_MODELS]})
        else:
            self._json(404, {"error": {"message": f"no route {self.path}"}})

    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self._json(404, {"error": {"message": f"no route {self.path}"}})
            return
        length = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(length) or b"{}")
        self.server.requests.append({"headers": dict(self.headers), "body": body})
        if body.get("model") == "missing":
            self._json(404, {"error": {"message": "model 'missing' not found"}})
            return
        if _prompt_text(body).startswith("fail"):
            self._json(500, {"error": {"message": "the model failed to load"}})
            return
        if _prompt_text(body).startswith("boom"):
            payload = {"error": {"message": "the request exceeds the context "
                                            "length of the model"}}
            self._json(400, payload)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.close_connection = True
        self.end_headers()
        frames = [
            {"choices": [{"delta": {"reasoning_content": "considering"}}]},
            {"choices": [{"delta": {"content": "Hello"}}]},
            {"choices": [{"delta": {"content": " world"}}]},
            {"choices": [], "usage": {"prompt_tokens": 11, "completion_tokens": 2}},
        ]
        for frame in frames:
            self.wfile.write(b"data: " + json.dumps(frame).encode("utf-8") + b"\n\n")
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()


def _prompt_text(body):
    messages = body.get("messages") or []
    if not messages:
        return ""
    content = messages[-1].get("content")
    if isinstance(content, list):
        return "".join(part.get("text") or "" for part in content
                       if isinstance(part, dict))
    return content or ""


def _closed_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


@unittest.skipUnless(HAVE_CLIENT, "no HTTP client available (curl_cffi/httpx)")
class ApiProviderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), StubHandler)
        cls.server.requests = []
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_address[1]}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.server.requests.clear()

    # -- helpers -----------------------------------------------------------

    def env(self, **extra):
        values = os.environ.copy()
        values.update({
            "LMS_BASE_URL": self.base,
            "LMS_MODEL": "",
            "LMS_API_KEY": "",
            "OPENAI_BASE_URL": self.base,
            "OPENAI_MODEL": "",
            "OPENAI_API_KEY": "",
            "XDG_STATE_HOME": os.path.join(self.tmp.name, "state"),
        })
        values.update(extra)
        return values

    def run_ai(self, *args, provider="lms", env=None, stdin=None):
        executable = os.path.join(ROOT, "bin", f"ai.{provider}")
        return subprocess.run([sys.executable, executable, *args],
                              capture_output=True, text=True, input=stdin,
                              env=env or self.env(), timeout=90)

    def last_request(self):
        self.assertTrue(self.server.requests, "the stub received no request")
        return self.server.requests[-1]["body"]

    # -- discovery ---------------------------------------------------------

    def test_list_shows_the_chat_models(self):
        done = self.run_ai("--list")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("stub-chat", done.stdout)
        self.assertIn("stub-vlm", done.stdout)
        self.assertNotIn("stub-embed", done.stdout)

    def test_print_session_describes_the_endpoint(self):
        done = self.run_ai("--print-session")
        self.assertEqual(done.returncode, 0, done.stderr)
        report = json.loads(done.stdout)
        self.assertTrue(report["reachable"])
        self.assertEqual(report["model"], "stub-chat")
        self.assertTrue(report["base_url"].endswith("/v1"))

    def test_probe_finds_the_endpoint_and_caches_it(self):
        from ai.providers import lms as lms_mod
        from unittest import mock

        state_dir = os.path.join(self.tmp.name, "state")
        saved = dict(os.environ)
        try:
            os.environ["XDG_STATE_HOME"] = state_dir
            os.environ.pop("LMS_BASE_URL", None)
            provider = lms_mod.Lms()
            probe = self.base
            with mock.patch.object(lms_mod.Lms, "probe_urls", (probe,)), \
                    mock.patch.object(lms_mod.Lms, "_cli_endpoint",
                                      staticmethod(lambda: "")):
                self.assertEqual(provider.base_url(), self.base + "/v1")
            self.assertEqual(lms_mod._cached_endpoint(), self.base + "/v1")
            # A later run reuses the cached endpoint even though the probe
            # (patched to a dead port) would find nothing.
            dead = f"http://127.0.0.1:{_closed_port()}"
            with mock.patch.object(lms_mod.Lms, "probe_urls", (dead,)), \
                    mock.patch.object(lms_mod.Lms, "_cli_endpoint",
                                      staticmethod(lambda: "")):
                self.assertEqual(provider.base_url(), self.base + "/v1")
        finally:
            os.environ.clear()
            os.environ.update(saved)

    # -- one turn ----------------------------------------------------------

    def test_turn_maps_text_reasoning_and_usage(self):
        done = self.run_ai("--json", "hello")
        self.assertEqual(done.returncode, 0, done.stderr)
        events = [json.loads(line) for line in done.stdout.splitlines() if line.strip()]
        kinds = [event["kind"] for event in events]
        self.assertIn("thinking", kinds)
        self.assertIn("usage", kinds)
        self.assertEqual(kinds[-1], "done")
        text = "".join(event.get("text", "") for event in events
                       if event["kind"] == "text")
        self.assertEqual(text, "Hello world")
        body = self.last_request()
        self.assertTrue(body["stream"])
        self.assertEqual(body["model"], "stub-chat")
        self.assertEqual(body["messages"], [{"role": "user", "content": "hello"}])

    def test_request_parameters_are_forwarded(self):
        done = self.run_ai("--param", "temperature=0.25", "--param",
                           "max_tokens=64", "--raw", "hi")
        self.assertEqual(done.returncode, 0, done.stderr)
        body = self.last_request()
        self.assertEqual(body["temperature"], 0.25)
        self.assertEqual(body["max_tokens"], 64)

    def test_text_and_image_attachments_are_inlined(self):
        note = os.path.join(self.tmp.name, "note.md")
        image = os.path.join(self.tmp.name, "pixel.png")
        with open(note, "w", encoding="utf-8") as handle:
            handle.write("# plan\none step\n")
        with open(image, "wb") as handle:
            handle.write(b"\x89PNG\r\n\x1a\nstub")
        done = self.run_ai("-f", note, "-f", image, "--raw", "look")
        self.assertEqual(done.returncode, 0, done.stderr)
        content = self.last_request()["messages"][-1]["content"]
        self.assertIsInstance(content, list)
        text = content[0]["text"]
        self.assertIn("File: note.md", text)
        self.assertIn("one step", text)
        self.assertTrue(content[1]["image_url"]["url"].startswith(
            "data:image/png;base64,"))

    def test_continue_replays_the_transcript(self):
        state_env = self.env()
        first = self.run_ai("--raw", "first question", env=state_env)
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.run_ai("-c", "--raw", "second question", env=state_env)
        self.assertEqual(second.returncode, 0, second.stderr)
        messages = self.last_request()["messages"]
        self.assertEqual(messages[0], {"role": "user", "content": "first question"})
        self.assertEqual(messages[1], {"role": "assistant", "content": "Hello world"})
        self.assertEqual(messages[-1], {"role": "user", "content": "second question"})

    # -- refusals and remedies --------------------------------------------

    def test_prompt_file_is_refused(self):
        done = self.run_ai("--prompt-file", "hi")
        self.assertEqual(done.returncode, 2)
        self.assertIn("not supported", done.stderr)

    def test_a_missing_model_lists_the_alternatives(self):
        done = self.run_ai("-m", "missing", "--raw", "hi")
        self.assertEqual(done.returncode, 1)
        self.assertIn("Available models", done.stderr)
        self.assertIn("stub-chat", done.stderr)

    def test_a_context_limit_error_gets_the_local_remedy(self):
        done = self.run_ai("--raw", "boom")
        self.assertEqual(done.returncode, 1)
        self.assertIn("no upload endpoint", done.stderr)
        self.assertIn("context length", done.stderr)

    def test_an_unreachable_endpoint_names_the_server(self):
        env = self.env(LMS_BASE_URL=f"http://127.0.0.1:{_closed_port()}")
        done = self.run_ai("--raw", "hi", env=env)
        self.assertEqual(done.returncode, 1)
        self.assertIn("LM Studio", done.stderr)

    def test_a_server_error_keeps_the_message_and_no_traceback(self):
        # The status line alone ("HTTP 500: Internal Server Error") would hide
        # the reason; net.stream_lines reads the error body for that.
        done = self.run_ai("--raw", "fail")
        self.assertEqual(done.returncode, 1)
        self.assertIn("500", done.stderr)
        self.assertIn("the model failed to load", done.stderr)
        self.assertNotIn("Traceback", done.stderr)

    def test_a_missing_key_is_a_clean_error(self):
        # api.openai.com needs a key; the failure must not escape as a traceback
        # from inside the turn.
        env = self.env(OPENAI_BASE_URL="https://api.openai.com/v1",
                       OPENAI_MODEL="stub-chat")
        done = self.run_ai("-m", "stub-chat", "--raw", "hi", provider="openai",
                           env=env)
        self.assertEqual(done.returncode, 1)
        self.assertIn("OPENAI_API_KEY", done.stderr)
        self.assertNotIn("Traceback", done.stderr)

    # -- the shared engine reaches openai too ------------------------------

    def test_openai_uses_the_same_engine(self):
        env = self.env(OPENAI_MODEL="stub-chat")
        done = self.run_ai("--raw", "hi", provider="openai", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout.strip(), "Hello world")

    def test_openai_requires_a_model(self):
        done = self.run_ai("--raw", "hi", provider="openai")
        self.assertEqual(done.returncode, 1)
        self.assertIn("choose a model", done.stderr)


if __name__ == "__main__":
    unittest.main()
