"""Ask two or more AIs the same question, then let them verify each other.

``ai.debate`` opens one conversation per provider (each site's signed-in web
session, no API key), asks the first participant the question, then feeds its
answer to the next one as ``Another AI said: …`` together with a verification
request, and keeps passing the newest answer around the ring.  Every provider
keeps its own conversation, so later turns only carry the other AI's latest
answer — the same thing you do by hand with two chat tabs.

The final answer (or, with ``--context all``, the whole labelled transcript) is
what ``--context-file`` receives; ``prompt``'s ``.ai.debate`` sink reads it back
as the chain context and the clipboard, exactly like the other ``.ai.*`` sinks.
"""

from __future__ import annotations

import argparse
import copy
import json
import os
import re
import signal
import sys
from dataclasses import dataclass, field

from . import session as session_mod
from . import state
from .cli import (
    _open,
    _session_summary,
    copy_answer,
    make_renderer,
    open_context_file,
    read_prompt,
    run_turn,
)
from .providers import all_providers

#: The default request appended to every verification turn.
DEFAULT_INSTRUCTION = (
    "Verify the other AI's answer against the question above. Point out "
    "anything wrong, unsupported or missing, and say where you agree. Then "
    "give your own corrected answer; if the other answer is already right, "
    "say so and keep it."
)

#: Added to the instruction by ``--until-agree`` so the relay can stop early.
VERDICT_SUFFIX = (
    "End your answer with one line: `VERDICT: AGREE` if you agree with the "
    "other answer's conclusion, or `VERDICT: DISAGREE` if you still disagree."
)

#: Command spellings that are not the provider registry keys.
ALIASES = {"dseek": "deepseek", "gapgpt": "gap"}

_VERDICT_RE = re.compile(r"(?im)^\s*VERDICT:\s*(AGREE|DISAGREE)\b")


class UsageError(Exception):
    """Bad command line; ``main`` prints it with the usage and exits 2."""


class DebateError(Exception):
    """A usable debate could not be started (missing session, bad file, …)."""


@dataclass
class Turn:
    round: int
    provider: str
    answer: str


@dataclass
class PanelResult:
    status: int
    transcript: list = field(default_factory=list)
    stop_reason: str = "rounds"  # rounds | agreement | failed
    failed_provider: str = ""
    rounds_run: int = 0


def parse_verdict(answer):
    """The last ``VERDICT: AGREE|DISAGREE`` line of an answer, or ``None``."""
    matches = _VERDICT_RE.findall(answer)
    return matches[-1].upper() if matches else None


def build_prompt(question, previous, first, instruction):
    """One turn's prompt.

    The first turn is the question verbatim (there is no other answer to verify
    yet). A participant's first turn (when it is not the very first turn) also
    gets the question, because its conversation has not seen it yet. Later turns
    carry only the newest answer.
    """
    if previous is None:
        return question.strip()
    parts = []
    if first:
        parts.append(question.strip())
    parts.append("Another AI said:\n\n" + previous[1].strip())
    if instruction:
        parts.append(instruction.strip())
    return "\n\n".join(parts)


def run_panel(participants, question, *, rounds, until_agree, instruction,
              writer, on_turn=None):
    """Run the relay and return a :class:`PanelResult`.

    *participants* only need ``name`` and an ``ask(prompt, fields)`` returning
    ``(status, answer)``, which keeps the loop testable without the network.
    *writer* receives each completed answer through ``writer.add(name, round,
    answer)``.
    """
    result = PanelResult(status=0)
    started = set()
    verdicts = {}
    previous = None
    total = rounds * len(participants)
    turn_no = 0

    for round_no in range(1, rounds + 1):
        result.rounds_run = round_no
        for participant in participants:
            first = participant.name not in started
            prompt = build_prompt(question, previous, first, instruction)
            turn_no += 1
            if on_turn is not None:
                on_turn(turn_no, total, round_no, participant, previous)
            fields = {"turn": turn_no, "round": round_no,
                      "provider": participant.name}
            if previous is not None:
                fields["reply_to"] = previous[0]
            status, answer = participant.ask(prompt, fields)
            if status != 0:
                result.status = status
                result.stop_reason = "failed"
                result.failed_provider = participant.name
                return result
            answer = answer.strip()
            if not answer:
                result.status = 1
                result.stop_reason = "failed"
                result.failed_provider = participant.name
                return result
            started.add(participant.name)
            previous = (participant.name, answer)
            result.transcript.append(Turn(round_no, participant.name, answer))
            writer.add(participant.name, round_no, answer)
            if until_agree:
                verdicts[participant.name] = parse_verdict(answer)
        if until_agree and all(verdicts.get(p.name) == "AGREE"
                               for p in participants):
            result.stop_reason = "agreement"
            break
    return result


