"""Command line interface for ``ai.save``.

The store is Markdown-only, so every command is a scan or a write; there is no
index to keep in sync.  Mutating commands commit their changes to git unless
``--no-commit`` is given.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from .. import history, net, session
from ..providers import all_providers
from . import frontmatter, migrate as migrate_mod, render, rules
from .store import Store, month_of, safe_title


# -- small helpers -----------------------------------------------------------


def _time(value):
    return history.epoch_of(value)


def _json(value):
    print(json.dumps(value, ensure_ascii=False))


def _jsonl(values):
    for value in values:
        _json(value)


def _fail(message):
    print(f"ai.save: {message}", file=sys.stderr)
    return 1


def _store(args, create=False):
    store = Store.open(args.dir, create=create)
    if create:
        store.ensure_layout()
    return store


#: Site names people naturally type mapped to the registry keys.
PROVIDER_ALIASES = {
    "gapgpt": "gap",
    "gap-gpt": "gap",
    "dseek": "deepseek",
}


def _canonical_provider(name):
    key = name.strip()
    return PROVIDER_ALIASES.get(key.casefold(), key)


def _recordable(provider):
    """Whether the store can sync *provider* (an API provider has no chat list)."""
    return getattr(provider, "recordable", True)


def _providers(requested):
    registry = all_providers()
    if not requested:
        return [name for name, cls in registry.items() if _recordable(cls)]
    names = []
    for item in requested:
        for part in item.split(","):
            if not part.strip():
                continue
            name = _canonical_provider(part)
            if name not in registry:
                raise LookupError(
                    f"unknown provider {name!r} (known: {', '.join(registry)})")
            if not _recordable(registry[name]):
                raise LookupError(
                    f"{name} is a stateless API provider with no chat list to "
                    f"sync; save prompts and answers with `ai.save add` instead")
            if name not in names:
                names.append(name)
    return names


def _session(provider):
    return session.load(provider.session_target)


def _retry(label, operation, retries=3, delay=5.0):
    """Run *operation*, retrying transient HTTP/network failures."""
    attempt = 0
    while True:
        try:
            return operation()
        except Exception as exc:  # noqa: BLE001 - classify, then re-raise
            attempt += 1
            if attempt >= retries or not net.is_transient(exc):
                raise
            wait = max(0.0, delay) * (2 ** (attempt - 1))
            print(f"{label}: retrying in {wait:.0f}s ({exc})", file=sys.stderr)
            time.sleep(wait)


def _provider_filter(args):
    values = getattr(args, "provider", None) or []
    names = []
    for item in values:
        for part in item.split(","):
            if not part.strip():
                continue
            name = _canonical_provider(part)
            if name not in names:
                names.append(name)
    return names


def _check_provider_filter(args):
    """Validate ``--provider`` for read commands (aliases included)."""
    names = _provider_filter(args)
    if not names:
        return None
    registry = all_providers()
    for name in names:
        if name not in registry:
            return _fail(f"unknown provider {name!r} "
                         f"(known: {', '.join(registry)})")
        if not _recordable(registry[name]):
            return _fail(f"{name} is a stateless API provider with no chat "
                         f"history to save")
    return None


def _matches(local, args):
    providers = _provider_filter(args)
    if providers and local.provider not in providers:
        return False
    category = getattr(args, "category", None)
    if category and local.category != category:
        return False
    tags = getattr(args, "tag", None) or []
    if tags and not all(tag in local.all_tags for tag in tags):
        return False
    month = getattr(args, "month", None)
    if month and not local.path.parent.name.startswith(month):
        return False
    year = getattr(args, "year", None)
    if year and not local.path.parent.name.startswith(f"{year}-"):
        return False
    since = getattr(args, "since", None)
    if since and _time(local.last_message) < _time(since):
        return False
    return True


def _scope(args, store):
    if getattr(args, "all", False):
        return "all"
    if getattr(args, "month", None) or getattr(args, "year", None):
        return "all"
    return store.config.get("recent", "month")


def _print_chat(local, args):
    if getattr(args, "paths", False):
        print(local.path)
    elif getattr(args, "jsonl", False):
        _json(local.summary())
    else:
        stamp = (local.last_message or local.saved)[:16].replace("T", " ")
        print(f"{local.provider:<9} {local.category:<12} {stamp}  "
              f"{local.title}  ({local.chat_id[:8]})")


# -- sync --------------------------------------------------------------------


def _write_synced(store, summary, chat, local, args):
    history.merge(summary, chat)
    if local is not None and local.local_edits and not args.force:
        return "skipped"
    saved = history.now_iso()
    body_text = render.body(chat, saved)
    category, auto_tags, category_source = store.classify(chat, body_text)
    if local is not None and local.category_source == "manual":
        category = local.category
        category_source = "manual"
    if args.dry_run:
        print(f"would save {summary.provider}/{summary.remote_id} -> "
              f"{category}/{month_of(chat.created)}/{safe_title(chat.title)}.md")
        return "new" if local is None else "updated"
    store.write_chat(chat, category=category, source="synced",
                     body_text=body_text, saved=saved,
                     category_source=category_source, auto_tags=auto_tags,
                     existing=local, preserve_local_edits=not args.force)
    return "new" if local is None else "updated"


def cmd_sync(args):
    store = _store(args, create=True)
    registry = all_providers()
    local_map = store.local_map("all")
    totals = {"new": 0, "updated": 0, "skipped": 0, "failed": 0}

    for name in _providers(args.provider):
        provider = registry[name]()
        try:
            match = _session(provider)
        except session.SessionError as exc:
            print(f"{name}: no session: {exc}", file=sys.stderr)
            totals["failed"] += 1
            continue
        print(f"{name}: listing chats...", file=sys.stderr)
        try:
            summaries = _retry(
                f"{name}: listing",
                lambda: list(provider.iter_chat_summaries(
                    match, limit=args.limit)),
                args.retries, args.retry_delay)
        except Exception as exc:  # noqa: BLE001 - one provider must not stop sync
            print(f"{name}: listing failed: {exc}", file=sys.stderr)
            totals["failed"] += 1
            continue

        skips = getattr(provider, "list_skips", None)
        if skips:
            totals.setdefault("incomplete", {})[name] = list(skips)

        todo = []
        for summary in summaries:
            local = local_map.get((summary.provider, summary.remote_id))
            if local is not None and not args.full:
                if local.local_edits and not args.force:
                    totals["skipped"] += 1
                    continue
                if (summary.updated and local.updated
                        and _time(local.updated) >= _time(summary.updated)):
                    totals["skipped"] += 1
                    continue
            todo.append((summary, local))

        if not todo:
            print(f"{name}: 0 new, 0 updated, {len(summaries)} unchanged",
                  file=sys.stderr)
            continue

        try:
            provider.history_headers(match)
        except Exception:  # noqa: BLE001 - fetch will report it
            pass

        counts = {"new": 0, "updated": 0, "skipped": 0, "failed": 0}

        def fetch(summary):
            return _retry(
                f"{name}/{summary.remote_id}",
                lambda: provider.fetch_chat(match, summary.remote_id),
                args.retries, args.retry_delay)

        jobs = max(1, args.jobs)
        if jobs <= 1:
            for summary, _ in todo:
                try:
                    chat = fetch(summary)
                    local = local_map.get(
                        (summary.provider, summary.remote_id))
                    outcome = _write_synced(store, summary, chat, local, args)
                except Exception as exc:  # noqa: BLE001
                    print(f"{name}/{summary.remote_id}: {exc}", file=sys.stderr)
                    counts["failed"] += 1
                    continue
                counts[outcome] += 1
        else:
            with ThreadPoolExecutor(max_workers=jobs) as pool:
                futures = {
                    pool.submit(fetch, summary): summary
                    for summary, _ in todo}
                for future in as_completed(futures):
                    summary = futures[future]
                    try:
                        chat = future.result()
                        local = local_map.get(
                            (summary.provider, summary.remote_id))
                        outcome = _write_synced(store, summary, chat, local,
                                                args)
                    except Exception as exc:  # noqa: BLE001
                        print(f"{name}/{summary.remote_id}: {exc}",
                              file=sys.stderr)
                        counts["failed"] += 1
                        continue
                    counts[outcome] += 1
        for key in counts:
            totals[key] += counts[key]
        print(f"{name}: {counts['new']} new, {counts['updated']} updated, "
              f"{counts['skipped']} unchanged, {counts['failed']} failed",
              file=sys.stderr)

    if not args.dry_run and not args.no_links:
        created, removed, total = store.relink()
        print(f"months/: {created} link(s) created, {removed} removed, "
              f"{total} topic-month pair(s)", file=sys.stderr)
    if not args.dry_run:
        message = (f"ai.save: sync {totals['new']} new, "
                   f"{totals['updated']} updated")
        if store.commit(message, args.no_commit):
            print("committed", file=sys.stderr)
    print(json.dumps(totals, ensure_ascii=False))
    return 1 if totals["failed"] else 0


# -- read commands -----------------------------------------------------------


def cmd_last(args):
    problem = _check_provider_filter(args)
    if problem is not None:
        return problem
    store = _store(args)
    scope = _scope(args, store)
    if args.chat:
        candidates = [store.find(args.chat, "all")]
    else:
        candidates = [local for local in store.iter_local(scope)
                      if _matches(local, args)]
        if scope != "all" and candidates:
            best = max(_time(local.last_message) for local in candidates)
            scanned = set(store.scope_dirs(scope))
            extra = [path for path, mtime in store.month_dirs()
                     if path not in scanned and mtime >= best]
            if extra:
                candidates.extend(
                    local for local in store.iter_local_in(extra)
                    if _matches(local, args))
        elif scope != "all" and not candidates:
            candidates = [local for local in store.iter_local("all")
                          if _matches(local, args)]
    candidates.sort(key=lambda local: local.sort_key(args.by), reverse=True)

    found = []
    for local in candidates:
        if len(found) >= args.limit:
            break
        try:
            messages = store.messages(local)
        except OSError:
            continue
        for message in reversed(messages):
            if args.role and message["role"] != args.role:
                continue
            if args.kind and message["kind"] != args.kind:
                continue
            if not message["text"].strip():
                continue
            found.append((local, message))
            if len(found) >= args.limit:
                break
    if not found:
        return _fail("no saved messages match")

    if args.json:
        for local, message in found:
            record = local.summary()
            record.update({
                "role": message["role"],
                "kind": message["kind"],
                "message_id": message["id"],
                "message_created": message["created"],
                "message_saved": message["saved"],
                "text": message["text"],
            })
            _json(record)
        return 0

    blocks = []
    for local, message in found:
        if args.limit != 1:
            blocks.append(f"[{local.provider}] {local.title} "
                          f"({message['created'][:16]})")
        blocks.append(message["text"])
    print("\n\n".join(blocks))
    return 0


def cmd_list(args):
    problem = _check_provider_filter(args)
    if problem is not None:
        return problem
    store = _store(args)
    scope = _scope(args, store)
    chats = [local for local in store.iter_local(scope) if _matches(local, args)]
    reverse = args.reverse if args.sort == "title" else not args.reverse
    chats.sort(key=lambda local: local.sort_key(args.sort), reverse=reverse)
    if args.limit:
        chats = chats[:args.limit]
    if args.json:
        _json([local.summary() for local in chats])
    elif args.jsonl or args.paths:
        for local in chats:
            _print_chat(local, args)
    else:
        for local in chats:
            _print_chat(local, args)
        print(f"\n{len(chats)} chat(s)", file=sys.stderr)
    return 0


def cmd_show(args):
    store = _store(args)
    local = store.find(args.ref, "all")
    if args.open:
        subprocess.run(["xdg-open", str(local.path)], check=False)
        return 0
    body = store.body(local)
    if args.json:
        record = local.summary()
        record["messages"] = store.messages(local)
        record["body"] = body
        _json(record)
    elif args.messages:
        for message in store.messages(local):
            first = message["text"].splitlines()[0] if message["text"] else ""
            print(f"{message['role']:<9} {message['kind']:<10} "
                  f"{message['created'][:16]:<16} {first[:100]}")
    elif args.raw:
        print(body)
    else:
        print(frontmatter.dumps(local.meta))
        print(body)
    return 0


# -- writes ------------------------------------------------------------------


def _read_texts(files):
    texts = []
    if not files and not sys.stdin.isatty():
        data = sys.stdin.read()
        if data.strip():
            texts.append(data)
        return texts
    for name in files:
        if name == "-":
            data = sys.stdin.read()
            if data.strip():
                texts.append(data)
        else:
            texts.append(Path(name).expanduser().read_text(encoding="utf-8"))
    return texts


def _last_role(messages):
    for message in reversed(messages):
        if message.get("kind") == history.TEXT:
            return message.get("role") or history.USER
    return history.USER


def _add(args, chat_ref=None):
    store = _store(args, create=True)
    texts = _read_texts(args.files)
    if not texts:
        return _fail("nothing to save (pass files, or pipe text on stdin)")
    now = history.now_iso()

    if chat_ref:
        local = store.find(chat_ref, "all")
        start = args.role or (
            history.ASSISTANT if _last_role(store.messages(local)) == history.USER
            else history.USER)
        messages = []
        for index, text in enumerate(texts):
            role = args.role or (start if index % 2 == 0 else
                                 history.USER if start == history.ASSISTANT
                                 else history.ASSISTANT)
            messages.append(history.Message(
                role=role, text=text.strip(), created=now))
        category = args.category or None
        local = store.append(local, messages, category=category,
                             tags=args.tag, source=local.source or "manual")
        if args.title:
            local = store.update_meta(local, title=args.title)
            local = store.repath(local)
        print(local.path)
        if args.json:
            _json(local.summary())
        store.relink()
        store.commit(f"ai.save: reply to {local.title}", args.no_commit)
        return 0

    provider = _canonical_provider(args.provider or "manual")
    remote_id = args.id or str(uuid.uuid4())
    created = args.date or now
    title = args.title or next(
        (line.strip().lstrip("# ").strip() for line in texts[0].splitlines()
         if line.strip()), "Untitled")
    chat = history.Chat(
        provider=provider,
        remote_id=remote_id,
        title=title,
        created=created,
        updated=now,
        model=args.model or "",
    )
    role = args.role or history.USER
    for index, text in enumerate(texts):
        current = args.role or (role if index % 2 == 0 else
                                history.ASSISTANT if role == history.USER
                                else history.USER)
        chat.messages.append(history.Message(
            role=current, text=text.strip(), created=now))
    body_text = render.body(chat, now)
    category, auto_tags, category_source = store.classify(
        chat, body_text, args.category)
    local = store.write_chat(
        chat, category=category, tags=args.tag, source="manual",
        body_text=body_text, saved=now, category_source=category_source,
        auto_tags=auto_tags)
    print(local.path)
    if args.json:
        _json(local.summary())
    store.relink()
    store.commit(f"ai.save: add {local.title}", args.no_commit)
    return 0


def cmd_add(args):
    return _add(args, chat_ref=args.chat)


def cmd_reply(args):
    return _add(args, chat_ref=args.ref)


def cmd_categorize(args):
    store = _store(args)
    local = store.find(args.ref, "all")
    local = store.move(local, args.category, source="manual")
    if args.tag:
        tags = list(local.tags)
        for tag in args.tag:
            if tag and tag not in tags:
                tags.append(tag)
        local = store.update_meta(local, tags=tags)
    print(local.path)
    if args.json:
        _json(local.summary())
    store.relink()
    store.commit(f"ai.save: categorize {local.title} -> {local.category}",
                 args.no_commit)
    return 0


def cmd_tag(args, remove=False):
    store = _store(args)
    local = store.find(args.ref, "all")
    tags = list(local.tags)
    for tag in args.tags:
        if remove:
            tags = [current for current in tags if current != tag]
        elif tag and tag not in tags:
            tags.append(tag)
    local = store.update_meta(local, tags=tags)
    print(local.path)
    store.commit(f"ai.save: {'untag' if remove else 'tag'} {local.title}",
                 args.no_commit)
    return 0


def cmd_untag(args):
    return cmd_tag(args, remove=True)


def cmd_recategorize(args):
    store = _store(args)
    changed = 0
    for local in store.iter_local("all"):
        if local.category_source == "manual" and not args.force:
            continue
        body = store.body(local)
        chat = history.Chat(
            provider=local.provider, remote_id=local.chat_id,
            title=local.title, created=local.created, updated=local.updated,
            model=local.model, project=local.project, url=local.url)
        category, auto_tags, source = store.classify(chat, body)
        if source == "manual" and not args.force:
            continue
        if category == local.category and auto_tags == local.auto_tags:
            continue
        changed += 1
        print(f"{local.path} -> {category}")
        if args.dry_run:
            continue
        if category != local.category:
            local = store.move(local, category, source=source)
        local = store.update_meta(local, auto_tags=auto_tags,
                                  category_source=source)
    if not args.dry_run:
        store.relink()
        store.commit(f"ai.save: recategorize {changed} chat(s)", args.no_commit)
    print(f"{changed} chat(s) would change" if args.dry_run
          else f"{changed} chat(s) changed", file=sys.stderr)
    return 0


def cmd_categories(args):
    store = _store(args)
    counts = {}
    for local in store.iter_local("all"):
        counts[local.category] = counts.get(local.category, 0) + 1
    if args.json:
        _json(dict(sorted(counts.items())))
    else:
        for name, count in sorted(counts.items(), key=lambda item: -item[1]):
            print(f"{count:6d}  {name}")
    return 0


def cmd_rename_category(args):
    store = _store(args)
    old = rules.sanitize_category(args.old)
    new = rules.sanitize_category(args.new)
    if old == new:
        return 0
    chats = [local for local in store.iter_local("all") if local.category == old]
    for local in chats:
        store.move(local, new, source=local.category_source or "manual")
    old_dir = store.topics / old
    try:
        old_dir.rmdir()
    except OSError:
        pass
    store.relink()
    store.commit(f"ai.save: rename category {old} -> {new}", args.no_commit)
    print(f"{len(chats)} chat(s) moved to {new}")
    return 0


def cmd_remove(args):
    store = _store(args)
    local = store.find(args.ref, "all")
    path = local.path
    store.remove(local)
    store.relink()
    store.commit(f"ai.save: remove {local.title}", args.no_commit)
    print(path)
    return 0


def cmd_reload(args):
    store = _store(args)
    local = store.find(args.ref, "all")
    if local.source != "synced" or not local.provider or not local.chat_id:
        return _fail("only synced chats can be reloaded")
    registry = all_providers()
    if local.provider not in registry:
        return _fail(f"unknown provider {local.provider!r}")
    provider = registry[local.provider]()
    try:
        match = _session(provider)
        chat = _retry(
            f"{local.provider}/{local.chat_id}",
            lambda: provider.fetch_chat(match, local.chat_id))
    except Exception as exc:  # noqa: BLE001
        return _fail(str(exc))
    summary = history.ChatSummary(
        provider=local.provider, remote_id=local.chat_id, title=local.title,
        created=local.created, updated=local.updated, model=local.model,
        project=local.project, url=local.url)
    history.merge(summary, chat)
    saved = history.now_iso()
    body_text = render.body(chat, saved)
    category, auto_tags, category_source = store.classify(chat, body_text)
    if local.category_source == "manual":
        category = local.category
        category_source = "manual"
    local = store.write_chat(
        chat, category=category, source="synced", body_text=body_text,
        saved=saved, category_source=category_source, auto_tags=auto_tags,
        existing=local)
    print(local.path)
    store.relink()
    store.commit(f"ai.save: reload {local.title}", args.no_commit)
    return 0


# -- maintenance -------------------------------------------------------------


def cmd_relink(args):
    store = _store(args)
    created, removed, total = store.relink()
    store.commit("ai.save: relink months", args.no_commit)
    print(f"{created} link(s) created, {removed} removed, "
          f"{total} topic-month pair(s)")
    return 0


def cmd_verify(args):
    store = _store(args)
    issues = []
    seen = {}
    for local in store.iter_local("all"):
        for key in ("provider", "chat", "title", "category", "created"):
            if not local.meta.get(key):
                issues.append(f"{local.path}: missing {key}")
        if local.chat_id:
            other = seen.get(local.key)
            if other is not None and other != local.path:
                issues.append(f"{local.path}: duplicate of {other}")
            seen.setdefault(local.key, local.path)
        if local.path.parent.parent.name != local.category:
            issues.append(f"{local.path}: category {local.category!r} does not "
                          f"match its directory")
        expected = store.path_for(local.category, local.created, local.title,
                                  local.provider, local.chat_id)
        if local.path != expected:
            issues.append(f"{local.path}: expected {expected}")
        try:
            markers = len(store.messages(local))
        except OSError:
            markers = 0
        if local.messages and markers != local.messages:
            issues.append(f"{local.path}: frontmatter says {local.messages} "
                          f"message(s), body has {markers}")
        if args.fix and local.category and local.path.parent.parent.name != local.category:
            local = store.move(local, local.path.parent.parent.name,
                               source=local.category_source or "legacy")
        if args.fix and local.path != store.path_for(
                local.category, local.created, local.title,
                local.provider, local.chat_id):
            local = store.repath(local)

    desired = {(month.name, month.parent.name)
               for month, _ in store.month_dirs()}
    actual = set()
    if store.months.is_dir():
        for month_dir in store.months.iterdir():
            if not month_dir.is_dir():
                continue
            for link in month_dir.iterdir():
                if link.is_symlink():
                    actual.add((month_dir.name, link.name))
    for missing in sorted(desired - actual):
        issues.append(f"months/{missing[0]}/{missing[1]}: missing link")
    for stale in sorted(actual - desired):
        issues.append(f"months/{stale[0]}/{stale[1]}: stale link")

    if args.fix:
        store.relink()
        if not args.no_commit:
            store.commit("ai.save: repair store layout")
    for issue in issues:
        print(issue)
    if args.json:
        _json({"issues": issues, "fixed": bool(args.fix)})
    else:
        print(f"{len(issues)} issue(s)", file=sys.stderr)
    return 0 if not issues or args.fix else 1


def cmd_migrate(args):
    store = _store(args, create=True)
    stats = migrate_mod.migrate(store, dry_run=args.dry_run,
                                keep_old=args.keep_old)
    if not args.dry_run:
        store.commit("ai.save: migrate legacy store", args.no_commit)
    _json(stats)
    return 0


def cmd_providers(args):
    registry = all_providers()
    rows = []
    for name, cls in registry.items():
        if not _recordable(cls):
            continue
        rows.append({
            "name": name,
            "label": cls.label,
            "session": cls.session_target,
            "default_model": cls.default_model,
            "aliases": sorted(alias for alias, target
                              in PROVIDER_ALIASES.items() if target == name),
        })
    if args.json:
        _json(rows)
    else:
        for row in rows:
            print(f"{row['name']:<9} {row['label']:<10} "
                  f"{row['session']:<20} {row['default_model']}")
        aliases = ", ".join(
            f"{alias}={target}" for alias, target in PROVIDER_ALIASES.items())
        if aliases:
            print(f"\naliases: {aliases}")
    return 0


def cmd_config(args):
    store = _store(args, create=True)
    if args.edit:
        editor = os.environ.get("EDITOR") or "vi"
        subprocess.run([editor, str(store.config_path)], check=False)
        return 0
    if args.json:
        _json(store.config)
    else:
        print(store.config_path)
        print(json.dumps(store.config, ensure_ascii=False, indent=2))
    return 0


# -- argument parsing --------------------------------------------------------


def _common():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--dir", default=argparse.SUPPRESS,
                        help="store root (default: $AI_CHATS_DIR or "
                             "~/Documents/AI-Chats)")
    parser.add_argument("--debug", action="store_true",
                        default=argparse.SUPPRESS)
    return parser


def _commit_options(parser):
    parser.add_argument("--no-commit", action="store_true",
                        help="leave the git tree dirty")
    parser.add_argument("--json", action="store_true", help="JSON output")


def build_parser():
    common = _common()
    parser = argparse.ArgumentParser(
        prog="ai.save",
        description="Local, git-backed Markdown store for AI conversations.")
    parser.add_argument("--dir", default=None,
                        help="store root (default: $AI_CHATS_DIR or "
                             "~/Documents/AI-Chats)")
    parser.add_argument("--debug", action="store_true")
    sub = parser.add_subparsers(dest="command", required=True)

    sync = sub.add_parser("sync", parents=[common],
                          help="fetch chats from the signed-in providers")
    sync.add_argument("--provider", action="append", metavar="NAME",
                      help="only these providers (repeatable, comma separated; "
                           "aliases: gapgpt=gap, dseek=deepseek)")
    sync.add_argument("--full", action="store_true",
                      help="re-fetch chats that look unchanged")
    sync.add_argument("--limit", type=int, default=0,
                      help="at most N sidebar entries per provider")
    sync.add_argument("--jobs", type=int, default=4,
                      help="parallel fetches per provider (default 4)")
    sync.add_argument("--retries", type=int, default=3,
                      help="attempts for transient HTTP failures (default 3)")
    sync.add_argument("--retry-delay", type=float, default=5.0,
                      help="initial retry backoff in seconds (default 5)")
    sync.add_argument("--dry-run", action="store_true")
    sync.add_argument("--force", action="store_true",
                      help="overwrite chats with local edits")
    sync.add_argument("--no-links", action="store_true",
                      help="do not regenerate months/")
    _commit_options(sync)
    sync.set_defaults(func=cmd_sync)

    last = sub.add_parser("last", parents=[common],
                          help="print the newest saved message")
    last.add_argument("--provider", action="append", metavar="NAME",
                      help="only these providers (aliases accepted)")
    last.add_argument("--role", choices=("user", "assistant", "system", "tool"))
    last.add_argument("--kind", choices=("text", "thinking", "tool_call",
                                         "attachment"), default="text",
                      help="message kind (default: text)")
    last.add_argument("--category")
    last.add_argument("--tag", action="append")
    last.add_argument("--month")
    last.add_argument("--year")
    last.add_argument("--all", action="store_true",
                      help="scan every month, not just the recent window")
    last.add_argument("--by", choices=("message", "saved", "updated", "created"),
                      default="message")
    last.add_argument("--chat", help="restrict to one chat")
    last.add_argument("--limit", type=int, default=1)
    last.add_argument("--json", action="store_true")
    last.set_defaults(func=cmd_last)

    listing = sub.add_parser("list", parents=[common],
                             help="list stored chats")
    listing.add_argument("--provider", action="append", metavar="NAME",
                         help="only these providers (aliases accepted)")
    listing.add_argument("--category")
    listing.add_argument("--tag", action="append")
    listing.add_argument("--month")
    listing.add_argument("--year")
    listing.add_argument("--since")
    listing.add_argument("--all", action="store_true")
    listing.add_argument("--sort", choices=("message", "saved", "updated",
                                            "created", "title"),
                         default="message")
    listing.add_argument("--reverse", action="store_true")
    listing.add_argument("--limit", type=int, default=50)
    listing.add_argument("--json", action="store_true")
    listing.add_argument("--jsonl", action="store_true")
    listing.add_argument("--paths", action="store_true")
    listing.set_defaults(func=cmd_list)

    show = sub.add_parser("show", parents=[common],
                          help="print one chat")
    show.add_argument("ref")
    show.add_argument("--json", action="store_true")
    show.add_argument("--messages", action="store_true")
    show.add_argument("--raw", action="store_true")
    show.add_argument("--open", action="store_true")
    show.set_defaults(func=cmd_show)

    add = sub.add_parser("add", parents=[common],
                         help="save prompts/responses from files or stdin")
    add.add_argument("files", nargs="*", metavar="FILE")
    add.add_argument("--chat", help="append to this chat (same as reply)")
    add.add_argument("--title")
    add.add_argument("--category")
    add.add_argument("--tag", action="append")
    add.add_argument("--provider", default="manual")
    add.add_argument("--model")
    add.add_argument("--date")
    add.add_argument("--id")
    add.add_argument("--role", choices=("user", "assistant", "system", "tool"))
    _commit_options(add)
    add.set_defaults(func=cmd_add)

    reply = sub.add_parser("reply", parents=[common],
                           help="append a response to an existing chat")
    reply.add_argument("ref")
    reply.add_argument("files", nargs="*", metavar="FILE")
    reply.add_argument("--title")
    reply.add_argument("--category")
    reply.add_argument("--tag", action="append")
    reply.add_argument("--role", choices=("user", "assistant", "system", "tool"))
    _commit_options(reply)
    reply.set_defaults(func=cmd_reply)

    categorize = sub.add_parser("categorize", parents=[common],
                                help="move a chat to a category")
    categorize.add_argument("ref")
    categorize.add_argument("category")
    categorize.add_argument("--tag", action="append")
    _commit_options(categorize)
    categorize.set_defaults(func=cmd_categorize)

    tag = sub.add_parser("tag", parents=[common], help="add manual tags")
    tag.add_argument("ref")
    tag.add_argument("tags", nargs="+")
    _commit_options(tag)
    tag.set_defaults(func=cmd_tag)

    untag = sub.add_parser("untag", parents=[common], help="remove manual tags")
    untag.add_argument("ref")
    untag.add_argument("tags", nargs="+")
    _commit_options(untag)
    untag.set_defaults(func=cmd_untag)

    recat = sub.add_parser("recategorize", parents=[common],
                           help="re-apply the topic rules")
    recat.add_argument("--force", action="store_true",
                       help="also change manual categories")
    recat.add_argument("--dry-run", action="store_true")
    _commit_options(recat)
    recat.set_defaults(func=cmd_recategorize)

    categories = sub.add_parser("categories", parents=[common],
                                help="list categories and their chat counts")
    categories.add_argument("--json", action="store_true")
    categories.set_defaults(func=cmd_categories)

    rename = sub.add_parser("rename-category", parents=[common],
                            help="rename a category directory")
    rename.add_argument("old")
    rename.add_argument("new")
    _commit_options(rename)
    rename.set_defaults(func=cmd_rename_category)

    remove = sub.add_parser("remove", parents=[common], help="delete one chat")
    remove.add_argument("ref")
    _commit_options(remove)
    remove.set_defaults(func=cmd_remove)

    reload_parser = sub.add_parser("reload", parents=[common],
                                   help="re-fetch one chat from its provider")
    reload_parser.add_argument("ref")
    _commit_options(reload_parser)
    reload_parser.set_defaults(func=cmd_reload)

    relink = sub.add_parser("relink", parents=[common],
                            help="regenerate the months/ symlink view")
    _commit_options(relink)
    relink.set_defaults(func=cmd_relink)

    verify = sub.add_parser("verify", parents=[common],
                            help="check frontmatter, paths and links")
    verify.add_argument("--fix", action="store_true")
    _commit_options(verify)
    verify.set_defaults(func=cmd_verify)

    migrate = sub.add_parser("migrate", parents=[common],
                             help="import the legacy ai-save cache")
    migrate.add_argument("--dry-run", action="store_true")
    migrate.add_argument("--keep-old", action="store_true",
                         help="keep .ai/chat-meta and .ai/chat-content")
    _commit_options(migrate)
    migrate.set_defaults(func=cmd_migrate)

    providers = sub.add_parser("providers", parents=[common],
                               help="list the supported providers")
    providers.add_argument("--json", action="store_true")
    providers.set_defaults(func=cmd_providers)

    config = sub.add_parser("config", parents=[common],
                            help="show or edit .ai/config.json")
    config.add_argument("--edit", action="store_true")
    config.add_argument("--json", action="store_true")
    config.set_defaults(func=cmd_config)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except LookupError as exc:
        return _fail(str(exc))
    except session.SessionError as exc:
        return _fail(f"session: {exc}")
    except KeyboardInterrupt:
        return 130
    except (RuntimeError, OSError) as exc:
        return _fail(str(exc))
