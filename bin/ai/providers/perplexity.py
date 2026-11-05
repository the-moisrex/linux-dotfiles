"""Perplexity (www.perplexity.ai) adapter.

Replays the signed-in perplexity.ai session against its internal ask stream,
``POST /rest/sse/perplexity_ask``.  A turn is an SSE stream of JSON envelopes
whose ``blocks`` carry the answer incrementally: older frames patch a
``markdown_block`` with RFC-6902 diffs, current frames stream the answer as
``workflow_block`` items (``payload.text_payload.chunks``) and the plan/search
steps as ``plan_block`` goals.  The parser reconstructs the cumulative answer
from either shape and emits only the new suffix, so the renderer sees a normal
delta stream.

Continuation is by the thread's ``backend_uuid`` (plus its ``read_write_token``
when the site hands one back): the server keeps the transcript, so a follow-up
only sends the new message.  ``-c``/``-i``/``.ai.reply`` therefore resume with
that id, and the thread slug is remembered as the conversation id.

``--file`` uploads through ``POST /rest/uploads/batch_create_upload_urls``, the
returned S3 form, then ``/rest/sse/attachment_processing/subscribe``; the
resulting object URLs travel in the ask body's ``attachments``.  The subscribe
step is best-effort because Perplexity's edge sometimes refuses it for document
parsing (images still work).

``ai.save`` lists threads with ``POST /rest/thread/list_ask_threads`` and reads
one with ``GET /rest/thread/<uuid>`` (cursor paginated), using the same block
extraction as the live stream.
"""

from __future__ import annotations

import json
import os
import re
import time
import uuid
from urllib.parse import quote

from .. import diagnostics
from .. import events as ev
from .. import files as files_mod
from .. import history
from .. import net
from .. import session as session_mod
from . import Conversation, Provider

ASK_PATH = "/rest/sse/perplexity_ask"
UPLOAD_PATH = "/rest/uploads/batch_create_upload_urls"
UPLOAD_SUBSCRIBE_PATH = "/rest/sse/attachment_processing/subscribe"
LIST_PATH = "/rest/thread/list_ask_threads"
THREAD_PATH = "/rest/thread/{uuid}"
API_VERSION = "2.18"

#: How many stored turns a fresh process replays when it cannot reattach to
#: the thread id (the site keeps the transcript, so this is only a fallback).
MAX_REPLAY = 40

#: The block use-cases the current web client advertises.  The schematized API
#: validates this list, so it mirrors the browser's request body.
SUPPORTED_BLOCK_USE_CASES = [
    "answer_modes",
    "media_items",
    "knowledge_cards",
    "inline_entity_cards",
    "place_widgets",
    "finance_widgets",
    "sports_widgets",
    "news_widgets",
    "shopping_widgets",
    "jobs_widgets",
    "search_result_widgets",
    "inline_images",
    "inline_assets",
    "placeholder_cards",
    "diff_blocks",
    "inline_knowledge_cards",
    "entity_group_v2",
    "refinement_filters",
    "canvas_mode",
    "maps_preview",
    "answer_tabs",
    "price_comparison_widgets",
    "preserve_latex",
    "generic_onboarding_widgets",
    "in_context_suggestions",
    "pending_followups",
    "inline_claims",
    "unified_assets",
    "workflow_steps",
    "workflow_widgets",
    "navigation_results",
    "background_agents",
]

#: Friendly names accepted by ``--model``; the site's own ids also work.
MODEL_ALIASES = {
    "auto": "pplx_pro",
    "best": "pplx_pro",
    "sonar": "turbo",
    "sonar-pro": "experimental",
    "experimental": "experimental",
    "gpt-5": "gpt56_terra",
    "gpt-5-thinking": "gpt56_terra_thinking",
    "gpt-5-sol": "gpt56_sol",
    "gpt-5-sol-thinking": "gpt56_sol_thinking",
    "gemini": "gemini37flash",
    "gemini-thinking": "gemini37flashthinking",
    "sonnet": "claude50sonnet",
    "sonnet-thinking": "claude50sonnetthinking",
    "opus": "claude50opus",
    "opus-thinking": "claude50opusthinking",
    "grok": "grok46low",
    "grok-thinking": "grok46medium",
    "kimi": "kimik3thinking",
    "glm": "glm_5_2",
    "nemotron": "nv_nemotron_3_ultra",
    "deep-research": "pplx_alpha",
    "research": "pplx_alpha",
    "council": "pplx_agentic_research",
    "agentic-research": "pplx_agentic_research",
    "turbo": "turbo",
    "pplx_pro": "pplx_pro",
    "pplx_alpha": "pplx_alpha",
    "pplx_agentic_research": "pplx_agentic_research",
}