class ContextWriter:
    """The plain side channel (``--context-file``) and the clipboard text."""

    def __init__(self, path, mode, question=""):
        self.mode = "all" if mode == "all" else "last"
        self.question = question.strip()
        self.turns = []
        self.handle = open_context_file(path)

    def add(self, provider, round_no, answer):
        self.turns.append((provider, round_no, answer.strip()))
        self._flush()

    @property
    def text(self):
        if self.mode == "last":
            return self.turns[-1][2] if self.turns else ""
        blocks = []
        if self.question:
            blocks.append("## Question\n\n" + self.question)
        for provider, round_no, answer in self.turns:
            blocks.append(f"## {provider} · round {round_no}\n\n{answer}")
        return "\n\n".join(blocks)

    def _flush(self):
        if self.handle is None:
            return
        text = self.text
        self.handle.seek(0)
        self.handle.truncate()
        self.handle.write(text)
        if text and not text.endswith("\n"):
            self.handle.write("\n")
        self.handle.flush()

    def close(self):
        if self.handle is not None:
            self.handle.close()
            self.handle = None


class Participant:
    """One AI in the panel: its provider, model and open conversation."""

    def __init__(self, name, provider, model, conversation, args, saver=None):
        self.name = name
        self.provider = provider
        self.model = model
        self.conversation = conversation
        # The debate owns --context-file: run_turn must not rewrite it per turn.
        self.args = args
        # Called after every successful turn to persist the panel's chats.
        self.saver = saver

    def record(self):
        """The saved-state entry for this participant (see ``ai.state``)."""
        return {
            "conversation_id": self.conversation.conversation_id,
            "model": self.model,
            "history": self.conversation.history,
            "resume": self.conversation.resume,
        }

    def ask(self, prompt, fields):
        renderer = make_renderer(self.args)
        try:
            status, answer = run_turn(self.conversation, prompt, self.args,
                                      renderer, self.provider, fields)
        except KeyboardInterrupt:
            if not self.args.json:
                renderer.finish()
            raise
        if not self.args.json and renderer.finish() and not status:
            status = 1
        if status == 0 and answer.strip() and self.saver is not None:
            self.saver()
        return status, answer

    def close(self):
        try:
            self.conversation.close()
        except Exception:  # noqa: BLE001 - closing must never mask a result
            pass


def build_parser(providers):
    names = ", ".join(providers)
    p = argparse.ArgumentParser(
        prog="ai.debate",
        allow_abbrev=False,
        description="Ask two or more AIs the same question, then let each "
                    "verify the other's answer, round after round (replays the "
                    "signed-in web sessions; no API key).",
        epilog=f"providers: {names} (dseek is accepted for deepseek)")
    p.add_argument("words", nargs="*", metavar="PROVIDER|QUESTION",
                   help="providers first (two or more), then the question; "
                        "with no question the prompt is read from stdin")
    p.add_argument("--rounds", type=int, default=2, metavar="N",
                   help="full passes through the providers (default: 2)")
    p.add_argument("--until-agree", action="store_true",
                   help="stop after a pass in which every AI's VERDICT is AGREE")
    p.add_argument("--instruction", default=None, metavar="TEXT",
                   help="verification request appended to each turn "
                        "(default: built in)")
    p.add_argument("--no-instruction", action="store_true",
                   help="relay the other answer with no verification request")
    p.add_argument("--context", choices=("last", "all"), default="last",
                   help="what --context-file and --copy receive: the last "
                        "answer (default) or the labelled transcript")
    p.add_argument("--transcript", action="store_true",
                   help="same as --context all")
    p.add_argument("-c", "--continue", dest="continue_", action="store_true",
                   help="continue each provider's chat from the last debate "
                        "between these providers")
    p.add_argument("--new", action="store_true",
                   help="start fresh chats and forget the saved debate")
    p.add_argument("-s", "--system", default=None,
                   help="system / custom instructions for every provider")
    p.add_argument("-m", "--model", action="append", default=[],
                   metavar="PROVIDER=MODEL",
                   help="pick one provider's model (repeatable), e.g. "
                        "-m grok=expert -m deepseek=deepseek-reasoner")
    p.add_argument("-f", "--file", action="append", default=[], metavar="PATH",
                   help="upload and attach a local file to each provider's "
                        "first turn (repeatable)")
    p.add_argument("--prompt-file", action=argparse.BooleanOptionalAction,
                   default=None,
                   help="upload a prompt as a text file: automatic when it is "
                        "too large for one message (the default); "
                        "--no-prompt-file always sends it inline")
    p.add_argument("--prompt-limit", type=int, default=None, metavar="BYTES",
                   help="override each provider's automatic prompt-file threshold")
    p.add_argument("--show-thinking", dest="thinking", action="store_true",
                   default=None,
                   help="show the models' reasoning (default on a TTY)")
    p.add_argument("--no-thinking", dest="thinking", action="store_false",
                   help="hide the models' reasoning")
    p.add_argument("--json", action="store_true",
                   help="emit one JSON event per line, tagged with provider/round")
    p.add_argument("--raw", action="store_true", help="print only the answer text")
    p.add_argument("--tty", action="store_true",
                   help="render as if stdout/stderr were terminals, even "
                        "through a pipe")
    p.add_argument("--copy", action="store_true",
                   help="copy the chosen context to the clipboard")
    p.add_argument("--no-color", action="store_true", help="disable colours")
    p.add_argument("--debug", action="store_true",
                   help="print raw stream lines to stderr")
    p.add_argument("--timeout", type=float, default=600,
                   help="per-request timeout in seconds")
    p.add_argument("--browser", action="append", default=[],
                   help="restrict the sessions to a browser id (repeatable)")
    p.add_argument("--profile", default=None,
                   help="restrict the sessions to a profile")
    p.add_argument("--family", choices=["chromium", "firefox"], default=None)
    p.add_argument("--open-only", action="store_true",
                   help="only use browsers/profiles that are currently running")
    p.add_argument("--exact", action="store_true",
                   help="match the session hosts exactly")
    p.add_argument("--print-session", action="store_true",
                   help="show the session each provider would use and exit")
    p.add_argument("--print-prompt", action="store_true",
                   help="print the first turn and the verification template and exit")
    p.add_argument("--list", action="store_true",
                   help="list the available providers and exit")
    p.add_argument("--stdin-only", action="store_true", help=argparse.SUPPRESS)
    p.add_argument("--context-file", default=None, metavar="PATH",
                   help=argparse.SUPPRESS)
    p.add_argument("--width", type=int, default=None, metavar="COLS",
                   help=argparse.SUPPRESS)
    p.add_argument("--height", type=int, default=None, metavar="ROWS",
                   help=argparse.SUPPRESS)
    return p


