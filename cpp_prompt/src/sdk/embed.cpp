#include "prompt/sdk/embed.hpp"
#include <fstream>
#include <string>
#include <string_view>
#include <filesystem>

namespace prompt {

std::string infer_lang(std::filesystem::path const& file) noexcept {
    auto ext = file.extension().string();
    if (ext == ".c" || ext == ".h") return "c";
    if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" || 
        ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".ixx") return "cpp";
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
    if (ext == ".html" || ext == ".htm") return "html";
    if (ext == ".css") return "css";
    if (ext == ".dockerfile" || file.filename() == "Dockerfile") return "dockerfile";
    if (file.filename() == "Makefile" || file.filename() == "GNUmakefile") return "makefile";
    if (file.filename() == "CMakeLists.txt") return "cmake";
    return "text";
}

std::string trim_context(std::string_view content, std::size_t head_lines) noexcept {
    if (head_lines == 0) return std::string(content);
    
    std::size_t lines = 0;
    std::size_t pos = 0;
    while (pos < content.size() && lines < head_lines) {
        pos = content.find('\n', pos);
        if (pos == std::string_view::npos) break;
        pos++;
        lines++;
    }
    return std::string(content.substr(0, pos));
}

std::string read_file(std::filesystem::path const& path) noexcept {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string embed_file(
    std::filesystem::path const& path,
    std::string_view label,
    std::size_t head_lines) noexcept {
    
    if (!std::filesystem::exists(path)) return "";
    
    std::string name = label.empty() ? path.filename().string() : std::string(label);
    std::string content = read_file(path);
    if (content.empty()) return "";
    
    std::string output;
    output += "File: " + name + "\n\n";
    output += "```" + infer_lang(path) + "\n";
    output += trim_context(content, head_lines);
    output += "\n```\n\n";
    return output;
}

std::string embed_stdin(std::string_view content, std::size_t head_lines) noexcept {
    if (content.empty()) return "";
    std::string output = trim_context(content, head_lines);
    output += "\n\n";
    return output;
}

} // namespace prompt