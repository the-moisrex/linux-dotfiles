"""One-shot import of the legacy ``ai-save`` store.

The old layout kept the API JSON in ``.ai/chat-meta`` + ``.ai/chat-content`` and
wrote Markdown into ``<category>/<date>_<title>.md`` with a human footer.  This
module rebuilds those chats as normal store files (topic/month tree, JSON
frontmatter, invisible message markers) and then removes the legacy cache.
"""

from __future__ import annotations

import json
import os
import re
import uuid
from datetime import datetime
from pathlib import Path

from .. import history
from . import rules

DATE_NAME = re.compile(r"^(\d{4}-\d{2}-\d{2})_(\d{2}-\d{2})")
FOOTER = re.compile(r"\n---\n\*\*Model:\*\*")
MODEL = re.compile(r"\*\*Model:\*\*\s*`([^`]*)`")
TOKEN = re.compile(r"\*\*Chat Token:\*\*\s*`([^`]*)`")
DETAILS = re.compile(r"<details><summary>Reasoning</summary>\s*(.*?)\s*</details>",
                     re.S)
SECTION = re.compile(r"^## (User|Assistant)\s*$", re.M)


def _projects(store):
    try:
        with open(store.ai / "projects.json", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return {}
    return {item.get("token"): item.get("name") or ""
            for item in data.get("results") or []
            if isinstance(item, dict) and item.get("token")}


def _legacy_chat(meta, content, projects):
    token = meta.get("token") or ""
    project = projects.get(meta.get("project_token")) or "Uncategorized"
    chat = history.Chat(
        provider="gap",
        remote_id=token,
        title=meta.get("title") or "Untitled",
        created=meta.get("created_at") or "",
        updated=(meta.get("last_message_created_at")
                 or meta.get("created_at") or ""),
        model=meta.get("model") or "",
        project=project if project != "Uncategorized" else "",
        url=f"https://gapgpt.app/chat/{token}",
    )
    for item in content.get("messages") or []:
        moment = item.get("created_at") or chat.created
        text = item.get("text") or ""
        if text:
            chat.messages.append(history.Message(
                role=history.USER, text=text, created=moment,
                message_id=item.get("token") or ""))
        for response in item.get("response") or []:
            body = response.get("content") or ""
            if not body:
                continue
            rtype = response.get("type") or ""
            if rtype == "reasoning":
                kind = history.THINKING
            elif rtype in ("agentic", "canvas"):
                kind = history.TOOL_CALL
            elif rtype.startswith("generated-image"):
                kind = history.ATTACHMENT
            else:
                kind = history.TEXT
            chat.messages.append(history.Message(
                role=history.ASSISTANT, text=body, kind=kind, created=moment,
                message_id=response.get("block_id") or "",
                name=rtype if kind != history.TEXT else ""))
    if not chat.created and chat.messages:
        chat.created = chat.messages[0].created
    return chat


def _markdown_chat(path, root):
    """Parse one legacy Markdown file into a normalised chat."""
    text = path.read_text(encoding="utf-8", errors="replace")
    title = ""
    for line in text.splitlines():
        if line.startswith("# "):
            title = line[2:].strip()
            break
    footer = FOOTER.search(text)
    body = text[:footer.start()] if footer else text
    model = MODEL.search(text)
    token = TOKEN.search(text)

    match = DATE_NAME.match(path.name)
    created = ""
    if match:
        try:
            moment = datetime.strptime(
                f"{match.group(1)} {match.group(2)}", "%Y-%m-%d %H-%M")
            created = moment.astimezone().isoformat(timespec="seconds")
        except ValueError:
            created = ""
    if not created:
        created = datetime.fromtimestamp(
            path.stat().st_mtime).astimezone().isoformat(timespec="seconds")

    remote = token.group(1).strip() if token else ""
    provider = "gap" if remote else "manual"
    chat = history.Chat(
        provider=provider,
        remote_id=remote or str(uuid.uuid4()),
        title=title or path.stem,
        created=created,
        updated=created,
        model=model.group(1).strip() if model else "",
    )
    parts = SECTION.split(body)
    for index in range(1, len(parts) - 1, 2):
        role = parts[index].lower()
        content = parts[index + 1].strip()
        if not content:
            continue
        if role == "assistant":
            thinking = DETAILS.search(content)
            if thinking:
                chat.messages.append(history.Message(
                    role=history.ASSISTANT, text=thinking.group(1).strip(),
                    kind=history.THINKING, created=created))
            visible = DETAILS.sub("", content).strip()
            if visible:
                chat.messages.append(history.Message(
                    role=history.ASSISTANT, text=visible, created=created))
        else:
            chat.messages.append(history.Message(
                role=history.USER, text=content, created=created))
    if chat.messages:
        chat.updated = history.newest(m.created for m in chat.messages)
    return chat, remote


def _old_markdown(store):
    """Every legacy ``<category>/*.md`` (never the new tree)."""
    skip = {".git", ".ai", "topics", "months"}
    files = []
    for entry in os_scandir(store.root):
        if entry.is_file() and entry.name.endswith(".md"):
            files.append(Path(entry.path))
        elif entry.is_dir() and entry.name not in skip:
            for child in os_scandir(Path(entry.path)):
                if child.is_file() and child.name.endswith(".md"):
                    files.append(Path(child.path))
    return files


def os_scandir(path):
    try:
        return list(os.scandir(path))
    except OSError:
        return []


def migrate(store, dry_run=False, keep_old=False):
    """Import the legacy cache; returns a stats dict."""
    stats = {"chats": 0, "markdown": 0, "skipped": 0, "removed": 0}
    projects = _projects(store)
    imported = set()

    meta_dir = store.ai / "chat-meta"
    content_dir = store.ai / "chat-content"
    for meta_path in sorted(meta_dir.glob("*.json")) if meta_dir.is_dir() else []:
        token = meta_path.stem
        content_path = content_dir / f"{token}.json"
        try:
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
            content = json.loads(content_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            stats["skipped"] += 1
            continue
        if not isinstance(content.get("messages"), list):
            stats["skipped"] += 1
            continue
        chat = _legacy_chat(meta, content, projects)
        category = (chat.project or "Uncategorized")
        imported.add(("gap", token))
        stats["chats"] += 1
        if not dry_run:
            store.write_chat(
                chat, category=category, source="synced",
                category_source="project" if chat.project else "legacy")

    for path in _old_markdown(store):
        try:
            chat, remote = _markdown_chat(path, store.root)
        except (OSError, ValueError):
            stats["skipped"] += 1
            continue
        if remote and ("gap", remote) in imported:
            stats["skipped"] += 1
        else:
            category = path.parent.name if path.parent != store.root else "inbox"
            stats["markdown"] += 1
            if not dry_run:
                store.write_chat(
                    chat, category=rules.sanitize_category(category),
                    source="legacy", category_source="legacy")
        if not dry_run:
            try:
                path.unlink()
                stats["removed"] += 1
            except OSError:
                pass

    if not dry_run:
        if not keep_old:
            for directory in (meta_dir, content_dir):
                if directory.is_dir():
                    for child in directory.iterdir():
                        if child.is_file():
                            child.unlink()
                    try:
                        directory.rmdir()
                    except OSError:
                        pass
            for name in ("projects.json", "auth.json"):
                try:
                    (store.ai / name).unlink()
                except OSError:
                    pass
        _drop_gitignore(store.root)
        store.relink()
    return stats


def _drop_gitignore(root):
    path = root / ".gitignore"
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return
    kept = [line for line in lines if line.strip() != ".ai/auth.json"]
    if kept != lines:
        path.write_text("\n".join(kept) + "\n", encoding="utf-8")
