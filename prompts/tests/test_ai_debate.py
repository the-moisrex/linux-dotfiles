#!/usr/bin/env python3
"""Offline tests for the ``ai.debate`` relay (no network, no browser).

The loop only needs participants with ``name`` and ``ask(prompt, fields)``, so
fake conversations can prove the turn order, the ``Another AI said:`` framing,
``--rounds``/``--until-agree`` stopping and the context selection.
"""

from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile
import unittest
from unittest import mock

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "bin"))

from ai import debate  # noqa: E402
from ai import events as ev  # noqa: E402
from ai import state  # noqa: E402


class FakeParticipant:
    def __init__(self, name, answers):
        self.name = name
        self.answers = list(answers)
        self.prompts = []
        self.fields = []

    def ask(self, prompt, fields):
        self.prompts.append(prompt)
        self.fields.append(fields)
        status, answer = self.answers.pop(0)
        return status, answer


class RecordingWriter:
    def __init__(self):
        self.turns = []

    def add(self, provider, round_no, answer):
        self.turns.append((provider, round_no, answer))


class BuildPromptTest(unittest.TestCase):
    def test_the_first_turn_is_the_question(self):
        self.assertEqual(debate.build_prompt("Q?", None, True, ""), "Q?")
        # There is no other answer to verify yet, so the instruction is not
        # appended to the opening question.
        self.assertEqual(debate.build_prompt("Q?", None, True, "I"), "Q?")

    def test_a_providers_first_turn_gets_the_question_and_the_other_answer(self):
        text = debate.build_prompt("Q?", ("grok", "A1"), True, "I")
        self.assertTrue(text.startswith("Q?\n\n"))
        self.assertIn("Another AI said:\n\nA1", text)
        self.assertTrue(text.endswith("\n\nI"))

    def test_a_later_turn_only_carries_the_other_answer(self):
        text = debate.build_prompt("Q?", ("dseek", "A2"), False, "I")
        self.assertNotIn("Q?", text)
        self.assertIn("Another AI said:\n\nA2", text)
        self.assertTrue(text.endswith("\n\nI"))


class VerdictTest(unittest.TestCase):
    def test_parses_the_last_verdict_line(self):
        answer = "some text\nVERDICT: DISAGREE\nmore\nverdict: agree\n"
        self.assertEqual(debate.parse_verdict(answer), "AGREE")
        self.assertEqual(debate.parse_verdict("VERDICT: DISAGREE"), "DISAGREE")
        self.assertIsNone(debate.parse_verdict("no verdict here"))

    def test_disagree_is_not_read_as_agree(self):
        self.assertEqual(debate.parse_verdict("  VERDICT: DISAGREE  "), "DISAGREE")


