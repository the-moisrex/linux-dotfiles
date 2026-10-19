"""Local attachments for ``--file``: read, name and type a path.

Each provider uploads the bytes itself (the flow differs per site), so this
module only turns a path into the ``(name, bytes, mime)`` triple they all need,
with one shared sanity check on size.
"""

from __future__ import annotations

import mimetypes
import os
from dataclasses import dataclass

#: Refuse anything larger than this up front (the sites' own caps are looser).
MAX_SIZE = 100 * 1024 * 1024


class FileError(Exception):
    """An attachment could not be read or is not acceptable."""


@dataclass
class Attachment:
    path: str
    name: str
    data: bytes
    mime: str

    @property
    def size(self):
        return len(self.data)


def read(path):
    """Read one attachment, raising :class:`FileError` when it is unusable."""
    full = os.path.expanduser(path)
    if not os.path.exists(full):
        raise FileError(f"{path}: no such file")
    if not os.path.isfile(full):
        raise FileError(f"{path}: not a regular file")
    try:
        with open(full, "rb") as handle:
            data = handle.read(MAX_SIZE + 1)
    except OSError as exc:
        raise FileError(f"{path}: {exc}") from exc
    if not data:
        raise FileError(f"{path}: the file is empty")
    if len(data) > MAX_SIZE:
        raise FileError(f"{path}: larger than {MAX_SIZE // (1024 * 1024)} MiB")
    name = os.path.basename(full) or "file"
    mime = mimetypes.guess_type(name)[0] or "application/octet-stream"
    return Attachment(full, name, data, mime)


def read_all(paths):
    """Read every path, preserving order."""
    return [read(path) for path in (paths or ())]
