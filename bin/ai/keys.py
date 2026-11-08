"""Resolve an API key for an OpenAI-compatible provider.

The API providers (``ai.openai``, ``ai.openrouter``, ``ai.lms``) need a key
that the user should not have to paste on every invocation.  This module looks
for one, in this order:

1. an explicit ``--api-key`` (the caller passes it in);
2. the provider's environment variables (e.g. ``OPENROUTER_API_KEY``);
3. a ``.env`` file (``$AI_ENV_FILE``, then ``.env``/``.env.local`` walking up
   from the current directory to the git root/home, then
   ``$XDG_CONFIG_HOME/ai/.env`` and ``~/.env``);
4. the credentials the installed **opencode** knows about — read with
   ``opencode2 auth export`` (or ``opencode auth export``), falling back to
   ``$XDG_DATA_HOME/opencode/auth.json``;
5. the ``options.apiKey`` of a provider defined in
   ``$XDG_CONFIG_HOME/opencode/opencode.jsonc``.

Only the first hit wins.  The key is never printed by this module; callers that
want to show *where* a key came from use :func:`resolve`'s source string and
:func:`mask` for a redacted preview.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path

#: Provider registry name -> the id opencode stores it under.
OPENCODE_IDS = {
    "openai": "openai",
    "openrouter": "openrouter",
    "lms": "lmstudio",
    "lmstudio": "lmstudio",
    "deepseek": "deepseek",
    "dseek": "deepseek",
    "gap": "gap",
    "gapgpt": "gap",
    "gemini": "gemini",
    "grok": "grok",
    "kimi": "kimi",
    "perplexity": "perplexity",
    "pplx": "perplexity",
}

#: Environment variables that may hold a provider's key, on top of the
#: provider's own ``key_env``.  Ordered: the provider-specific one first.
EXTRA_ENV = {
    "openrouter": ("OPENROUTER_API_KEY", "OPENROUTER_KEY"),
    "openai": ("OPENAI_API_KEY",),
    "lms": ("LMS_API_KEY", "LMSTUDIO_API_KEY"),
    "deepseek": ("DEEPSEEK_API_KEY",),
}

DOTENV_NAMES = (".env", ".env.local")


def mask(value):
    """A redacted preview of *value* (never the whole secret)."""
    if not value:
        return ""
    if len(value) <= 8:
        return "*" * len(value)
    return f"{value[:6]}…{value[-4:]}"


def _xdg(env_name, default_parts):
    base = os.environ.get(env_name)
    if base:
        return Path(base)
    return Path.home().joinpath(*default_parts)


def opencode_data_dir():
    return _xdg("XDG_DATA_HOME", (".local", "share")) / "opencode"


def opencode_config_dir():
    return _xdg("XDG_CONFIG_HOME", (".config",)) / "opencode"


# -- .env files -------------------------------------------------------------


def _parse_env_file(path):
    """Read a ``KEY=VALUE`` file into a dict (``export``, quotes, comments)."""
    values = {}
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return values
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export "):].lstrip()
        name, separator, value = line.partition("=")
        if not separator:
            continue
        name = name.strip()
        if not name or not name.replace("_", "").isalnum():
            continue
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]
        else:
            # An unquoted value ends at a trailing comment.
            value = value.split(" #", 1)[0].rstrip()
        values[name] = value
    return values


def _dotenv_paths():
    """The ``.env`` files to consult, nearest first."""
    seen = set()
    paths = []

    def add(path):
        try:
            key = str(path.resolve())
        except OSError:
            key = str(path)
        if key in seen:
            return
        seen.add(key)
        paths.append(path)

    explicit = os.environ.get("AI_ENV_FILE")
    if explicit:
        add(Path(explicit).expanduser())
        return paths

    here = Path.cwd()
    stop = Path.home()
    # The git root, if any, is a natural stopping point.
    try:
        root = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True, text=True, timeout=2, check=False)
        if root.returncode == 0 and root.stdout.strip():
            stop = Path(root.stdout.strip())
    except (OSError, subprocess.SubprocessError):
        pass
    for directory in (here, *here.parents):
        # Never read the filesystem root's own .env unless it is the intended
        # stopping point (e.g. HOME=/ or a repo at /).
        if directory.parent == directory and directory != stop:
            break
        for name in DOTENV_NAMES:
            add(directory / name)
        if directory == stop:
            break
    add(opencode_config_dir().parent / "ai" / ".env")
    add(Path.home() / ".env")
    return paths


def _from_dotenv(env_names):
    for path in _dotenv_paths():
        if not path.is_file():
            continue
        values = _parse_env_file(path)
        for name in env_names:
            if values.get(name):
                return values[name], f"dotenv:{path}"
    return "", ""


# -- opencode ---------------------------------------------------------------


def _run_capture(cmd, timeout):
    """Run *cmd* and return ``(returncode, stdout_text)``.

    ``opencode`` may leave a background daemon holding the inherited stdout
    pipe, which makes ``capture_output`` block forever waiting for EOF.  Writing
    stdout to a temporary file sidesteps that: the file can be read as soon as
    the direct child exits, whatever else still holds the descriptor.
    """
    import tempfile

    with tempfile.TemporaryFile(mode="w+b") as out:
        try:
            result = subprocess.run(
                cmd, stdin=subprocess.DEVNULL, stdout=out,
                stderr=subprocess.DEVNULL, timeout=timeout,
                start_new_session=True, check=False)
        except (OSError, subprocess.SubprocessError):
            return 1, ""
        out.seek(0)
        return result.returncode, out.read().decode("utf-8", "replace")


_OPENCODE_CACHE = None


def _opencode_export():
    """``{integrationID: key}`` from ``opencode2 auth export`` (or ``opencode``)."""
    global _OPENCODE_CACHE
    if _OPENCODE_CACHE is not None:
        return _OPENCODE_CACHE
    for tool in ("opencode2", "opencode"):
        exe = shutil.which(tool)
        if not exe:
            continue
        returncode, stdout = _run_capture([exe, "auth", "export"], timeout=5)
        if returncode != 0 or not stdout.strip():
            continue
        try:
            payload = json.loads(stdout)
        except ValueError:
            continue
        keys = {}
        if isinstance(payload, dict):
            # The ``auth.json`` shape: ``{integration: {type, key}}``.
            for integration, value in payload.items():
                if isinstance(value, dict):
                    key = value.get("key") or ""
                elif isinstance(value, str):
                    key = value
                else:
                    key = ""
                if integration and key:
                    keys[str(integration)] = str(key)
        else:
            for entry in payload if isinstance(payload, list) else ():
                if not isinstance(entry, dict):
                    continue
                integration = entry.get("integrationID") or entry.get("integration")
                value = entry.get("value")
                key = ""
                if isinstance(value, dict):
                    key = value.get("key") or ""
                elif isinstance(value, str):
                    key = value
                if integration and key and entry.get("active", True):
                    keys.setdefault(str(integration), str(key))
        if keys:
            _OPENCODE_CACHE = (keys, f"opencode:{tool} auth export")
            return _OPENCODE_CACHE
    _OPENCODE_CACHE = ({}, "")
    return _OPENCODE_CACHE


def _opencode_auth_json():
    path = opencode_data_dir() / "auth.json"
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, UnicodeDecodeError):
        return {}, ""
    keys = {}
    if isinstance(payload, dict):
        for integration, value in payload.items():
            if isinstance(value, dict):
                key = value.get("key") or ""
            elif isinstance(value, str):
                key = value
            else:
                key = ""
            if key:
                keys[str(integration)] = str(key)
    return keys, f"opencode:{path}"


def _opencode_keys():
    if os.environ.get("AI_KEYS_NO_OPENCODE", "").strip().lower() in ("1", "true", "yes", "on"):
        return {}, ""
    keys, source = _opencode_export()
    if keys:
        return keys, source
    return _opencode_auth_json()


# -- opencode config --------------------------------------------------------


def _strip_jsonc(text):
    """Drop ``//`` and ``/* */`` comments and trailing commas from JSONC."""
    out = []
    i, n = 0, len(text)
    in_string = False
    escape = False
    while i < n:
        ch = text[i]
        if in_string:
            out.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            out.append(ch)
            i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(ch)
        i += 1
    cleaned = "".join(out)
    # Remove trailing commas before } or ] — string-aware so a comma inside a
    # string literal is never dropped.
    result = []
    in_string = False
    escape = False
    for idx, ch in enumerate(cleaned):
        if in_string:
            result.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            result.append(ch)
            continue
        if ch == ",":
            j = idx + 1
            while j < len(cleaned) and cleaned[j] in " \t\r\n":
                j += 1
            if j < len(cleaned) and cleaned[j] in "}]":
                continue
        result.append(ch)
    return "".join(result)


