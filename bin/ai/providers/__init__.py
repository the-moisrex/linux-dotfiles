"""Provider registry.

Every provider exposes the same ``stream(match, prompt, model, system, debug)``
generator of :class:`ai.events.StreamEvent`, so the CLI and renderer never need
to know which site is behind them.  The site-specific request shape, anti-bot
handling and wire parsing live in the individual modules.
"""

from __future__ import annotations


class Provider:
    name = ""
    label = ""
    origin = ""            # canonical site origin, e.g. https://grok.com
    session_target = ""    # what browser.session is asked for
    default_model = ""
    models = ()

    def stream(self, match, prompt, model, system, debug=False, timeout=600):
        """Yield StreamEvents for *prompt* using the signed-in *match*."""
        raise NotImplementedError

    @classmethod
    def chosen_model(cls, requested):
        return requested or cls.default_model


def all_providers():
    from .chatgpt import ChatGPT
    from .grok import Grok
    from .deepseek import DeepSeek
    return {p.name: p for p in (ChatGPT, Grok, DeepSeek)}


def get(name):
    providers = all_providers()
    if name not in providers:
        raise KeyError(name)
    return providers[name]
