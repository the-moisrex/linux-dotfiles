#!/usr/bin/env python3
"""Offline tests for the Perplexity adapter's block-stream parser.

Perplexity's ask stream is a sequence of JSON envelopes whose ``blocks`` carry
the answer in two different shapes: the older ``markdown_block`` diffs and the
current ``workflow_block`` items.  Both have to reconstruct the same cumulative
answer and emit only the new suffix, and the ``COMPLETED`` frame's
double-encoded step blob is the fallback when neither shape arrived.  No
network, no browser.
"""

from __future__ import annotations

import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "bin"))

from ai import events  # noqa: E402
from ai.providers import perplexity  # noqa: E402


def text_of(items):
    return "".join(item.text for item in items if item.kind == events.TEXT)


class MarkdownDiffTest(unittest.TestCase):
    def test_initial_block_then_chunk_patches(self):
        parser = perplexity._Parser()
        first = parser.feed({
            "status": "PENDING",
            "blocks": [{
                "intended_usage": "ask_text_0_markdown",
                "markdown_block": {"answer": "Hello"},
            }],
        })
        self.assertEqual(text_of(first), "Hello")
        second = parser.feed({
            "blocks": [{
                "intended_usage": "ask_text_0_markdown",
                "diff_block": {
                    "field": "markdown_block",
                    "patches": [{"path": "/chunks/0", "value": "Hello world"}],
                },
            }],
        })
        self.assertEqual(text_of(second), " world")
        self.assertEqual(parser.full_answer, "Hello world")

    def test_root_replace(self):
        parser = perplexity._Parser()
        parser.feed({
            "blocks": [{
                "intended_usage": "ask_text",
                "markdown_block": {"answer": "old"},
            }],
        })
        parser.feed({
            "blocks": [{
                "intended_usage": "ask_text",
                "diff_block": {
                    "field": "markdown_block",
                    "patches": [{"path": "", "value": {"chunks": ["new", " text"]}}],
                },
            }],
        })
        self.assertEqual(parser.full_answer, "new text")


class WorkflowTest(unittest.TestCase):
    def test_workflow_block_then_chunk_patches(self):
        parser = perplexity._Parser()
        first = parser.feed({
            "blocks": [{
                "intended_usage": "ask_text_0_markdown",
                "workflow_block": {
                    "steps": [{
                        "items": [{
                            "payload": {"text_payload": {
                                "variant": "answer",
                                "chunks": ["One"],
                            }},
                        }],
                    }],
                },
            }],
        })
        self.assertEqual(text_of(first), "One")
        second = parser.feed({
            "blocks": [{
                "intended_usage": "ask_text_0_markdown",
                "diff_block": {
                    "field": "workflow_block",
                    "patches": [{
                        "path": "/steps/0/items/0/payload/text_payload/chunks/1",
                        "value": " two",
                    }],
                },
            }],
        })
        self.assertEqual(text_of(second), " two")
        self.assertEqual(parser.full_answer, "One two")

    def test_thinking_steps_are_not_answer_text(self):
        parser = perplexity._Parser()
        out = parser.feed({
            "blocks": [{
                "intended_usage": "plan",
                "plan_block": {"goals": [{"description": "Find the docs"}]},
            }],
        })
        self.assertEqual(text_of(out), "")
        self.assertEqual([item.text for item in out if item.kind == events.THINKING],
                         ["Find the docs"])


class CompletedFallbackTest(unittest.TestCase):
    def test_final_flag_marks_done(self):
        parser = perplexity._Parser()
        parser.feed({
            "blocks": [{
                "intended_usage": "workflow_root",
                "diff_block": {
                    "field": "workflow_block",
                    "patches": [{
                        "op": "add", "path": "/steps/0",
                        "value": {"items": [{"payload": {"text_payload": {
                            "variant": "answer", "chunks": ["done"]}}}]},
                    }],
                },
            }],
            "final": True,
            "status": "PENDING",
        })
        self.assertTrue(parser.done)
        self.assertEqual(parser.full_answer, "done")

    def test_recover_answer_from_step_blob(self):
        parser = perplexity._Parser()
        blob = ('[{"step_type":"FINAL","content":'
                '"{\\"answer\\": \\"Recovered answer\\"}"}]')
        out = parser.feed({"status": "COMPLETED", "text": blob})
        self.assertTrue(parser.done)
        self.assertEqual(text_of(out), "Recovered answer")
        self.assertEqual(parser.full_answer, "Recovered answer")

    def test_error_frame(self):
        parser = perplexity._Parser()
        out = parser.feed({"error_code": "RATE_LIMITED",
                           "error_message": "slow down"})
        self.assertTrue(parser.done)
        self.assertEqual(out[0].kind, events.ERROR)
        self.assertIn("slow down", out[0].text)


class SseTest(unittest.TestCase):
    def test_end_of_stream_keeps_the_final_payload(self):
        lines = [
            'data: {"status": "PENDING"}',
            "",
            "event: end_of_stream",
            'data: {"status": "COMPLETED"}',
            "",
        ]
        events = list(perplexity._sse_events(lines))
        self.assertEqual([event["status"] for event in events],
                         ["PENDING", "COMPLETED"])

    def test_done_marker_stops(self):
        events = list(perplexity._sse_events(["data: [DONE]", ""]))
        self.assertEqual(events, [])


class ModelAliasTest(unittest.TestCase):
    def test_friendly_names_map_to_site_ids(self):
        self.assertEqual(perplexity.MODEL_ALIASES["auto"], "pplx_pro")
        self.assertEqual(perplexity.MODEL_ALIASES["sonar"], "turbo")
        self.assertEqual(perplexity.MODEL_ALIASES["council"],
                         "pplx_agentic_research")

    def test_unknown_id_passes_through(self):
        provider = perplexity.Perplexity()
        self.assertEqual(provider.chosen_model("site_model_id"), "site_model_id")


class HistoryExtractionTest(unittest.TestCase):
    def test_markdown_block(self):
        entry = {
            "query_str": "hi",
            "blocks": [{
                "intended_usage": "ask_text_0_markdown",
                "markdown_block": {"answer": "the answer"},
            }],
        }
        self.assertEqual(perplexity._entry_answer(entry), "the answer")

    def test_workflow_block(self):
        entry = {
            "blocks": [{
                "workflow_block": {
                    "steps": [{
                        "items": [
                            {"payload": {"text_payload": {
                                "variant": "answer", "chunks": ["a", "b"]}}},
                            {"payload": {"text_payload": {
                                "variant": "thinking", "chunks": ["no"]}}},
                        ],
                    }],
                },
            }],
        }
        self.assertEqual(perplexity._entry_answer(entry), "ab")


if __name__ == "__main__":
    unittest.main()
