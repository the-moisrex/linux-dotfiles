---
name: ai-providers
description: Use when adding a new ai.* chat provider or fixing an existing one in this repo — the standalone bin/ai.<name> CLI, the native .ai.<name> prompt sink, stream parsing, resume/state, file uploads, ai.save history sync, debate/reply aliases, and the tests/docs that must move together. Trigger on: ai.perplexity, ai.chatgpt, ai.claude, ai.grok, ai.dseek, ai.gap, ai.kimi, providers/, ai.cpp, kAiProviders, ai.save sync.
---

# Adding or fixing an `ai.*` provider

Two surfaces, one engine. The standalone `bin/ai.<name>` and the native
`.ai.<name>` sink both drive `bin/ai/cli.py` + `bin/ai/providers/<name>.py`;
the C++ sink is a thin wrapper that shells out to `bin/ai.<name>`.

**Never duplicate provider logic in C++.** The sink only forwards args and
reads the plain answer back via `--context-file` (handled by `execute_ai` in
`prompts/src/prompts/ai.cpp`).

## File map

| Concern | File |
| --- | --- |
| Adapter + stream parser + upload + history | `bin/ai/providers/<name>.py` |
| Registry | `bin/ai/providers/__init__.py` (`all_providers`) |
| CLI entry | `bin/ai.<name>` (executable, thin `main("<name>")`) |
| Native sink | `prompts/src/prompts/ai.cpp` |
| `.last.ai` provider list | `prompts/src/prompts/history.cpp` (`kAiProviders`) |
| Top-level help | `prompts/src/main.cpp` |
| Sink registration test | `prompts/tests/test_chaining.cpp` |
| Offline tests | `prompts/tests/test_ai_<name>.py` + `prompts/tests/CMakeLists.txt` |
| Debate alias | `bin/ai/debate.py` (`ALIASES`) |
| `ai.save` alias | `bin/ai/save/cli.py` (`PROVIDER_ALIASES`) |
| Docs | `AGENTS.md`, `bin/README.md`, `prompts/AGENTS.md`, `CHANGELOG.md` |

## 1. Adapter (`bin/ai/providers/<name>.py`)

Subclass `Provider` and `Conversation` from `.` (see
`bin/ai/providers/__init__.py`). Minimum shape:

```python
from .. import events as ev, files as files_mod, history, net, session as session_mod
from . import Conversation, Provider

class Foo(Provider):
    name = "foo"
    label = "Foo"
    origin = "https://foo.example"
    session_target = "foo.example"      # browser.session target
    default_model = "auto"
    models = ("auto", "model-2")
    inline_limit = 64 * 1024            # prompt-file threshold, bytes
    # needs_session = False             # API providers only
    # recordable = False                # no chat list to sync
    # prompt_file_upload = False        # no upload endpoint

    def open_conversation(self, match, model, system="", debug=False,
                          timeout=600, conversation_id=None, history=None,
                          resume=None):
        return _Conversation(self, match, model, system, debug, timeout,
                             conversation_id, history, resume)
```

`_Conversation.__init__` calls `super().__init__(...)`, keeps `self.provider`,
builds headers with `session_mod.build_headers(match, {...})`, and restores
`self.resume` fields. `send(prompt, files=None)` is a generator:

- yield `ev.text(delta)`, `ev.thinking(text)`, `ev.tool_call(...)`,
  `ev.raw(line)`, `ev.error(message)`, and finally `ev.done()`;
- update `self.conversation_id`, `self.resume`, `self.turns += 1`;
- on success only: `self._record("user", prompt)` and
  `self._record("assistant", answer)`.

`Conversation` base (`bin/ai/providers/__init__.py`) provides `self.match`,
`self.model`, `self.system`, `self.history` (list of `{"role","text"}`),
`self.resume`, `self.turns`, `_record`, and `_attachments(files)`.

### Transport helpers

- `net.stream_lines(url, headers=..., json_body=..., timeout=..., debug=...)`
  yields decoded SSE/NDJSON lines.
- `net.request_json(...)`, `net.request_raw(...)`, `net.encode_multipart(fields)`,
  `net.StreamError`.
- `session_mod.build_headers(match, extra)`, `storage_value(match, *keys)`.
- `history.ChatSummary`, `history.Chat`, `history.Message`, `history.newest`.

### Resume / state

`bin/ai/cli.py` persists `conversation_id`, `history` and `resume` via
`state.save` and restores them in `_open`. A server-side thread id goes in
`resume` (e.g. `backend_uuid`); a provider with no reattach id replays
`self.history` as **plain text** (see `grok.py` / `perplexity.py`), never as a
JSON blob in the query field.

### Uploads and history sync

- Upload: `self._attachments(files)` → provider-specific `_upload(...)` →
  append the returned id/URL to the first message; yield one
  `ev.tool_call(name="upload", status="done", args=attachment.name)`.
