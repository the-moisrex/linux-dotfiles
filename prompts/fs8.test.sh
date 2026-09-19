#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt fs8.test [--head N]
       prompt fs8.test | prompt note "Write tests for ..."

Builds a prompt for writing GoogleTest tests for a foresight pipeline mod.
Must be run from inside the foresight git repo (or a subdirectory of it).

The prompt embeds the test infrastructure, patterns, and example test files
so the AI generates tests that follow the project's conventions.

Options:
  --head N   Keep only the first N lines of each embedded context file
EOF
}

source "$(dirname "$0")/_common.sh"
init_prompt --no-files

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

echo "You are an expert C++26 developer writing GoogleTest tests for a foresight pipeline mod."
echo
echo "Tests live in tests/ as *_test.cxx files. Each file is a standalone test suite."
echo "Tests are only built in Debug mode. The CMakeLists.txt auto-discovers all *_test.cxx files."
echo
echo "Your task: generate a complete, correct test file that follows all project conventions."
echo

echo "## Build/test reference"
echo
cat <<'EOF'
To build and run tests:

    cmake --preset debug-gcc
    cmake --build --preset debug-gcc
    ctest --test-dir build-debug-gcc/tests -R test-<name>

Or run a single test binary directly:

    ./build-debug-gcc/tests/test-<name>
EOF

echo
echo "## Test patterns"
echo
cat <<'PATTERN'
Every test file follows this structure:

1. First include (before any import):
   #include "common/tests_common_pch.hpp"

2. Then system headers, then module import:
   #include <linux/input-event-codes.h>
   import fs8.mods;

3. Basic test with emit_all + record (most common pattern):
   - Build a pipeline: context | emit_all[{...}] | <mod-under-test> | record
   - Get a reference: auto& col = pipeline.mod<basic_record>();
   - Run: pipeline();
   - Check results: col.without_syn() returns non-SYN events

4. The emit_all initializer list uses this format:
   {EV_ABS, ABS_X, 1000},  // {type, code, value}
   {EV_SYN, SYN_REPORT, 0},

5. For time-dependent tests (like debounce):
   - Use a timed_sequence custom load_event provider with explicit timestamps
   - Define a consteval timed_ev() helper for event construction
   - See debounce_test.cxx for the full pattern

6. Common assertions:
   - ASSERT_EQ / EXPECT_EQ for values
   - ASSERT_EQ(events.size(), NU) for event counts
   - Check event.type(), event.code(), event.value()

7. The record mod:
   - record captures all events that pass through
   - col.without_syn() returns events excluding SYN_REPORT
   - col.size() gives total event count
   - col[i] provides indexed access
PATTERN

echo
echo "## Example test file (scale_test.cxx)"
embed_file "$FORESIGHT_ROOT/tests/scale_test.cxx" "tests/scale_test.cxx"

echo
echo "## Example: time-dependent test (debounce_test.cxx)"
embed_file "$FORESIGHT_ROOT/tests/debounce_test.cxx" "tests/debounce_test.cxx"

echo
echo "## Mod source being tested (for reference)"
embed_file "$FORESIGHT_ROOT/mods/mods.ixx" "mods/mods.ixx"
