#!/usr/bin/env python3
"""ai.gap — start a new GapGPT chat from a prompt or stdin.

Replays the signed-in gapgpt.app session over its own WebSocket protocol
(``wss://gapgpt.app/ws/salam``) — no API key.
See ``ai.gap --help``.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))

from ai.cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main("gap"))
