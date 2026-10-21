"""Streaming renderer for the normalised events.

Two behaviours, chosen automatically:

* **interactive TTY** — a ``rich`` live region renders the answer as Markdown
  while it streams, with the model's reasoning shown dimmed above it;
* **piped / no ``rich``** — the visible text streams to stdout untouched and the
  reasoning goes to stderr, so ``ai.grok … > answer.md`` stays clean.

``force_tty=True`` (``--tty``) treats both streams as terminals even though a
pipe is on the other end. That is what ``prompt``'s ``.ai.*`` sinks need: they
capture the output and forward the bytes to the real terminal, so the child
would otherwise never render Markdown or colour anything. ``width``/``height``
(``--width``/``--height``) pin the layout to that terminal, which the child
cannot measure itself.

Tool calls are announced (display only), and token usage is reported at the end.
"""

from __future__ import annotations

import os
import sys
import time

from . import events as ev

try:
    from rich.console import Console, Group
    from rich.live import Live
    from rich.markdown import Markdown
    from rich.text import Text
    _RICH = True
except Exception:  # pragma: no cover - environment dependent
    _RICH = False

_REFRESH = 0.08
_DIM = "\x1b[2m"
_RESET = "\x1b[0m"


class Renderer:
    def __init__(self, show_thinking=True, raw=False, color=True, debug=False,
                 force_tty=False, width=None, height=None):
        self.show_thinking = show_thinking
        self.raw = raw
        self.debug = debug
        self.out = sys.stdout
        self.err = sys.stderr
        self.force_tty = force_tty
        self.tty = force_tty or self.out.isatty()
        self.err_tty = force_tty or self.err.isatty()
        self.color = color and self.tty and not os.environ.get("NO_COLOR")
        self.interactive = self.tty and not raw
        self.text_parts = []
        self.thinking_parts = []
        self.tool_notes = []
        self.usage = None
        self.error = None
        self._last_refresh = 0.0
        self._live = None
        self._console = None
        self._rich = _RICH and self.interactive
        if self._rich:
            self._console = Console(file=self.out, highlight=False,
                                    force_terminal=True,
                                    color_system="auto" if self.color else None,
                                    width=width, height=height)

    # -- event intake ------------------------------------------------------

    def handle(self, event):
        if event.kind == ev.TEXT:
            self.text_parts.append(event.text)
            self._emit_text(event.text)
        elif event.kind == ev.THINKING:
            self.thinking_parts.append(event.text)
            self._emit_thinking(event.text)
        elif event.kind == ev.TOOL_CALL:
            self._emit_tool(event)
        elif event.kind == ev.USAGE:
            self.usage = event.data
        elif event.kind == ev.ERROR:
            self.error = event.text
            self._write_err((_DIM + "error: " + _RESET if self.color else "error: ")
                            + event.text + "\n")
        # DONE / RAW handled by finish / debug

    @property
    def text(self):
        return "".join(self.text_parts)

    @property
    def thinking(self):
        return "".join(self.thinking_parts)

    # -- sinks -------------------------------------------------------------

    def _emit_text(self, chunk):
        if self._rich:
            self._refresh(force=False)
        else:
            self.out.write(chunk)
            self.out.flush()

    def _emit_thinking(self, chunk):
        if self._rich:
            self._refresh(force=False)
            return
        if not self.show_thinking:
            return
        style = self.color and self.err_tty
        prefix = "💭 " if (style and not self.thinking_parts[:-1]) else ""
        if style:
            self._write_err(_DIM + prefix + chunk + _RESET)
        else:
            if self.show_thinking:
                self._write_err(prefix + chunk)

    def _emit_tool(self, event):
        if event.status and event.status != "start" and event.args == "":
            return
        label = event.name or "tool"
        note = f"⚙ {label} ({event.status or 'running'})"
        self.tool_notes.append(note)
        if self._rich:
            self._refresh(force=True)
        else:
            style = self.color and self.err_tty
            self._write_err((_DIM + note + _RESET + "\n") if style else (note + "\n"))

    def _write_err(self, text):
        try:
            self.err.write(text)
            self.err.flush()
        except (OSError, ValueError):
            pass

    # -- rich live region --------------------------------------------------

    def _refresh(self, force):
        if not self._rich:
            return
        now = time.time()
        if not force and now - self._last_refresh < _REFRESH:
            return
        self._last_refresh = now
        renderable = self._renderable()
        if self._live is None:
            self._live = Live(renderable, console=self._console, refresh_per_second=15,
                              transient=True, vertical_overflow="visible")
            self._live.start()
        else:
            self._live.update(renderable)

    def _renderable(self):
        blocks = []
        if self.show_thinking and self.thinking_parts:
            blocks.append(Text(self.thinking, style="dim"))
        for note in self.tool_notes:
            blocks.append(Text(note, style="dim italic"))
        answer = self.text
        if answer:
            blocks.append(Markdown(answer))
        return Group(*blocks) if blocks else Text("")

    # -- completion --------------------------------------------------------

    def finish(self):
        if self._live is not None:
            try:
                self._live.stop()
            except Exception:
                pass
            self._live = None
            if self.show_thinking and self.thinking_parts:
                self._console.print(Text(self.thinking, style="dim"))
            if self.text_parts:
                self._console.print(Markdown(self.text))
        elif self.text_parts and not self.text.endswith("\n"):
            # Piped/raw output is the answer byte-for-byte; terminate the line
            # the way every other command does.
            self.out.write("\n")
            self.out.flush()
        if (not self.interactive and self.show_thinking and self.thinking_parts
                and not self.thinking.endswith("\n")):
            self._write_err("\n")
        if self.usage:
            self._report_usage()
        return self.error

    def _report_usage(self):
        parts = ", ".join(f"{k}={v}" for k, v in self.usage.items() if v is not None)
        if parts:
            self._write_err((_DIM + f"[usage] {parts}" + _RESET + "\n")
                            if (self.color and self.err_tty) else f"[usage] {parts}\n")

    def write_debug(self, event):
        if self.debug and event.kind == ev.RAW and event.text:
            self._write_err(f"[raw] {event.text}\n")
