"""Gemini (gemini.google.com) adapter.

Replays the signed-in gemini.google.com session against the web app's private
``StreamGenerate`` RPC.  A turn is a form-encoded request whose ``f.req``
carries the prompt inside an 81-slot JSPB array; the answer streams back as
Google's length-prefixed frames (``)]}'``, then ``<length>\\n<json>``), and each
frame's ``[2]`` slot holds another JSON document whose ``[4]`` candidates carry
the text (``[1][0]``) and the model's thoughts (``[37][0][0]``).

The server keeps the transcript, so a follow-up only sends the new message plus
the conversation metadata the previous turn returned (``[1]`` on the first
frames, ``[25]`` once the turn is stored).  ``-c``/``-i``/``.ai.reply`` resume
from that metadata, and the chat id (``c_…``) is remembered as the conversation
id.

``--file`` uploads go to ``content-push.googleapis.com/upload`` (the push id
comes from the page) and the returned identifiers travel in the message's
``[3]`` slot.  ``ai.save`` lists chats with the ``MaZiqc`` RPC and reads one
with ``hNvQHb``.

The access token (``SNlM0e``), build label (``cfb2h``), session id
(``FdrFJe``), language (``TuX5cc``) and push id (``qKIAYe``) are read from the
``/app`` page with the session's cookies; there is no API key.
"""

from __future__ import annotations

import codecs
import json
import random
import re
import uuid
from urllib.parse import urlencode

from .. import events as ev
from .. import files as files_mod
from .. import history
from .. import net
from .. import session as session_mod
from . import Conversation, Provider

INIT_PATH = "/app"
GENERATE_URL = ("https://gemini.google.com/_/BardChatUi/data/"
                "assistant.lamda.BardFrontendService/StreamGenerate")
BATCH_URL = "https://gemini.google.com/_/BardChatUi/data/batchexecute"
UPLOAD_URL = "https://content-push.googleapis.com/upload"

#: Google's RPC ids for the history calls `ai.save` uses.
LIST_CONVERSATIONS = "MaZiqc"
LIST_CONVERSATION_TURNS = "hNvQHb"

#: The blank conversation metadata a new chat starts with.
DEFAULT_METADATA = ["", "", "", None, None, None, None, None, None, ""]

#: The model selector header and the (id, capacity, number) triple each model
#: needs.  The id is Google's internal model id; the number is mirrored into
#: the request body.  ``auto`` sends no header and lets Google pick.
MODEL_HEADER_KEY = "x-goog-ext-525001261-jspb"
MODEL_SPECS = {
    "gemini-pro": ("9d8ca3786ebdfbea", 1, 3),
    "gemini-flash": ("fbb127bbb056c959", 1, 1),
    "gemini-flash-lite": ("cf41b0e0dd7d53e5", 1, 6),
}
MODEL_ALIASES = {
    "auto": "auto",
    "default": "auto",
    "gemini": "gemini-pro",
    "pro": "gemini-pro",
    "flash": "gemini-flash",
    "lite": "gemini-flash-lite",
    "flash-lite": "gemini-flash-lite",
}

#: The batch-execute selector header (no model chosen, only the session id).
BATCH_MODEL_HEADER = ("[1,null,null,null,null,null,null,null,[4,5,6,8],"
                      "null,null,null,null,null,null,null]")

_ACCESS_TOKEN_RE = re.compile(r'"SNlM0e":\s*"(.*?)"')
_BUILD_LABEL_RE = re.compile(r'"cfb2h":\s*"(.*?)"')
_SESSION_ID_RE = re.compile(r'"FdrFJe":\s*"(.*?)"')
_LANGUAGE_RE = re.compile(r'"TuX5cc":\s*"(.*?)"')
_PUSH_ID_RE = re.compile(r'"qKIAYe":\s*"(.*?)"')
_CARD_CONTENT_RE = re.compile(r"^https?://googleusercontent\.com/card_content/\d+")
_ARTIFACTS_RE = re.compile(r"https?://googleusercontent\.com/(?:\w+/)+\d+\n*")
_LENGTH_RE = re.compile(r"(\d+)\n")


