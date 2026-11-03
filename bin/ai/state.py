"""Saved conversations for ``-c`` / ``-i`` / ``--reply``.

Every provider keeps the id of its most recent conversation (its
``conversation_id`` plus an opaque ``resume`` dict) and a trimmed transcript
under ``$XDG_STATE_HOME/ai/<provider>.json``.  ``--continue`` reopens it with
that id; a provider that cannot reattach to an id (Grok's WebSocket session is
per-process) replays the transcript instead.

The same record is additionally mirrored per terminal under
``$XDG_STATE_HOME/ai/terminals/<scope>/<provider>.json``, and a plain-text
``last`` file names the provider that talked most recently there (``ai/last``
holds the global one).  ``prompt``'s ``.ai.reply`` sink reads that marker and
passes ``-c --reply`` so it continues the chat this terminal actually used,
even when another terminal used the same provider in between.  The scope is
``key-<...>`` for ``PROMPT_TERMINAL_KEY``, otherwise
``sid-<n>[_tty-<path>]`` (see ``prompt::terminal::scope`` in
``prompts/src/sdk/terminal.cpp``, which implements the same encoding).

``ai.debate`` keeps the same record for every participant of its last debate
under ``$XDG_STATE_HOME/ai/debate-<providers>.json``, keyed by the sorted
provider names, so ``ai.debate -c grok dseek`` resumes that panel without
touching the single-provider ``<provider>.json`` files.  It also mirrors each
participant's chat into the terminal scope and marks ``last`` as ``debate``,
so a later ``.ai.reply`` asks for a provider instead of silently guessing.
"""

from __future__ import annotations

import json
import os
import time

from . import diagnostics

#: How many transcript entries to keep alongside the id.
MAX_HISTORY = 20

#: A saved conversation older than this is ignored by ``--continue``.
MAX_AGE = 7 * 24 * 3600


def _directory():
    base = (os.environ.get("XDG_STATE_HOME")
            or os.path.join(os.path.expanduser("~"), ".local", "state"))
    return os.path.join(base, "ai")


def path(provider):
    """The global state file for *provider* (what plain ``-c`` reads)."""
    return os.path.join(_directory(), f"{provider}.json")


def _env(name):
    return os.environ.get(name, "")


def _controlling_tty():
    """The controlling terminal, matching ``prompt::terminal``'s detection."""
    try:
        fd = os.open("/dev/tty", os.O_RDONLY | os.O_NOCTTY)
    except OSError:
        fd = None
    if fd is not None:
        try:
            try:
                name = os.ttyname(fd)
            except OSError:
                name = ""
            if name:
                return name
        finally:
            os.close(fd)
    for candidate in (0, 1, 2):
        try:
            name = os.ttyname(candidate)
        except OSError:
            continue
        if name:
            return name
    return ""


def _slug(raw):
    """Filesystem-safe identity, byte-identical to ``prompt::terminal::scope``."""
    out = []
    for byte in raw.encode("utf-8", "surrogatepass"):
        if len(out) >= 120:
            break
        safe = (48 <= byte <= 57) or (65 <= byte <= 90) or (97 <= byte <= 122) or byte in (46, 95, 45)
        out.append(chr(byte) if safe else "-")
    text = "".join(out)
    return text or "unknown"


def terminal_scope():
    """The current terminal's state scope (see the module docstring)."""
    key = _env("PROMPT_TERMINAL_KEY")
    if key:
        return _slug("key-" + key)

    sid = 0
    raw_sid = _env("PROMPT_TERMINAL_SID")
    if raw_sid:
        try:
            sid = int(raw_sid)
        except ValueError:
            sid = 0
    if sid == 0:
        try:
            sid = os.getsid(0)
        except OSError:
            sid = 0
    text = f"sid-{max(sid, 0)}"

    tty = _env("PROMPT_TERMINAL_TTY") or _controlling_tty()
    if tty:
        text += "_tty-" + tty
    return _slug(text)


def terminals_dir():
    """The per-terminal state directory."""
    return os.path.join(_directory(), "terminals")


def terminal_path(provider, scope=None):
    """The per-terminal state file for *provider*."""
    return os.path.join(terminals_dir(), scope or terminal_scope(), f"{provider}.json")


def last_path(scope=None):
    """The marker naming the provider that talked last (terminal or global)."""
    if scope is None:
        return os.path.join(_directory(), "last")
    return os.path.join(terminals_dir(), scope, "last")


