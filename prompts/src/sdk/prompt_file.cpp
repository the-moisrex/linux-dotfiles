#include "prompt_file.hpp"
#include "completion.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace prompt {
namespace {

// One file's worth of tokens, plus the lexer's one possible failure. The
// public `tokenize_prompt_text` below shares this with the function pass.
tokenized_text lex_text(std::string_view text, std::string_view label) {
    tokenized_text out;
    std::string current;
    bool in_token = false;
    std::size_t line = 1;
    std::size_t opened_at = 1;

    auto flush = [&] {
        if (!in_token) return;
        out.tokens.push_back(std::move(current));
        current.clear();
        in_token = false;
    };

    for (std::size_t i = 0; i < text.size();) {
        char const c = text[i];
        if (c == '\n') {
            flush();
            ++line;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            flush();
            ++i;
            continue;
        }
        // Only a token-leading `#` starts a comment: `a#b` and `src/x#1.prompt`
        // stay single tokens.
        if (c == '#' && !in_token) {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            char const quote = c;
            in_token = true;
            opened_at = line;
            ++i;
            bool closed = false;
            while (i < text.size()) {
                char const d = text[i];
                if (quote == '"' && d == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) {
                    current += text[i + 1];
                    i += 2;
                    continue;
                }
                if (d == quote) {
                    closed = true;
                    ++i;
                    break;
                }
                if (d == '\n') ++line;
                current += d;
                ++i;
            }
            if (!closed) {
                out.error = std::string(label) + ":" + std::to_string(opened_at) +
                            (quote == '"' ? ": unterminated \"" : ": unterminated '");
                out.tokens.clear();
                return out;
            }
            continue;
        }
        if (c == '\\' && i + 1 < text.size() && text[i + 1] != '\n') {
            in_token = true;
            current += text[i + 1];
            i += 2;
            continue;
        }
        in_token = true;
        current += c;
        ++i;
    }
    flush();
    return out;
}

std::optional<std::filesystem::path> config_prompt_dir() {
    if (char const* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "prompt" / "prompts";
    }
    if (char const* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "prompt" / "prompts";
    }
    return std::nullopt;
}

bool is_file(std::filesystem::path const& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

// `base_dir` is the directory of the file being expanded (the current
// directory at the top level), so a nested `inner.prompt` means the one next to
// the file that named it, exactly like a relative include.
std::optional<std::filesystem::path> resolve(std::string_view token, std::filesystem::path const& base_dir,
                                             std::filesystem::path const& git_root) {
    std::filesystem::path const given{std::string(token)};
    // A token with a separator is a path the user spelled out: use it as-is.
    bool const explicit_path = token.contains('/');

    auto candidate = base_dir / given;
    if (is_file(candidate)) return candidate;
    if (explicit_path) return std::nullopt;
    if (!git_root.empty()) {
        candidate = git_root / ".prompt" / given;
        if (is_file(candidate)) return candidate;
    }
    if (auto const dir = config_prompt_dir()) {
        candidate = *dir / given;
        if (is_file(candidate)) return candidate;
    }
    return std::nullopt;
}

std::filesystem::path canonical_path(std::filesystem::path const& path) {
    std::error_code ec;
    auto const canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : canonical;
}

std::optional<std::string> read_text(std::filesystem::path const& path, std::string& error) {
    errno = 0;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        int const code = errno;
        error = "prompt: cannot read prompt file '" + path.string() +
                "': " + (code == 0 ? std::string("cannot open") : std::string(std::strerror(code)));
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

struct frame {
    std::filesystem::path canonical;
    std::string display; // the token as it was written, for error messages
};

// Splices resolved prompt files into one flat token list. `active_` is the
// nesting stack: it detects cycles and names the chain in depth errors.
class expander {
  public:
    expander(std::filesystem::path const& git_root, std::filesystem::path base_dir, std::vector<std::string>& storage)
        : git_root_(git_root), base_dir_(std::move(base_dir)), storage_(storage) {}

    // `sequence` and `file` recurse into each other, but the chain is bounded:
    // `file` rejects a cycle before recursing and refuses depth >=
    // kMaxPromptFileDepth, so the nesting is at most that many frames.
    // NOLINTNEXTLINE(misc-no-recursion)
    bool sequence(std::vector<std::string> const& tokens, std::size_t depth) {
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            std::string const& token = tokens[i];
            if (token == "--") {
                // Explicit end of expansion: the rest belongs to another tool
                // (`.exec -- …`) and must reach it untouched.
                storage_.insert(storage_.end(), tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.end());
                return true;
            }
            if (!has_prompt_file_suffix(token)) {
                storage_.push_back(token);
                continue;
            }
            auto const resolved = resolve(token, base_dir_, git_root_);
            if (!resolved) {
                // Only the head position is unambiguously a prompt-file
                // request; elsewhere the prompt owning the argument decides
                // (`.files missing.prompt` keeps its own warning).
                if (i == 0) {
                    error_ = missing_file_message(token);
                    return false;
                }
                storage_.push_back(token);
                continue;
            }
            if (!file(*resolved, token, depth)) return false;
        }
        return true;
    }

    [[nodiscard]] std::string const& error() const { return error_; }

  private:
    // NOLINTNEXTLINE(misc-no-recursion) — see the depth/cycle guards in the body
    bool file(std::filesystem::path const& path, std::string const& token, std::size_t depth) {
        auto const canonical = canonical_path(path);

        auto const same = [&](frame const& open) { return open.canonical == canonical; };
        if (auto const it = std::ranges::find_if(active_, same); it != active_.end()) {
            auto const index = static_cast<std::size_t>(it - active_.begin());
            error_ = "prompt: prompt file cycle: " + chain_from(index, token);
            return false;
        }
        if (depth >= kMaxPromptFileDepth) {
            error_ = "prompt: prompt files nested too deep (" + std::to_string(kMaxPromptFileDepth) +
                     "): " + chain_from(0, token);
            return false;
        }

        std::string read_error;
        auto text = read_text(path, read_error);
        if (!text) {
            error_ = read_error;
            return false;
        }
        auto lexed = lex_text(*text, path.string());
        if (!lexed.error.empty()) {
            error_ = "prompt: " + lexed.error;
            return false;
        }
        auto tokens = std::move(lexed.tokens);
        // `prompt .note …` pasted verbatim is the obvious mistake; accept it.
        if (!tokens.empty() && tokens.front() == "prompt") tokens.erase(tokens.begin());

        auto const previous_base = base_dir_;
        base_dir_ = path.parent_path();
        active_.push_back(frame{.canonical = canonical, .display = token});
        bool const ok = sequence(tokens, depth + 1);
        active_.pop_back();
        base_dir_ = previous_base;
        return ok;
    }

    [[nodiscard]] std::string chain_from(std::size_t first, std::string const& current) const {
        std::string out;
        for (std::size_t i = first; i < active_.size(); ++i) {
            out += active_[i].display;
            out += " -> ";
        }
        out += current;
        return out;
    }

    [[nodiscard]] static std::string missing_file_message(std::string const& token) {
        std::error_code ec;
        if (std::filesystem::exists(token, ec)) return "prompt: '" + token + "' is not a regular file";
        return "prompt: cannot find prompt file '" + token + "'";
    }

    std::filesystem::path const& git_root_;
    std::filesystem::path base_dir_;
    std::vector<std::string>& storage_;
    std::vector<frame> active_;
    std::string error_;
};

} // namespace

bool has_prompt_file_suffix(std::string_view token) {
    constexpr std::string_view suffix = ".prompt";
    return token.size() > suffix.size() && token.ends_with(suffix);
}

tokenized_text tokenize_prompt_text(std::string_view text, std::string_view label) { return lex_text(text, label); }

namespace {

// The shared core of the two public entry points: `head` (an argv[0], when
// there is one) is copied verbatim, `rest` is spliced.
expanded_argv expand_token_list(std::vector<std::string> rest, std::filesystem::path const& git_root,
                                std::vector<std::string> head, bool expand) {
    expanded_argv out;
    out.storage = std::move(head);

    if (expand) {
        std::error_code ec;
        auto const cwd = std::filesystem::current_path(ec);
        expander splice{git_root, ec ? std::filesystem::path{} : cwd, out.storage};
        if (!splice.sequence(rest, 0)) {
            out.error = splice.error();
            out.storage.clear();
        }
    } else {
        out.storage.insert(out.storage.end(), rest.begin(), rest.end());
    }
    return out;
}

} // namespace

expanded_argv expand_prompt_files(std::span<char const* const> argv, std::filesystem::path const& git_root,
                                  bool expand) {
    std::vector<std::string> tokens;
    tokens.reserve(argv.size());
    for (char const* arg : argv) tokens.emplace_back(arg == nullptr ? "" : arg);

    std::size_t const first = tokens.empty() ? 0 : 1;
    std::vector<std::string> head;
    if (first == 1) head.push_back(std::move(tokens.front()));
    std::vector<std::string> rest(tokens.begin() + static_cast<std::ptrdiff_t>(first), tokens.end());
    return expand_token_list(std::move(rest), git_root, std::move(head), expand);
}

expanded_argv expand_prompt_file_tokens(std::span<std::string_view const> tokens, std::filesystem::path const& git_root,
                                        bool expand) {
    std::vector<std::string> rest;
    rest.reserve(tokens.size());
    for (auto const token : tokens) rest.emplace_back(token);
    return expand_token_list(std::move(rest), git_root, {}, expand);
}

namespace {

// Every non-hidden `*.prompt` file in `dir`, sorted. Empty when `dir` cannot be
// read: completion never reports an error.
std::vector<std::string> prompt_files_in(std::filesystem::path const& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    std::filesystem::directory_iterator it{dir, ec};
    if (ec) return names;
    std::filesystem::directory_iterator const end{};
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        auto const name = it->path().filename().string();
        if (name.empty() || name.front() == '.') continue;
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec) || type_ec) continue;
        if (!std::string_view(name).ends_with(".prompt")) continue;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

void write_prompt_file_candidates(std::ostream& out, std::string_view current, std::filesystem::path const& git_root) {
    // A token with a separator is a path: list that directory, offering its
    // `*.prompt` files and its subdirectories so the user can descend into one.
    if (auto const slash = current.rfind('/'); slash != std::string_view::npos) {
        std::string const shown_dir{current.substr(0, slash + 1)};
        std::string_view const base = current.substr(slash + 1);

        std::error_code ec;
        std::filesystem::directory_iterator it{std::filesystem::path{shown_dir}, ec};
        if (ec) return;
        std::filesystem::directory_iterator const end{};
        std::vector<std::string> files;
        std::vector<std::string> dirs;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            auto const name = it->path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            std::error_code type_ec;
            if (it->is_directory(type_ec)) {
                dirs.push_back(name);
            } else if (it->is_regular_file(type_ec) && !type_ec && std::string_view(name).ends_with(".prompt")) {
                files.push_back(name);
            }
        }
        std::sort(files.begin(), files.end());
        std::sort(dirs.begin(), dirs.end());
        for (auto const& name : files) {
            if (completion::matches(base, name)) completion::write_item(out, shown_dir + name, "prompt file");
        }
        for (auto const& name : dirs) {
            if (completion::matches(base, name)) completion::write_item(out, shown_dir + name + "/", "directory");
        }
        return;
    }

    // A bare name: the current directory first, then the project and user
    // sources the resolver would try, all as the bare name it accepts. A name
    // the sources share is emitted once, by the nearest one.
    std::vector<std::string> seen;
    auto const emit = [&](std::filesystem::path const& dir, std::string_view description) {
        for (auto const& name : prompt_files_in(dir)) {
            if (std::ranges::find(seen, name) != seen.end()) continue;
            seen.push_back(name);
            if (completion::matches(current, name)) completion::write_item(out, name, description);
        }
    };

    std::error_code ec;
    auto const cwd = std::filesystem::current_path(ec);
    if (!ec) emit(cwd, "prompt file");
    if (!git_root.empty()) emit(git_root / ".prompt", "prompt file (project)");
    if (auto const config = config_prompt_dir()) emit(*config, "prompt file (config)");
}

} // namespace prompt
