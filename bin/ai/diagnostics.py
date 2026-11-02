"""Visible failure reporting for the ai package.

Every operation that can fail either raises (a hard failure) or reports through
:warn / :note.  There is no ``except ...: pass``: a failure is either propagated
or written to stderr, and a best-effort failure still shows up under
``--debug`` / ``AI_DEBUG=1``.

The one channel that cannot use this module is stderr itself; see
:meth:`ai.render.Renderer._write_err`.
"""

from __future__ import annotations

import os
import sys

_PREFIX = "ai"
_TRUE = {"1", "true", "yes", "on"}


def _enabled():
    return os.environ.get("AI_DEBUG", "").strip().lower() in _TRUE


def _write(message):
    try:
        sys.stderr.write(message + "\n")
        sys.stderr.flush()
    except (OSError, ValueError):
        # No usable reporting channel; there is nothing left to do.
        pass


def warn(message, prefix=_PREFIX):
    """Report a recoverable failure that the user should know about."""
    _write(f"[{prefix}] {message}")


def note(message, debug=None, prefix=_PREFIX):
    """Report a best-effort failure only when debugging is on."""
    if debug is None:
        debug = _enabled()
    if debug:
        warn(message, prefix=prefix)


def failed(message, prefix=_PREFIX, code=1):
    """Report an unrecoverable failure and return the process exit code."""
    warn(message, prefix=prefix)
    return code