def _nested(data, path, default=None):
    """Read ``path`` out of nested lists/dicts, returning *default* on a miss."""
    current = data
    for key in path:
        if isinstance(current, list) and isinstance(key, int) and -len(current) <= key < len(current):
            current = current[key]
        elif isinstance(current, dict) and key in current:
            current = current[key]
        else:
            return default
    return default if current is None else current


def _delta(new, old):
    """The part of *new* that is not already in *old*.

    The common prefix and suffix are kept, so an insertion in the middle
    ("Hello world" -> "Hello brave world") emits just the inserted text while
    an ordinary streaming extension emits the new tail.
    """
    if new == old:
        return ""
    if not old:
        return new
    prefix = 0
    limit = min(len(new), len(old))
    while prefix < limit and new[prefix] == old[prefix]:
        prefix += 1
    suffix = 0
    limit -= prefix
    while suffix < limit and new[-1 - suffix] == old[-1 - suffix]:
        suffix += 1
    return new[prefix:len(new) - suffix]


class _FrameParser:
    """Incrementally split Google's ``<length>\\n<json>`` stream frames.

    The length marker counts UTF-16 code units, like JavaScript's string
    length, so astral characters (emoji) count as two.
    """

    def __init__(self):
        self.buffer = ""
        self.expected = None
        self.start = 0
        self.scanned_chars = 0
        self.scanned_units = 0
        self.prefix_checked = False

    def flush(self):
        return self.feed("")

    def feed(self, text):
        if text:
            self.buffer += text
        if not self.prefix_checked:
            prefix = ")]}'"
            if len(self.buffer) < len(prefix) and prefix.startswith(self.buffer):
                return []
            if self.buffer.startswith(prefix):
                self.buffer = self.buffer[len(prefix):].lstrip()
            self.prefix_checked = True

        frames = []
        while True:
            if self.expected is None:
                self.buffer = self.buffer.lstrip()
                match = _LENGTH_RE.match(self.buffer)
                if not match:
                    break
                self.expected = int(match.group(1))
                # The declared length counts from the newline after the digits
                # through the frame's own trailing newline, so the payload
                # slice starts right after the digits (the newline included).
                self.start = len(match.group(1))
                self.scanned_chars = 0
                self.scanned_units = 0

            index = self.start + self.scanned_chars
            while self.scanned_units < self.expected and index < len(self.buffer):
                units = 2 if ord(self.buffer[index]) > 0xFFFF else 1
                if self.scanned_units + units > self.expected:
                    break
                self.scanned_units += units
                self.scanned_chars += 1
                index += 1
            if self.scanned_units < self.expected:
                break

            end = self.start + self.scanned_chars
            chunk = self.buffer[self.start:end]
            self.buffer = self.buffer[end:]
            self.expected = None
            self.start = 0
            self.scanned_chars = 0
            self.scanned_units = 0
            if not chunk.strip():
                continue
            try:
                parsed = json.loads(chunk)
            except ValueError:
                continue
            if isinstance(parsed, list):
                frames.extend(parsed)
            else:
                frames.append(parsed)
        return frames


