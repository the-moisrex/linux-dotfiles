#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

static void execute_branchless(prompt_context& ctx) {
    auto opts = file_options_or_throw(ctx);

    std::string output;
    output +=
        R"PROMPT(Rewrite this C++ code to be branchless. Eliminate all conditional branches (if, else, switch, early returns in hot loops) that harm branch prediction.

Use these techniques — each in the shortest sentence:

BRANCHLESS TECHNIQUES:
- Ternary (?:) often compiles to CMOV (conditional move) — no pipeline flush.
- Bitmask from comparison: mask = (a > b) - 1; gives 0x00000000 or 0xFFFFFFFF.
- Bool as integer: bools are 0 or 1 in arithmetic. x += (int)flag;
- Bool multiplication: result = flag * value; selects value or 0.
- Sign-bit right-shift: mask = x >> 31; gives all 0s or all 1s.
- Branchless abs: mask = x >> 31; abs = (x ^ mask) - mask;
- XOR swap without temp: x ^= y; y ^= x; x ^= y;
- Lookup table: replace complex conditions with table[condition].
- std::min / std::max / std::clamp often compile to CMOV — prefer them over if-else.
- Evaluate both paths, select one: result = (c * a) + (!c * b);
- Bitwise select: result = (a & mask) | (b & ~mask); mask from comparison.
- Boolean logic to mask: mask = -(int)(bool_expr); negation gives all 1s or 0s.

RULES:
- Only optimize branches that are genuinely unpredictable (random data, user input).
- Do NOT touch branches that are highly predictable — they are faster as branches.
- Preserve correctness. The branchless version must produce identical results.
- Prefer readability when the technique is obvious (std::min, ternary).
- Use bitwise tricks only when the benefit is clear.
- Keep the diff minimal — change only what needs to be branchless.

OUTPUT:
- Briefly explain which branches you removed and why.
- Provide a git diff for the changes.
)PROMPT";
    output += '\n';

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += embed_file_block(file, opts);
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File not found or is not a regular file: {}\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_branchless(std::ostream& os) {
    os << file_usage_help("branchless") << R"EOF(
Rewrite this C++ code to be branchless.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("branchless", "Rewrite this C++ code to be branchless.",
                                                &execute_branchless, &render_help_branchless);

} // namespace prompt::prompts
