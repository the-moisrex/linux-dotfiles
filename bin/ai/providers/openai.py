"""OpenAI adapter — api.openai.com, or any hosted OpenAI-compatible endpoint.

The official API is stateless and keyed, so this is the shared API engine
(:mod:`ai.providers.openai_compat`) with different configuration:
``OPENAI_BASE_URL`` points it at OpenRouter, Groq, Together, vLLM or a remote
llama.cpp instead of api.openai.com, and ``OPENAI_API_KEY`` authenticates it
(required for the official host, optional for a local one).

There is deliberately no default model — the catalogue changes too often to
guess — so ``-m``/``--model`` or ``OPENAI_MODEL`` is required and
``ai.openai --list`` shows what the endpoint offers.
"""

from __future__ import annotations

import os

from .openai_compat import ApiError, OpenAICompat


class OpenAI(OpenAICompat):
    name = "openai"
    label = "OpenAI"
    origin = "https://api.openai.com"
    base_url_env = "OPENAI_BASE_URL"
    key_env = "OPENAI_API_KEY"
    model_env = "OPENAI_MODEL"
    default_base_url = "https://api.openai.com/v1"

    def resolve_model(self, requested=None, base=None):
        wanted = (requested or os.environ.get(self.model_env) or "").strip()
        if wanted:
            return wanted
        raise ApiError("choose a model with -m/--model or OPENAI_MODEL=… "
                       "(`ai.openai --list` shows what the endpoint offers)")

    def headers(self, key=None):
        headers = super().headers(key)
        if not self.api_key(key) and "api.openai.com" in self.base_url():
            raise ApiError("no API key: set OPENAI_API_KEY or pass --api-key")
        return headers
