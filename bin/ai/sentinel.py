"""ChatGPT's sentinel gate: requirements token, proof-of-work and turnstile.

Before ``/backend-api/f/conversation`` accepts a turn, the web client has to
pass ``/backend-api/sentinel/chat-requirements/prepare`` → ``finalize``:

* a **requirements token** (``gAAAAAC…``) — the base64 of a 25-element browser
  fingerprint, sent as ``{"p": …}`` to ``prepare``;
* a **proof-of-work** (``gAAAAAB…``) when ``prepare`` demands one: the client
  searches for the attempt whose hash of ``seed + answer`` falls below the
  server's ``difficulty``;
* a **turnstile** token when ``prepare`` ships a ``dx`` program: a small stack
  machine the client decodes (XOR keyed by the requirements token) and runs
  until it emits its result.

Everything here is a faithful port of the site's own client code (confirmed
against a ``bin/browser.record`` capture and the published reverse-engineering
write-up).  The proof-of-work has two historical hashes; the site currently
uses the FNV-based :func:`zvt`, with :func:`_sha3_digest` kept for the older
challenge shape.
"""

from __future__ import annotations

import base64
import hashlib
import json
import random
import time
import uuid
from datetime import datetime

#: The proof-of-work hash the server currently verifies (``zvt`` is the client's
#: minified name for the FNV-1a + murmur finaliser mix).
PROOF_HASH = "zvt"

REQUEST_PREFIX = "gAAAAAC"   # requirements token
PROOF_PREFIX = "gAAAAAB"     # proof token

# Browser-shaped values the fingerprint draws from, so the array looks like the
# one a real client produces.  The server hashes, not inspects, these.
_SCREENS = ((1920, 1080), (1440, 900), (2560, 1440), (3840, 2160))
_CORES = (8, 16, 24, 32)
_DOCUMENT_KEYS = ("__reactContainer$fzelfjyxej8", "_reactListening5dehydibo78", "location")
_NAVIGATOR_KEYS = (
    "registerProtocolHandler−function registerProtocolHandler() { [native code] }",
    "storage−[object StorageManager]",
    "locks−[object LockManager]",
    "appCodeName−Mozilla",
    "permissions−[object Permissions]",
    "share−function share() { [native code] }",
    "webdriver−false",
    "managed−[object NavigatorManagedData]",
    "canShare−function canShare() { [native code] }",
    "vendor−Google Inc.",
    "mediaDevices−[object MediaDevices]",
    "vibrate−function vibrate() { [native code] }",
    "storageBuckets−[object StorageBucketManager]",
    "mediaCapabilities−[object MediaCapabilities]",
    "cookieEnabled−true",
    "virtualKeyboard−[object VirtualKeyboard]",
    "product−Gecko",
    "presentation−[object Presentation]",
    "onLine−true",
    "mimeTypes−[object MimeTypeArray]",
    "credentials−[object CredentialsContainer]",
    "serviceWorker−[object ServiceWorkerContainer]",
    "keyboard−[object Keyboard]",
    "gpu−[object GPU]",
    "doNotTrack",
    "serial−[object Serial]",
    "pdfViewerEnabled−true",
    "language−zh-CN",
    "geolocation−[object Geolocation]",
    "userAgentData−[object NavigatorUAData]",
    "getUserMedia−function getUserMedia() { [native code] }",
    "sendBeacon−function sendBeacon() { [native code] }",
    "hardwareConcurrency−32",
    "windowControlsOverlay−[object WindowControlsOverlay]",
)
_WINDOW_KEYS = (
    "0", "window", "self", "document", "name", "location", "customElements",
    "history", "navigation", "innerWidth", "innerHeight", "scrollX", "scrollY",
    "visualViewport", "screenX", "screenY", "outerWidth", "outerHeight",
    "devicePixelRatio", "screen", "chrome", "navigator", "onresize",
    "performance", "crypto", "indexedDB", "sessionStorage", "localStorage",
    "scheduler", "alert", "atob", "btoa", "fetch", "matchMedia", "postMessage",
    "queueMicrotask", "requestAnimationFrame", "setInterval", "setTimeout",
    "caches", "__NEXT_DATA__", "__BUILD_MANIFEST", "__NEXT_PRELOADREADY",
)


class SentinelError(Exception):
    """The sentinel gate rejected us, or a puzzle could not be solved."""


def _parse_time():
    now = datetime.now().astimezone()
    offset = now.strftime("%z") or "+0000"
    return (now.strftime("%a %b %d %Y %H:%M:%S")
            + f" GMT{offset} ({now.tzname() or 'UTC'})")


