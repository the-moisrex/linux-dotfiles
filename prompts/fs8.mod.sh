#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt fs8.mod [--head N]
       prompt fs8.mod | prompt note "Create a mod that ..."

Builds a prompt for writing a new pipeline mod for the foresight project.
Must be run from inside the foresight git repo (or a subdirectory of it).

The prompt embeds AGENTS.md, a mod-writing guide, and example mod files
so the AI has enough context to generate correct, buildable code.

Options:
  --head N   Keep only the first N lines of each embedded context file
EOF
}

NO_FILES=true
source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

# Find the foresight repo root: look for AGENTS.md + mods/ directory
find_foresight_root() {
    local dir="$PWD"
    while [[ "$dir" != "/" ]]; do
        if [[ -f "$dir/AGENTS.md" && -d "$dir/mods" ]]; then
            echo "$dir"
            return 0
        fi
        dir="$(dirname "$dir")"
    done
    return 1
}

FORESIGHT_ROOT="$(find_foresight_root)" || {
    echo "Error: not inside the foresight git repo (could not find AGENTS.md + mods/)." >&2
    echo "Change to the foresight directory or a subdirectory and try again." >&2
    exit 1
}

echo "You are an expert C++26 developer writing a new pipeline mod for the foresight project."
echo
echo "Foresight is a Linux input manager: a C++26 library (libforesight) plus the foresight CLI."
echo "Mods are consteval-copyable callables in the fs8 namespace that chain via operator| in a pipeline."
echo
echo "Your task: generate a complete, correct, buildable mod that follows all project conventions."
echo

echo "## Project overview"
embed_file "$FORESIGHT_ROOT/AGENTS.md" "foresight/AGENTS.md"

echo
echo "## How to write a mod (guide)"
echo
cat <<'GUIDE'
### Checklist for a new mod

1. Create `mods/<name>.ixx` as `export module fs8.mods:<name>;`
   - Define `basic_<name>` in `export namespace fs8`, inheriting `consteval_copyable`
     (or `pimpl_idiom<basic_<name>>` if it holds runtime state).
   - Expose a constexpr instance `<name>`.
2. Create `mods/<name>.cxx` as `module fs8.mods;` with the `impl` definition and method bodies.
   - Skip this file if the mod is header-only (no state, all methods inline).
3. Register both files in the root `CMakeLists.txt`:
   - `.cxx` → `target_sources(... PRIVATE ...)`
   - `.ixx` → `PUBLIC FILE_SET foresight TYPE CXX_MODULES FILES`
4. Add `export import :<name>;` to `mods/mods.ixx`.
5. `static_assert` the `Modifier`/`OutputModifier` concept where relevant and
   any inter-mod dependency.
6. Handle lifecycle tags via `operator()(special_event const&)`:
   - Return `next`/`drop_event` for ordinary events.
   - All invocations must be `noexcept`.

### Mod invocation forms

- `mod(ctx)` — sees the whole context (event + sibling mods).
- `mod(event)` — only needs the current event.
- `mod(ctx, tag)` — a tag request (start, load_event, next_event, etc.).

### context_action return values

| Action       | Meaning                                |
|--------------|----------------------------------------|
| `next`       | Pass the event to the next mod.        |
| `drop_event` | Drop this event.                       |
| `recovery`   | Restart / enter watch mode.            |
| `exit`       | Exit the pipeline.                     |

### Key invariants

- Mods derive from `consteval_copyable`: runtime copies abort.
- Every mod must be `nothrow`-invocable.
- `pimpl_idiom`-based mods allocate lazily at `start`; handlers must outlive the pipeline.
GUIDE

echo
echo "## Example: header-only mod (stopper.ixx)"
embed_file "$FORESIGHT_ROOT/mods/stopper.ixx" "mods/stopper.ixx"

echo
echo "## Example: mod with state + .cxx (scale.ixx)"
embed_file "$FORESIGHT_ROOT/mods/scale.ixx" "mods/scale.ixx"

echo
echo "## Example: .cxx implementation (scale.cxx)"
embed_file "$FORESIGHT_ROOT/mods/scale.cxx" "mods/scale.cxx"

echo
echo "## Example: mod with pimpl (io_manager.ixx header)"
embed_file "$FORESIGHT_ROOT/mods/io_manager.ixx" "mods/io_manager.ixx"

echo
echo "## Registration: mods/mods.ixx (export imports)"
embed_file "$FORESIGHT_ROOT/mods/mods.ixx" "mods/mods.ixx"
