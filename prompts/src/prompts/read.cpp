#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace prompt::prompts {

namespace {

// --offset N: the 1-based starting position of the read window.
[[nodiscard]] std::size_t parse_offset(std::span<std::string_view const> args) {
    std::optional<std::size_t> offset;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] != "--offset") {
            continue;
        }

        if (offset || i + 1 >= args.size()) [[unlikely]] {
            throw usage_error("read: --offset requires exactly one positive integer value\n");
        }

        std::string_view const value = args[++i];
        std::size_t parsed = 0;

        auto const [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);

        if (value.empty() || value.front() < '0' || value.front() > '9' || ec != std::errc{} ||
            ptr != value.data() + value.size()) [[unlikely]] {
            throw usage_error(std::format("read: invalid --offset value: {}\n", value));
        }

        if (parsed == 0) [[unlikely]] {
            throw usage_error(std::format("read: --offset out of range: {}\n", value));
        }

        offset = parsed;
    }

    return offset.value_or(1);
}

// Extract FILE... while removing options and their values.
[[nodiscard]] std::vector<std::string> read_positionals(std::span<std::string_view const> args) {
    std::vector<std::string> result;
    result.reserve(args.size());

    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view const arg = args[i];

        if (arg == "--head" || arg == "--offset") {
            ++i;
        } else if (is_number_flag(arg)) {
            continue;
        } else if (arg.size() > 1 && arg.front() == '-' && arg != "-") [[unlikely]] {
            throw usage_error(std::format("read: unknown option: {}\n", arg));
        } else {
            result.emplace_back(arg);
        }
    }

    return result;
}

// A NUL byte in the first 8 KiB indicates binary content.
[[nodiscard]] bool is_binary(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) [[unlikely]] {
        return false;
    }

    std::array<char, 8192> buffer{};
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    auto const count = file.gcount();
    return std::find(buffer.begin(), buffer.begin() + count, '\0') != buffer.begin() + count;
}

void append_truncation(std::string& output, std::size_t next_offset) {
    output += std::format("[Output truncated. Continue reading with offset: {}]\n", next_offset);
}

void append_numbered_line(std::string& output, std::size_t line_number, std::string_view line) {
    output += std::format("{}: {}\n", line_number, line);
}

} // namespace

static void execute_read(view const& v, result& r) {
    file_options const opts = file_options_or_throw(v);
    std::size_t const offset = parse_offset(v.args);

    // Preserve the existing --head 0 behavior.
    std::size_t const limit = opts.head_lines == 0 ? 1 : opts.head_lines.value_or(2000);

    std::string output;
    std::string error;

    // Piped stdin is consumed once and shared with subsequent chain segments.
    if (v.in.available()) {
        std::string_view const text = v.in.text();
        v.in.spend();

        std::vector<std::string_view> lines;
        lines.reserve(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1);

        for (std::size_t pos = 0; pos < text.size();) {
            auto const newline = text.find('\n', pos);

            if (newline == std::string_view::npos) {
                lines.push_back(text.substr(pos));
                break;
            }

            lines.push_back(text.substr(pos, newline - pos));
            pos = newline + 1;
        }

        std::size_t const begin = offset - 1;

        // Check before subtracting: size_t arithmetic would otherwise underflow.
        if (begin >= lines.size()) [[unlikely]] {
            output = "Read stdin, 0 lines\n";
        } else {
            std::size_t const end = begin + std::min(limit, lines.size() - begin);

            output += std::format("Read stdin, lines {}-{}\n", begin + 1, end);

            for (std::size_t i = begin; i < end; ++i) {
                append_numbered_line(output, i + 1, lines[i]);
            }

            if (end < lines.size()) {
                append_truncation(output, end + 1);
            }
        }

        r.out = std::move(output);
        r.err = std::move(error);
        return;
    }

    auto const paths = read_positionals(v.args);
    if (paths.empty()) [[unlikely]] {
        throw usage_error("read: no FILE or DIRECTORY arguments\n");
    }

    for (auto const& path_str : paths) {
        std::filesystem::path const path(path_str);
        std::error_code ec;

        if (std::filesystem::is_directory(path, ec)) {
            std::vector<std::string> entries;
            std::filesystem::directory_iterator it(path, ec);
            std::filesystem::directory_iterator const end_it;

            if (ec) [[unlikely]] {
                error += std::format("read: {}: {}\n", path_str, ec.message());
                continue;
            }

            for (; it != end_it; it.increment(ec)) {
                if (ec) [[unlikely]] {
                    break;
                }
                entries.push_back(it->path().string());
            }

            if (ec) [[unlikely]] {
                error += std::format("read: {}: {}\n", path_str, ec.message());
                continue;
            }

            std::ranges::sort(entries);

            std::size_t const begin = offset - 1;

            if (begin >= entries.size()) [[unlikely]] {
                output += std::format("Read directory {}, 0 entries\n", path_str);
                continue;
            }

            std::size_t const end = begin + std::min(limit, entries.size() - begin);

            output += std::format("Read directory {}, entries {}-{}\n", path_str, begin + 1, end);

            for (std::size_t i = begin; i < end; ++i) {
                output += entries[i];
                output += '\n';
            }

            if (end < entries.size()) {
                append_truncation(output, end + 1);
            }
        } else if (std::filesystem::is_regular_file(path, ec)) {
            if (is_binary(path)) [[unlikely]] {
                error += std::format("read: {}: binary content, skipped\n", path_str);
                continue;
            }

            std::ifstream file(path);
            if (!file) [[unlikely]] {
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

            while (std::getline(file, line)) {
                ++line_no;

                if (line_no < offset) {
                    continue;
                }

                if (read_count >= limit) {
                    truncated = true;
                    break;
                }

                if (read_count == 0) {
                    actual_start = line_no;
                }

                actual_end = line_no;
                append_numbered_line(numbered, line_no, line);
                ++read_count;
            }

            if (actual_start == 0) [[unlikely]] {
                output += std::format("Read file {}, 0 lines\n", path_str);
            } else {
                output += std::format("Read file {}, lines {}-{}\n", path_str, actual_start, actual_end);

                output += numbered;

                if (truncated) {
                    append_truncation(output, actual_end + 1);
                }
            }
        } else [[unlikely]] {
            if (ec) {
                error += std::format("read: {}: {}\n", path_str, ec.message());
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
