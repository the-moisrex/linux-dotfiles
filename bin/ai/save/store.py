"""The on-disk store: one Markdown file per conversation.

Layout::

    <root>/
      topics/<category>/<YYYY-MM>/<YYYY-MM-DD_HH-MM>_<title>.md
      months/<YYYY-MM>/<category> -> ../../topics/<category>/<YYYY-MM>
      .ai/config.json

``topics/`` is the source of truth; ``months/`` is a committed view of relative
symlinks.  Nothing is indexed: a scan reads only the frontmatter (the first
few KiB of each file), and ``last`` reads the winning body to recover the
message text.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from .. import history
from . import frontmatter, render, rules

DEFAULT_ROOT = "~/Documents/AI-Chats"
CONFIG_NAME = "config.json"
REQUIRED = ("provider", "chat", "title", "category", "created")


def parse_datetime(value):
    """Parse an ISO date/time, a Unix timestamp or ``None``."""
    if isinstance(value, datetime):
        return value
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        try:
            return datetime.fromtimestamp(value).astimezone()
        except (OverflowError, OSError, ValueError):
            return None
    if not isinstance(value, str) or not value.strip():
        return None
    text = value.strip()
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        moment = datetime.fromisoformat(text)
    except ValueError:
        try:
            moment = datetime.fromisoformat(text.replace(" ", "T", 1))
        except ValueError:
            return None
    if moment.tzinfo is None:
        moment = moment.astimezone()
    return moment


def month_of(value):
    moment = parse_datetime(value) or datetime.now().astimezone()
    return moment.strftime("%Y-%m")


def date_of(value):
    moment = parse_datetime(value) or datetime.now().astimezone()
    return moment.strftime("%Y-%m-%d_%H-%M")


def _bounded(text, limit):
    """Truncate *text* to at most *limit* UTF-8 bytes."""
    if len(text.encode("utf-8")) <= limit:
        return text
    return text.encode("utf-8")[:limit].decode("utf-8", "ignore").rstrip(" .")


def safe_title(value):
    """A filename-safe, length-bounded title (Unicode preserved)."""
    text = " ".join((value or "").split())
    text = re.sub(r'[<>:"/\\|?*\x00-\x1f]', " ", text)
    text = " ".join(text.split()).strip(" .")
    if not text:
        return "Untitled"
    return _bounded(text[:80].rstrip(" ."), 180) or "Untitled"


def _time(value):
    return history.epoch_of(value)


@dataclass
class LocalChat:
    """One stored chat, represented by its path and frontmatter."""

    path: Path
    meta: dict

    @property
    def provider(self):
        return str(self.meta.get("provider") or "")

    @property
    def chat_id(self):
        return str(self.meta.get("chat") or "")

    @property
    def key(self):
        return (self.provider, self.chat_id)

    @property
    def title(self):
        return str(self.meta.get("title") or "")

    @property
    def category(self):
        return str(self.meta.get("category") or "")

    @property
    def category_source(self):
        return str(self.meta.get("category_source") or "")

    @property
    def source(self):
        return str(self.meta.get("source") or "")

    @property
    def created(self):
        return str(self.meta.get("created") or "")

    @property
    def updated(self):
        return str(self.meta.get("updated") or "")

    @property
    def saved(self):
        return str(self.meta.get("saved") or "")

    @property
    def last_message(self):
        return str(self.meta.get("last_message") or self.saved or "")

    @property
    def last_user_saved(self):
        return str(self.meta.get("last_user_saved") or "")

    @property
    def last_assistant_saved(self):
        return str(self.meta.get("last_assistant_saved") or "")

    @property
    def tags(self):
        value = self.meta.get("tags") or []
        return list(value) if isinstance(value, list) else [str(value)]

    @property
    def auto_tags(self):
        value = self.meta.get("auto_tags") or []
        return list(value) if isinstance(value, list) else [str(value)]

    @property
    def all_tags(self):
        seen = []
        for tag in self.tags + self.auto_tags:
            if tag and tag not in seen:
                seen.append(tag)
        return seen

    @property
    def messages(self):
        try:
            return int(self.meta.get("messages") or 0)
        except (TypeError, ValueError):
            return 0

    @property
    def model(self):
        return str(self.meta.get("model") or "")

    @property
    def project(self):
        return str(self.meta.get("project") or "")

    @property
    def url(self):
        return str(self.meta.get("url") or "")

    @property
    def local_edits(self):
        return bool(self.meta.get("local_edits"))

    def sort_key(self, by="message"):
        if by == "saved":
            return _time(self.saved)
        if by == "updated":
            return _time(self.updated)
        if by == "created":
            return _time(self.created)
        if by == "title":
            return self.title.casefold()
        return _time(self.last_message)

    def summary(self):
        return {
            "provider": self.provider,
            "chat": self.chat_id,
            "title": self.title,
            "category": self.category,
            "category_source": self.category_source,
            "source": self.source,
            "local_edits": self.local_edits,
            "model": self.model,
            "project": self.project,
            "url": self.url,
            "created": self.created,
            "updated": self.updated,
            "saved": self.saved,
            "last_message": self.last_message,
            "last_user_saved": self.last_user_saved,
            "last_assistant_saved": self.last_assistant_saved,
            "messages": self.messages,
            "tags": self.tags,
            "auto_tags": self.auto_tags,
            "path": str(self.path),
        }


class Store:
    """Read/write access to an ``AI-Chats`` directory."""

    def __init__(self, root, config=None):
        self.root = Path(root).expanduser().resolve()
        self.topics = self.root / "topics"
        self.months = self.root / "months"
        self.ai = self.root / ".ai"
        self.config_path = self.ai / CONFIG_NAME
        self.config = rules.merge(rules.defaults(), config or self._read_config())
        self.categorizer = rules.Categorizer(self.config)

    # -- construction ----------------------------------------------------

    @classmethod
    def open(cls, root=None, create=False):
        root = root or os.environ.get("AI_CHATS_DIR") or DEFAULT_ROOT
        store = cls(root)
        if create:
            store.ensure_layout()
        return store

    def _read_config(self):
        try:
            with open(self.config_path, encoding="utf-8") as handle:
                data = json.load(handle)
            return data if isinstance(data, dict) else {}
        except (OSError, ValueError):
            return {}

    def ensure_layout(self):
        self.topics.mkdir(parents=True, exist_ok=True)
        self.months.mkdir(parents=True, exist_ok=True)
        self.ai.mkdir(parents=True, exist_ok=True)
        if not self.config_path.exists():
            self.save_config()

    def save_config(self):
        self.ai.mkdir(parents=True, exist_ok=True)
        with open(self.config_path, "w", encoding="utf-8") as handle:
            json.dump(self.config, handle, ensure_ascii=False, indent=2,
                      sort_keys=False)
            handle.write("\n")

    # -- scanning --------------------------------------------------------

    def month_dirs(self):
        """Every ``topics/<topic>/<month>`` directory with its newest mtime."""
        found = []
        if not self.topics.is_dir():
            return found
        for topic in sorted(self.topics.iterdir()):
            if not topic.is_dir() or topic.name.startswith("."):
                continue
            for month in sorted(topic.iterdir()):
                if not month.is_dir() or month.name.startswith("."):
                    continue
                newest = 0.0
                try:
                    for entry in os.scandir(month):
                        if entry.is_file() and entry.name.endswith(".md"):
                            newest = max(newest, entry.stat().st_mtime)
                except OSError:
                    continue
                if newest:
                    found.append((month, newest))
        return found

    def scope_dirs(self, scope="recent"):
        """The month directories to scan for *scope*.

        ``recent`` (the default) keeps the current year plus any older month
        directory whose newest file was touched this month.  A fresh clone
        resets mtimes, so the first scan is deliberately wide.
        """
        dirs = self.month_dirs()
        if scope in (None, "all"):
            return [path for path, _ in dirs]
        now = time.time()
        if scope == "year":
            cutoff = datetime.now().astimezone().replace(
                month=1, day=1, hour=0, minute=0, second=0,
                microsecond=0).timestamp()
        elif scope == "month":
            cutoff = datetime.now().astimezone().replace(
                day=1, hour=0, minute=0, second=0, microsecond=0).timestamp()
        elif scope == "week":
            cutoff = now - 7 * 86400
        elif isinstance(scope, str) and scope.endswith("d") and scope[:-1].isdigit():
            cutoff = now - int(scope[:-1]) * 86400
        else:
            cutoff = datetime.now().astimezone().replace(
                day=1, hour=0, minute=0, second=0, microsecond=0).timestamp()
        year = datetime.now().astimezone().strftime("%Y")
        return [path for path, newest in dirs
                if path.name.startswith(year + "-") or newest >= cutoff]

    def iter_local_in(self, directories):
        """Yield :class:`LocalChat` for the Markdown files in *directories*."""
        for directory in directories:
            try:
                entries = list(os.scandir(directory))
            except OSError:
                continue
            for entry in entries:
                if not entry.is_file() or not entry.name.endswith(".md"):
                    continue
                try:
                    meta = frontmatter.read_meta(entry.path)
                except OSError:
                    continue
                yield LocalChat(Path(entry.path), meta)

    def iter_local(self, scope="all"):
        """Yield :class:`LocalChat` for the frontmatter in *scope*."""
        return self.iter_local_in(self.scope_dirs(scope))

    def local_map(self, scope="all"):
        mapping = {}
        for local in self.iter_local(scope):
            if local.provider and local.chat_id:
                mapping.setdefault(local.key, local)
        return mapping

    def find(self, reference, scope="all"):
        """Resolve a path, ``provider:chat`` key, title or chat id prefix."""
        if not reference:
            raise LookupError("empty chat reference")
        candidate = Path(reference).expanduser()
        if candidate.is_file():
            return LocalChat(candidate.resolve(),
                             frontmatter.read_meta(candidate))
        if reference in ("@last", "last"):
            return self.last_chat()

        provider = ""
        needle = reference
        if ":" in reference and not reference.startswith(("http://", "https://")):
            head, _, tail = reference.partition(":")
            if head and tail and " " not in head:
                provider, needle = head, tail

        matches = []
        for local in self.iter_local(scope):
            score = 0
            if provider and local.provider != provider:
                continue
            if provider:
                if local.chat_id == needle:
                    score = 100
                elif local.chat_id.startswith(needle):
                    score = 80
            else:
                title = local.title.casefold()
                query = needle.casefold()
                if local.chat_id == needle:
                    score = 100
                elif local.chat_id.startswith(needle):
                    score = 80
                elif title == query:
                    score = 70
                elif query in title:
                    score = 50
                elif query in str(local.path).casefold():
                    score = 30
            if score:
                matches.append((score, local))
        if not matches:
            raise LookupError(f"no chat matches {reference!r}")
        matches.sort(key=lambda item: (-item[0], -_time(item[1].last_message)))
        best = matches[0][0]
        top = [local for score, local in matches if score == best]
        if len(top) > 1:
            names = ", ".join(str(local.path) for local in top[:8])
            raise LookupError(f"{reference!r} is ambiguous: {names}")
        return top[0]

    def last_chat(self, scope="all"):
        chats = list(self.iter_local(scope))
        if not chats:
            raise LookupError("the store is empty")
        return max(chats, key=lambda local: _time(local.last_message))

    # -- paths and writing ----------------------------------------------

    def path_for(self, category, created, title, provider, chat_id):
        category = rules.sanitize_category(category)
        month = month_of(created)
        base = self.topics / category / month
        name = f"{date_of(created)}_{safe_title(title)}.md"
        path = base / name
        if path.exists():
            meta = frontmatter.read_meta(path)
            if (meta.get("provider"), meta.get("chat")) != (provider, chat_id):
                suffix = (chat_id or "chat")[:8]
                path = base / f"{date_of(created)}_{safe_title(title)}-{suffix}.md"
        return path

    def body(self, local):
        _, body = frontmatter.read(local.path)
        return body

    def messages(self, local):
        return render.parse(self.body(local))

    def classify(self, chat, body_text="", category=None):
        return self.categorizer.classify(chat, body_text, category)

    def _times(self, messages, saved):
        created = [m.created for m in messages if m.created]
        users = [m.created for m in messages
                 if m.role == history.USER and m.kind == history.TEXT and m.created]
        assistants = [m.created for m in messages
                      if m.role == history.ASSISTANT and m.kind == history.TEXT
                      and m.created]
        return {
            "last_message": history.newest(created) or saved,
            "last_user_saved": history.newest(users),
            "last_assistant_saved": history.newest(assistants),
        }

    def write_chat(self, chat, category=None, tags=None, source="synced",
                   body_text=None, saved=None, category_source=None,
                   auto_tags=None, local_edits=False, existing=None,
                   preserve_local_edits=True):
        """Create or replace the file for *chat*; returns its :class:`LocalChat`."""
        saved = saved or history.now_iso()
        if category is None:
            category, inferred, category_source = self.classify(
                chat, body_text or "")
            auto_tags = inferred if auto_tags is None else auto_tags
        category = rules.sanitize_category(category)
        if body_text is None:
            body_text = render.body(chat, saved)
        if auto_tags is None:
            _, auto_tags, _ = self.classify(chat, body_text)
        if existing is not None:
            if tags is None:
                tags = existing.tags
            if existing.local_edits and preserve_local_edits:
                local_edits = True
        times = self._times(chat.messages, saved)
        created = chat.created or times["last_message"]
        meta = {
            "provider": chat.provider or "manual",
            "chat": chat.remote_id or "",
            "title": chat.title or "Untitled",
            "category": category,
            "category_source": category_source or "rule",
            "source": source,
            "created": created,
            "updated": chat.updated or times["last_message"],
            "saved": saved,
            "last_message": times["last_message"],
            "last_user_saved": times["last_user_saved"],
            "last_assistant_saved": times["last_assistant_saved"],
            "messages": len(chat.messages),
            "tags": list(tags or []),
            "auto_tags": list(auto_tags or []),
        }
        if chat.model:
            meta["model"] = chat.model
        if chat.project:
            meta["project"] = chat.project
        if chat.url:
            meta["url"] = chat.url
        if local_edits:
            meta["local_edits"] = True

        path = self.path_for(category, created, chat.title, meta["provider"],
                             meta["chat"])
        frontmatter.write(path, meta, body_text)
        if existing is not None and existing.path != path:
            self._unlink(existing.path)
        return LocalChat(path, meta)

    def append(self, local, messages, category=None, tags=None, source=None,
               saved=None):
        """Append messages to an existing chat (and optionally move it)."""
        saved = saved or history.now_iso()
        body_text = render.append_messages(self.body(local), messages, saved)
        meta = dict(local.meta)
        meta["messages"] = int(meta.get("messages") or 0) + len(messages)
        times = self._times(messages, saved)
        meta["last_message"] = history.newest(
            [meta.get("last_message"), times["last_message"]]) or saved
        meta["last_user_saved"] = history.newest(
            [meta.get("last_user_saved"), times["last_user_saved"]])
        meta["last_assistant_saved"] = history.newest(
            [meta.get("last_assistant_saved"), times["last_assistant_saved"]])
        meta["updated"] = history.newest(
            [meta.get("updated"), times["last_message"]]) or saved
        meta["saved"] = saved
        if local.source == "synced":
            meta["local_edits"] = True
        if source:
            meta["source"] = source
        if tags:
            current = list(meta.get("tags") or [])
            for tag in tags:
                if tag and tag not in current:
                    current.append(tag)
            meta["tags"] = current

        category = rules.sanitize_category(category or local.category)
        meta["category"] = category
        if category != local.category:
            meta["category_source"] = "manual"
        path = self.path_for(category, meta.get("created"), meta.get("title"),
                             meta.get("provider"), meta.get("chat"))
        frontmatter.write(path, meta, body_text)
        if path != local.path:
            self._unlink(local.path)
        return LocalChat(path, meta)

    def move(self, local, category, source="manual"):
        """Move a chat to another topic directory."""
        category = rules.sanitize_category(category)
        if category == local.category:
            return local
        meta = dict(local.meta)
        meta["category"] = category
        meta["category_source"] = source
        path = self.path_for(category, meta.get("created"), meta.get("title"),
                             meta.get("provider"), meta.get("chat"))
        body_text = self.body(local)
        frontmatter.write(path, meta, body_text)
        self._unlink(local.path)
        return LocalChat(path, meta)

    def repath(self, local):
        """Rename a chat to the canonical date/title path (same category)."""
        path = self.path_for(local.category, local.meta.get("created"),
                             local.meta.get("title"), local.provider,
                             local.chat_id)
        if path == local.path:
            return local
        body_text = self.body(local)
        if path.exists():
            other = LocalChat(path, frontmatter.read_meta(path))
            if other.key == local.key and self._prefer_existing(other, local):
                self._unlink(local.path)
                return other
        frontmatter.write(path, local.meta, body_text)
        self._unlink(local.path)
        return LocalChat(path, local.meta)

    @staticmethod
    def _prefer_existing(existing, current):
        """Keep the canonical copy when it is newer or has local edits."""
        if existing.local_edits != current.local_edits:
            return existing.local_edits
        return _time(existing.saved) >= _time(current.saved)

    def update_meta(self, local, **changes):
        meta = dict(local.meta)
        meta.update(changes)
        frontmatter.write(local.path, meta, self.body(local))
        return LocalChat(local.path, meta)

    def remove(self, local):
        self._unlink(local.path)

    def _unlink(self, path):
        try:
            Path(path).unlink()
        except FileNotFoundError:
            return
        parent = Path(path).parent
        while parent != self.root and parent != self.topics and parent.is_dir():
            try:
                next(parent.iterdir())
                break
            except StopIteration:
                try:
                    parent.rmdir()
                except OSError:
                    break
                parent = parent.parent
            except OSError:
                break

    # -- month symlink view ---------------------------------------------

    def relink(self):
        """Regenerate ``months/<YYYY-MM>/<topic>`` relative symlinks."""
        if not self.config.get("links", True):
            return (0, 0, 0)
        desired = {}
        for month, _ in self.month_dirs():
            desired[(month.name, month.parent.name)] = month
        removed = 0
        if self.months.is_dir():
            for month_dir in list(self.months.iterdir()):
                if not month_dir.is_dir():
                    continue
                for link in list(month_dir.iterdir()):
                    if not link.is_symlink():
                        continue
                    if (month_dir.name, link.name) not in desired:
                        link.unlink()
                        removed += 1
                try:
                    month_dir.rmdir()
                except OSError:
                    pass
        created = 0
        for (month_name, topic), target in desired.items():
            link_dir = self.months / month_name
            link_dir.mkdir(parents=True, exist_ok=True)
            link = link_dir / topic
            relative = os.path.relpath(target, link_dir)
            if link.is_symlink():
                if os.readlink(link) == relative:
                    continue
                link.unlink()
            elif link.exists():
                continue
            os.symlink(relative, link, target_is_directory=True)
            created += 1
        return (created, removed, len(desired))

    # -- git -------------------------------------------------------------

    def _git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args],
                              text=True, capture_output=True, check=False)

    def commit(self, message, no_commit=False):
        """Stage and commit every change; returns ``True`` when it committed."""
        if no_commit:
            return False
        if not (self.root / ".git").exists():
            self._git("init")
        self._git("add", "-A")
        status = self._git("status", "--porcelain")
        if not status.stdout.strip():
            return False
        result = self._git("commit", "-m", message)
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip())
        return True
