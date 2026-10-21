"""Topic classification for chats.

Rules are ordered and declarative.  Every matching category rule contributes a
tag (so a chat can be *linked* to several topics), while the highest-priority
match chooses the directory.  Manual categories and GapGPT projects beat the
rules, which keeps a hand-picked layout stable.
"""

from __future__ import annotations

import re

DEFAULT_CATEGORY = "inbox"


def _defaults():
    return {
        "default_category": DEFAULT_CATEGORY,
        "recent": "month",
        "links": True,
        "projects": {},
        "rules": [
            {
                "category": "tse",
                "priority": 100,
                "where": "any",
                "patterns": ["\\btse\\b", "tsetmc", "codal", "فولاد",
                             "بورس", "شاخص کل", "tedpix"],
            },
            {
                "category": "وب++",
                "priority": 90,
                "where": "any",
                "patterns": ["webpp", "web\\+\\+", "وب\\+\\+"],
            },
            {
                "category": "کدنویسی",
                "priority": 40,
                "where": "content",
                "min_code_blocks": 3,
                "patterns": [],
            },
        ],
        "tag_rules": [
            {"tags": ["cpp"], "where": "content",
             "patterns": ["\\bstd::", "```cpp", "#include <"]},
            {"tags": ["python"], "where": "content",
             "patterns": ["```python", "\\bdef \\w+\\("]},
            {"tags": ["bash"], "where": "content",
             "patterns": ["```bash", "#!/usr/bin/env bash"]},
            {"tags": ["diff"], "where": "content",
             "patterns": ["diff --git", "^@@ "]},
            {"tags": ["tse"], "where": "any",
             "patterns": ["tsetmc", "فولاد", "بورس"]},
        ],
    }


def defaults():
    """A fresh configuration with the topic rules seeded from the old folders."""
    return _defaults()


def merge(base, override):
    """Merge a stored config over the defaults (one level deep)."""
    result = dict(base)
    for key, value in (override or {}).items():
        result[key] = value
    return result


def code_blocks(text):
    """Count fenced code blocks (an odd trailing fence counts as one)."""
    return len(re.findall(r"^```", text or "", re.M)) // 2


def _bounded(text, limit=180):
    """Truncate *text* to at most *limit* UTF-8 bytes."""
    if len(text.encode("utf-8")) <= limit:
        return text
    return text.encode("utf-8")[:limit].decode("utf-8", "ignore").rstrip(" .")


def sanitize_category(name, default=DEFAULT_CATEGORY):
    """A single safe directory name (no separators, no dot segments)."""
    value = " ".join((name or "").split()).strip()
    value = value.replace("/", "-").replace("\\", "-")
    value = value.strip(". ")
    if not value or value in (".", ".."):
        return default
    return _bounded(value[:120]) or default


class Categorizer:
    """Apply the configured topic rules to one chat."""

    def __init__(self, config=None):
        self.config = config or defaults()
        self.default = sanitize_category(
            self.config.get("default_category") or DEFAULT_CATEGORY)
        self.rules = list(self.config.get("rules") or [])
        self.tag_rules = list(self.config.get("tag_rules") or [])
        self.projects = dict(self.config.get("projects") or {})

    def classify(self, chat, body_text="", category=None):
        """Return ``(category, auto_tags, source)``.

        *category* is a manual override; otherwise a project name wins over
        rules, and the default is ``inbox``.
        """
        title = chat.title or ""
        project = chat.project or ""
        auto = set()

        for rule in self.tag_rules:
            if self._matches(rule, chat, title, body_text):
                auto.update(str(tag) for tag in rule.get("tags") or [] if tag)

        if category:
            return sanitize_category(category), sorted(auto), "manual"

        if project:
            mapped = self.projects.get(project) or project
            auto.add(project)
            return sanitize_category(mapped), sorted(auto), "project"

        best = None
        best_key = None
        for order, rule in enumerate(self.rules):
            if not self._matches(rule, chat, title, body_text):
                continue
            name = sanitize_category(rule.get("category") or "", self.default)
            auto.add(name)
            priority = rule.get("priority") or 0
            key = (priority, -order)
            if best_key is None or key > best_key:
                best, best_key = name, key
        if best:
            return best, sorted(auto), "rule"
        return self.default, sorted(auto), "default"

    def _matches(self, rule, chat, title, body_text):
        provider = rule.get("provider")
        if provider and provider != chat.provider:
            return False
        wanted_project = rule.get("project")
        if wanted_project and wanted_project != (chat.project or ""):
            return False

        where = rule.get("where") or "any"
        if where == "title":
            haystack = title
        elif where == "content":
            haystack = body_text or ""
        elif where == "provider":
            haystack = chat.provider or ""
        elif where == "project":
            haystack = chat.project or ""
        else:
            haystack = "\n".join((title, chat.project or "", body_text or ""))

        minimum = rule.get("min_code_blocks")
        if minimum is not None and code_blocks(body_text) < int(minimum):
            return False

        patterns = rule.get("patterns") or []
        if not patterns:
            return minimum is not None
        for pattern in patterns:
            try:
                if re.search(pattern, haystack, re.I | re.M):
                    return True
            except re.error:
                continue
        return False