#: The ``variant`` that marks a workflow item as answer text.
_ANSWER_VARIANT = "answer"
_ANSWER_USAGE = re.compile(r"^ask_text(?:_\d+(?:_markdown)?)?$")
_CHUNK_PATH = re.compile(r"^/chunks/(\d+)$")
_WORKFLOW_CHUNK = re.compile(
    r"^/steps/(\d+)/items/(\d+)/payload/text_payload/chunks/(\d+)$")
_WORKFLOW_TEXT = re.compile(
    r"^/steps/(\d+)/items/(\d+)/payload/text_payload/text$")
_WORKFLOW_STEP = re.compile(r"^/steps/(\d+)$")
_WORKFLOW_ITEM = re.compile(r"^/steps/(\d+)/items/(\d+)$")


def _is_answer_usage(usage):
    return bool(_ANSWER_USAGE.match(usage or ""))


def _workflow_key(step, item):
    return f"workflow_root:{step}:{item}"


def _answer_item(item):
    if not isinstance(item, dict):
        return False
    payload = item.get("payload") or {}
    text_payload = payload.get("text_payload") or {}
    return (text_payload.get("variant") or item.get("variant")) == _ANSWER_VARIANT


def _seed_item(acc, item):
    payload = (item.get("payload") or {}).get("text_payload") or {}
    chunks = payload.get("chunks")
    if isinstance(chunks, list) and chunks:
        acc["chunks"] = [str(chunk) for chunk in chunks]
    elif isinstance(payload.get("text"), str) and payload["text"]:
        acc["chunks"] = [payload["text"]]


def _apply_markdown_diff(acc, patches):
    for patch in patches or []:
        path = patch.get("path") or ""
        if path == "":
            value = patch.get("value")
            if isinstance(value, str):
                acc["chunks"] = [value]
            elif isinstance(value, list):
                acc["chunks"] = [str(chunk) for chunk in value]
            elif isinstance(value, dict):
                chunks = value.get("chunks")
                if isinstance(chunks, list):
                    acc["chunks"] = [str(chunk) for chunk in chunks]
                elif isinstance(value.get("answer"), str):
                    acc["chunks"] = [value["answer"]]
                else:
                    acc["chunks"] = []
            else:
                acc["chunks"] = []
            continue
        match = _CHUNK_PATH.match(path)
        if match and isinstance(patch.get("value"), str):
            index = int(match.group(1))
            chunks = acc.setdefault("chunks", [])
            while len(chunks) <= index:
                chunks.append("")
            chunks[index] = patch["value"]


def _apply_workflow_block(state, workflow):
    for step_index, step in enumerate((workflow or {}).get("steps") or []):
        for item_index, item in enumerate((step or {}).get("items") or []):
            if _answer_item(item):
                _seed_item(state.setdefault(_workflow_key(step_index, item_index),
                                            {"chunks": []}), item)


def _apply_workflow_diff(state, patches):
    for patch in patches or []:
        path = patch.get("path") or ""
        value = patch.get("value")
        match = _WORKFLOW_STEP.match(path)
        if match:
            step_index = int(match.group(1))
            for item_index, item in enumerate((value or {}).get("items") or []):
                if _answer_item(item):
                    _seed_item(state.setdefault(
                        _workflow_key(step_index, item_index), {"chunks": []}),
                        item)
            continue
        match = _WORKFLOW_ITEM.match(path)
        if match:
            if _answer_item(value):
                _seed_item(state.setdefault(
                    _workflow_key(int(match.group(1)), int(match.group(2))),
                    {"chunks": []}), value)
            continue
        match = _WORKFLOW_CHUNK.match(path)
        if match and isinstance(value, str):
            key = _workflow_key(int(match.group(1)), int(match.group(2)))
            acc = state.get(key)
            if acc is None:
                continue
            index = int(match.group(3))
            chunks = acc.setdefault("chunks", [])
            while len(chunks) <= index:
                chunks.append("")
            chunks[index] = value
            continue
        match = _WORKFLOW_TEXT.match(path)
        if match and isinstance(value, str) and value:
            key = _workflow_key(int(match.group(1)), int(match.group(2)))
            acc = state.get(key)
            if acc is not None and not "".join(acc.get("chunks") or []):
                acc["chunks"] = [value]