def split_providers(words, providers):
    """Split leading provider names from the question words."""
    aliases = dict(ALIASES)
    aliases.update({name: name for name in providers})
    names = []
    index = 0
    while index < len(words) and words[index] in aliases:
        names.append(aliases[words[index]])
        index += 1
    return names, words[index:]


def parse_models(specs, names):
    models = {}
    for spec in specs:
        if "=" not in spec:
            raise UsageError(f"--model expects PROVIDER=MODEL, got {spec!r}")
        name, model = (part.strip() for part in spec.split("=", 1))
        name = ALIASES.get(name, name)
        if name not in names:
            raise UsageError(f"--model names {name!r}, which is not one of "
                             f"this debate's providers ({', '.join(names)})")
        if not model:
            raise UsageError(f"--model {name}= needs a model name")
        models[name] = model
    return models


def instruction_for(args):
    if args.no_instruction:
        if args.until_agree:
            raise UsageError("--until-agree needs the verification instruction")
        return ""
    if args.instruction is not None:
        text = args.instruction.strip()
    else:
        text = DEFAULT_INSTRUCTION
    if args.until_agree:
        text = f"{text}\n\n{VERDICT_SUFFIX}"
    return text


def print_plan(names, question, instruction):
    parts = [f"# turn 1 → {names[0]}", question.strip(),
             "# later turns", "Another AI said:", "<answer>"]
    if instruction:
        parts.append(instruction.strip())
    print("\n\n".join(parts))


def load_sessions(names, providers, args):
    matches = {}
    for name in names:
        provider = providers[name]
        try:
            match = session_mod.load(
                provider.session_target, browser=args.browser or None,
                profile=args.profile, family=args.family,
                open_only=args.open_only, exact=args.exact)
        except session_mod.SessionError as exc:
            raise DebateError(str(exc)) from exc
        if not match:
            raise DebateError(
                f"no signed-in session found for {provider.session_target}; "
                f"open {provider.origin} in a browser once")
        matches[name] = match
    return matches


def check_files(paths):
    for path in paths:
        full = os.path.expanduser(path)
        if not os.path.exists(full):
            raise DebateError(f"{path}: no such file")
        if not os.path.isfile(full):
            raise DebateError(f"{path}: not a regular file")
        if not os.access(full, os.R_OK):
            raise DebateError(f"{path}: permission denied")


def saved_debate(names, args):
    """The panel's saved conversations for ``-c``/``--new`` (or ``None``)."""
    if args.new:
        state.clear_debate(names)
        return None
    if not args.continue_:
        return None
    saved = state.load_debate(names)
    if not saved:
        print("[ai.debate] no saved debate to continue; starting a new one",
              file=sys.stderr)
        return None
    continuing = [name for name in names if name in saved]
    print(f"[ai.debate] continuing {', '.join(continuing)}", file=sys.stderr)
    missing = [name for name in names if name not in saved]
    if missing:
        print(f"[ai.debate] no saved chat for {', '.join(missing)}; "
              f"starting a new one", file=sys.stderr)
    return saved