def build_fingerprint(user_agent, script_source=None, data_build=""):
    """The 25-element array the client serialises into the ``p`` token."""
    now = time.perf_counter() * 1000
    return [
        sum(random.choice(_SCREENS)),
        _parse_time(),
        4294705152,                       # jsHeapSizeLimit (4 GiB, rounded)
        1,                                # attempt (rewritten by the solver)
        user_agent,
        script_source,
        data_build,
        "en-US",
        "en-US,en",
        random.random(),                  # elapsed ms (rewritten by the solver)
        random.choice(_NAVIGATOR_KEYS),
        random.choice(_DOCUMENT_KEYS),
        random.choice(_WINDOW_KEYS),
        now,                              # performance.now()
        str(uuid.uuid4()),                # session id
        "",
        random.choice(_CORES),
        time.time() * 1000 - now,         # performance.timeOrigin
        0, 0, 0, 0, 0, 0,
        1 if "Firefox" in (user_agent or "") else 0,
    ]


def requirements_token(fingerprint):
    """``gAAAAAC`` + base64(fingerprint) — the value sent as ``{"p": …}``."""
    return REQUEST_PREFIX + _encode(fingerprint)


def _encode(value):
    return base64.b64encode(json.dumps(value, separators=(",", ":"),
                                       ensure_ascii=False).encode()).decode()


# --------------------------------------------------------------------- hashes

def zvt(text):
    """The client's current proof-of-work hash (FNV-1a + murmur finaliser)."""
    value = 2166136261
    for char in text:
        value ^= ord(char)
        value = (value * 16777619) & 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 2246822507) & 0xFFFFFFFF
    value ^= value >> 13
    value = (value * 3266489909) & 0xFFFFFFFF
    value ^= value >> 16
    return format(value & 0xFFFFFFFF, "08x")


def _sha3_digest(data):
    return hashlib.sha3_512(data).digest()


# ------------------------------------------------------------------ proof-of-work

def solve_proof(seed, difficulty, fingerprint, algo=PROOF_HASH, limit=500000):
    """Return the ``gAAAAAB…`` proof token for *seed* / *difficulty*."""
    if not seed or not difficulty:
        raise SentinelError("the proof-of-work challenge is malformed")
    if algo == "sha3":
        return _solve_proof_sha3(seed, difficulty, fingerprint, limit)
    return _solve_proof_zvt(seed, difficulty, fingerprint, limit)


def _solve_proof_zvt(seed, difficulty, fingerprint, limit):
    length = len(difficulty)
    for attempt in range(limit):
        config = list(fingerprint)
        config[3] = attempt
        config[9] = 0
        answer = _encode(config)
        if zvt(seed + answer)[:length] <= difficulty:
            return PROOF_PREFIX + answer + "~S"
    raise SentinelError(f"proof-of-work unsolved after {limit} attempts")


def _solve_proof_sha3(seed, difficulty, fingerprint, limit):
    target = bytes.fromhex(difficulty)
    diff_len = len(difficulty) // 2
    head = (json.dumps(fingerprint[:3], separators=(",", ":"),
                       ensure_ascii=False)[:-1] + ",").encode()
    middle = ("," + json.dumps(fingerprint[4:9], separators=(",", ":"),
                               ensure_ascii=False)[1:-1] + ",").encode()
    tail = ("," + json.dumps(fingerprint[10:], separators=(",", ":"),
                             ensure_ascii=False)[1:]).encode()
    seed_bytes = seed.encode()
    for attempt in range(limit):
        final = head + str(attempt).encode() + middle + str(attempt >> 1).encode() + tail
        encoded = base64.b64encode(final)
        if _sha3_digest(seed_bytes + encoded)[:diff_len] <= target:
            return PROOF_PREFIX + encoded.decode()
    raise SentinelError(f"proof-of-work unsolved after {limit} attempts")


# ------------------------------------------------------------------ turnstile

class _OrderedMap:
    def __init__(self):
        self.keys = []
        self.values = {}

    def add(self, key, value):
        if key not in self.values:
            self.keys.append(key)
        self.values[key] = value


def _to_str(value):
    if value is None:
        return "undefined"
    if isinstance(value, float):
        return str(value)
    if isinstance(value, str):
        return _NATIVE.get(value, value)
    if isinstance(value, list) and all(isinstance(item, str) for item in value):
        return ",".join(value)
    return str(value)


_NATIVE = {
    "window.Math": "[object Math]",
    "window.Reflect": "[object Reflect]",
    "window.performance": "[object Performance]",
    "window.localStorage": "[object Storage]",
    "window.Object": "function Object() { [native code] }",
    "window.Reflect.set": "function set() { [native code] }",
    "window.performance.now": "function () { [native code] }",
    "window.Object.create": "function create() { [native code] }",
    "window.Object.keys": "function keys() { [native code] }",
    "window.Math.random": "function random() { [native code] }",
}

