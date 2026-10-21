"""ai.save — a local, git-backed store for AI conversations.

Every conversation is one Markdown file with JSON-encoded frontmatter:

    ---
    provider: "chatgpt"
    chat: "6ad8..."
    title: "..."
    category: "tse"
    ...
    ---

The physical tree is ``topics/<topic>/<YYYY-MM>/<date>_<title>.md`` and
``months/<YYYY-MM>/<topic>`` is a generated directory of relative symlinks, so
the same chats can be browsed by topic or by month.  There is no database:
``last`` and ``list`` scan frontmatter, and the later search tool greps the
Markdown itself.

The package is also the storage API for ``ai.*`` and prompt scripts: call
:class:`ai.save.store.Store` directly, or shell out to ``ai.save add --json``.
"""

from __future__ import annotations

__version__ = "0.1.0"