def open_participants(names, providers, matches, models, args, saved=None):
    participants = []

    def save_all():
        state.save_debate(names, {p.name: p.record() for p in participants})

    try:
        for name in names:
            provider = providers[name]
            resume = (saved or {}).get(name)
            model = provider.chosen_model(
                models.get(name) or (resume or {}).get("model"))
            instance = provider()
            conversation = _open(instance, matches[name], model, args, resume)
            participants.append(
                Participant(name, provider, model, conversation, args, save_all))
    except BaseException:
        for participant in participants:
            participant.close()
        raise
    return participants


def header_printer(args):
    def on_turn(turn_no, total, round_no, participant, previous):
        if turn_no > 1 and not args.json:
            print(file=sys.stdout)
        arrow = f" ← {previous[0]}" if previous else ""
        print(f"[ai.debate] turn {turn_no}/{total} · {participant.name} · "
              f"round {round_no}/{args.rounds}{arrow}", file=sys.stderr)
    return on_turn


def main(argv=None):
    # Die quietly on a closed downstream pipe (``ai.debate … | head``).
    try:
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (AttributeError, ValueError):
        pass

    providers = all_providers()
    parser = build_parser(providers)
    # Positional provider names/questions may come after options (`grok dseek
    # --rounds 3 "…"`), which plain parse_args rejects for a `nargs="*"` list.
    args = parser.parse_intermixed_args(argv)

    if args.list:
        for name, provider in providers.items():
            print(f"{name:<10} {provider.label:<10} {', '.join(provider.models)}")
        return 0

    try:
        names, words = split_providers(args.words, providers)
        if len(names) < 2:
            raise UsageError(
                "name at least two providers, e.g. `ai.debate grok dseek` "
                f"(one of: {', '.join(providers)})")
        if len(set(names)) != len(names):
            raise UsageError("each provider may appear only once")
        if args.rounds < 1:
            raise UsageError("--rounds must be at least 1")
        question = " ".join(words).strip()
        if args.stdin_only and question:
            raise UsageError(
                "positional prompt text is not supported in a chain; put the "
                "question in a `.note \"...\"` before the sink or pipe it on stdin")
        if not question and not sys.stdin.isatty():
            question = read_prompt([])
        if not question:
            raise UsageError("no question given; pass it after the providers "
                             "or pipe it on stdin")
        models = parse_models(args.model, names)
        instruction = instruction_for(args)
        if args.continue_ and args.new:
            raise UsageError("-c/--continue and --new are mutually exclusive")
    except UsageError as exc:
        print(f"ai.debate: {exc}", file=sys.stderr)
        parser.print_usage(sys.stderr)
        return 2

    if args.print_prompt:
        print_plan(names, question, instruction)
        return 0

    try:
        matches = load_sessions(names, providers, args)
    except DebateError as exc:
        print(f"ai.debate: {exc}", file=sys.stderr)
        return 1

    if args.print_session:
        print(json.dumps({name: _session_summary(matches[name]) for name in names},
                         indent=2))
        return 0

    try:
        check_files(args.file)
    except DebateError as exc:
        print(f"ai.debate: {exc}", file=sys.stderr)
        return 1

    saved = saved_debate(names, args)

    # The debate writes --context-file itself, once per completed turn.
    turn_args = copy.copy(args)
    turn_args.context_file = None

    participants = []
    writer = ContextWriter(args.context_file,
                           "all" if args.transcript else args.context, question)
    status = 0
    try:
        participants = open_participants(names, providers, matches, models,
                                         turn_args, saved)
        try:
            result = run_panel(
                participants, question, rounds=args.rounds,
                until_agree=args.until_agree, instruction=instruction,
                writer=writer, on_turn=header_printer(args))
            status = result.status
            if status == 0:
                if result.stop_reason == "agreement":
                    note = f"agreement after {len(result.transcript)} turns"
                elif args.until_agree:
                    note = f"no agreement after {len(result.transcript)} turns"
                else:
                    note = f"done after {len(result.transcript)} turns"
                print(f"[ai.debate] {note}", file=sys.stderr)
            else:
                print(f"[ai.debate] {result.failed_provider} failed; stopping",
                      file=sys.stderr)
        except KeyboardInterrupt:
            print("\n[ai.debate] interrupted; keeping the last complete answer",
                  file=sys.stderr)
            status = 130
    finally:
        for participant in participants:
            participant.close()
        if args.copy and writer.text:
            copy_answer(writer.text)
        writer.close()
    return status


if __name__ == "__main__":
    sys.exit(main())
