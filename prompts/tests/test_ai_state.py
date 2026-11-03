#!/usr/bin/env python3
"""Offline tests for the per-terminal conversation state (``ai.state``).

``.ai.reply`` relies on three things this file pins down:

* ``terminal_scope()`` is the same encoding ``prompt::terminal::scope`` uses;
* ``save()`` keeps the plain ``-c`` global file **and** mirrors the record into
  the terminal scope (plus the ``last`` markers);
* ``load_terminal()`` never falls back to another terminal's chat, while
  ``save_debate()`` mirrors each participant and marks ``last`` as ``debate``.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "bin"))

from ai import state  # noqa: E402


class ScopeTests(unittest.TestCase):
    def test_key_wins_and_is_slugged(self):
        with mock.patch.dict(os.environ, {"PROMPT_TERMINAL_KEY": "foo/bar baz"},
                             clear=False):
            self.assertEqual(state.terminal_scope(), "key-foo-bar-baz")

    def test_sid_and_tty(self):
        with mock.patch.dict(os.environ, {
                "PROMPT_TERMINAL_KEY": "",
                "PROMPT_TERMINAL_SID": "1234",
                "PROMPT_TERMINAL_TTY": "/dev/pts/3",
        }, clear=False):
            self.assertEqual(state.terminal_scope(), "sid-1234_tty--dev-pts-3")

    def test_sid_only(self):
        with mock.patch.dict(os.environ, {
                "PROMPT_TERMINAL_KEY": "",
                "PROMPT_TERMINAL_SID": "42",
                "PROMPT_TERMINAL_TTY": "",
        }, clear=False):
            # No controlling tty in the test process is unlikely; pin the
            # fallback so the assertion is about the encoding, not the host.
            with mock.patch.object(state, "_controlling_tty", return_value=""):
                self.assertEqual(state.terminal_scope(), "sid-42")


class StateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.env = mock.patch.dict(os.environ, {
            "XDG_STATE_HOME": self.tmp.name,
            "PROMPT_TERMINAL_KEY": "test-terminal",
            "PROMPT_TERMINAL_SID": "",
            "PROMPT_TERMINAL_TTY": "",
        })
        self.env.start()
        self.addCleanup(self.env.stop)

    def test_save_mirrors_and_marks_last(self):
        scope = state.terminal_scope()
        data = state.save("gap", "conv-1", "model", history=["hi"], resume={"k": "v"})

        self.assertTrue(os.path.isfile(state.path("gap")))
        self.assertTrue(os.path.isfile(state.terminal_path("gap", scope)))
        self.assertEqual(state.load("gap")["conversation_id"], "conv-1")
        self.assertEqual(state.load_terminal("gap", scope)["conversation_id"], "conv-1")
        self.assertEqual(data["conversation_id"], "conv-1")

        with open(state.last_path(), encoding="utf-8") as handle:
            self.assertEqual(handle.read().strip(), "gap")
        with open(state.last_path(scope), encoding="utf-8") as handle:
            self.assertEqual(handle.read().strip(), "gap")

    def test_load_terminal_does_not_fall_back(self):
        state.save("gap", "conv-a", "model")
        self.assertIsNotNone(state.load_terminal("gap"))
        self.assertIsNone(state.load_terminal("gap", "key-other-terminal"))

    def test_save_is_trimmed(self):
        history = [str(i) for i in range(state.MAX_HISTORY + 5)]
        state.save("gap", "conv", "model", history=history)
        self.assertEqual(len(state.load_terminal("gap")["history"]), state.MAX_HISTORY)

    def test_old_terminal_record_is_ignored(self):
        state.save("gap", "conv", "model")
        file_path = state.terminal_path("gap")
        with open(file_path, encoding="utf-8") as handle:
            data = json.load(handle)
        data["updated"] = 1
        with open(file_path, "w", encoding="utf-8") as handle:
            json.dump(data, handle)
        self.assertIsNone(state.load_terminal("gap"))

    def test_clear_removes_both_records(self):
        state.save("gap", "conv", "model")
        state.clear("gap")
        self.assertIsNone(state.load("gap"))
        self.assertIsNone(state.load_terminal("gap"))

    def test_save_debate_mirrors_participants(self):
        state.save("gap", "single", "model")
        state.save_debate(["gap", "dseek"], {
            "gap": {"conversation_id": "debate-gap", "model": "m"},
            "dseek": {"conversation_id": "debate-dseek", "model": "m"},
        })

        # The single-provider global record is left alone for plain `-c`.
        self.assertEqual(state.load("gap")["conversation_id"], "single")
        self.assertEqual(state.load_terminal("gap")["conversation_id"], "debate-gap")
        self.assertEqual(state.load_terminal("dseek")["conversation_id"], "debate-dseek")
        with open(state.last_path(), encoding="utf-8") as handle:
            self.assertEqual(handle.read().strip(), "debate")
        with open(state.last_path(state.terminal_scope()), encoding="utf-8") as handle:
            self.assertEqual(handle.read().strip(), "debate")

    def test_clear_debate_drops_terminal_mirrors(self):
        state.save_debate(["gap", "dseek"], {
            "gap": {"conversation_id": "debate-gap"},
            "dseek": {"conversation_id": "debate-dseek"},
        })
        state.clear_debate(["gap", "dseek"])
        self.assertIsNone(state.load_terminal("gap"))
        self.assertIsNone(state.load_terminal("dseek"))
        self.assertIsNone(state.load_debate(["gap", "dseek"]))


if __name__ == "__main__":
    unittest.main()