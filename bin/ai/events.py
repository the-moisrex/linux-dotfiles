"""The normalised stream-event model shared by every provider adapter.

Adapters translate their site's wire format into a flat sequence of
``StreamEvent`` objects; the renderer and the ``--json`` emitter only ever see
this model.  ``kind`` is one of:

  ``thinking``   reasoning text (DeepSeek ``reasoning_content``, ChatGPT
                 ``thoughts``, Grok thinking tags)
  ``text``       the visible answer
  ``tool_call``  a tool the *site* is running (web search, code, browsing …);
                 display only — we never execute anything
  ``usage``      token accounting, when the provider reports it
  ``done``       the stream finished normally
  ``error``      a fatal problem
  ``raw``        an unrecognised line (kept for ``--debug``)
"""

from __future__ import annotations

from dataclasses import dataclass, field

THINKING = "thinking"
TEXT = "text"
TOOL_CALL = "tool_call"
USAGE = "usage"
DONE = "done"
ERROR = "error"
RAW = "raw"


@dataclass
class StreamEvent:
    kind: str
    text: str = ""
    name: str = ""            # tool name
    status: str = ""          # tool phase: start | delta | done
    args: str = ""            # tool arguments (raw JSON text)
    data: dict = field(default_factory=dict)


def thinking(text):
    return StreamEvent(THINKING, text=text)


def text(chunk):
    return StreamEvent(TEXT, text=chunk)


def tool_call(name="", status="delta", args="", **data):
    return StreamEvent(TOOL_CALL, name=name, status=status, args=args, data=data)


def usage(**data):
    return StreamEvent(USAGE, data=data)


def done():
    return StreamEvent(DONE)


def error(message):
    return StreamEvent(ERROR, text=message)


def raw(line):
    return StreamEvent(RAW, text=line)
