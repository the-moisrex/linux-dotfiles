"""Saved conversations for ``-c`` / ``-i``.

Every provider keeps the id of its most recent conversation (its
``conversation_id`` plus an opaque ``resume`` dict) and a trimmed transcript
under ``$XDG_STATE_HOME/ai/<provider>.json``.  ``--continue`` reopens it with
that id; a provider that cannot reattach to an id (Grok's WebSocket session is
per-process) replays the transcript instead.

``ai.debate`` keeps the same record for every participant of its last debate
under ``$XDG_STATE_HOME/ai/debate-<providers>.json``, keyed by the sorted
provider names, so ``ai.debate -c grok dseek`` resumes that panel without
touching the single-provider ``<provider>.json`` files.
"""

from __future__ import annotations

import json
import os
import time

#: How many transcript entries to keep alongside the id.
MAX_HISTORY = 20

#: A saved conversation older than this is ignored by ``--continue``.
MAX_AGE = 7 * 24 * 3600


def _directory():
    base = (os.environ.get("XDG_STATE_HOME")
            or os.path.join(os.path.expanduser("~"), ".local", "state"))
    return os.path.join(base, "ai")


def path(provider):
    """The state file for *provider*."""
    return os.path.join(_directory(), f"{provider}.json")


def load(provider, max_age=MAX_AGE):
    """Return the stored conversation, or ``None`` when there is none/too old."""
    try:
        with open(path(provider), encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict):
        return None
    updated = data.get("updated")
    if max_age and isinstance(updated, (int, float)) and time.time() - updated > max_age:
        return None
    return data


def save(provider, conversation_id, model, history=None, resume=None):
    """Persist a conversation (best effort) and return the stored dict."""
    data = {
        "conversation_id": conversation_id or "",
        "model": model or "",
        "updated": time.time(),
        "history": list(history or [])[-MAX_HISTORY:],
        "resume": dict(resume or {}),
    }
    _write(path(provider), data)
    return data


def _write(file_path, data):
    """Atomically write *data* as JSON, ignoring I/O errors (best effort)."""
    directory = os.path.dirname(file_path)
    try:
        os.makedirs(directory, exist_ok=True)
        temporary = file_path + ".tmp"
        with open(temporary, "w", encoding="utf-8") as handle:
            json.dump(data, handle, ensure_ascii=False)
        os.replace(temporary, file_path)
    except OSError:
        pass


def clear(provider):
    """Forget the stored conversation for *provider*."""
    try:
        os.remove(path(provider))
    except OSError:
        pass


def debate_path(names):
    """The state file for the last debate between *names* (order-insensitive)."""
    key = "debate-" + "-".join(sorted(names))
    return os.path.join(_directory(), f"{key}.json")


def load_debate(names, max_age=MAX_AGE):
    """Return ``{provider: record}`` for the last debate, or ``None``.

    The file is keyed by the sorted provider set, so running the panel in a
    different order resumes the same conversations.
    """
    try:
        with open(debate_path(names), encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict):
        return None
    updated = data.get("updated")
    if max_age and isinstance(updated, (int, float)) and time.time() - updated > max_age:
        return None
    participants = data.get("participants")
    if not isinstance(participants, dict):
        return None
    return {name: record for name, record in participants.items()
            if isinstance(record, dict)}


def save_debate(names, participants):
    """Persist the panel's conversations (best effort) and return the dict.

    *participants* maps a provider name to a record with ``conversation_id``,
    ``model``, ``history`` and ``resume``; each is trimmed like a
    single-provider record.
    """
    records = {}
    for name, record in participants.items():
        record = record or {}
        records[name] = {
            "conversation_id": record.get("conversation_id") or "",
            "model": record.get("model") or "",
            "history": list(record.get("history") or [])[-MAX_HISTORY:],
            "resume": dict(record.get("resume") or {}),
        }
    data = {
        "updated": time.time(),
        "providers": sorted(names),
        "participants": records,
    }
    _write(debate_path(names), data)
    return data


def clear_debate(names):
    """Forget the last debate between *names*."""
    try:
        os.remove(debate_path(names))
    except OSError:
        pass
