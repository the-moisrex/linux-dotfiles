#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prompt::prompts {

namespace {

// --offset N (custom, value-taking): the 1-based start position of the read
// window. Not part of the shared file-prompt flags, so it lives here and is
// validated here; a non-positive or malformed value fails with exit 2.
std::size_t parse_offset(std::span<std::string_view const> args) {
    std::optional<std::size_t> offset;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] != "--offset") continue;
        if (offset || i + 1 >= args.size()) {
            throw usage_error("read: --offset requires exactly one positive integer value\n");
        }
        std::string const value(args[++i]);
        std::size_t consumed = 0;
        unsigned long long n = 0;
        try {
            if (value.empty() || value.front() < '0' || value.front() > '9') {
                throw std::invalid_argument("not an unsigned integer");
            }
            n = std::stoull(value, &consumed);
            if (consumed != value.size()) throw std::invalid_argument("trailing characters");
        } catch (std::exception const&) {
            throw usage_error("read: invalid --offset value: " + value + "\n");
        }
        if (n == 0 || n > std::numeric_limits<std::size_t>::max()) {
            throw usage_error("read: --offset out of range: " + value + "\n");
        }
        offset = static_cast<std::size_t>(n);
    }
    return offset.value_or(1);
}

// The positional arguments (FILE...), with --head's value, --offset's value,
// and the value-less --num/--numbered removed — the read prompt's own loop
// because --offset is not a shared flag.
std::vector<std::string> read_positionals(std::span<std::string_view const> args) {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view const a = args[i];
        if (a == "--head" || a == "--offset") {
            ++i;
        } else if (is_number_flag(a)) {
            continue;
        } else if (a.size() > 1 && a.front() == '-' && a != "-") {
            throw usage_error("read: unknown option: " + std::string(a) + "\n");
        } else {
            result.emplace_back(a);
        }
    }
    return result;
}

// Binary detection: a NUL byte in the first 8 KB is enough. The original tool
// returned a BinaryFileError; a prompt skips the file with a warning so the
// rest of the chain still gets the readable files.
bool is_binary(std::filesystem::path const& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::array<char, 8192> buf{};
    f.read(buf.data(), buf.size());
    std::streamsize const n = f.gcount();
    for (std::streamsize i = 0; i < n; ++i) {
        if (buf[i] == '\0') return true;
    }
    return false;
}

} // namespace

static void execute_read(view const& v, result& r) {
    file_options const opts = file_options_or_throw(v);
    std::size_t const offset = parse_offset(v.args);
    // The tool defaults and caps at 2000; --head 0 is sed's `1,0p` (one line).
    std::size_t const limit = opts.head_lines == 0 ? 1 : opts.head_lines.value_or(2000);

    std::string output;
    std::string error;

    // Piped stdin: a line-numbered read of the (anonymous) input. spend() after
    // text() so a later chain segment sees the shared pipe already consumed.
    if (v.in.available()) {
        std::string_view const text = v.in.text();
        v.in.spend();

        std::vector<std::string_view> lines;
        std::size_t pos = 0;
        while (pos < text.size()) {
            auto const nl = text.find('\n', pos);
            if (nl == std::string_view::npos) {
                lines.push_back(text.substr(pos));
                break;
            }
            lines.push_back(text.substr(pos, nl - pos));
            pos = nl + 1;
        }

        std::size_t const begin = offset - 1;
        std::size_t const end = begin + std::min(limit, lines.size() - begin);
        if (begin >= lines.size()) {
            output += "Read stdin, 0 lines\n";
        } else {
            output += "Read stdin, lines " + std::to_string(begin + 1) + "-" + std::to_string(end) + "\n";
            for (std::size_t i = begin; i < end; ++i) {
                output += std::to_string(i + 1);
                output += ": ";
                output += lines[i];
                output += '\n';
            }
            if (end < lines.size()) {
                output += "[Output truncated. Continue reading with offset: " + std::to_string(end + 1) + "]\n";
            }
        }
        r.out = std::move(output);
        r.err = std::move(error);
        return;
    }

    auto const paths = read_positionals(v.args);
    if (paths.empty()) throw usage_error("read: no FILE or DIRECTORY arguments\n");

    for (auto const& path_str : paths) {
        std::filesystem::path const p(path_str);
        std::error_code ec;

        if (std::filesystem::is_directory(p, ec)) {
            std::vector<std::string> entries;
            std::filesystem::directory_iterator it(p, ec);
            std::filesystem::directory_iterator end_it;
            if (ec) {
                error += "read: " + path_str + ": " + ec.message() + "\n";
                continue;
            }
            for (; it != end_it; it.increment(ec)) {
                if (ec) break;
                entries.push_back(it->path().string());
            }
            if (ec) {
                error += "read: " + path_str + ": " + ec.message() + "\n";
                continue;
            }
            std::sort(entries.begin(), entries.end());

            std::size_t const begin = offset - 1;
            if (begin >= entries.size()) {
                output += "Read directory " + path_str + ", 0 entries\n";
                continue;
            }
            std::size_t const end = begin + std::min(limit, entries.size() - begin);
            output += "Read directory " + path_str + ", entries " + std::to_string(begin + 1) + "-" +
                      std::to_string(end) + "\n";
            for (std::size_t i = begin; i < end; ++i) {
                output += entries[i];
                output += '\n';
            }
            if (end < entries.size()) {
                output += "[Output truncated. Continue reading with offset: " + std::to_string(end + 1) + "]\n";
            }
        } else if (std::filesystem::is_regular_file(p, ec)) {
            if (is_binary(p)) {
                error += "read: " + path_str + ": binary content, skipped\n";
                continue;
            }
            std::ifstream f(p);
            if (!f) {
                warn_missing_file(error, path_str);
                continue;
            }
            std::string line;
            std::size_t line_no = 0;
            std::size_t read_count = 0;
            std::size_t actual_start = 0;
            std::size_t actual_end = 0;
            std::string numbered;
            bool truncated = false;
            while (std::getline(f, line)) {
                ++line_no;
                if (line_no < offset) continue;
                if (read_count >= limit) {
                    truncated = true;
                    break;
                }
                if (read_count == 0) actual_start = line_no;
                actual_end = line_no;
                numbered += std::to_string(line_no);
                numbered += ": ";
                numbered += line;
                numbered += '\n';
                ++read_count;
            }
            if (actual_start == 0) {
                output += "Read file " + path_str + ", 0 lines\n";
            } else {
                output += "Read file " + path_str + ", lines " + std::to_string(actual_start) + "-" +
                          std::to_string(actual_end) + "\n";
                output += numbered;
                if (truncated) {
                    output +=
                        "[Output truncated. Continue reading with offset: " + std::to_string(actual_end + 1) + "]\n";
                }
            }
        } else [[unlikely]] {
            if (ec) {
                error += "read: " + path_str + ": " + ec.message() + "\n";
            } else {
                warn_missing_file(error, path_str);
            }
        }
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_read(std::ostream& os) {
    os << file_usage_help("read", "[--offset N] [FILE...]") << R"EOF(
Read the contents of a file or directory, with each text line prefixed by its 1-based line number.

Directory entries are returned one per line. Use --offset to start at line or entry N and --head to limit how many lines or entries are read (default 2000).

Options:
--offset N        Start reading from line or entry N (1-based, default 1)
)EOF" << file_options_help("the file or directory content");
}

bool const registered = prompt::register_prompt(
    "read", "Read the contents of a file or directory, with each text line prefixed by its 1-based line number.",
    &execute_read, &render_help_read);

} // namespace prompt::prompts