class RunPanelTest(unittest.TestCase):
    def test_ring_order_and_rounds(self):
        a = FakeParticipant("grok", [(0, "a1"), (0, "a2")])
        b = FakeParticipant("dseek", [(0, "b1"), (0, "b2")])
        writer = RecordingWriter()
        result = debate.run_panel([a, b], "Q?", rounds=2, until_agree=False,
                                  instruction="I", writer=writer)
        self.assertEqual(result.status, 0)
        self.assertEqual(result.stop_reason, "rounds")
        self.assertEqual([t.provider for t in result.transcript],
                         ["grok", "dseek", "grok", "dseek"])
        self.assertEqual([t.round for t in result.transcript], [1, 1, 2, 2])
        self.assertEqual(writer.turns,
                         [("grok", 1, "a1"), ("dseek", 1, "b1"),
                          ("grok", 2, "a2"), ("dseek", 2, "b2")])
        # The very first turn is the bare question; dseek's first turn knows the
        # question too; grok's second turn only sees dseek's latest answer.
        self.assertEqual(a.prompts[0], "Q?")
        self.assertIn("Q?", b.prompts[0])
        self.assertIn("a1", b.prompts[0])
        self.assertNotIn("Q?", a.prompts[1])
        self.assertIn("b1", a.prompts[1])
        self.assertEqual(a.fields[0], {"turn": 1, "round": 1, "provider": "grok"})
        self.assertEqual(b.fields[0]["reply_to"], "grok")

    def test_three_providers_round_robin(self):
        a = FakeParticipant("grok", [(0, "a1"), (0, "a2")])
        b = FakeParticipant("dseek", [(0, "b1"), (0, "b2")])
        c = FakeParticipant("kimi", [(0, "c1"), (0, "c2")])
        writer = RecordingWriter()
        result = debate.run_panel([a, b, c], "Q?", rounds=2, until_agree=False,
                                  instruction="", writer=writer)
        self.assertEqual([t.provider for t in result.transcript],
                         ["grok", "dseek", "kimi", "grok", "dseek", "kimi"])
        self.assertIn("Q?", c.prompts[0])
        self.assertIn("b1", c.prompts[0])

    def test_until_agree_stops_after_the_first_agreeing_pass(self):
        a = FakeParticipant("grok", [(0, "a1"), (0, "a2\nVERDICT: AGREE")])
        b = FakeParticipant("dseek", [(0, "b1\nVERDICT: AGREE"),
                                      (0, "b2\nVERDICT: AGREE")])
        writer = RecordingWriter()
        result = debate.run_panel([a, b], "Q?", rounds=5, until_agree=True,
                                  instruction="I", writer=writer)
        self.assertEqual(result.status, 0)
        self.assertEqual(result.stop_reason, "agreement")
        self.assertEqual(result.rounds_run, 2)
        self.assertEqual(len(result.transcript), 4)

    def test_until_agree_keeps_going_without_agreement(self):
        a = FakeParticipant("grok", [(0, "a1"), (0, "a2\nVERDICT: DISAGREE")])
        b = FakeParticipant("dseek", [(0, "b1\nVERDICT: AGREE"),
                                      (0, "b2\nVERDICT: AGREE")])
        result = debate.run_panel([a, b], "Q?", rounds=2, until_agree=True,
                                  instruction="I", writer=RecordingWriter())
        self.assertEqual(result.stop_reason, "rounds")
        self.assertEqual(result.rounds_run, 2)

    def test_a_failed_turn_stops_the_relay(self):
        a = FakeParticipant("grok", [(0, "a1")])
        b = FakeParticipant("dseek", [(1, "partial")])
        writer = RecordingWriter()
        result = debate.run_panel([a, b], "Q?", rounds=2, until_agree=False,
                                  instruction="I", writer=writer)
        self.assertEqual(result.status, 1)
        self.assertEqual(result.stop_reason, "failed")
        self.assertEqual(result.failed_provider, "dseek")
        self.assertEqual(writer.turns, [("grok", 1, "a1")])

    def test_an_empty_answer_fails(self):
        a = FakeParticipant("grok", [(0, "  ")])
        b = FakeParticipant("dseek", [(0, "b1")])
        result = debate.run_panel([a, b], "Q?", rounds=1, until_agree=False,
                                  instruction="", writer=RecordingWriter())
        self.assertEqual(result.status, 1)
        self.assertEqual(result.failed_provider, "grok")


class ContextWriterTest(unittest.TestCase):
    def test_last_keeps_only_the_newest_answer(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "context.md")
            writer = debate.ContextWriter(path, "last", "Q?")
            writer.add("grok", 1, "a1")
            writer.add("dseek", 1, "b1")
            self.assertEqual(writer.text, "b1")
            with open(path, encoding="utf-8") as handle:
                self.assertEqual(handle.read().strip(), "b1")
            writer.close()

    def test_all_keeps_the_labelled_transcript(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "context.md")
            writer = debate.ContextWriter(path, "all", "Q?")
            writer.add("grok", 1, "a1")
            writer.add("dseek", 1, "b1\n\nmore")
            text = writer.text
            self.assertIn("## Question\n\nQ?", text)
            self.assertIn("## grok · round 1\n\na1", text)
            self.assertIn("## dseek · round 1", text)
            self.assertIn("b1\n\nmore", text)
            with open(path, encoding="utf-8") as handle:
                self.assertEqual(handle.read().strip(), text)
            writer.close()


class FakeConversation:
    def __init__(self, name, replies, conversation_id=""):
        self.name = name
        self.replies = list(replies)
        self.turns = 0
        self.prompts = []
        self.closed = False
        self.conversation_id = conversation_id
        self.history = []
        self.resume = {}

    def send(self, message, files=None):
        self.prompts.append(message)
        self.turns += 1
        self.history.append({"role": "user", "text": message})
        text = self.replies.pop(0)
        self.history.append({"role": "assistant", "text": text})
        yield ev.text(text)
        yield ev.done()

    def close(self):
        self.closed = True


class FakeInstance:
    def __init__(self, provider):
        self.provider = provider

    def open_conversation(self, match, model, conversation_id=None,
                          history=None, resume=None, **kwargs):
        conversation = self.provider.conversation
        if conversation_id:
            conversation.conversation_id = conversation_id
        if history:
            conversation.history = [dict(turn) for turn in history]
        if resume:
            conversation.resume = dict(resume)
        self.provider.opened = {
            "model": model, "conversation_id": conversation_id,
            "history": history, "resume": resume, **kwargs}
        return conversation