def _plan_goals(block):
    plan = block.get("plan_block") or {}
    for goal in plan.get("goals") or []:
        text = (goal or {}).get("description") or ""
        if text:
            yield text
    for patch in (block.get("diff_block") or {}).get("patches") or []:
        value = patch.get("value")
        if isinstance(value, dict):
            for goal in value.get("goals") or []:
                text = (goal or {}).get("description") or ""
                if text:
                    yield text


def _search_steps(block):
    plan = block.get("plan_block") or {}
    for step in plan.get("steps") or []:
        step_type = (step or {}).get("step_type") or ""
        if step_type == "SEARCH_WEB":
            for query in ((step or {}).get("search_web_content") or {}).get("queries") or []:
                text = (query or {}).get("query") or ""
                if text:
                    yield f"Searching: {text}"
        elif step_type == "READ_RESULTS":
            for url in ((step or {}).get("read_results_content") or {}).get("urls") or []:
                if url:
                    yield f"Reading: {url}"


def _answer_from_final_text(text):
    """Recover the answer from the COMPLETED frame's double-encoded step blob."""
    if not isinstance(text, str):
        return None
    trimmed = text.strip()
    if not trimmed:
        return None
    if not trimmed.startswith(("{", "[")):
        return trimmed
    try:
        parsed = json.loads(trimmed)
    except ValueError:
        return None
    steps = parsed if isinstance(parsed, list) else [parsed]
    for step in steps:
        if not isinstance(step, dict):
            continue
        step_type = str(step.get("step_type") or step.get("stepType") or "")
        if step_type and step_type != "FINAL":
            continue
        content = step.get("content")
        if isinstance(content, str):
            raw = content
        elif isinstance(content, dict):
            raw = content.get("answer")
        else:
            raw = None
        if raw is None:
            raw = step.get("answer")
        if not isinstance(raw, str):
            continue
        inner = raw.strip()
        if not inner:
            continue
        if inner.startswith(("{", "[")):
            try:
                obj = json.loads(inner)
            except ValueError:
                return inner
            if isinstance(obj, dict):
                if isinstance(obj.get("answer"), str) and obj["answer"].strip():
                    return obj["answer"]
                if isinstance(obj.get("chunks"), list) and obj["chunks"]:
                    return "".join(str(chunk) for chunk in obj["chunks"])
                if isinstance(obj.get("structured_answer"), list):
                    joined = "".join(
                        str(block.get("text") or "")
                        for block in obj["structured_answer"]
                        if isinstance(block, dict))
                    if joined.strip():
                        return joined
        return inner
    return None


def _entry_answer(entry):
    """The answer text of one stored thread entry (markdown or workflow)."""
    blocks = entry.get("blocks") or []
    for block in blocks:
        markdown = (block or {}).get("markdown_block") or {}
        if _is_answer_usage((block or {}).get("intended_usage")) and markdown.get("answer"):
            return markdown["answer"]
    for block in blocks:
        workflow = (block or {}).get("workflow_block")
        if not workflow:
            continue
        parts = []
        for step in workflow.get("steps") or []:
            for item in (step or {}).get("items") or []:
                if not _answer_item(item):
                    continue
                payload = (item.get("payload") or {}).get("text_payload") or {}
                chunks = payload.get("chunks")
                if isinstance(chunks, list) and chunks:
                    parts.append("".join(str(chunk) for chunk in chunks))
                elif isinstance(payload.get("text"), str):
                    parts.append(payload["text"])
        if parts:
            return "".join(parts)
    if isinstance(entry.get("answer"), str) and entry["answer"].strip():
        return entry["answer"]
    return _answer_from_final_text(entry.get("text"))