def _expand_env(value):
    """Expand ``{env:NAME}`` placeholders in a config value."""
    if not isinstance(value, str) or "{env:" not in value:
        return value

    def replace(match):
        return os.environ.get(match.group(1), "")

    import re
    return re.sub(r"\{env:([A-Za-z_][A-Za-z0-9_]*)\}", replace, value)


def _opencode_config():
    """``{provider_id: apiKey}`` from ``opencode.jsonc``."""
    path = opencode_config_dir() / "opencode.jsonc"
    if not path.is_file():
        path = opencode_config_dir() / "opencode.json"
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return {}, ""
    try:
        payload = json.loads(_strip_jsonc(text))
    except ValueError:
        return {}, ""
    keys = {}
    providers = payload.get("provider") if isinstance(payload, dict) else None
    if isinstance(providers, dict):
        for provider_id, entry in providers.items():
            if not isinstance(entry, dict):
                continue
            options = entry.get("options")
            if not isinstance(options, dict):
                continue
            key = _expand_env(options.get("apiKey") or "")
            if key and key not in ("public", "not-needed", "none"):
                keys[str(provider_id)] = key
    return keys, f"opencode:{path}"


# -- public API -------------------------------------------------------------


def _env_names(provider, env_names):
    if isinstance(env_names, str):
        names = [env_names] if env_names else []
    else:
        names = list(env_names or ())
    for name in EXTRA_ENV.get(provider, ()):
        if name not in names:
            names.append(name)
    return names


def resolve(provider, env_names=(), explicit=None):
    """Return ``(key, source)`` for *provider*.

    ``source`` is a short human string such as ``env:OPENROUTER_API_KEY`` or
    ``opencode:opencode2 auth export``; it is empty when nothing was found.
    The key itself is returned unmodified.
    """
    if explicit:
        return explicit.strip(), "flag:--api-key"

    names = _env_names(provider, env_names)
    for name in names:
        value = os.environ.get(name)
        if value and value.strip():
            return value.strip(), f"env:{name}"

    key, source = _from_dotenv(names)
    if key:
        return key, source

    integration = OPENCODE_IDS.get(provider, provider)
    keys, source = _opencode_keys()
    if keys.get(integration):
        return keys[integration], source

    config_keys, config_source = _opencode_config()
    for candidate in (integration, provider):
        if config_keys.get(candidate):
            return config_keys[candidate], config_source

    return "", ""


def describe(provider, env_names=()):
    """``(source, masked_key)`` for a diagnostic listing."""
    key, source = resolve(provider, env_names)
    return (source or "(none)", mask(key))
