"""Shared command line for ``ai.chatgpt`` / ``ai.grok`` / ``ai.dseek``.

Reads the prompt from the arguments or stdin, loads the site's signed-in session
through ``bin/browser.session``, streams the answer through the selected
provider and renders it (Markdown on a TTY, clean text when piped, or one JSON
event per line with ``--json``).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys

from . import session as session_mod
from .events import ERROR, DONE, TEXT
from .providers import all_providers
from .render import Renderer

BIN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _bin_tool(name):
    path = os.path.join(BIN, name)
    if os.path.exists(path):
        return path
    return shutil.which(name)


def read_prompt(parts):
    text = " ".join(parts).strip()
    if not text and not sys.stdin.isatty():
        text = sys.stdin.read().strip()
    return text


def build_parser(provider):
    models = ", ".join(provider.models)
    p = argparse.ArgumentParser(
        prog=f"ai.{provider.name}",
        description=f"Start a new {provider.label} chat from a prompt "
                    f"(replays the signed-in web session; no API key).")
    p.add_argument("prompt", nargs="*", help="prompt text (or pipe it on stdin)")
    p.add_argument("-m", "--model", default=None,
                   help=f"model (default: {provider.default_model}; one of {models})")
    p.add_argument("-s", "--system", default=None, help="system / custom instructions")
    p.add_argument("--show-thinking", dest="thinking", action="store_true", default=None,
                   help="show the model's reasoning (default on a TTY)")
    p.add_argument("--no-thinking", dest="thinking", action="store_false",
                   help="hide the model's reasoning")
    p.add_argument("--json", action="store_true", help="emit one JSON event per line")
    p.add_argument("--raw", action="store_true", help="print only the answer text")
    p.add_argument("--copy", action="store_true", help="copy the answer to the clipboard")
    p.add_argument("--no-color", action="store_true", help="disable colours")
    p.add_argument("--debug", action="store_true", help="print raw stream lines to stderr")
    p.add_argument("--timeout", type=float, default=600, help="request timeout in seconds")
    p.add_argument("--browser", action="append", default=[],
                   help="restrict the session to a browser id (repeatable)")
    p.add_argument("--profile", default=None, help="restrict the session to a profile")
    p.add_argument("--family", choices=["chromium", "firefox"], default=None)
    p.add_argument("--open-only", action="store_true",
                   help="only use browsers/profiles that are currently running")
    p.add_argument("--exact", action="store_true",
                   help="match the session host exactly")
    p.add_argument("--print-session", action="store_true",
                   help="show which session would be used and exit")
    p.add_argument("--list", action="store_true",
                   help="list the available providers and exit")
    return p


def _session_summary(match):
    cookies = match.get("cookies") or []
    storage = match.get("storage") or {}
    return {
        "browser": match.get("browser"),
        "profile": match.get("profile"),
        "path": match.get("path"),
        "is_open": match.get("is_open"),
        "user_agent": match.get("user_agent"),
        "cookies": len(cookies),
        "localstorage_keys": sorted((storage.get("localstorage") or {}).keys()),
        "sessionstorage_keys": sorted((storage.get("sessionstorage") or {}).keys()),
    }


def emit_json(event):
    obj = {"kind": event.kind}
    if event.text:
        obj["text"] = event.text
    if event.name:
        obj["name"] = event.name
    if event.status:
        obj["status"] = event.status
    if event.args:
        obj["args"] = event.args
    if event.data:
        obj["data"] = event.data
    print(json.dumps(obj, ensure_ascii=False), flush=True)


def copy_answer(text):
    exe = _bin_tool("clipboard")
    if not exe:
        return False
    try:
        subprocess.run([exe, "copy"], input=text, text=True, check=False)
        return True
    except OSError:
        return False


def main(provider_name, argv=None):
    # Die quietly on a closed downstream pipe (e.g. ``ai.dseek … | head``)
    # instead of raising BrokenPipeError through the stream loop.
    try:
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (AttributeError, ValueError):
        pass
    providers = all_providers()
    if provider_name not in providers:
        print(f"ai: unknown provider {provider_name!r}", file=sys.stderr)
        return 2
    provider = providers[provider_name]
    args = build_parser(provider).parse_args(argv)

    if args.list:
        for name, prov in providers.items():
            print(f"{name:<10} {prov.label:<10} {', '.join(prov.models)}")
        return 0

    try:
        match = session_mod.load(
            provider.session_target, browser=args.browser or None,
            profile=args.profile, family=args.family,
            open_only=args.open_only, exact=args.exact)
    except session_mod.SessionError as exc:
        print(f"ai.{provider_name}: {exc}", file=sys.stderr)
        return 1
    if not match:
        print(f"ai.{provider_name}: no signed-in session found for "
              f"{provider.session_target}; open {provider.origin} in a browser once",
              file=sys.stderr)
        return 1

    if args.print_session:
        print(json.dumps(_session_summary(match), indent=2))
        return 0

    prompt = read_prompt(args.prompt)
    if not prompt:
        build_parser(provider).print_usage(sys.stderr)
        return 2

    model = provider.chosen_model(args.model)
    thinking = args.thinking if args.thinking is not None else sys.stderr.isatty()
    renderer = Renderer(show_thinking=thinking, raw=args.raw,
                        color=not args.no_color, debug=args.debug)

    instance = provider()
    status = 0
    answer = []
    try:
        for event in instance.stream(match, prompt, model, args.system,
                                     args.debug, timeout=args.timeout):
            if args.json:
                emit_json(event)
            else:
                renderer.handle(event)
                renderer.write_debug(event)
            if event.kind == TEXT:
                answer.append(event.text)
            if event.kind == ERROR:
                status = 1
            elif event.kind == DONE:
                break
    except KeyboardInterrupt:
        if not args.json:
            renderer.finish()
        return 130

    if not args.json:
        if renderer.finish() and not status:
            status = 1
    if args.copy:
        copy_answer(renderer.text or "".join(answer))
    return status
