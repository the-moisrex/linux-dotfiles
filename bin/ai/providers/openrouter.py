"""OpenRouter adapter — openrouter.ai.

OpenRouter is an OpenAI-compatible gateway to many models behind one endpoint,
so this is the shared API engine (:mod:`ai.providers.openai_compat`) with a
different configuration: ``https://openrouter.ai/api/v1`` is the default root,
``OPENROUTER_API_KEY`` authenticates it (see :mod:`ai.keys` for the full lookup
order, including the installed opencode's credentials), and the model comes from
``-m/--model``, ``OPENROUTER_MODEL`` or, for convenience, ``OPENAI_MODEL``,
falling back to ``openrouter/auto-code``.

``ai.openrouter --list`` shows what OpenRouter currently offers (with context
length and pricing), so a different model is one flag away.
"""

from __future__ import annotations

import os

from .openai_compat import ApiError, OpenAICompat


class OpenRouter(OpenAICompat):
    name = "openrouter"
    label = "OpenRouter"
    origin = "https://openrouter.ai"
    base_url_env = "OPENROUTER_BASE_URL"
    key_env = "OPENROUTER_API_KEY"
    model_env = "OPENROUTER_MODEL"
    default_base_url = "https://openrouter.ai/api/v1"
    default_model = "openrouter/auto-code"

    @classmethod
    def chosen_model(cls, requested):
        # The environment must win over the class default, so resolve it here
        # rather than letting the base `chosen_model` return the default first.
        return (requested or os.environ.get(cls.model_env)
                or os.environ.get("OPENAI_MODEL")
                or cls.default_model or "").strip()

    def resolve_model(self, requested=None, base=None):
        wanted = self.chosen_model(requested)
        if wanted:
            return wanted
        raise ApiError("choose a model with -m/--model, OPENROUTER_MODEL=… or "
                       "OPENAI_MODEL=… (`ai.openrouter --list` shows what "
                       "OpenRouter offers)")
