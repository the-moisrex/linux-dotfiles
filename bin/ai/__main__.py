"""Allow ``python3 -m ai`` (from a directory containing this package)."""

from __future__ import annotations

import sys

from .cli import main

if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "chatgpt"
    sys.exit(main(name, sys.argv[2:]))