- History: implement `history_headers(match)`,
  `iter_chat_summaries(match, limit=None)` (yield `history.ChatSummary`) and
  `fetch_chat(match, remote_id)` (return `history.Chat` with
  `history.Message`s). `ai.save` calls these directly.

### CLI entry

Copy an existing entry (e.g. `bin/ai.perplexity`):

```python
#!/usr/bin/env python3
import os, sys
sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from ai.cli import main
if __name__ == "__main__":
    sys.exit(main("foo"))
```

`chmod +x bin/ai.foo`. Register the class in `all_providers()`.

## 2. Native sink (`prompts/src/prompts/ai.cpp`)

Add, near the other providers:

```cpp
constexpr ai_provider kFoo{.name = "foo", .label = "Foo"};
void execute_ai_foo(view const& v, result& r) { execute_ai(v, r, kFoo.name); }
void render_help_ai_foo(std::ostream& out) { render_help_ai(out, kFoo); }

bool const registered_ai_foo = prompt::register_sink(
    "ai.foo", "Send the accumulated chain context to Foo and use its answer as the next context.",
    &execute_ai_foo, &render_help_ai_foo);
```

Then update every fixed-size list (compile catches size, not omission):

- `kReplyProviders` — add the name, bump `std::array<..., N>`.
- `complete_ai_debate` and `complete_ai_reply` — add `{name, label}` and the
  alias, bump `std::array<item, N>` (currently 12).
- `canonical_provider` — map the alias to the canonical name.
- `render_help_ai_debate` provider list and `.ai.reply` help line.
- `prompts/src/prompts/history.cpp`: `kAiProviders` (currently 9) + its help
  list.
- `prompts/src/main.cpp`: the sink list in the top-level help.

## 3. Aliases

- `bin/ai/debate.py` → `ALIASES`
- `bin/ai/save/cli.py` → `PROVIDER_ALIASES`
- `prompts/src/prompts/ai.cpp` → `canonical_provider` + both completion arrays

## 4. Tests

- `prompts/tests/test_ai_<name>.py`: offline only — parser deltas, thinking,
  stop flags, error frames, upload/history extraction, model aliases. Insert
  `ROOT` on `sys.path` like the existing tests.
- `prompts/tests/CMakeLists.txt`: `add_test(NAME ai_<name>_unit COMMAND
  ${Python3_EXECUTABLE} .../test_ai_<name>.py)`.
- `prompts/tests/test_chaining.cpp`: add the sink name to the registration
  list.

## 5. Docs

Update the provider lists and the one-paragraph endpoint description in
`AGENTS.md`, `bin/README.md`, `prompts/AGENTS.md`; add a `CHANGELOG.md` entry.

## 6. Verify

```bash
python3 prompts/tests/test_ai_<name>.py
cmake --build prompts/build -j 8
cmake --install prompts/build --prefix "$HOME/cmd" --component prompt
clang-format -i prompts/src/prompts/ai.cpp prompts/src/prompts/history.cpp \
  prompts/src/main.cpp prompts/tests/test_chaining.cpp
clang-format --dry-run -Werror <same files>
clang-tidy -p prompts/build <same files>
cmake --build prompts/build-asan -j 8
ctest --test-dir prompts/build-asan --output-on-failure -j 8
```

Tests only build in a Debug configuration (`prompts/build-asan`).

## 7. Pitfalls

- **Fixed arrays**: `kReplyProviders`, `kAiProviders`, the two completion
  arrays, and `kAiProviders`' help text are separate edits.
- **Stream stop**: stop on `final`, `text_completed`, `final_sse_message`,
  `status == "COMPLETED"`, or SSE `event: end_of_stream` — otherwise the sink
  can hang after the answer.
- **Answer vs reasoning**: only seed answer items into the cumulative
  accumulator; if thinking text enters it, "longest" selection can emit
  reasoning as the answer.
- **History replay**: fold stored turns as plain text when the server cannot
  reattach; do not send a JSON object as the user query.
- **Per-turn ids**: a thread's turn id can change each turn; store the
  server's `last_backend_uuid` for the next request.
- **Headers**: use base/session headers for uploads and history; add
  ask-specific headers only to the ask request.
- **Session**: a profile with only visitor/Cloudflare cookies is anonymous;
  `--print-session` is the quick check. `--print-prompt` is offline.
- **Live-test cleanup**: a real turn writes `$XDG_STATE_HOME/ai/<name>.json`,
  `terminals/<scope>/<name>.json`, a terminal `last`, and the global
  `ai/last`. Remove them and restore `last` afterward.
- **`ai.save`**: web providers are `recordable` by default; set
  `recordable = False` only for keyless API providers.
- **Prompt files**: `prompt_file_upload = False` if the site has no upload
  endpoint; otherwise tune `inline_limit`.
- **Entry point**: `bin/ai.<name>` must be executable and use
  `main("<name>")`, matching the registry key.