class FakeProvider:
    """Only the surface ``ai.debate``/``run_turn`` touch."""

    inline_limit = 48 * 1024

    def __init__(self, name, replies):
        self.name = name
        self.label = name.title()
        self.models = ["fake"]
        self.session_target = f"https://{name}.example/*"
        self.origin = f"https://{name}.example"
        self.conversation = FakeConversation(name, replies)
        self.opened = None

    def chosen_model(self, requested):
        return requested or "fake"

    def __call__(self):
        return FakeInstance(self)


class DebateStateTest(unittest.TestCase):
    def test_roundtrip_is_keyed_by_the_sorted_provider_set(self):
        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.dict(os.environ, {"XDG_STATE_HOME": tmp}):
            state.save_debate(["dseek", "grok"], {
                "grok": {"conversation_id": "g1", "model": "m",
                         "history": [{"role": "user", "text": "hi"}],
                         "resume": {"socket": 1}},
                "dseek": {"conversation_id": "d1"},
            })
            self.assertEqual(state.debate_path(["dseek", "grok"]),
                             state.debate_path(["grok", "dseek"]))
            saved = state.load_debate(["grok", "dseek"])
            self.assertEqual(saved["grok"]["conversation_id"], "g1")
            self.assertEqual(saved["grok"]["resume"], {"socket": 1})
            self.assertEqual(saved["dseek"]["conversation_id"], "d1")
            state.clear_debate(["grok", "dseek"])
            self.assertIsNone(state.load_debate(["grok", "dseek"]))

    def test_history_is_trimmed_like_a_single_provider(self):
        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.dict(os.environ, {"XDG_STATE_HOME": tmp}):
            history = [{"role": "user", "text": str(i)}
                       for i in range(state.MAX_HISTORY + 5)]
            state.save_debate(["grok", "dseek"], {"grok": {"history": history}})
            saved = state.load_debate(["grok", "dseek"])
            self.assertEqual(len(saved["grok"]["history"]), state.MAX_HISTORY)
            self.assertEqual(saved["grok"]["history"][-1]["text"],
                             str(state.MAX_HISTORY + 4))


