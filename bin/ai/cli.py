"""Shared command line for ``ai.chatgpt`` / ``ai.grok`` / ``ai.dseek``.

Reads the prompt from the arguments or stdin, loads the site's signed-in session
through ``bin/browser.session``, streams the answer through the selected
provider and renders it (Markdown on a TTY, clean text when piped, or one JSON
event per line with ``--json``).

Two flags turn the one-shot call into a chat:

* ``-i`` / ``--interactive`` keeps the conversation open and prompts in a loop;
* ``-c`` / ``--continue`` reopens the last conversation with that provider
  (``--new`` starts a fresh one instead).  The conversation is remembered
  between runs under ``$XDG_STATE_HOME/ai/<provider>.json`` (see :mod:`ai.state`).
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
from . import state
from .events import ERROR, DONE, TEXT
from .providers import all_providers
from .render import Renderer

BIN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

_QUIT_WORDS = (":q", ":quit", ":exit", "exit", "quit")
_NEW_WORDS = (":new",)


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
        description=f"Chat with {provider.label} from a prompt or interactively "
                    f"(replays the signed-in web session; no API key).")
    p.add_argument("prompt", nargs="*", help="prompt text (or pipe it on stdin)")
    p.add_argument("-f", "--file", action="append", default=[], metavar="PATH",
                   help="upload and attach a local file to the first message "
                        "(repeatable)")
    p.add_argument("-m", "--model", default=None,
                   help=f"model (default: {provider.default_model}; one of {models})")
    p.add_argument("-s", "--system", default=None, help="system / custom instructions")
    p.add_argument("-i", "--interactive", action="store_true",
                   help="keep the conversation open and prompt in a loop")
    p.add_argument("-c", "--continue", dest="continue_", action="store_true",
                   help="continue the last conversation with this provider")
    p.add_argument("--new", action="store_true",
                   help="start a fresh conversation (ignore the saved one)")
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


def make_renderer(args):
    thinking = args.thinking if args.thinking is not None else sys.stderr.isatty()
    return Renderer(show_thinking=thinking, raw=args.raw,
                    color=not args.no_color, debug=args.debug)


def run_turn(conversation, prompt, args, renderer):
    """Stream one turn; return ``(status, answer)``."""
    status = 0
    parts = []
    # --file attaches to the first message only; the site keeps it in context.
    files = args.file if not conversation.turns else None
    for event in conversation.send(prompt, files=files):
        if args.json:
            emit_json(event)
        else:
            renderer.handle(event)
            renderer.write_debug(event)
        if event.kind == TEXT:
            parts.append(event.text)
        if event.kind == ERROR:
            status = 1
        elif event.kind == DONE:
            break
    return status, "".join(parts)


def save_conversation(provider_name, conversation, model):
    state.save(provider_name, conversation.conversation_id, model,
               conversation.history, conversation.resume)


def _open(instance, match, model, args, resume):
    return instance.open_conversation(
        match, model, system=args.system, debug=args.debug, timeout=args.timeout,
        conversation_id=(resume or {}).get("conversation_id"),
        history=(resume or {}).get("history"),
        resume=(resume or {}).get("resume"))


def run_once(conversation, prompt, args, provider_name, model):
    renderer = make_renderer(args)
    try:
        status, answer = run_turn(conversation, prompt, args, renderer)
    except KeyboardInterrupt:
        if not args.json:
            renderer.finish()
        return 130
    if not args.json:
        if renderer.finish() and not status:
            status = 1
    if args.copy:
        copy_answer(answer or renderer.text)
    if status == 0:
        save_conversation(provider_name, conversation, model)
    conversation.close()
    return status


def run_repl(conversation, instance, match, model, args, provider_name):
    """Prompt in a loop, one turn at a time, keeping the conversation open."""
    where = f"ai.{provider_name}: {model}"
    if conversation.history:
        where += f" (continuing, {len(conversation.history)} message(s) in context)"
    print(f"{where}", file=sys.stderr)
    print(":new starts a new chat, :q (or Ctrl-D) quits.", file=sys.stderr)

    first = read_prompt(args.prompt)
    status = 0
    while True:
        if first:
            text = first
            first = ""
        else:
            try:
                line = input(f"{provider_name}> ")
            except EOFError:
                print(file=sys.stderr)
                break
            except KeyboardInterrupt:
                print(file=sys.stderr)
                continue
            text = line.strip()
            if not text:
                continue
        if text in _QUIT_WORDS:
            break
        if text in _NEW_WORDS:
            conversation.close()
            conversation = _open(instance, match, model, args, None)
            continue

        renderer = make_renderer(args)
        try:
            turn_status, answer = run_turn(conversation, text, args, renderer)
        except KeyboardInterrupt:
            if not args.json:
                renderer.finish()
            print(file=sys.stderr)
            status = status or 130
            continue
        if not args.json:
            renderer.finish()
        if args.copy:
            copy_answer(answer)
        if turn_status == 0:
            save_conversation(provider_name, conversation, model)
        else:
            status = status or turn_status
    conversation.close()
    return status


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

    if args.interactive:
        if args.json:
            print(f"ai.{provider_name}: -i cannot be combined with --json",
                  file=sys.stderr)
            return 2
        if not sys.stdin.isatty():
            print(f"ai.{provider_name}: -i needs a terminal on stdin",
                  file=sys.stderr)
            return 2

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

    for path in args.file:
        full = os.path.expanduser(path)
        if not os.path.exists(full):
            print(f"ai.{provider_name}: {path}: no such file", file=sys.stderr)
            return 1
        if not os.path.isfile(full):
            print(f"ai.{provider_name}: {path}: not a regular file",
                  file=sys.stderr)
            return 1
        if not os.access(full, os.R_OK):
            print(f"ai.{provider_name}: {path}: permission denied",
                  file=sys.stderr)
            return 1

    if args.new:
        state.clear(provider_name)
        resume = None
    elif args.continue_:
        resume = state.load(provider_name)
        if not resume:
            print(f"ai.{provider_name}: no saved conversation to continue; "
                  f"starting a new one", file=sys.stderr)
    else:
        resume = None

    model = provider.chosen_model(args.model or (resume or {}).get("model"))
    instance = provider()
    conversation = _open(instance, match, model, args, resume)

    if args.interactive:
        return run_repl(conversation, instance, match, model, args, provider_name)

    prompt = read_prompt(args.prompt)
    if not prompt:
        build_parser(provider).print_usage(sys.stderr)
        return 2
    return run_once(conversation, prompt, args, provider_name, model)
