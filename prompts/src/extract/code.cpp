#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <array>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::vector<match> run_code(std::string_view text, std::string_view qualifier) {
    auto blocks = parse_document(text, "");
    assign_indices(blocks);
    std::vector<match> out;
    for (auto const& b : blocks) {
        if (b.kind != "code") continue;
        if (!lang_matches(b, qualifier)) continue;
        out.push_back(match_from(b));
    }
    return out;
}

contract_text contract_code(std::string_view qualifier) {
    std::string const language = qualifier.empty() ? std::string{} : std::format(" tagged `{}`", qualifier);
    return make_contract(std::format("Return exactly one fenced code block{} containing only the code. "
                                     "Put no prose before or after the block.\n",
                                     language),
                         std::format("one fenced code block{} containing only the code", language));
}

void complete_code(std::string_view prefix, std::ostream& os) {
    static constexpr std::array<std::string_view, 59> families = {
        "asm",    "bash",   "c",      "cmake",   "cpp",     "csharp",   "css",        "csv",     "dart",    "diff",
        "docker", "elixir", "erlang", "fortran", "go",      "graphql",  "groovy",     "haskell", "html",    "java",
        "js",     "json",   "julia",  "kotlin",  "lisp",    "lua",      "make",       "matlab",  "md",      "mermaid",
        "nginx",  "nim",    "nix",    "ocaml",   "perl",    "php",      "powershell", "proto",   "python",  "r",
        "regex",  "ruby",   "rust",   "scala",   "sh",      "solidity", "sql",        "swift",   "systemd", "terraform",
        "tex",    "toml",   "ts",     "vba",     "verilog", "wasm",     "xml",        "yaml",    "zig",
    };
    for (auto name : families) {
        if (completion::matches(prefix, name)) completion::write_item(os, name, "language family");
    }
}

} // namespace

bool const registered_code = register_extractor(extractor{
    .name = "code",
    .summary = "fenced ``` / ~~~ blocks; code:lang filters by language family",
    .run = &run_code,
    .contract = &contract_code,
    .complete = &complete_code,
});

} // namespace prompt::extract