class _Parser:
    """Turn ``StreamGenerate`` frames into text/thinking deltas."""

    def __init__(self):
        self.frames = _FrameParser()
        self.cid = ""
        self.rid = ""
        self.rcid = ""
        self.metadata = None
        self.full_text = ""
        self.full_thoughts = ""
        self.done = False
        self.error = ""
        self._last_text = {}
        self._last_thoughts = {}

    def feed(self, text):
        events = []
        for part in self.frames.feed(text):
            events.extend(self._part(part))
        return events

    def flush(self):
        return self.feed("")

    def _part(self, part):
        if not isinstance(part, list):
            return []
        error_code = _nested(part, [5, 2, 0, 1, 0])
        if isinstance(error_code, int) and error_code:
            self.error = _error_message(error_code)
            return [ev.error(self.error)]

        inner = _nested(part, [2])
        if not isinstance(inner, str) or not inner:
            return []
        try:
            data = json.loads(inner)
        except ValueError:
            return []

        metadata = _nested(data, [1])
        if isinstance(metadata, list):
            if len(metadata) > 0 and metadata[0]:
                self.cid = metadata[0]
            if len(metadata) > 1 and metadata[1]:
                self.rid = metadata[1]
            self.metadata = metadata

        context = _nested(data, [25])
        if isinstance(context, str):
            self.metadata = [None] * 9 + [context]
            self.done = True

        events = []
        candidates = _nested(data, [4], [])
        if not isinstance(candidates, list):
            return events
        for index, candidate in enumerate(candidates):
            if not isinstance(candidate, list):
                continue
            rcid = _nested(candidate, [0])
            if not rcid:
                continue
            self.rcid = rcid
            text, thoughts = _candidate_text(candidate)
            indicator = _nested(candidate, [8, 0])
            is_final = indicator == 2

            key = rcid or f"idx_{index}"
            delta = _delta(text, self._last_text.get(key, ""))
            thought_delta = _delta(thoughts, self._last_thoughts.get(key, ""))
            self._last_text[key] = text
            self._last_thoughts[key] = thoughts
            if index == 0:
                self.full_text = text
                self.full_thoughts = thoughts
            if delta:
                events.append(ev.text(delta))
            if thought_delta:
                events.append(ev.thinking(thought_delta))
            if is_final and text:
                self.done = True
        return events


def _candidate_text(candidate):
    """The answer and reasoning text of one candidate."""
    text = _nested(candidate, [1, 0], "")
    if not isinstance(text, str):
        text = ""
    if _CARD_CONTENT_RE.match(text):
        text = _nested(candidate, [22, 0]) or text
    text = _ARTIFACTS_RE.sub("", text)
    thoughts = _nested(candidate, [37, 0, 0], "")
    if not isinstance(thoughts, str):
        thoughts = ""
    return text, thoughts


def _error_message(code):
    return {
        1013: "Gemini hit a temporary error (1013); try again",
        1037: "Gemini usage limit exceeded; switch model or wait for the quota to reset",
        1050: "the selected model is inconsistent with this conversation",
        1052: "the selected model is unavailable or the request shape is outdated",
        1060: "this IP is temporarily blocked by Google; try another network or wait",
    }.get(code, f"Gemini returned error code {code}")


def _model_spec(model):
    """Resolve ``--model`` to ``(header, model_number)``, or ``(None, 1)``."""
    key = MODEL_ALIASES.get((model or "auto").strip().lower(), (model or "").strip().lower())
    if key in ("", "auto"):
        return None, 1
    spec = MODEL_SPECS.get(key)
    if spec is None:
        return None, 1
    ident, capacity, number = spec
    header = [1, None, None, None, ident, None, None, 0, [4, 5, 6, 8],
              None, None, capacity, None, None, number]
    return header, number


class Gemini(Provider):
    name = "gemini"
    label = "Gemini"
    origin = "https://gemini.google.com"
    session_target = "gemini.google.com"
    default_model = "auto"
    models = ("auto", "gemini-pro", "gemini-flash", "gemini-flash-lite")
    #: A single message comfortably takes hundreds of KiB; the site's own
    #: context window is the real limit.
    inline_limit = 256 * 1024

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)

    # -- session -----------------------------------------------------------

    def _base_headers(self, match):
        return session_mod.build_headers(match, {
            "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            "Referer": self.origin + "/",
        })

    def _init(self, match):
        """Read the page's access token, build label, session id and push id."""
        cached = getattr(self, "_init_cache", None)
        if cached is not None and cached[0] is match:
            return dict(cached[1])
        text = net.request_text(self.origin + INIT_PATH,
                                headers=self._base_headers(match), timeout=60)
        tokens = {}
        for key, pattern in (
            ("at", _ACCESS_TOKEN_RE),
            ("bl", _BUILD_LABEL_RE),
            ("sid", _SESSION_ID_RE),
            ("language", _LANGUAGE_RE),
            ("push_id", _PUSH_ID_RE),
        ):
            found = pattern.search(text)
            tokens[key] = found.group(1) if found else ""
        self._init_cache = (match, tokens)
        return dict(tokens)

    def _upload(self, headers, attachment, push_id):
        """Upload one attachment and return Google's file identifier."""
        body, content_type = net.encode_multipart([
            ("file", (attachment.name, attachment.data, attachment.mime)),
        ])
        request_headers = {
            key: value for key, value in headers.items()
            if key.lower() not in ("content-type", "content-length", "accept")
        }
        request_headers.update({
            "Content-Type": content_type,
            "X-Tenant-Id": "bard-storage",
            "Push-ID": push_id or "feeds/mcudyrk2a4khkz",
            "Origin": self.origin,
            "Referer": self.origin + "/",
        })
        status, response = net.request_raw(
            UPLOAD_URL, method="POST", headers=request_headers, data=body, timeout=300)
        if status >= 400:
            raise net.StreamError(
                f"upload failed: HTTP {status}: {response[:200]!r}")
        identifier = response.decode("utf-8", "replace").strip()
        if not identifier:
            raise net.StreamError("upload failed: the server returned no file id")
        return identifier

    # -- history (used by ai.save) ----------------------------------------

    def history_headers(self, match):
        cached = getattr(self, "_history_headers", None)
        if cached is not None and getattr(self, "_history_match", None) is match:
            return dict(cached)
        headers = self._base_headers(match)
        self._history_headers = headers
        self._history_match = match
        return dict(headers)

    def _batch(self, match, rpcid, payload, source_path="/app"):
        """Run one ``batchexecute`` RPC and return its parsed bodies."""
        tokens = self._init(match)
        headers = self.history_headers(match)
        headers.update({
            "Accept": "*/*",
            "Content-Type": "application/x-www-form-urlencoded;charset=utf-8",
            "Origin": self.origin,
            "Referer": self.origin + source_path,
            "X-Same-Domain": "1",
            "x-goog-ext-73010989-jspb": "[0]",
            MODEL_HEADER_KEY: BATCH_MODEL_HEADER[:-1] + ',"' + str(uuid.uuid4()).upper() + '"]',
        })
        params = {
            "rpcids": rpcid,
            "hl": tokens.get("language") or "en",
            "_reqid": str(random.randint(10000, 99999)),
            "rt": "c",
            "source-path": source_path,
        }
        if tokens.get("bl"):
            params["bl"] = tokens["bl"]
        if tokens.get("sid"):
            params["f.sid"] = tokens["sid"]
        body = urlencode({
            "at": tokens.get("at") or "",
            "f.req": json.dumps([[[rpcid, payload, None, "generic"]]]),
        }).encode()
        text = net.request_text(
            BATCH_URL + "?" + urlencode(params), method="POST",
            headers=headers, data=body, timeout=120)
        return _batch_bodies(text, rpcid)

    def iter_chat_summaries(self, match, limit=None):
        """Walk the account's recent chats (two RPC passes, newest first)."""
        seen = set()
        count = 0
        for flag in (1, 0):
            payload = json.dumps([13, None, [flag, None, 1]])
            try:
                bodies = self._batch(match, LIST_CONVERSATIONS, payload)
            except net.StreamError:
                continue
            for body in bodies:
                chats = _nested(body, [2], [])
                if not isinstance(chats, list):
                    continue
                for chat in chats:
                    if not isinstance(chat, list):
                        continue
                    cid = _nested(chat, [0], "")
                    if not cid or cid in seen:
                        continue
                    seen.add(cid)
                    timestamp = _nested(chat, [5])
                    updated = ""
                    if isinstance(timestamp, list) and len(timestamp) >= 2:
                        try:
                            updated = history.iso_from_epoch(
                                float(timestamp[0]) + float(timestamp[1]) / 1e9)
                        except (TypeError, ValueError):
                            updated = ""
                    yield history.ChatSummary(
                        provider=self.name,
                        remote_id=cid,
                        title=_nested(chat, [1], ""),
                        created=updated,
                        updated=updated,
                        model=self.default_model,
                        url=f"{self.origin}/app/{cid}",
                    )
                    count += 1
                    if limit and count >= limit:
                        return
                break

    def fetch_chat(self, match, remote_id):
        """Fetch one chat's turns through ``hNvQHb``."""
        payload = json.dumps([remote_id, 10, None, 1, [1], [4], None, 1])
        bodies = self._batch(match, LIST_CONVERSATION_TURNS, payload)
        chat = history.Chat(
            provider=self.name, remote_id=remote_id,
            url=f"{self.origin}/app/{remote_id}")
        turns = []
        for body in bodies:
            turns = _nested(body, [0], [])
            if turns:
                break
        if not isinstance(turns, list):
            return chat
        for turn in reversed(turns):
            if not isinstance(turn, list):
                continue
            rid = _nested(turn, [0, 1], "")
            user_text = _nested(turn, [2, 0, 0], "")
            if isinstance(user_text, str) and user_text:
                chat.messages.append(history.Message(
                    role=history.USER, text=user_text, message_id=rid))
            candidates = _nested(turn, [3, 0], [])
            if isinstance(candidates, list):
                for candidate in candidates:
                    if not isinstance(candidate, list):
                        continue
                    text, thoughts = _candidate_text(candidate)
                    rcid = _nested(candidate, [0], "")
                    if text:
                        chat.messages.append(history.Message(
                            role=history.ASSISTANT, text=text,
                            message_id=rcid or rid))
                    if thoughts:
                        chat.messages.append(history.Message(
                            role=history.ASSISTANT, text=thoughts,
                            kind=history.THINKING, message_id=rcid or rid))
        if chat.messages:
            chat.created = chat.messages[0].created
            chat.updated = history.newest(
                message.created for message in chat.messages)
        return chat