class Perplexity(Provider):
    name = "perplexity"
    label = "Perplexity"
    origin = "https://www.perplexity.ai"
    session_target = "www.perplexity.ai"
    default_model = "auto"
    models = (
        "auto", "sonar", "sonar-pro", "gpt-5", "gpt-5-thinking", "gemini",
        "gemini-thinking", "sonnet", "sonnet-thinking", "opus",
        "opus-thinking", "grok", "grok-thinking", "kimi", "glm", "nemotron",
        "deep-research", "council",
    )
    #: A single ask message comfortably takes hundreds of KiB; the site's own
    #: context window is the real limit.
    inline_limit = 256 * 1024

    def open_conversation(self, match, model, system="", debug=False, timeout=600,
                          conversation_id=None, history=None, resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)

    # -- session -----------------------------------------------------------

    def _base_headers(self, match):
        headers = session_mod.build_headers(match, {
            "Accept": "application/json",
            "Content-Type": "application/json",
            "Origin": self.origin,
            "Referer": self.origin + "/",
        })
        token = session_mod.storage_value(
            match, "accessToken", "access_token", "userToken")
        if isinstance(token, str) and token and "Authorization" not in headers:
            headers["Authorization"] = "Bearer " + token
        return headers

    def _headers(self, match):
        headers = self._base_headers(match)
        headers.update({
            "Accept": "text/event-stream, application/json",
            "x-perplexity-request-endpoint": self.origin + ASK_PATH,
            "x-perplexity-request-reason": "ask-query-state-provider",
            "x-perplexity-request-try-number": "1",
        })
        return headers

    # -- upload ------------------------------------------------------------

    def _upload(self, headers, attachment, debug=False):
        """Upload one attachment and return its S3 object URL."""
        file_uuid = str(uuid.uuid4())
        body = {
            "files": {
                file_uuid: {
                    "filename": attachment.name,
                    "content_type": attachment.mime,
                    "source": "default",
                    "file_size": len(attachment.data),
                    "force_image": False,
                    "skip_parsing": False,
                },
            },
        }
        data = net.request_json(
            f"{self.origin}{UPLOAD_PATH}?version={API_VERSION}&source=default",
            method="POST", headers=headers, json_body=body, timeout=120) or {}
        result = (data.get("results") or {}).get(file_uuid)
        if not isinstance(result, dict) or not result.get("s3_bucket_url"):
            raise net.StreamError(f"upload failed: {data}")
        fields = [(key, value) for key, value in (result.get("fields") or {}).items()]
        fields.append(("file", (attachment.name, attachment.data, attachment.mime)))
        payload, content_type = net.encode_multipart(fields)
        status, response = net.request_raw(
            result["s3_bucket_url"], method="POST",
            headers={"Content-Type": content_type}, data=payload, timeout=300)
        if status >= 400:
            raise net.StreamError(
                f"upload failed: HTTP {status}: {response[:200]!r}")
        # The processing subscription is what lets documents be read; images
        # are usable without it, so a refusal is reported but not fatal.
        try:
            net.request_json(
                self.origin + UPLOAD_SUBSCRIBE_PATH, method="POST",
                headers=headers, json_body={"file_uuids": [file_uuid]},
                timeout=120)
        except net.StreamError as exc:
            diagnostics.note(
                f"{attachment.name}: attachment processing subscription failed "
                f"({exc}); the site may not read this document", debug,
                "perplexity")
        time.sleep(4.0)
        url = result.get("s3_object_url") or ""
        if "image/upload/" in url:
            url = url.replace("image/upload/", "")
        return url

    # -- history (used by ai.save) ----------------------------------------

    def history_headers(self, match):
        cached = getattr(self, "_history_headers", None)
        if cached is not None and getattr(self, "_history_match", None) is match:
            return dict(cached)
        headers = self._base_headers(match)
        self._history_headers = headers
        self._history_match = match
        return dict(headers)

    def iter_chat_summaries(self, match, limit=None):
        """Walk ``POST /rest/thread/list_ask_threads`` page by page."""
        headers = self.history_headers(match)
        offset = 0
        seen = set()
        count = 0
        while True:
            body = {
                "limit": 20,
                "ascending": False,
                "offset": offset,
                "search_term": "",
                "exclude_asi": False,
                "include_assets": True,
            }
            data = net.request_json(
                f"{self.origin}{LIST_PATH}?version={API_VERSION}&source=default",
                method="POST", headers=headers, json_body=body, timeout=120)
            if isinstance(data, list):
                items = data
            else:
                items = (data or {}).get("threads") or (data or {}).get("items") or []
            if not items:
                return
            new = 0
            for item in items:
                remote_id = (item.get("uuid") or item.get("entry_uuid")
                             or item.get("context_uuid") or "")
                if not remote_id or remote_id in seen:
                    continue
                seen.add(remote_id)
                new += 1
                updated = item.get("last_query_datetime") or ""
                slug = item.get("slug") or remote_id
                yield history.ChatSummary(
                    provider=self.name,
                    remote_id=remote_id,
                    title=item.get("title") or item.get("query_str") or "",
                    created=updated,
                    updated=updated,
                    model=item.get("display_model") or "",
                    url=f"{self.origin}/search/{slug}",
                )
                count += 1
                if limit and count >= limit:
                    return
            if new == 0 or len(items) < body["limit"]:
                return
            offset += len(items)

    def fetch_chat(self, match, remote_id):
        """Fetch one thread (cursor paginated) and normalise its entries."""
        headers = self.history_headers(match)
        chat = history.Chat(
            provider=self.name, remote_id=remote_id,
            url=f"{self.origin}/search/{remote_id}")
        cursor = ""
        seen = set()
        seen_cursors = set()
        while True:
            url = (f"{self.origin}{THREAD_PATH.format(uuid=remote_id)}"
                   f"?with_schematized_response=true&version={API_VERSION}"
                   f"&source=default&limit=100&offset=0&from_first=true"
                   f"&supported_block_use_cases=answer_modes"
                   f"&supported_block_use_cases=preserve_latex")
            if cursor:
                url += "&cursor=" + quote(cursor)
            data = net.request_json(url, headers=headers, timeout=180) or {}
            meta = data.get("thread_metadata") or {}
            chat.title = chat.title or meta.get("title") or ""
            chat.created = chat.created or meta.get("created_at") or ""
            chat.updated = meta.get("updated_at") or chat.updated
            chat.model = chat.model or meta.get("mode") or ""
            for entry in data.get("entries") or []:
                entry_id = (entry.get("uuid") or entry.get("backend_uuid")
                            or entry.get("context_uuid") or "")
                if entry_id and entry_id in seen:
                    continue
                if entry_id:
                    seen.add(entry_id)
                created = entry.get("entry_created_datetime") or ""
                query = entry.get("query_str") or ""
                if query:
                    chat.messages.append(history.Message(
                        role=history.USER, text=query, created=created,
                        message_id=entry_id))
                answer = _entry_answer(entry)
                if answer:
                    chat.messages.append(history.Message(
                        role=history.ASSISTANT, text=answer, created=created,
                        message_id=entry_id))
                chat.model = chat.model or entry.get("display_model") or ""
            cursor = data.get("next_cursor") or ""
            if not data.get("has_next_page") or not cursor or cursor in seen_cursors:
                break
            seen_cursors.add(cursor)
        if chat.messages:
            chat.created = chat.created or chat.messages[0].created
            chat.updated = history.newest(
                [chat.updated] + [message.created for message in chat.messages])
        return chat


