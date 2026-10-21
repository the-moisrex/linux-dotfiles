"""Read and write the JSON-encoded frontmatter used by the store.

Values are written as JSON (``title: "..."``, ``tags: ["a", "b"]``), which is
valid YAML for other tools but needs no YAML dependency here.  Reading only
looks at the first few kilobytes, which keeps ``ai.save list``/``last`` fast on
a directory with thousands of chats.
"""

from __future__ import annotations

import json

#: How much of a file to read before deciding the frontmatter is malformed.
HEAD_BYTES = 16 * 1024

OPEN = "---\n"
CLOSE = "\n---\n"


def dumps(meta):
    """Render *meta* as a frontmatter block (without the body)."""
    lines = ["---"]
    for key, value in meta.items():
        if value is None or value == "":
            continue
        lines.append(f"{key}: {json.dumps(value, ensure_ascii=False)}")
    lines.append("---")
    return "\n".join(lines) + "\n"


def loads(text):
    """Parse the frontmatter block at the start of *text*.

    Returns ``(meta, body)``; a file without frontmatter yields ``({}, text)``.
    """
    if not text.startswith(OPEN):
        return {}, text
    end = text.find(CLOSE, len(OPEN))
    if end < 0:
        return {}, text
    header = text[len(OPEN):end]
    body = text[end + len(CLOSE):]
    meta = {}
    for line in header.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        key, separator, raw = line.partition(": ")
        if not separator:
            continue
        key = key.strip()
        raw = raw.strip()
        if not key:
            continue
        try:
            meta[key] = json.loads(raw)
        except ValueError:
            meta[key] = raw
    return meta, body


def _read_head(path, limit=HEAD_BYTES):
    """Read enough of *path* to cover its frontmatter."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read(limit)
        while text.startswith(OPEN) and text.find(CLOSE, len(OPEN)) < 0:
            more = handle.read(limit)
            if not more:
                break
            text += more
    return text


def read(path):
    """Read one file's frontmatter and full body."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        return loads(handle.read())


def read_meta(path):
    """Read only the frontmatter of *path* (the cheap scan path)."""
    meta, _ = loads(_read_head(path))
    return meta


def write(path, meta, body):
    """Write *meta* + *body* to *path* atomically."""
    import os
    import tempfile

    directory = os.path.dirname(path)
    os.makedirs(directory, exist_ok=True)
    text = dumps(meta) + "\n" + body
    descriptor, temporary = tempfile.mkstemp(prefix=".ai-save-", dir=directory)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(text)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise
