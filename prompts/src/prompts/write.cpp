#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>

namespace prompt::prompts {

static void render_help_write(std::ostream& os);

namespace {

// `.write` takes exactly one path. The dispatcher strips the shared sink flags
// before the sink runs, but keeps a literal `--` in the remaining arguments, so
// skip it before counting.
std::filesystem::path parse_target(view const& v) {
    std::size_t first = 0;
    if (!v.args.empty() && v.args.front() == "--") first = 1;

    if (v.args.size() - first != 1) {
        throw usage_error{"prompt write: expected exactly one FILE argument\n" + help_text(render_help_write)};
    }

    std::string_view const arg = v.args[first];
    if (arg.empty()) throw usage_error{"prompt write: the target path is empty\n"};
    // A leading `--` already told the dispatcher the next token is a path, so
    // only an option-looking token without it is an error.
    if (first == 0 && arg.size() > 1 && arg.front() == '-') {
        throw usage_error{
            std::format("prompt write: unknown option: {} (use -- before a path that starts with '-')\n", arg)};
    }
    return std::filesystem::path{std::string(arg)};
}

// Create the parent directories and reject a directory target. Shared by the
// preflight (fail before the expensive batch runs) and the write itself.
void prepare_target(std::filesystem::path const& target) {
    auto const parent = target.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw soft_error{std::format("prompt write: cannot create {}: {}\n", parent.string(), ec.message())};
        }
    }

    std::error_code ec;
    if (std::filesystem::is_directory(target, ec)) {
        throw soft_error{std::format("prompt write: {} is a directory\n", target.string())};
    }
}

void preflight_write(view const& v) { prepare_target(parse_target(v)); }

} // namespace

static void execute_write(view const& v, result& r) {
    auto const target = parse_target(v);
    prepare_target(target);

    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw soft_error{std::format("prompt write: cannot write {}\n", target.string())};
    }

    file.write(v.text.data(), static_cast<std::streamsize>(v.text.size()));
    if (!v.text.empty() && v.text.back() != '\n') file.put('\n');
    file.close();
    if (!file) {
        throw soft_error{std::format("prompt write: failed while writing {}\n", target.string())};
    }

    // A silent passthrough sink: no stdout answer, so the dispatcher keeps the
    // chain context for later segments. Status goes to stderr only.
    r.no_clipboard = true;
    r.err = std::format("prompt write: wrote {}\n", target.string());
}

static void render_help_write(std::ostream& os) {
    os << R"EOF(Usage: prompt <other prompts...> .write FILE

Write the accumulated chain context to FILE, overwriting it if it exists.

`.write` is a passthrough sink: it writes the file and leaves the chain context
unchanged, so it can sit before another sink such as `.ai.grok`. Unlike `.save`
it takes no filename hint and derives nothing: FILE is used exactly as given,
and an existing file is replaced. Missing parent directories are created.

The file receives the same sanitized context an AI sink would receive, including
--head trimming and the redaction legend when applicable. Status goes to stderr
and is never copied to the clipboard.

Examples:
  prompt .files src/main.cpp .write /tmp/context.txt
  prompt .note "review this" .files a.cpp .ai.grok .write review.md

)EOF";
    render_sink_help(os);
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_sink("write", "Write the accumulated chain context to FILE, overwriting it if it exists.",
                          &execute_write, &render_help_write, nullptr, false, &preflight_write);

} // namespace prompt::prompts