class _Parser:
    """Reconstruct the cumulative answer from Perplexity's block stream."""

    def __init__(self):
        self.markdown = {}
        self.primary_usage = None
        self.seen_len = 0
        self.full_answer = ""
        self.backend_uuid = None
        self.read_write_token = None
        self.thread_slug = None
        self.thread_title = None
        self.status = None
        self.done = False
        self.error = None
        self.last_text = None
        self.seen_thinking = set()

    def feed(self, event):
        out = []
        if not isinstance(event, dict):
            return out
        if event.get("error_code") or event.get("error_message"):
            message = (event.get("error_message")
                       or f"Perplexity error: {event.get('error_code')}")
            self.error = message
            self.done = True
            return [ev.error(message)]
        if event.get("backend_uuid"):
            self.backend_uuid = event["backend_uuid"]
        if event.get("read_write_token") and not self.read_write_token:
            self.read_write_token = event["read_write_token"]
        if event.get("thread_url_slug") and not self.thread_slug:
            self.thread_slug = event["thread_url_slug"]
        if event.get("thread_title"):
            self.thread_title = event["thread_title"]
        if event.get("text"):
            self.last_text = event["text"]
        if event.get("status"):
            self.status = event["status"]

        blocks = event.get("blocks") or []
        for block in blocks:
            out.extend(self._block(block or {}))

        current = self._longest()
        if len(current) > self.seen_len:
            delta = current[self.seen_len:]
            self.full_answer = current
            self.seen_len = len(current)
            out.append(ev.text(delta))

        # Legacy frames carried the answer as a plain ``text`` field.
        if not self.primary_usage and not self.markdown and not blocks:
            text = (event.get("text") or "").strip()
            if text and not text.startswith(("{", "[")) and len(text) > self.seen_len:
                delta = text[self.seen_len:]
                self.full_answer = text
                self.seen_len = len(text)
                out.append(ev.text(delta))

        if (self.status == "COMPLETED" or event.get("final")
                or event.get("final_sse_message") or event.get("text_completed")):
            self.done = True
            if not self.full_answer.strip():
                recovered = _answer_from_final_text(
                    event.get("text") or self.last_text)
                if recovered:
                    delta = recovered[self.seen_len:]
                    self.full_answer = recovered
                    self.seen_len = len(recovered)
                    if delta:
                        out.append(ev.text(delta))
        return out

    def _block(self, block):
        out = []
        usage = block.get("intended_usage") or ""
        if usage == "pro_search_steps":
            for text in _search_steps(block):
                if text not in self.seen_thinking:
                    self.seen_thinking.add(text)
                    out.append(ev.thinking(text))
        if usage == "plan":
            for text in _plan_goals(block):
                if text not in self.seen_thinking:
                    self.seen_thinking.add(text)
                    out.append(ev.thinking(text))

        if block.get("workflow_block"):
            _apply_workflow_block(self.markdown, block["workflow_block"])
            return out
        diff = block.get("diff_block") or {}
        if diff.get("field") == "workflow_block":
            _apply_workflow_diff(self.markdown, diff.get("patches"))
            return out
        if not _is_answer_usage(usage):
            return out
        if diff.get("field") and diff["field"] != "markdown_block":
            return out

        acc = self.markdown.setdefault(usage, {"chunks": []})
        if diff.get("patches"):
            _apply_markdown_diff(acc, diff["patches"])
        else:
            markdown = block.get("markdown_block") or {}
            chunks = markdown.get("chunks")
            if isinstance(chunks, list) and chunks:
                acc["chunks"] = [str(chunk) for chunk in chunks]
            elif isinstance(markdown.get("answer"), str) and markdown["answer"]:
                acc["chunks"] = [markdown["answer"]]
        if usage == "ask_text":
            self.primary_usage = "ask_text"
        elif not self.primary_usage:
            self.primary_usage = usage
        return out

    def _longest(self):
        best = ""
        if self.primary_usage:
            best = "".join(self.markdown.get(self.primary_usage, {}).get("chunks") or [])
        for acc in self.markdown.values():
            joined = "".join(acc.get("chunks") or [])
            if len(joined) > len(best):
                best = joined
        return best


