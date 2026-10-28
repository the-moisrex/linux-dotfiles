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

A prompt too large for one message is uploaded as a text file by default (see
:mod:`ai.promptfile`); ``--prompt-file`` forces that path and
``--no-prompt-file`` disables it.

``--tty`` renders as if stdout/stderr were terminals even when they are pipes
(``prompt``'s ``.ai.*`` sinks forward the bytes to the real terminal), and the
hidden ``--context-file PATH`` writes the plain answer there while it streams, so
a sink can chain the text without the styled/JSON bytes.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys

from . import events as ev
from . import promptfile
from . import session as session_mod
from . import state
from .events import ERROR, DONE, TEXT
from .providers import all_providers
from .providers.openai_compat import ApiError
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


def open_context_file(path):
    """Open the plain-answer side channel (``--context-file``), if requested.

    ``prompt``'s sinks get styled/JSON bytes on stdout and need the bare answer
    for the next link of the chain and for the clipboard. A fresh file per turn
    also keeps a REPL's file down to the last answer.
    """
    if not path:
        return None
    try:
        return open(path, "w", encoding="utf-8", buffering=1)
    except OSError:
        return None


def build_parser(provider):
    api = not getattr(provider, "needs_session", True)
    discovers = getattr(provider, "discovers_model", False)
    if api:
        description = (f"Chat with {provider.label} from a prompt or interactively "
                       f"over its OpenAI-compatible API (no browser session).")
        if discovers:
            model_help = (f"model id (default: ${provider.model_env}, else the model "
                          f"the server has loaded; `--list` shows them)")
        else:
            model_help = (f"model id (default: ${provider.model_env}; required when "
                          f"that is unset, since there is no default)")
        file_help = ("attach a local file to the first message: text files inline, "
                     "images as data URLs (repeatable)")
        prompt_file_help = ("not used: this provider has no upload endpoint and "
                            "always sends the prompt inline")
    else:
        models = ", ".join(provider.models)
        description = (f"Chat with {provider.label} from a prompt or interactively "
                       f"(replays the signed-in web session; no API key).")
        model_help = f"model (default: {provider.default_model}; one of {models})"
        file_help = ("upload and attach a local file to the first message "
                     "(repeatable)")
        prompt_file_help = ("upload a prompt as a text file: automatic when it is "
                            "too large for one message (the default); "
                            "--no-prompt-file always sends it inline")
    p = argparse.ArgumentParser(
        prog=f"ai.{provider.name}", description=description)
    p.add_argument("prompt", nargs="*", help="prompt text (or pipe it on stdin)")
    p.add_argument("-f", "--file", action="append", default=[], metavar="PATH",
                   help=file_help)
    p.add_argument("--prompt-file", action=argparse.BooleanOptionalAction,
                   default=None, help=prompt_file_help)
    p.add_argument("--prompt-limit", type=int, default=None, metavar="BYTES",
                   help="override the provider's automatic prompt-file threshold "
                        f"(default: {promptfile.human_size(provider.inline_limit)})")
    p.add_argument("-m", "--model", default=None, help=model_help)
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
    p.add_argument("--tty", action="store_true",
                   help="render as if stdout/stderr were terminals, even through a pipe")
    p.add_argument("--copy", action="store_true", help="copy the answer to the clipboard")
    p.add_argument("--no-color", action="store_true", help="disable colours")
    p.add_argument("--debug", action="store_true", help="print raw stream lines to stderr")
    p.add_argument("--timeout", type=float, default=600, help="request timeout in seconds")
    if api:
        p.add_argument("--base-url", default=None, metavar="URL",
                       help="API root (default: "
                            + (provider.default_base_url or "discovered locally")
                            + f"; env {provider.base_url_env})")
        p.add_argument("--api-key", default=None, metavar="KEY",
                       help=f"API key (default: ${provider.key_env} or none)")
        p.add_argument("--param", action="append", default=[], metavar="KEY=VALUE",
                       help="extra request parameter, e.g. temperature=0.2 "
                            "(repeatable)")
    if not api:
        p.add_argument("--browser", action="append", default=[],
                       help="restrict the session to a browser id (repeatable)")
        p.add_argument("--profile", default=None,
                       help="restrict the session to a profile")
        p.add_argument("--family", choices=["chromium", "firefox"], default=None)
        p.add_argument("--open-only", action="store_true",
                       help="only use browsers/profiles that are currently running")
        p.add_argument("--exact", action="store_true",
                       help="match the session host exactly")
    p.add_argument("--print-session", action="store_true",
                   help="show which session would be used and exit")
    p.add_argument("--print-prompt", action="store_true",
                   help="print the prompt that would be sent and exit")
    p.add_argument("--stdin-only", action="store_true",
                   help=argparse.SUPPRESS)
    p.add_argument("--context-file", default=None, metavar="PATH",
                   help=argparse.SUPPRESS)
    p.add_argument("--width", type=int, default=None, metavar="COLS",
                   help=argparse.SUPPRESS)
    p.add_argument("--height", type=int, default=None, metavar="ROWS",
                   help=argparse.SUPPRESS)
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


def emit_json(event, fields=None):
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
    if fields:
        obj.update(fields)
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
    thinking = (args.thinking if args.thinking is not None
                else (sys.stderr.isatty() or args.tty))
    return Renderer(show_thinking=thinking, raw=args.raw,
                    color=not args.no_color, debug=args.debug,
                    force_tty=args.tty, width=args.width, height=args.height)


def _send_events(conversation, message, files):
    """The turn's events, with an API failure reported like any other error.

    The web providers report their own failures as ``error`` events; the API
    engine raises ``ApiError`` instead (the HTTP status, the endpoint, the model
    hint), so it is turned into the same event here and annotated by the caller.
    """
    try:
        yield from conversation.send(message, files=files or None)
    except ApiError as exc:
        yield ev.error(str(exc))


def run_turn(conversation, prompt, args, renderer, provider, fields=None):
    """Stream one turn; return ``(status, answer)``.

    *fields* is merged into every ``--json`` event, which is how ``ai.debate``
    tags events with their provider and round without changing the event model.
    """
    status = 0
    parts = []
    # --file attaches to the first message only; the site keeps it in context.
    files = list(args.file) if not conversation.turns else []
    mode = args.prompt_file
    # A provider without an upload endpoint cannot turn an oversized prompt into
    # a file, so it always sends the prompt inline.
    if not getattr(provider, "prompt_file_upload", True):
        mode = False
    auto_size = getattr(provider, "auto_attach_by_size", True)
    if mode is None and not args.prompt_limit and not auto_size:
        attach = False
    else:
        attach = promptfile.should_attach(prompt, provider, mode, args.prompt_limit)
    temp = None
    message = prompt
    if attach:
        count = promptfile.size(prompt)
        try:
            temp = promptfile.write(prompt)
        except OSError as exc:
            problem = ev.error(f"could not write the prompt file: {exc}")
            if args.json:
                emit_json(problem, fields)
            else:
                renderer.handle(problem)
            return 1, ""
        message = promptfile.reference(temp, count)
        files.append(temp)
        if not args.json:
            if mode is True:
                detail = f"{promptfile.human_size(count)}, forced by --prompt-file"
            else:
                budget = promptfile.limit_for(provider, args.prompt_limit)
                detail = (f"{promptfile.human_size(count)}, above the "
                          f"{promptfile.human_size(budget)} inline budget")
            print(f"[ai] prompt {detail}; uploading it as "
                  f"{os.path.basename(temp)}", file=sys.stderr)
    context = open_context_file(args.context_file)
    try:
        for event in _send_events(conversation, message, files):
            if event.kind == ERROR:
                event = ev.error(promptfile.annotate_error(
                    event.text, provider, prompt, used_file=attach,
                    disabled=(mode is False), limit=args.prompt_limit))
            if args.json:
                emit_json(event, fields)
            else:
                renderer.handle(event)
                renderer.write_debug(event)
            if event.kind == TEXT:
                parts.append(event.text)
                if context is not None:
                    context.write(event.text)
                    context.flush()
            if event.kind == ERROR:
                status = 1
            elif event.kind == DONE:
                break
    finally:
        if context is not None:
            context.close()
        promptfile.remove(temp)
    return status, "".join(parts)


def save_conversation(provider_name, conversation, model):
    state.save(provider_name, conversation.conversation_id, model,
               conversation.history, conversation.resume)


def api_options(args):
    """The endpoint configuration an API provider takes from the command line."""
    return {
        "base_url": getattr(args, "base_url", None),
        "api_key": getattr(args, "api_key", None),
        "params": list(getattr(args, "param", []) or []),
    }


def session_report(provider, match, options=None):
    """What ``--print-session`` shows: the browser session or the API endpoint."""
    if getattr(provider, "api", False):
        return provider.session_descriptor(options)
    return _session_summary(match)


def _open(instance, match, model, args, resume):
    kwargs = dict(system=args.system, debug=args.debug, timeout=args.timeout,
                  conversation_id=(resume or {}).get("conversation_id"),
                  history=(resume or {}).get("history"),
                  resume=(resume or {}).get("resume"))
    if getattr(instance, "api", False):
        kwargs["options"] = api_options(args)
    return instance.open_conversation(match, model, **kwargs)


def run_once(conversation, prompt, args, provider_name, model, provider):
    renderer = make_renderer(args)
    try:
        status, answer = run_turn(conversation, prompt, args, renderer, provider)
    except KeyboardInterrupt:
        if not args.json:
            renderer.finish()
        return 130
    except Exception as exc:
        if not args.json:
            renderer.finish()
        if getattr(args, "debug", False):
            import traceback
            traceback.print_exc()
        else:
            print("ai." + provider_name + ": " + type(exc).__name__ + ": "
                  + str(exc) + " (re-run with --debug for a traceback)",
                  file=sys.stderr)
        return 1
    if not args.json:
        if renderer.finish() and not status:
            status = 1
    if args.copy:
        copy_answer(answer or renderer.text)
    if status == 0:
        save_conversation(provider_name, conversation, model)
    conversation.close()
    return status


def run_repl(conversation, instance, match, model, args, provider_name, provider):
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
            try:
                fresh = _open(instance, match, model, args, None)
            except ApiError as exc:
                print(f"ai.{provider_name}: {exc}", file=sys.stderr)
                continue
            conversation.close()
            conversation = fresh
            continue

        renderer = make_renderer(args)
        try:
            turn_status, answer = run_turn(conversation, text, args, renderer,
                                           provider)
        except KeyboardInterrupt:
            if not args.json:
                renderer.finish()
            print(file=sys.stderr)
            status = status or 130
            continue
        except Exception as exc:
            if not args.json:
                renderer.finish()
            if getattr(args, "debug", False):
                import traceback
                traceback.print_exc()
            else:
                print("ai." + provider_name + ": " + type(exc).__name__ + ": "
                      + str(exc) + " (re-run with --debug for a traceback)",
                      file=sys.stderr)
            status = status or 1
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
        for name, prov_cls in providers.items():
            prov = prov_cls()
            if getattr(prov, "api", False):
                models = ", ".join(prov.available_models())
            else:
                models = ", ".join(prov.models)
            print(f"{name:<10} {prov.label:<10} {models}")
        return 0

    if args.prompt_file and not getattr(provider, "prompt_file_upload", True):
        print(f"ai.{provider_name}: --prompt-file is not supported: "
              f"{provider.label} has no upload endpoint, so the prompt is always "
              f"sent inline; raise the model's context length instead",
              file=sys.stderr)
        return 2

    if args.stdin_only and args.prompt:
        print(f"ai.{provider_name}: positional prompt text is not supported in a "
              f"chain; put instructions in `.note \"...\"` before the sink or "
              f"use --system", file=sys.stderr)
        return 2

    if args.print_prompt:
        prompt = read_prompt(args.prompt)
        if not prompt:
            build_parser(provider).print_usage(sys.stderr)
            return 2
        print(prompt)
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

    if getattr(provider, "needs_session", True):
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
    else:
        # An API provider has no browser session; the endpoint is the transport.
        match = {}

    if args.print_session:
        print(json.dumps(session_report(provider(), match, api_options(args)),
                         indent=2))
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
    try:
        conversation = _open(instance, match, model, args, resume)
    except ApiError as exc:
        print(f"ai.{provider_name}: {exc}", file=sys.stderr)
        return 1
    # An API provider resolves the model against the server, so the open
    # conversation is the authority on what is actually being asked.
    model = conversation.model

    if args.interactive:
        return run_repl(conversation, instance, match, model, args, provider_name,
                        provider)

    prompt = read_prompt(args.prompt)
    if not prompt:
        build_parser(provider).print_usage(sys.stderr)
        return 2
    return run_once(conversation, prompt, args, provider_name, model, provider)
