"""Load a site's signed-in session (cookies + web storage) via browser.session."""

from __future__ import annotations

import json
import os
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.dirname(HERE)


class SessionError(Exception):
    """No usable session could be assembled for a provider."""


def _tool(name):
    path = os.path.join(BIN, name)
    if os.path.exists(path):
        return path
    return shutil.which(name)


def load(target, browser=None, profile=None, family=None, open_only=False,
         exact=False, timeout=120):
    """Return the best ``browser.session`` match for *target* (or None)."""
    exe = _tool("browser.session")
    if not exe:
        raise SessionError("browser.session not found next to ai or on PATH")
    cmd = [exe, target, "--format", "json"]
    if browser:
        cmd += ["--browser", browser]
    if profile:
        cmd += ["--profile", profile]
    if family:
        cmd += ["--family", family]
    if open_only:
        cmd.append("--open-only")
    if exact:
        cmd.append("--exact")
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError) as exc:
        raise SessionError(f"browser.session failed: {exc}") from exc
    try:
        data = json.loads(out.stdout or "{}")
    except json.JSONDecodeError:
        raise SessionError("browser.session returned no JSON")
    matches = data.get("matches") or []
    return matches[0] if matches else None


def build_headers(match, extra=None):
    """Request headers seeded from the session (UA + Cookie), plus *extra*."""
    headers = {}
    if match.get("user_agent"):
        headers["User-Agent"] = match["user_agent"]
    if match.get("cookie_header"):
        headers["Cookie"] = match["cookie_header"]
    if extra:
        headers.update(extra)
    return headers


def localstorage(match):
    return ((match or {}).get("storage") or {}).get("localstorage") or {}


def sessionstorage(match):
    return ((match or {}).get("storage") or {}).get("sessionstorage") or {}


def storage_value(match, *keys):
    """Look up the first present key in localStorage then sessionStorage.

    Stored values that are a JSON object with a ``value`` member (a common
    framing, e.g. ``{"value": "…", "__version": "0"}``) are unwrapped.
    """
    local = localstorage(match)
    session = sessionstorage(match)
    for key in keys:
        for store in (local, session):
            if key in store:
                return _unwrap(store[key])
    return None


def _unwrap(value):
    if isinstance(value, str) and value[:1] in "{[":
        try:
            parsed = json.loads(value)
        except ValueError:
            return value
        if isinstance(parsed, dict) and "value" in parsed:
            return parsed["value"]
    return value


def cookie_value(match, name):
    for cookie in (match or {}).get("cookies") or []:
        if cookie.get("name") == name:
            return cookie.get("value")
    return None
