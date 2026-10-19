"""ai — replay the web chat sites (ChatGPT, Grok, DeepSeek) over their own APIs.

This package drives the *chat products* using the session already present in a
local browser (via ``bin/browser.session``) — no API keys, no browser at run
time.  Each provider normalises its wire format into the same stream events
(``thinking`` / ``text`` / ``tool_call`` / ``usage`` / ``done`` / ``error``) so
the renderer and ``--json`` output stay provider-agnostic.

Entry points are the ``ai.chatgpt`` / ``ai.grok`` / ``ai.dseek`` commands.
"""

__version__ = "0.1.0"