_LOCAL_STORAGE_KEYS = [
    "STATSIG_LOCAL_STORAGE_INTERNAL_STORE_V4",
    "STATSIG_LOCAL_STORAGE_STABLE_ID",
    "client-correlated-secret",
    "oai/apps/capExpiresAt",
    "oai-did",
    "STATSIG_LOCAL_STORAGE_LOGGING_REQUEST",
    "UiState.isNavigationCollapsed.1",
]


def _xor(text, key):
    if not key:
        return text
    return "".join(chr(ord(char) ^ ord(key[i % len(key)]))
                   for i, char in enumerate(text))


def solve_turnstile(dx, key):
    """Decode and run a ``dx`` program; return its base64 result (or None)."""
    try:
        decoded = base64.b64decode(dx).decode()
        program = json.loads(_xor(decoded, key))
    except Exception:  # noqa: BLE001 - a malformed dx simply yields no token
        return None
    if not isinstance(program, list):
        return None

    memory = {}
    started = time.time()
    result = ""

    def op_xor(target, source):
        memory[target] = _xor(_to_str(memory[target]), _to_str(memory[source]))

    def op_assign(target, value):
        memory[target] = value

    def op_result(value):
        nonlocal result
        result = base64.b64encode(_to_str(value).encode()).decode()

    def op_concat(target, source):
        current = memory[target]
        incoming = memory[source]
        if isinstance(current, (list, tuple)):
            memory[target] = list(current) + [incoming]
        elif isinstance(current, (str, float)) or isinstance(incoming, (str, float)):
            memory[target] = _to_str(current) + _to_str(incoming)
        else:
            memory[target] = "NaN"

    def op_join(target, left, right):
        a, b = memory[left], memory[right]
        if isinstance(a, str) and isinstance(b, str):
            value = f"{a}.{b}"
            memory[target] = ("https://chatgpt.com/"
                              if value == "window.document.location" else value)

    def op_call(target, *args):
        callee = memory[target]
        values = [memory[arg] for arg in args]
        if callee == "window.Reflect.set":
            values[0].add(str(values[1]), values[2])
        elif callable(callee):
            callee(*values)

    def op_copy(target, source):
        memory[target] = memory[source]

    def op_parse(target, source):
        memory[target] = json.loads(memory[source])

    def op_stringify(target, source):
        memory[target] = json.dumps(memory[source])

    def op_invoke(target, source, *args):
        values = [memory[arg] for arg in args]
        callee = memory[source]
        if callee == "window.performance.now":
            elapsed = time.time_ns() - int(started * 1e9)
            memory[target] = (elapsed + random.random()) / 1e6
        elif callee == "window.Object.create":
            memory[target] = _OrderedMap()
        elif callee == "window.Object.keys":
            if values and values[0] == "window.localStorage":
                memory[target] = list(_LOCAL_STORAGE_KEYS)
        elif callee == "window.Math.random":
            memory[target] = random.random()
        elif callable(callee):
            memory[target] = callee(*values)

    def op_atob(target):
        memory[target] = base64.b64decode(_to_str(memory[target])).decode()

    def op_btoa(target):
        memory[target] = base64.b64encode(_to_str(memory[target]).encode()).decode()

    def op_if_equal(target, other, action, *args):
        if memory[target] == memory[other]:
            callee = memory[action]
            if callable(callee):
                callee(*[memory[arg] for arg in args])

    def op_noop(*_):
        return

    def op_call_if(target, source, *args):
        if memory[target] is not None and callable(memory[source]):
            memory[source](*args)

    def op_join_str(target, left, right):
        a, b = memory[left], memory[right]
        if isinstance(a, str) and isinstance(b, str):
            memory[target] = f"{a}.{b}"

    memory.update({
        1: op_xor, 2: op_assign, 3: op_result, 5: op_concat, 6: op_join,
        7: op_call, 8: op_copy, 9: program, 10: "window", 14: op_parse,
        15: op_stringify, 16: key, 17: op_invoke, 18: op_atob, 19: op_btoa,
        20: op_if_equal, 21: op_noop, 23: op_call_if, 24: op_join_str,
    })

    for token in program:
        if not isinstance(token, list) or not token:
            continue
        func = memory.get(token[0])
        if callable(func):
            try:
                func(*token[1:])
            except Exception:  # noqa: BLE001 - one bad op must not abort the run
                continue
    return result or None
