"""Render a normalised chat as Markdown, and read the messages back.

The body is human-readable Markdown plus one invisible JSON comment per
message.  The comment carries the role/kind/timestamps so ``show --json`` and
``last`` can recover messages without a database, while a later snippet tool
can map a fenced block to the message it came from.
"""

from __future__ import annotations

import json
import re

from .. import history

MARKER = re.compile(r"<!-- ai\.save: (\{.*?\}) -->\n")
HEADINGS = {
    history.USER: "## User",
    history.ASSISTANT: "## Assistant",
    history.SYSTEM: "## System",
    history.TOOL: "## Tool",
}


def _marker(message, saved):
    payload = {
        "role": message.role,
        "kind": message.kind,
        "id": message.message_id,
        "parent": message.parent_id,
        "created": message.created,
        "saved": saved,
    }
    if message.name:
        payload["name"] = message.name
    return "<!-- ai.save: " + json.dumps(payload, ensure_ascii=False) + " -->\n"


def message_block(message, saved=""):
    """Render one message (marker + visible block)."""
    parts = [_marker(message, saved)]
    text = (message.text or "").rstrip()
    if message.kind == history.THINKING:
        parts.append("<details><summary>Reasoning</summary>\n")
        parts.append(text + "\n")
        parts.append("</details>\n")
    elif message.kind == history.TOOL_CALL:
        label = message.name or "tool"
        parts.append(f"> **Tool: {label}**\n")
        if text:
            quoted = "\n".join("> " + line if line else ">"
                               for line in text.splitlines())
            parts.append(quoted + "\n")
    elif message.kind == history.ATTACHMENT:
        parts.append(f"**Attachment:** `{text}`\n")
    else:
        parts.append(HEADINGS.get(message.role, "## " + message.role.title()) + "\n")
        if text:
            parts.append(text + "\n")
    return "\n".join(parts)


def body(chat, saved=""):
    """Render a whole chat (title + every message)."""
    title = chat.title or "Untitled"
    pieces = [f"# {title}\n"]
    for message in chat.messages:
        pieces.append(message_block(message, saved))
    return "\n".join(pieces).rstrip() + "\n"


def append_messages(body_text, messages, saved=""):
    """Append rendered messages to an existing body."""
    extra = "\n".join(message_block(message, saved) for message in messages)
    return body_text.rstrip() + "\n\n" + extra.rstrip() + "\n"


def _visible_text(kind, chunk):
    text = chunk.strip("\n")
    if kind == history.THINKING:
        start = text.find("<details>")
        end = text.rfind("</details>")
        if start >= 0 and end > start:
            text = text[start + len("<details>"):end]
        text = re.sub(r"^\s*<summary>.*?</summary>\s*", "", text,
                      flags=re.S)
    elif kind == history.TOOL_CALL:
        lines = text.splitlines()
        if lines and lines[0].startswith("> **Tool:"):
            lines = lines[1:]
        text = "\n".join(line[2:] if line.startswith("> ") else line
                         for line in lines)
    elif kind == history.ATTACHMENT:
        text = re.sub(r"^\*\*Attachment:\*\*\s*`?(.*?)`?\s*$", r"\1", text,
                      flags=re.S)
    elif text.startswith("## "):
        text = text.split("\n", 1)[1] if "\n" in text else ""
    return text.strip("\n")


def parse(body_text):
    """Recover the message list (marker metadata + visible text)."""
    parts = MARKER.split(body_text)
    messages = []
    for index in range(1, len(parts) - 1, 2):
        try:
            meta = json.loads(parts[index])
        except ValueError:
            continue
        if not isinstance(meta, dict):
            continue
        kind = meta.get("kind") or history.TEXT
        messages.append({
            "role": meta.get("role") or "",
            "kind": kind,
            "text": _visible_text(kind, parts[index + 1]),
            "id": meta.get("id") or "",
            "parent": meta.get("parent") or "",
            "created": meta.get("created") or "",
            "saved": meta.get("saved") or "",
            "name": meta.get("name") or "",
        })
    return messages