def _sse_events(lines):
    """Yield each SSE event's JSON object from an iterable of lines."""
    data = []
    end = False
    for line in lines:
        line = line.rstrip("\r")
        if line == "":
            if data:
                payload = "\n".join(data)
                data = []
                if payload.strip() == "[DONE]":
                    return
                try:
                    yield json.loads(payload)
                except ValueError:
                    yield payload
            if end:
                return
            continue
        if line.startswith("event:"):
            if line[6:].strip() == "end_of_stream":
                end = True
            continue
        if line.startswith("data:"):
            data.append(line[5:].lstrip())
    if data:
        payload = "\n".join(data)
        if payload.strip() != "[DONE]":
            try:
                yield json.loads(payload)
            except ValueError:
                yield payload


class _Conversation(Conversation):
    """A Perplexity thread, continued by its backend uuid."""

    def __init__(self, provider, match, model, system, debug, timeout,
                 conversation_id, history, resume):
        super().__init__(match, model, system, debug, timeout,
                         conversation_id, history, resume)
        self.provider = provider
        self.headers = provider._headers(match)
        requested = model or provider.default_model
        self.model_preference = MODEL_ALIASES.get(requested, requested)
        self.backend_uuid = (resume or {}).get("backend_uuid")
        self.read_write_token = (resume or {}).get("read_write_token")
        self.thread_slug = (resume or {}).get("thread_slug") or conversation_id

    def send(self, prompt, files=None):
        if not self.headers.get("Cookie") and not self.headers.get("Authorization"):
            yield ev.error(
                "no signed-in session found for www.perplexity.ai; log in once")
            return
        headers = dict(self.headers)
        headers["x-request-id"] = str(uuid.uuid4())
        attachments = []
        try:
            uploaded = self._attachments(files)
        except files_mod.FileError as exc:
            yield ev.error(str(exc))
            return
        for attachment in uploaded:
            try:
                url = self.provider._upload(
                    self.provider._base_headers(self.match), attachment,
                    self.debug)
            except (net.StreamError, files_mod.FileError) as exc:
                yield ev.error(str(exc))
                return
            if url:
                attachments.append(url)
            yield ev.tool_call(name="upload", status="done",
                               args=attachment.name)

        query = self._query(prompt)
        body = self._body(query, prompt, attachments)
        parser = _Parser()
        try:
            for event in _sse_events(net.stream_lines(
                    self.provider.origin + ASK_PATH, headers=headers,
                    json_body=body, timeout=self.timeout, debug=self.debug)):
                if isinstance(event, str):
                    yield ev.raw(event)
                    continue
                for item in parser.feed(event):
                    yield item
                if parser.done:
                    break
        except net.StreamError as exc:
            yield ev.error(str(exc))
            return

        if parser.error:
            return
        if not parser.full_answer.strip():
            yield ev.error(
                "Perplexity returned no answer"
                + (f" (thread {parser.thread_slug})" if parser.thread_slug else ""))
            return

        self.backend_uuid = parser.backend_uuid or self.backend_uuid
        self.read_write_token = parser.read_write_token or self.read_write_token
        self.thread_slug = parser.thread_slug or self.thread_slug
        if self.thread_slug or self.backend_uuid:
            self.conversation_id = self.thread_slug or self.backend_uuid
        self.resume = {
            "backend_uuid": self.backend_uuid or "",
            "read_write_token": self.read_write_token or "",
            "thread_slug": self.thread_slug or "",
        }
        self.turns += 1
        self._record("user", prompt)
        self._record("assistant", parser.full_answer)
        yield ev.done()

    def _query(self, prompt):
        """The text the site searches/answers, folding in a system prompt."""
        if self.backend_uuid:
            if self.system and self.turns == 0:
                return f"{self.system}\n\n{prompt}"
            return prompt
        if not self.system and not self.history:
            return prompt
        parts = []
        if self.turns == 0 and self.system:
            parts.append(self.system)
        if self.history:
            lines = ["This is a continuation of the conversation so far. "
                     "Reply to the final user message below.", ""]
            for turn in self.history[-MAX_REPLAY:]:
                who = "User" if turn.get("role") == "user" else "Assistant"
                lines.append(f"{who}: {turn.get('text', '')}")
            lines.append("")
            lines.append(f"User: {prompt}")
            parts.append("\n".join(lines))
        else:
            parts.append(prompt)
        return "\n\n".join(part for part in parts if part)

    def _body(self, query, prompt, attachments):
        params = {
            "attachments": list(attachments),
            "language": "en-US",
            "timezone": _timezone(),
            "search_focus": "internet",
            "sources": ["web"],
            "frontend_uuid": str(uuid.uuid4()),
            "mode": "copilot",
            "model_preference": self.model_preference,
            "is_related_query": False,
            "is_sponsored": False,
            "frontend_context_uuid": str(uuid.uuid4()),
            "prompt_source": "user",
            "query_source": "followup" if self.backend_uuid else "home",
            "is_incognito": False,
            "local_search_enabled": False,
            "use_schematized_api": True,
            "send_back_text_in_streaming_api": False,
            "supported_block_use_cases": SUPPORTED_BLOCK_USE_CASES,
            "client_coordinates": None,
            "mentions": [],
            "dsl_query": prompt,
            "skip_search_enabled": True,
            "is_nav_suggestions_disabled": False,
            "source": "default",
            "always_search_override": False,
            "override_no_search": False,
            "client_search_results_cache_key": str(uuid.uuid4()),
            "should_ask_for_mcp_tool_confirmation": True,
            "supports_tool_approval_modal": True,
            "browser_agent_allow_once_from_toggle": False,
            "force_enable_browser_agent": False,
            "supported_features": ["browser_agent_permission_banner_v1.1"],
            "extended_context": False,
            "version": API_VERSION,
            "rum_session_id": str(uuid.uuid4()),
        }
        if self.backend_uuid:
            params["last_backend_uuid"] = self.backend_uuid
            if self.read_write_token:
                params["read_write_token"] = self.read_write_token
        return {"query_str": query, "params": params}


def _timezone():
    """The IANA zone the site expects, from ``TZ`` or the system link."""
    zone = os.environ.get("TZ")
    if zone:
        return zone
    try:
        target = os.path.realpath("/etc/localtime")
    except OSError:
        return "UTC"
    marker = "/zoneinfo/"
    if marker in target:
        return target.split(marker, 1)[1]
    return "UTC"