class MainTest(unittest.TestCase):
    def setUp(self):
        # A debate persists its panel after every turn; keep the real state
        # directory out of the tests.
        self.state_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.state_dir.cleanup)
        patcher = mock.patch.dict(
            os.environ, {"XDG_STATE_HOME": self.state_dir.name})
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_main_wires_two_providers_and_writes_the_last_answer(self):
        providers = {
            "grok": FakeProvider("grok", ["first answer"]),
            "dseek": FakeProvider("dseek", ["second answer"]),
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "context.md")
            out = io.StringIO()
            with mock.patch.object(debate, "all_providers", lambda: providers), \
                    mock.patch.object(debate.session_mod, "load",
                                      lambda *a, **k: {"browser": "test"}), \
                    contextlib.redirect_stdout(out), \
                    contextlib.redirect_stderr(io.StringIO()):
                status = debate.main(["grok", "dseek", "Q?", "--rounds", "1",
                                      "--context-file", path])
            self.assertEqual(status, 0)
            with open(path, encoding="utf-8") as handle:
                self.assertEqual(handle.read().strip(), "second answer")
            self.assertEqual(providers["grok"].conversation.prompts[0], "Q?")
            self.assertIn("Another AI said:",
                          providers["dseek"].conversation.prompts[0])
            self.assertIn("first answer",
                          providers["dseek"].conversation.prompts[0])
            self.assertTrue(providers["grok"].conversation.closed)
            self.assertTrue(providers["dseek"].conversation.closed)

    def test_main_keeps_the_labelled_transcript_with_context_all(self):
        providers = {
            "grok": FakeProvider("grok", ["a1"]),
            "dseek": FakeProvider("dseek", ["b1"]),
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "context.md")
            with mock.patch.object(debate, "all_providers", lambda: providers), \
                    mock.patch.object(debate.session_mod, "load",
                                      lambda *a, **k: {"browser": "test"}), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()):
                status = debate.main(["grok", "dseek", "Q?", "--rounds", "1",
                                      "--context", "all",
                                      "--context-file", path])
            self.assertEqual(status, 0)
            with open(path, encoding="utf-8") as handle:
                text = handle.read()
            self.assertIn("## Question\n\nQ?", text)
            self.assertIn("## grok · round 1\n\na1", text)
            self.assertIn("## dseek · round 1\n\nb1", text)

    def test_continue_resumes_each_provider_and_keeps_saving(self):
        providers = {
            "grok": FakeProvider("grok", ["new grok"]),
            "dseek": FakeProvider("dseek", ["new dseek"]),
        }
        names = ["grok", "dseek"]
        state.save_debate(names, {
            "grok": {"conversation_id": "conv-grok", "model": "expert",
                     "history": [{"role": "user", "text": "old question"}],
                     "resume": {"socket": 1}},
            "dseek": {"conversation_id": "conv-dseek", "model": "fake",
                      "history": [], "resume": {}},
        })
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "context.md")
            err = io.StringIO()
            with mock.patch.object(debate, "all_providers", lambda: providers), \
                    mock.patch.object(debate.session_mod, "load",
                                      lambda *a, **k: {"browser": "test"}), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(err):
                status = debate.main(["-c", "grok", "dseek", "Q?", "--rounds",
                                      "1", "--context-file", path])
        self.assertEqual(status, 0)
        self.assertIn("continuing grok, dseek", err.getvalue())
        self.assertEqual(providers["grok"].opened["conversation_id"],
                         "conv-grok")
        # The saved model is reused unless -m overrides it.
        self.assertEqual(providers["grok"].opened["model"], "expert")
        self.assertEqual(providers["grok"].opened["resume"], {"socket": 1})
        self.assertEqual(providers["dseek"].opened["conversation_id"],
                         "conv-dseek")
        saved = state.load_debate(names)
        self.assertEqual(saved["grok"]["conversation_id"], "conv-grok")
        texts = [message["text"] for message in saved["grok"]["history"]]
        self.assertIn("old question", texts)
        self.assertIn("Q?", texts)
        self.assertIn("new grok", texts)

    def test_new_discards_the_saved_debate(self):
        providers = {
            "grok": FakeProvider("grok", ["new grok"]),
            "dseek": FakeProvider("dseek", ["new dseek"]),
        }
        names = ["grok", "dseek"]
        state.save_debate(names, {
            "grok": {"conversation_id": "conv-grok",
                     "history": [{"role": "user", "text": "old question"}]},
            "dseek": {"conversation_id": "conv-dseek"},
        })
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "context.md")
            with mock.patch.object(debate, "all_providers", lambda: providers), \
                    mock.patch.object(debate.session_mod, "load",
                                      lambda *a, **k: {"browser": "test"}), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()):
                status = debate.main(["grok", "dseek", "Q?", "--rounds", "1",
                                      "--new", "--context-file", path])
        self.assertEqual(status, 0)
        self.assertIsNone(providers["grok"].opened["conversation_id"])
        texts = [message["text"]
                 for message in state.load_debate(names)["grok"]["history"]]
        self.assertNotIn("old question", texts)
        self.assertIn("Q?", texts)

    def test_continue_without_a_saved_debate_says_so(self):
        providers = {
            "grok": FakeProvider("grok", ["a"]),
            "dseek": FakeProvider("dseek", ["b"]),
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "context.md")
            err = io.StringIO()
            with mock.patch.object(debate, "all_providers", lambda: providers), \
                    mock.patch.object(debate.session_mod, "load",
                                      lambda *a, **k: {"browser": "test"}), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(err):
                status = debate.main(["-c", "grok", "dseek", "Q?", "--rounds",
                                      "1", "--context-file", path])
        self.assertEqual(status, 0)
        self.assertIn("no saved debate to continue", err.getvalue())
        self.assertIsNone(providers["grok"].opened["conversation_id"])

    def test_continue_and_new_are_mutually_exclusive(self):
        providers = {
            "grok": FakeProvider("grok", []),
            "dseek": FakeProvider("dseek", []),
        }
        err = io.StringIO()
        with mock.patch.object(debate, "all_providers", lambda: providers), \
                contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(err):
            status = debate.main(["-c", "--new", "grok", "dseek", "Q?"])
        self.assertEqual(status, 2)
        self.assertIn("mutually exclusive", err.getvalue())


class CommandLineTest(unittest.TestCase):
    def test_split_providers_accepts_aliases(self):
        providers = {"chatgpt": object(), "grok": object(), "deepseek": object()}
        names, words = debate.split_providers(["grok", "dseek", "what", "now"],
                                              providers)
        self.assertEqual(names, ["grok", "deepseek"])
        self.assertEqual(words, ["what", "now"])

    def test_parse_models_maps_canonical_names(self):
        models = debate.parse_models(["grok=expert", "dseek=deepseek-reasoner"],
                                     ["grok", "deepseek"])
        self.assertEqual(models, {"grok": "expert",
                                  "deepseek": "deepseek-reasoner"})
        with self.assertRaises(debate.UsageError):
            debate.parse_models(["kimi=kimi"], ["grok", "deepseek"])
        with self.assertRaises(debate.UsageError):
            debate.parse_models(["grok"], ["grok", "deepseek"])


if __name__ == "__main__":
    unittest.main()
