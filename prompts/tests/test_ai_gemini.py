#!/usr/bin/env python3
"""Offline tests for the Gemini adapter's stream parser.

Gemini answers with Google's length-prefixed frames: ``)]}'``, then
``<utf-16 length>\\n<json>`` per frame.  Each frame's ``[2]`` slot is another
JSON document whose ``[4]`` candidates carry the answer (``[1][0]``) and the
model's reasoning (``[37][0][0]``).  The parser has to reconstruct the
cumulative answer and emit only the new suffix.  No network, no browser.
"""

from __future__ import annotations

import json
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "bin"))

from ai import events  # noqa: E402
from ai.providers import gemini  # noqa: E402


def text_of(items):
    return "".join(item.text for item in items if item.kind == events.TEXT)


def thinking_of(items):
    return "".join(item.text for item in items if item.kind == events.THINKING)


def candidate(rcid, text, thoughts="", indicator=2):
    data = [None] * 38
    data[0] = rcid
    data[1] = [text]
    data[8] = [indicator]
    data[37] = [[thoughts]] if thoughts else None
    return data


def inner(candidates, metadata=None, context=None):
    data = [None] * 26
    data[1] = metadata or ["c_abc", "r_1"]
    data[4] = candidates
    if context is not None:
        data[25] = context
    return json.dumps(data, ensure_ascii=False)


def part(candidates, metadata=None, context=None, error=None):
    item = [None, None, inner(candidates, metadata, context), None, None, None]
    if error is not None:
        item[5] = [None, None, [[None, [error]]]]
    return item


def frame(*parts):
    payload = json.dumps(list(parts), ensure_ascii=False)
    # Google counts the newline after the length digits and the frame's own
    # trailing newline as part of the declared length.
    units = len(("\n" + payload + "\n").encode("utf-16-le")) // 2
    return f"{units}\n{payload}\n"


class FrameParserTest(unittest.TestCase):
    def test_prefix_and_two_frames(self):
        parser = gemini._FrameParser()
        frames = parser.feed(")]}'\n" + frame(part([candidate("r1", "one")])))
        self.assertEqual(len(frames), 1)
        frames += parser.feed(frame(part([candidate("r1", "two")])))
        self.assertEqual(len(frames), 2)

    def test_utf16_length_counts_astral_characters(self):
        parser = gemini._FrameParser()
        frames = parser.feed(frame(part([candidate("r1", "hi 😀")])))
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0][2], inner([candidate("r1", "hi 😀")]))

    def test_split_frame_waits_for_the_rest(self):
        parser = gemini._FrameParser()
        payload = frame(part([candidate("r1", "hello")]))
        self.assertEqual(parser.feed(payload[:8]), [])
        self.assertEqual(len(parser.feed(payload[8:])), 1)


class StreamTest(unittest.TestCase):
    def test_text_delta_across_frames(self):
        parser = gemini._Parser()
        first = parser.feed(frame(part([candidate("r1", "Hello", indicator=1)])))
        self.assertEqual(text_of(first), "Hello")
        second = parser.feed(frame(part([candidate("r1", "Hello world")])))
        self.assertEqual(text_of(second), " world")
        self.assertEqual(parser.full_text, "Hello world")
        self.assertTrue(parser.done)

    def test_thinking_is_separate_from_the_answer(self):
        parser = gemini._Parser()
        out = parser.feed(frame(part([
            candidate("r1", "answer", thoughts="step one", indicator=1),
        ])))
        self.assertEqual(text_of(out), "answer")
        self.assertEqual(thinking_of(out), "step one")
        out = parser.feed(frame(part([
            candidate("r1", "answer", thoughts="step one, step two"),
        ])))
        self.assertEqual(text_of(out), "")
        self.assertEqual(thinking_of(out), ", step two")

    def test_mid_string_edit_emits_only_the_new_tail(self):
        parser = gemini._Parser()
        parser.feed(frame(part([candidate("r1", "Hello world", indicator=1)])))
        out = parser.feed(frame(part([candidate("r1", "Hello brave world")])))
        self.assertEqual(text_of(out), "brave ")

    def test_error_frame(self):
        parser = gemini._Parser()
        out = parser.feed(frame(part([], error=1037)))
        self.assertTrue(parser.error)
        self.assertIn("usage limit", parser.error)
        self.assertEqual([item.kind for item in out], [events.ERROR])

    def test_context_marks_the_turn_final(self):
        parser = gemini._Parser()
        parser.feed(frame(part([candidate("r1", "done")], context="ctx")))
        self.assertTrue(parser.done)
        self.assertEqual(parser.metadata, [None] * 9 + ["ctx"])

    def test_card_content_falls_back_to_its_attachment(self):
        data = candidate("r1", "https://googleusercontent.com/card_content/123")
        data[22] = ["real text"]
        text, _ = gemini._candidate_text(data)
        self.assertEqual(text, "real text")

    def test_googleusercontent_artifacts_are_removed(self):
        text, _ = gemini._candidate_text(candidate(
            "r1", "before https://googleusercontent.com/image/1\nafter"))
        self.assertEqual(text, "before after")


class ModelTest(unittest.TestCase):
    def test_auto_sends_no_header(self):
        header, number = gemini._model_spec("auto")
        self.assertIsNone(header)
        self.assertEqual(number, 1)

    def test_alias_resolves_to_the_model_header(self):
        header, number = gemini._model_spec("flash")
        self.assertIsNotNone(header)
        self.assertEqual(header[4], "fbb127bbb056c959")
        self.assertEqual(number, 1)


class BatchTest(unittest.TestCase):
    def test_reads_the_matching_rpc_body(self):
        body = {"2": [["c_1", "Title", False]]}
        text = ")]}'\n" + frame(
            [None, "MaZiqc", json.dumps(body)],
            [None, "other", json.dumps({"ignored": True})],
        )
        self.assertEqual(gemini._batch_bodies(text, "MaZiqc"), [body])

    def test_plain_json_fallback(self):
        body = {"2": []}
        text = json.dumps([[None, "MaZiqc", json.dumps(body)]])
        self.assertEqual(gemini._batch_bodies(text, "MaZiqc"), [body])


if __name__ == "__main__":
    unittest.main()
