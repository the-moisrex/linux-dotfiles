#include "embed.hpp"
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prompt {

std::string_view infer_lang(std::filesystem::path const& file) {
    auto ext = file.extension().string();
    if (ext == ".c" || ext == ".h") return "c";
    if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" || ext == ".hpp" || ext == ".hh" ||
        ext == ".hxx" || ext == ".ixx")
        return "cpp";
    if (ext == ".rs") return "rust";
    if (ext == ".py" || ext == ".pyi") return "python";
    if (ext == ".sh" || ext == ".bash") return "bash";
    if (ext == ".js" || ext == ".cjs" || ext == ".mjs") return "javascript";
    if (ext == ".ts" || ext == ".mts" || ext == ".cts") return "typescript";
    if (ext == ".jsx") return "jsx";
    if (ext == ".tsx") return "tsx";
    if (ext == ".java") return "java";
    if (ext == ".kt" || ext == ".kts") return "kotlin";
    if (ext == ".go") return "go";
    if (ext == ".rb") return "ruby";
    if (ext == ".lua") return "lua";
    if (ext == ".json" || ext == ".stock" || ext == ".tse") return "json";
    if (ext == ".yaml" || ext == ".yml") return "yaml";
    if (ext == ".toml") return "toml";
    if (ext == ".md") return "markdown";
    if (ext == ".txt" || ext == ".log") return "text";
    if (ext == ".sql") return "sql";
    if (ext == ".csv") return "csv";
    if (ext == ".tsv") return "tsv";
    if (ext == ".html" || ext == ".htm") return "html";
    if (ext == ".css") return "css";
    if (ext == ".dockerfile" || file.filename() == "Dockerfile") return "dockerfile";
    if (file.filename() == "Makefile" || file.filename() == "GNUmakefile") return "makefile";
    if (file.filename() == "CMakeLists.txt") return "cmake";
    return "text";
}

std::string trim_context(std::string_view content, std::optional<std::size_t> head_lines) {
    if (!head_lines) return std::string(content);

    // sed's `1,0p` prints line 1, so a 0 cap keeps a single line.
    std::size_t limit = *head_lines == 0 ? 1 : *head_lines;
    std::size_t lines = 0;
    std::size_t pos = 0;
    while (pos < content.size() && lines < limit) {
        pos = content.find('\n', pos);
        if (pos == std::string_view::npos) break;
        pos++;
        lines++;
    }
    return std::string(content.substr(0, pos));
}

std::string trim_context_nl(std::string_view content, std::optional<std::size_t> head_lines) {
    while (!content.empty() && content.back() == '\n') content.remove_suffix(1);
    if (!head_lines) return std::string(content) + "\n";

    std::size_t limit = *head_lines == 0 ? 1 : *head_lines;
    std::size_t lines = 0;
    std::size_t pos = 0;
    while (pos < content.size() && lines < limit) {
        pos = content.find('\n', pos);
        if (pos == std::string_view::npos) break;
        ++pos;
        ++lines;
    }
    if (lines == limit) return std::string(content.substr(0, pos));
    return std::string(content) + "\n";
}

std::string head_output(std::string_view text, std::size_t n) {
    if (text.empty()) return {};

    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
        if (start == text.size()) break;
    }
    if (lines.size() <= n) return std::string(text);

    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        if (i != 0U) out += '\n';
        out += lines[i];
    }
    return out;
}

std::string read_file(std::filesystem::path const& path) {
    // A directory opens successfully but throws from the stream on read
    // (`basic_filebuf::underflow: Is a directory`), so gate on a regular file.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return "";
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string number_lines(std::string_view text, bool numbered) {
    if (!numbered || text.empty()) return std::string(text);

    bool ends_with_newline = text.back() == '\n';

    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (lines.empty()) return std::string(text);

    std::size_t width = std::to_string(lines.size()).size();
    std::string out;
    out.reserve(text.size() + (lines.size() * (width + 3)));
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string number = std::to_string(i + 1);
        out.append(width - number.size(), ' ');
        out += number;
        out += " | ";
        out += lines[i];
        out += '\n';
    }
    if (!ends_with_newline) out.pop_back(); // the input had no final newline
    return out;
}

std::string embed_file(std::filesystem::path const& path, std::string_view label, file_options opts) {

    if (!std::filesystem::exists(path)) return "";

    std::string name = label.empty() ? path.filename().string() : std::string(label);
    std::string content = read_file(path);
    if (content.empty()) return "";

    std::string output;
    output += std::format("File: {}\n\n", name);
    output += std::format("```{}\n", infer_lang(path));
    output += number_lines(trim_context(content, opts.head_lines), opts.numbered);
    output += "\n```\n\n";
    return output;
}

std::string embed_stdin(std::string_view content, std::optional<std::size_t> head_lines) {
    (void)head_lines; // bash's embed_stdin does not apply --head
    while (!content.empty() && content.back() == '\n') content.remove_suffix(1);
    return std::string(content) + "\n\n";
}

std::string embed_file_block(std::filesystem::path const& file, file_options opts) {
    std::string output;
    output += std::format("File: {}\n\n", file.filename().string());
    output += std::format("```{}\n", infer_lang(file));
    output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
    output += "```\n\n";
    return output;
}

void warn_missing_file(std::string& errors, std::string_view name) {
    errors += "Warning: File '";
    errors += name;
    errors += "' not found.\n";
}

} // namespace prompt