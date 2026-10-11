#include "prompt/prompts/branchless_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>

namespace prompt::prompts {

prompt_result execute_branchless(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

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
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            output += "```\n";
            output += "\n";
        } else {
            error += "Warning: File not found or is not a regular file: " + std::string(ctx.args[i]) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_branchless(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt branchless [--head N] [FILE...]
       some-command | prompt branchless [--head N] [FILE...]

Rewrite this C++ code to be branchless.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