def _batch_bodies(text, rpcid):
    """The decoded ``[2]`` bodies of the parts matching *rpcid*."""
    bodies = []
    for part in _parse_batch_frames(text):
        if _nested(part, [1]) != rpcid:
            continue
        raw = _nested(part, [2])
        if not isinstance(raw, str) or not raw:
            continue
        try:
            bodies.append(json.loads(raw))
        except ValueError:
            continue
    return bodies


def _parse_batch_frames(text):
    """Parse a batchexecute response (length-prefixed, with the XSSI prefix)."""
    parser = _FrameParser()
    frames = parser.feed(text)
    frames.extend(parser.flush())
    if frames:
        return frames
    stripped = text.removeprefix(")]}'").strip()
    try:
        parsed = json.loads(stripped)
    except ValueError:
        return []
    return parsed if isinstance(parsed, list) else [parsed]


class _Conversation(Conversation):
    """A Gemini chat, continued by the metadata the server returns."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        self.provider = provider
        self.metadata = (resume or {}).get("metadata") or list(DEFAULT_METADATA)
        self.cid = (resume or {}).get("cid") or conversation_id or ""
        self.rid = (resume or {}).get("rid") or ""
        self.rcid = (resume or {}).get("rcid") or ""
        self.session_id = str(uuid.uuid4()).upper()
        self.reqid = random.randint(10000, 99999)

    def send(self, prompt, files=None):
        try:
            tokens = self.provider._init(self.match)
        except net.StreamError as exc:
            yield ev.error(str(exc))
            return
        if not tokens.get("at"):
            yield ev.error(
                "no Gemini access token found; log in to gemini.google.com "
                "in the browser this session comes from")
            return

        file_data = None
        try:
            attachments = self._attachments(files)
        except files_mod.FileError as exc:
            yield ev.error(str(exc))
            return
        if attachments:
            uploaded = []
            headers = self.provider._base_headers(self.match)
            for attachment in attachments:
                try:
                    identifier = self.provider._upload(
                        headers, attachment, tokens.get("push_id"))
                except (net.StreamError, files_mod.FileError) as exc:
                    yield ev.error(str(exc))
                    return
                uploaded.append([[identifier], attachment.name])
                yield ev.tool_call(name="upload", status="done",
                                   args=attachment.name)
            file_data = uploaded

        content = f"{self.system}\n\n{prompt}" if (self.system and self.turns == 0) else prompt

        model_header, model_number = _model_spec(self.model)
        request_uuid = str(uuid.uuid4()).upper()
        inner = [None] * 81
        inner[0] = [content, 0, None, file_data, None, None, 0]
        inner[1] = [tokens.get("language") or "en"]
        inner[2] = self.metadata
        inner[6] = [1]
        inner[7] = 1
        inner[10] = 1
        inner[11] = 0
        inner[17] = [[0]]
        inner[18] = 0
        inner[27] = 1
        inner[30] = [4]
        inner[41] = [1]
        inner[53] = 0
        inner[59] = request_uuid
        inner[61] = []
        inner[68] = 1
        inner[79] = model_number
        inner[80] = 1

        headers = self.provider._base_headers(self.match)
        headers.update({
            "Accept": "*/*",
            "Content-Type": "application/x-www-form-urlencoded;charset=utf-8",
            "Origin": self.provider.origin,
            "Referer": self.provider.origin + INIT_PATH,
            "X-Same-Domain": "1",
            "x-goog-ext-525005358-jspb": f'["{request_uuid}",1]',
        })
        if model_header is not None:
            header = list(model_header)
            header.append(1)
            header.append(self.session_id)
            headers[MODEL_HEADER_KEY] = json.dumps(header)
            headers["x-goog-ext-73010989-jspb"] = "[0]"
            headers["x-goog-ext-73010990-jspb"] = "[0,0,0]"

        params = {
            "hl": tokens.get("language") or "en",
            "_reqid": str(self.reqid),
            "rt": "c",
        }
        self.reqid += 100000
        if tokens.get("bl"):
            params["bl"] = tokens["bl"]
        if tokens.get("sid"):
            params["f.sid"] = tokens["sid"]
        body = urlencode({
            "at": tokens.get("at") or "",
            "f.req": json.dumps([None, json.dumps(inner)]),
        }).encode()

        parser = _Parser()
        decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        try:
            for chunk in net.stream_chunks(
                    GENERATE_URL + "?" + urlencode(params), method="POST",
                    headers=headers, data=body, timeout=self.timeout,
                    debug=self.debug):
                for event in parser.feed(decoder.decode(chunk, final=False)):
                    yield event
                if parser.done and parser.full_text:
                    break
            for event in parser.feed(decoder.decode(b"", final=True)):
                yield event
            for event in parser.flush():
                yield event
        except net.StreamError as exc:
            yield ev.error(str(exc))
            return

        if parser.error:
            return
        if not parser.full_text.strip():
            yield ev.error(
                "Gemini returned no answer"
                + (f" (chat {parser.cid})" if parser.cid else ""))
            return

        self.cid = parser.cid or self.cid
        self.rid = parser.rid or self.rid
        self.rcid = parser.rcid or self.rcid
        self.metadata = parser.metadata or self.metadata
        if self.cid:
            self.conversation_id = self.cid
        self.resume = {
            "metadata": self.metadata,
            "cid": self.cid,
            "rid": self.rid,
            "rcid": self.rcid,
        }
        self.turns += 1
        self._record("user", prompt)
        self._record("assistant", parser.full_text)
        yield ev.done()
