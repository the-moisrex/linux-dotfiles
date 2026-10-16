#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_table_lookup(prompt_context& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += R"PROMPT(Replace conditional logic in this C++ code with precomputed table lookups.

TECHNIQUES:
- Small enum/bool switch → array indexed by value. const T table[] = {...}; result = table[expr];
- Bool condition → 2-element table. table[(bool)cond] gives one of two precomputed values.
- Integer range mapping → direct-index array. table[value] precomputed for every possible input.
- Character classification → 256-byte bitmap. unsigned char table[256] = {...}; flag = table[(unsigned char)c];
- Arithmetic expression with limited inputs → precomputed result array.
- Nested ternary chains → flat table. Replace chain of ?: with table[condition].
- Function with few discrete return paths → table of return values indexed by path selector.
- Bitfield / flag combinations → 2^N table. table[flags] where flags is a small bitmask.
- Lookup replaces branching only when table fits in cache. If table is huge, it defeats the purpose.
- Use constexpr tables for compile-time evaluation when possible.
- For variable-length mappings, use std::array or std::unordered_map only if the branch cost exceeds hash cost.

RULES:
- Only convert branches where the input domain is small and bounded.
- Do NOT create a table for a continuous range (e.g. every int) — it wastes memory.
- Prefer constexpr tables so the compiler can fold them at compile time.
- Ensure the table index cannot be out-of-bounds — use safe casts (e.g. unsigned char for char).
- Keep the original logic as a comment if the table is non-obvious.
- Preserve correctness exactly.
- Prefer readability: a well-named constexpr table is clearer than a chain of ifs.

OUTPUT:
- Briefly explain what conditional logic you replaced and why a table is faster.
- Provide a git diff for the changes.
)PROMPT";
    output += '\n';

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += std::format("File: {}\n\n", file.filename().string());
            output += std::format("```{}\n", infer_lang(file));
            output += trim_context_nl(read_file(file), head_lines);
            // table-lookup.sh closes the fence directly after the content.
            output += "```\n\n";
        } else {
            error += std::format("Warning: File not found or is not a regular file: {}\n", std::string(ctx.args[i]));
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_table_lookup(std::ostream& os) {
    os << R"EOF(Usage: prompt table-lookup [--head N] [FILE...]
       some-command | prompt table-lookup [--head N] [FILE...]

Replace conditional logic with precomputed table lookups.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("table-lookup", "Replace conditional logic with precomputed table lookups.",
                            &execute_table_lookup, &render_help_table_lookup);

} // namespace prompt::prompts
