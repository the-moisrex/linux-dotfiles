#!/usr/bin/env python3
"""Offline tests for the GapGPT adapter's turn-recovery path.

GapGPT runs its tools (``file_retrieval``) server-side, which leaves the
websocket quiet for seconds at a time.  The adapter then falls back to reading
the turn from ``GET /api/v1/get_chat/token/<token>`` (``_poll``), so that
fallback must (a) carry the same ``accessToken`` bearer the upload/history
paths use and (b) never abort the turn when the flaky ArvanCloud edge stalls or
resets the request.  No network, no browser, no websocket.
"""

from __future__ import annotations

import json
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "bin"))

from ai import net  # noqa: E402
from ai.providers import gap  # noqa: E402

MATCH = {
    "user_agent": "test-agent",
    "cookie_header": "accessToken=tok123; other=1",
    "cookies": [{"name": "accessToken", "value": "tok123"}],
}

ACK = json.dumps({
    "event": "ack_new_message",
    "rid": 7,
    "data": {
        "status": "ok",
        "chat_token": "chat-1",
        "message": {"token": "turn-1"},
    },
})

COMPLETED_CHAT = {
    "messages": [
        {"token": "turn-1", "status": "completed",
         "response": [{"type": "text", "content": "done"}]},
    ],
}


class FakeSocket:
    """A websocket that replays *frames* then times out on every read."""

    def __init__(self, frames):
        self.frames = list(frames)
        self.sent = []
        self.connected = True

    def settimeout(self, timeout):
        pass

    def send(self, data):
        self.sent.append(data)
        return len(data)

    def recv(self):
        if self.frames:
            return self.frames.pop(0)
        raise TimeoutError("recv timed out")

    def close(self):
        self.connected = False


def make_conversation(debug=False):
    provider = gap.Gap()
    return provider.open_conversation(MATCH, "A-GAP", debug=debug, timeout=1.0)


class PollHeadersTest(unittest.TestCase):
    def test_the_poll_headers_carry_the_bearer_token(self):
        conversation = make_conversation()
        self.assertEqual(conversation.headers.get("Authorization"),
                         "Bearer tok123")

    def test_poll_reads_the_turn_with_authenticated_headers(self):
        conversation = make_conversation()
        conversation.conversation_id = "chat-1"
        conversation.turn_token = "turn-1"

        captured = {}

        def fake_request(url, **kwargs):
            captured["url"] = url
            captured.update(kwargs)
            return COMPLETED_CHAT

        original = net.request_json
        net.request_json = fake_request
        try:
            snapshot, done = conversation._poll()
        finally:
            net.request_json = original

        self.assertTrue(done)
        self.assertEqual(snapshot["token"], "turn-1")
        self.assertEqual(captured["headers"].get("Authorization"),
                         "Bearer tok123")


class PollFailureTest(unittest.TestCase):
    def test_a_transport_error_is_not_fatal(self):
        # net.request_json raises the client's own exception (a curl timeout,
        # not a net.StreamError) when the edge tarpits.
        conversation = make_conversation()
        conversation.conversation_id = "chat-1"

        def boom(url, **kwargs):
            raise RuntimeError("curl: (28) Operation timed out")

        original = net.request_json
        net.request_json = boom
        try:
            self.assertEqual(conversation._poll(), (None, False))
        finally:
            net.request_json = original

    def test_a_stream_error_is_not_fatal(self):
        conversation = make_conversation()
        conversation.conversation_id = "chat-1"

        def boom(url, **kwargs):
            raise net.StreamError("HTTP 502")

        original = net.request_json
        net.request_json = boom
        try:
            self.assertEqual(conversation._poll(), (None, False))
        finally:
            net.request_json = original

    def test_read_turn_survives_a_raising_poll(self):
        # After the ack the socket goes quiet; a poll that raises must be
        # swallowed (the turn then ends on its own timeout, not on the poll).
        conversation = make_conversation()
        conversation.socket = FakeSocket([ACK])
        polled = []

        def boom():
            polled.append(True)
            raise RuntimeError("curl: (28) Operation timed out")

        conversation._poll = boom
        original = gap.POLL_AFTER
        gap.POLL_AFTER = 0.0
        try:
            events = list(conversation._read_turn(7, []))
        finally:
            gap.POLL_AFTER = original

        self.assertTrue(polled, "the poll fallback was never reached")
        self.assertTrue(any(event.kind == "error" and "timed out" in event.text
                            for event in events))


if __name__ == "__main__":
    unittest.main()
