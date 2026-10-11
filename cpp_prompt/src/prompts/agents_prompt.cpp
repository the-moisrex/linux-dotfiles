#include "prompt/prompts/agents_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <vector>

namespace prompt::prompts {

namespace {

// agents.sh embeds with plain-text fences (never ```markdown) because AGENTS.md
// frequently contains ``` blocks that would break an outer markdown fence.
// Format matches embed_agent_file: blank line, "File: <basename>", ```text,
// file body (head-capped), ```.
std::string embed_agent_file(std::filesystem::path const& path, std::size_t head_lines) noexcept {
    std::string out;
    out += "\n";
    out += "File: " + path.filename().string() + "\n";
    out += "```text\n";
    out += trim_context_nl(read_file(path), head_lines);
    out += "```\n";
    return out;
}

// `stat -Lc '%d:%i'` — device+inode of the target (symlinks followed), used to
// dedup names that resolve to the same file. Empty on failure (skip).
std::string target_inode_key(std::filesystem::path const& path) noexcept {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return {};
    return std::to_string(st.st_dev) + ":" + std::to_string(static_cast<long long>(st.st_ino));
}

} // namespace

prompt_result execute_agents(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    if (ctx.git_root.empty()) {
        return {std::string{}, 1, false, "Error: not inside a git repository.\n"};
    }

    static constexpr char const* agent_files[] = {
        "AGENTS.md",      "CLAUDE.md",       "COPILOT.md", ".cursorrules", ".github/copilot-instructions.md",
        "CONVENTIONS.md", "ARCHITECTURE.md",
    };

    std::string output;
    std::set<std::string> seen;

    // maybe_embed: -f (regular after following symlinks), dedup by inode.
    auto maybe_embed = [&](std::filesystem::path const& path) -> bool {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
        auto key = target_inode_key(path);
        if (key.empty()) return false;
        if (!seen.insert(key).second) return false;
        output += embed_agent_file(path, head_lines);
        return true;
    };

    for (auto const* name : agent_files) {
        maybe_embed(ctx.git_root / name);
    }

    // .opencode/*.md, maxdepth 1, regular files only, sorted like `sort -z`.
    std::error_code ec;
    auto opencode = ctx.git_root / ".opencode";
    if (std::filesystem::is_directory(opencode, ec)) {
        std::vector<std::string> md_paths;
        std::error_code it_ec;
        for (std::filesystem::directory_iterator it(opencode, it_ec), end; !it_ec && it != end; it.increment(it_ec)) {
            if (it_ec) break;
            auto const& p = it->path();
            // find -L ... -name '*.md' -type f : follows symlinks, regular only.
            std::error_code sec;
            if (!std::filesystem::is_regular_file(p, sec) || sec) continue;
            if (p.extension() != ".md") continue;
            md_paths.push_back(p.string());
        }
        std::sort(md_paths.begin(), md_paths.end());
        for (auto const& p : md_paths) {
            maybe_embed(std::filesystem::path(p));
        }
    }

    return {std::move(output), 0, false, std::string{}};
}

void render_help_agents(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt agents [--head N]

Find and embed agent instruction files (AGENTS.md, CLAUDE.md, etc.) from the current Git repository root.
This gives the AI project conventions, coding standards, and architectural context before it answers a question.

Searches the git root for any of these files:
  AGENTS.md, CLAUDE.md, COPILOT.md, .cursorrules,
  .github/copilot-instructions.md, CONVENTIONS.md, ARCHITECTURE.md,
  and any *.md inside .opencode/

Options:
  --head N   Keep only the first N lines of each embedded file
  -h, --help Show this help
)EOF";
}

} // namespace prompt::prompts
