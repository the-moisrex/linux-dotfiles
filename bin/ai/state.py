"""Saved conversations for ``-c`` / ``-i``.

Every provider keeps the id of its most recent conversation (its
``conversation_id`` plus an opaque ``resume`` dict) and a trimmed transcript
under ``$XDG_STATE_HOME/ai/<provider>.json``.  ``--continue`` reopens it with
that id; a provider that cannot reattach to an id (Grok's WebSocket session is
per-process) replays the transcript instead.
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
    directory = _directory()
    try:
        os.makedirs(directory, exist_ok=True)
        temporary = path(provider) + ".tmp"
        with open(temporary, "w", encoding="utf-8") as handle:
            json.dump(data, handle, ensure_ascii=False)
        os.replace(temporary, path(provider))
    except OSError:
        pass
    return data


def clear(provider):
    """Forget the stored conversation for *provider*."""
    try:
        os.remove(path(provider))
    except OSError:
        pass
