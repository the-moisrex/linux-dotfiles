"""Provider registry.

Every provider exposes ``open_conversation(...)`` returning a
:class:`Conversation` whose ``send(prompt, files=None)`` generator yields
:class:`ai.events.StreamEvent` for one turn, so the CLI and renderer never need
to know which site is behind them.  A conversation can be continued later by
opening it again with a saved ``conversation_id``/``resume`` (see
:mod:`ai.state`); a provider that cannot reattach to an id replays the stored
transcript instead.  ``Provider.stream`` is the one-turn convenience wrapper.
"""

from __future__ import annotations


class Conversation:
    """One multi-turn conversation.

    Subclasses implement :meth:`send` and, when the site exposes them, fill in
    :attr:`conversation_id` (the site's chat id) and :attr:`resume` (an opaque
    dict needed to continue) so the CLI can persist and restore them.
    """

    def __init__(self, match, model, system="", debug=False, timeout=600,
                 conversation_id=None, history=None, resume=None):
        self.match = match
        self.model = model
        self.system = system or ""
        self.debug = debug
        self.timeout = timeout
        self.conversation_id = conversation_id or ""
        self.history = [dict(turn) for turn in (history or [])]
        self.resume = dict(resume or {})
        self.turns = 0

    def send(self, prompt, files=None):
        """Yield the events of one turn, updating the conversation state.

        *files* is an optional list of local paths (``--file``) the provider
        uploads and attaches to this message.
        """
        raise NotImplementedError

    def close(self):
        """Release any transport held open between turns."""

    def _record(self, role, text):
        if text:
            self.history.append({"role": role, "text": text})

    @staticmethod
    def _attachments(files):
        """Read the ``--file`` paths into attachments (raises ``FileError``)."""
        from .. import files as files_mod
        return files_mod.read_all(files)


class Provider:
    name = ""
    label = ""
    origin = ""            # canonical site origin, e.g. https://grok.com
    session_target = ""    # what browser.session is asked for
    default_model = ""
    models = ()
    #: Prompts larger than this many UTF-8 bytes are uploaded as a text file
    #: by default (see :mod:`ai.promptfile`).  The sites cap a single message
    #: by *tokens*, so the byte budget is conservative enough for CJK too.
    inline_limit = 64 * 1024

    def open_conversation(self, match, model, system="", debug=False,
                          timeout=600, conversation_id=None, history=None,
                          resume=None):
        """Return a :class:`Conversation` for *match* (new or resumed)."""
        raise NotImplementedError

    def stream(self, match, prompt, model, system, debug=False, timeout=600):
        """One-turn convenience wrapper over :meth:`open_conversation`."""
        conversation = self.open_conversation(
            match, model, system=system, debug=debug, timeout=timeout)
        try:
            yield from conversation.send(prompt)
        finally:
            conversation.close()

    @classmethod
    def chosen_model(cls, requested):
        return requested or cls.default_model


def all_providers():
    from .chatgpt import ChatGPT
    from .grok import Grok
    from .deepseek import DeepSeek
    from .gap import Gap
    from .kimi import Kimi
    return {p.name: p for p in (ChatGPT, Grok, DeepSeek, Gap, Kimi)}
