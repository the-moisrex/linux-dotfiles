"""Normalised conversation history shared by the providers and ``ai.save``.

A provider's ``iter_chat_summaries`` yields :class:`ChatSummary` objects (the
sidebar list) and ``fetch_chat`` returns a :class:`Chat` with its
:class:`Message` list.  Both are deliberately provider-agnostic so the store,
renderer and search tools never need to know which site a chat came from.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone


#: Message roles.
USER = "user"
ASSISTANT = "assistant"
SYSTEM = "system"
TOOL = "tool"

#: Message kinds (what the text means, independent of who wrote it).
TEXT = "text"
THINKING = "thinking"
TOOL_CALL = "tool_call"
ATTACHMENT = "attachment"


def iso_from_epoch(value):
    """Convert a Unix timestamp (seconds or milliseconds) to ISO 8601 UTC."""
    if isinstance(value, str):
        return value
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return ""
    if value <= 0:
        return ""
    seconds = value / 1000.0 if value > 1e12 else float(value)
    try:
        moment = datetime.fromtimestamp(seconds, tz=timezone.utc)
    except (OverflowError, OSError, ValueError):
        return ""
    return moment.isoformat(timespec="seconds").replace("+00:00", "Z")


def epoch_of(value):
    """Parse an ISO 8601 timestamp to seconds (0 when it cannot be parsed)."""
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return float(value)
    if not isinstance(value, str) or not value.strip():
        return 0.0
    text = value.strip()
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        return datetime.fromisoformat(text).timestamp()
    except ValueError:
        # A space instead of ``T`` is still accepted by newer Python; retry
        # the common site format explicitly for older interpreters.
        try:
            return datetime.fromisoformat(text.replace(" ", "T", 1)).timestamp()
        except ValueError:
            return 0.0


def now_iso():
    """The current local time as an ISO 8601 string with its offset."""
    return datetime.now().astimezone().isoformat(timespec="seconds")


def newest(values):
    """The newest non-empty timestamp among *values* ("" when there is none)."""
    best = ""
    best_key = 0.0
    for value in values:
        key = epoch_of(value)
        if value and key >= best_key:
            best, best_key = value, key
    return best


@dataclass
class Message:
    """One message (or one fragment of one) in a conversation."""

    role: str
    text: str
    kind: str = TEXT
    created: str = ""
    message_id: str = ""
    parent_id: str = ""
    name: str = ""
    meta: dict = field(default_factory=dict)


@dataclass
class ChatSummary:
    """The cheap sidebar record for a conversation."""

    provider: str
    remote_id: str
    title: str = ""
    created: str = ""
    updated: str = ""
    model: str = ""
    project: str = ""
    url: str = ""


@dataclass
class Chat:
    """A conversation with its messages."""

    provider: str
    remote_id: str
    title: str = ""
    created: str = ""
    updated: str = ""
    model: str = ""
    project: str = ""
    url: str = ""
    messages: list = field(default_factory=list)


def merge(summary, chat):
    """Fill a fetched :class:`Chat` from its :class:`ChatSummary`."""
    chat.title = chat.title or summary.title
    chat.created = chat.created or summary.created
    chat.updated = newest([summary.updated, chat.updated])
    chat.model = chat.model or summary.model
    chat.project = chat.project or summary.project
    chat.url = chat.url or summary.url
    chat.provider = chat.provider or summary.provider
    chat.remote_id = chat.remote_id or summary.remote_id
    return chat