def _read_record(file_path, max_age=MAX_AGE):
    try:
        with open(file_path, encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict):
        return None
    updated = data.get("updated")
    if max_age and isinstance(updated, (int, float)) and time.time() - updated > max_age:
        return None
    return data


def load(provider, max_age=MAX_AGE):
    """Return the global stored conversation, or ``None`` when there is none/too old."""
    return _read_record(path(provider), max_age)


def load_terminal(provider, scope=None, max_age=MAX_AGE):
    """Return *provider*'s conversation from this terminal, or ``None``.

    Unlike :func:`load` there is no global fallback: ``--reply`` must continue
    the chat this terminal actually used, not another terminal's.
    """
    return _read_record(terminal_path(provider, scope), max_age)


def _record(conversation_id, model, history, resume):
    return {
        "conversation_id": conversation_id or "",
        "model": model or "",
        "updated": time.time(),
        "history": list(history or [])[-MAX_HISTORY:],
        "resume": dict(resume or {}),
    }


def _mark_last(provider, scope):
    """Best-effort marker naming the newest provider (terminal and global)."""
    _write_text(last_path(scope), provider)
    _write_text(last_path(), provider)


def save(provider, conversation_id, model, history=None, resume=None, scope=None):
    """Persist a conversation (best effort) and return the stored dict.

    The global file keeps plain ``-c`` working; the terminal mirror is what
    ``--reply`` reads.
    """
    data = _record(conversation_id, model, history, resume)
    scope = scope or terminal_scope()
    _write(path(provider), data)
    _write(terminal_path(provider, scope), data)
    _mark_last(provider, scope)
    return data


def _write(file_path, data):
    """Atomically write *data* as JSON; return ``True`` on success."""
    directory = os.path.dirname(file_path)
    try:
        os.makedirs(directory, exist_ok=True)
        temporary = file_path + ".tmp"
        with open(temporary, "w", encoding="utf-8") as handle:
            json.dump(data, handle, ensure_ascii=False)
        os.replace(temporary, file_path)
        return True
    except OSError as exc:
        diagnostics.warn(
            f"could not save {os.path.basename(file_path)}: {exc}; "
            f"the conversation will not be available to -c/--continue")
        return False


def _write_text(file_path, text):
    """Atomically write a short plain-text file; return ``True`` on success."""
    directory = os.path.dirname(file_path)
    try:
        os.makedirs(directory, exist_ok=True)
        temporary = file_path + ".tmp"
        with open(temporary, "w", encoding="utf-8") as handle:
            handle.write(text + "\n")
        os.replace(temporary, file_path)
        return True
    except OSError as exc:
        diagnostics.warn(f"could not save {os.path.basename(file_path)}: {exc}")
        return False


def clear(provider, scope=None):
    """Forget *provider*'s conversation (global and this terminal)."""
    scope = scope or terminal_scope()
    for file_path in (path(provider), terminal_path(provider, scope)):
        try:
            os.remove(file_path)
        except FileNotFoundError:
            continue
        except OSError as exc:
            diagnostics.warn(f"could not forget {os.path.basename(file_path)}: {exc}")


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


def save_debate(names, participants, scope=None):
    """Persist the panel's conversations (best effort) and return the dict.

    *participants* maps a provider name to a record with ``conversation_id``,
    ``model``, ``history`` and ``resume``; each is trimmed like a
    single-provider record.  Each participant is also mirrored into the
    terminal scope (so a later ``.ai.reply <provider>`` can continue it) and
    ``last`` is set to ``debate``, which makes ``.ai.reply`` ask for a provider
    instead of guessing one.
    """
    scope = scope or terminal_scope()
    records = {}
    for name, record in participants.items():
        record = record or {}
        records[name] = _record(record.get("conversation_id"),
                                record.get("model"),
                                record.get("history"),
                                record.get("resume"))
    data = {
        "updated": time.time(),
        "providers": sorted(names),
        "participants": records,
    }
    _write(debate_path(names), data)
    for name, record in records.items():
        _write(terminal_path(name, scope), record)
    _write_text(last_path(scope), "debate")
    _write_text(last_path(), "debate")
    return data


def clear_debate(names, scope=None):
    """Forget the last debate between *names* (and this terminal's mirrors)."""
    scope = scope or terminal_scope()
    targets = [debate_path(names)]
    targets += [terminal_path(name, scope) for name in names]
    for file_path in targets:
        try:
            os.remove(file_path)
        except FileNotFoundError:
            continue
        except OSError as exc:
            diagnostics.warn(f"could not forget {os.path.basename(file_path)}: {exc}")
