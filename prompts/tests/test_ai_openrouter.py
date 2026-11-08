#!/usr/bin/env python3
"""Offline tests for OpenRouter (``ai.openrouter``) and the key resolver.

A threaded ``http.server`` stub speaks OpenRouter's OpenAI-compatible protocol
(``/v1/models`` with context/pricing, ``/v1/chat/completions`` SSE), so the
tests prove the model listing, the ``OPENROUTER_MODEL``/``OPENAI_MODEL``
default, the ``--api-key``/env key resolution and the ``ai.keys`` lookup order
(env → .env → opencode export → auth.json) with no network and no real key.
"""

from __future__ import annotations

import json
import os
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "bin"))

from ai import keys  # noqa: E402
from ai import net  # noqa: E402

HAVE_CLIENT = net.client_name() != "none"

MODELS = [
    {"id": "anthropic/claude-x", "name": "Claude X", "context_length": 200000,
     "pricing": {"prompt": "0.000003", "completion": "0.000015"}},
    {"id": "free/model", "name": "Free Model", "context_length": 8192,
     "pricing": {"prompt": "0", "completion": "0"}},
    {"id": "embed/model", "name": "Embedder", "context_length": 512,
     "pricing": {"prompt": "0", "completion": "0"}},
]


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

    def do_GET(self):
        if self.path == "/v1/models":
            self._json(200, {"data": MODELS})
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
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "close")
        self.close_connection = True
        self.end_headers()
        for frame in (
            {"choices": [{"delta": {"content": "Hi"}}]},
            {"choices": [{"delta": {"content": " there"}}]},
            {"choices": [], "usage": {"prompt_tokens": 3, "completion_tokens": 2}},
        ):
            self.wfile.write(b"data: " + json.dumps(frame).encode() + b"\n\n")
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()


@unittest.skipUnless(HAVE_CLIENT, "no HTTP client available (curl_cffi/httpx)")
class OpenRouterTest(unittest.TestCase):
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

    def env(self, **extra):
        values = os.environ.copy()
        values.update({
            "OPENROUTER_BASE_URL": self.base,
            "OPENROUTER_MODEL": "",
            "OPENROUTER_API_KEY": "",
            "OPENAI_MODEL": "",
            "XDG_STATE_HOME": os.path.join(self.tmp.name, "state"),
            "XDG_DATA_HOME": os.path.join(self.tmp.name, "data"),
            "XDG_CONFIG_HOME": os.path.join(self.tmp.name, "config"),
            "AI_KEYS_NO_OPENCODE": "1",
            "AI_ENV_FILE": os.path.join(self.tmp.name, "absent.env"),
        })
        values.update(extra)
        return values

    def run_ai(self, *args, env=None):
        executable = os.path.join(ROOT, "bin", "ai.openrouter")
        return subprocess.run([sys.executable, executable, *args],
                              capture_output=True, text=True,
                              env=env or self.env(), timeout=90)

    def last_request(self):
        self.assertTrue(self.server.requests, "the stub received no request")
        return self.server.requests[-1]

    # -- model listing -----------------------------------------------------

    def test_list_shows_context_and_pricing(self):
        done = self.run_ai("--list")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("ctx 200000", done.stdout)
        self.assertIn("$3.00/M/$15.00/M", done.stdout)
        self.assertIn("free/free", done.stdout)
        self.assertNotIn("embed/model", done.stdout)  # embeddings are filtered

    def test_list_json(self):
        done = self.run_ai("--list", "--json")
        self.assertEqual(done.returncode, 0, done.stderr)
        records = json.loads(done.stdout)
        self.assertEqual(records[0]["id"], "anthropic/claude-x")
        self.assertEqual(records[0]["context"], 200000)
        self.assertEqual(records[0]["pricing"]["prompt"], "0.000003")

    # -- model default -----------------------------------------------------

    def test_model_from_openrouter_model(self):
        env = self.env(OPENROUTER_MODEL="free/model", OPENROUTER_API_KEY="k")
        done = self.run_ai("--raw", "hi", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.last_request()["body"]["model"], "free/model")

    def test_model_falls_back_to_openai_model(self):
        env = self.env(OPENAI_MODEL="free/model", OPENROUTER_API_KEY="k")
        done = self.run_ai("--raw", "hi", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.last_request()["body"]["model"], "free/model")

    def test_explicit_model_wins(self):
        env = self.env(OPENROUTER_MODEL="free/model", OPENROUTER_API_KEY="k")
        done = self.run_ai("-m", "anthropic/claude-x", "--raw", "hi", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.last_request()["body"]["model"], "anthropic/claude-x")

    def test_default_model_is_auto_code(self):
        env = self.env(OPENROUTER_API_KEY="k")
        done = self.run_ai("--raw", "hi", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.last_request()["body"]["model"], "openrouter/auto-code")

    # -- key resolution ----------------------------------------------------

    def test_env_key_is_sent(self):
        env = self.env(OPENROUTER_MODEL="free/model",
                       OPENROUTER_API_KEY="sk-or-env")
        done = self.run_ai("--raw", "hi", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.last_request()["headers"].get("Authorization"),
                         "Bearer sk-or-env")

    def test_api_key_flag_wins(self):
        env = self.env(OPENROUTER_MODEL="free/model",
                       OPENROUTER_API_KEY="sk-or-env")
        done = self.run_ai("--api-key", "sk-or-flag", "--raw", "hi", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.last_request()["headers"].get("Authorization"),
                         "Bearer sk-or-flag")


class KeyResolverTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        keys._OPENCODE_CACHE = None
        self._saved = dict(os.environ)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._saved)
        keys._OPENCODE_CACHE = None

    def test_explicit_flag(self):
        key, source = keys.resolve("openrouter", "OPENROUTER_API_KEY", "sk-x")
        self.assertEqual(key, "sk-x")
        self.assertEqual(source, "flag:--api-key")

    def test_environment(self):
        os.environ["OPENROUTER_API_KEY"] = "sk-env"
        key, source = keys.resolve("openrouter", "OPENROUTER_API_KEY")
        self.assertEqual(key, "sk-env")
        self.assertEqual(source, "env:OPENROUTER_API_KEY")

    def test_dotenv_file(self):
        path = os.path.join(self.tmp.name, "my.env")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("# comment\nexport OPENROUTER_API_KEY=\"sk-dot\"\n")
        os.environ["AI_ENV_FILE"] = path
        os.environ.pop("OPENROUTER_API_KEY", None)
        key, source = keys.resolve("openrouter", "OPENROUTER_API_KEY")
        self.assertEqual(key, "sk-dot")
        self.assertTrue(source.startswith("dotenv:"))

    def test_opencode_export(self):
        bindir = os.path.join(self.tmp.name, "bin")
        os.makedirs(bindir)
        fake = os.path.join(bindir, "opencode2")
        payload = [{"integrationID": "openrouter", "active": True,
                    "value": {"type": "key", "key": "sk-opencode"}}]
        with open(fake, "w", encoding="utf-8") as handle:
            handle.write("#!/bin/sh\ncat <<'EOF'\n" + json.dumps(payload) + "\nEOF\n")
        os.chmod(fake, os.stat(fake).st_mode | stat.S_IEXEC)
        os.environ["PATH"] = bindir + os.pathsep + os.environ.get("PATH", "")
        os.environ.pop("AI_KEYS_NO_OPENCODE", None)
        os.environ.pop("OPENROUTER_API_KEY", None)
        os.environ["AI_ENV_FILE"] = os.path.join(self.tmp.name, "absent.env")
        key, source = keys.resolve("openrouter", "OPENROUTER_API_KEY")
        self.assertEqual(key, "sk-opencode")
        self.assertIn("opencode", source)

    def test_auth_json_fallback(self):
        data = os.path.join(self.tmp.name, "data", "opencode")
        os.makedirs(data)
        with open(os.path.join(data, "auth.json"), "w", encoding="utf-8") as handle:
            json.dump({"openrouter": {"type": "api", "key": "sk-auth"}}, handle)
        os.environ["XDG_DATA_HOME"] = os.path.join(self.tmp.name, "data")
        os.environ["PATH"] = os.path.join(self.tmp.name, "empty")  # no opencode
        os.environ.pop("AI_KEYS_NO_OPENCODE", None)
        os.environ.pop("OPENROUTER_API_KEY", None)
        os.environ["AI_ENV_FILE"] = os.path.join(self.tmp.name, "absent.env")
        key, source = keys.resolve("openrouter", "OPENROUTER_API_KEY")
        self.assertEqual(key, "sk-auth")
        self.assertIn("auth.json", source)

    def test_mask_never_reveals(self):
        self.assertNotIn("secret", keys.mask("sk-secret-value-1234"))
        self.assertEqual(keys.mask(""), "")

    def test_jsonc_keeps_commas_inside_strings(self):
        # A comma that happens to sit before a closing brace/bracket inside a
        # string must not be mistaken for a trailing comma.
        text = '{"a": "x,}", "b": "y,]", "c": [1, 2,],}'
        self.assertEqual(keys._strip_jsonc(text),
                         '{"a": "x,}", "b": "y,]", "c": [1, 2]}')
        parsed = json.loads(keys._strip_jsonc(text))
        self.assertEqual(parsed["a"], "x,}")
        self.assertEqual(parsed["b"], "y,]")
        self.assertEqual(parsed["c"], [1, 2])

    def test_jsonc_strips_comments_but_keeps_urls(self):
        text = '{\n // comment\n "u": "https://example.test/a", /* block */\n}'
        parsed = json.loads(keys._strip_jsonc(text))
        self.assertEqual(parsed["u"], "https://example.test/a")

    def test_opencode_export_accepts_auth_json_shape(self):
        bindir = os.path.join(self.tmp.name, "bin")
        os.makedirs(bindir)
        fake = os.path.join(bindir, "opencode2")
        payload = {"openrouter": {"type": "api", "key": "sk-dict-export"}}
        with open(fake, "w", encoding="utf-8") as handle:
            handle.write("#!/bin/sh\ncat <<'EOF'\n" + json.dumps(payload) + "\nEOF\n")
        os.chmod(fake, os.stat(fake).st_mode | stat.S_IEXEC)
        os.environ["PATH"] = bindir + os.pathsep + os.environ.get("PATH", "")
        os.environ.pop("AI_KEYS_NO_OPENCODE", None)
        os.environ.pop("OPENROUTER_API_KEY", None)
        os.environ["AI_ENV_FILE"] = os.path.join(self.tmp.name, "absent.env")
        key, source = keys.resolve("openrouter", "OPENROUTER_API_KEY")
        self.assertEqual(key, "sk-dict-export")
        self.assertIn("opencode", source)


if __name__ == "__main__":
    unittest.main()
