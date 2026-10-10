#include "prompt/legacy/legacy_runner.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace prompt::legacy {

namespace {

namespace fs = std::filesystem;

// Search dirs mirroring the bash dispatcher: $XDG_CONFIG_DIRS/prompts then
// the repo prompts/ dir relative to this executable (bin/../prompts for the
// installed layout, cpp_prompt/../prompts for the build tree).
std::vector<fs::path> search_dirs() {
    std::vector<fs::path> dirs;
    
    char const* xdg = std::getenv("XDG_CONFIG_DIRS");
    std::string xdg_str = xdg ? xdg : "/etc/xdg";
    std::istringstream ss(xdg_str);
    std::string part;
    while (std::getline(ss, part, ':')) {
        if (!part.empty()) dirs.push_back(fs::path(part) / "prompts");
    }
    
    std::error_code ec;
    auto exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        auto dir = exe.parent_path();
        dirs.push_back(dir / ".." / "prompts");           // cpp_prompt/build -> cpp_prompt/prompts
        dirs.push_back(dir / ".." / ".." / "prompts");    // cpp_prompt/build -> cmd/prompts
        dirs.push_back(dir / ".." / ".." / ".." / "prompts");
    }
    
    return dirs;
}

fs::path find_prompt_file(std::string_view name) {
    static constexpr char const* exts[] = {".sh", ".txt", ".md", ""};
    for (auto const& dir : search_dirs()) {
        for (auto ext : exts) {
            auto candidate = dir / (std::string(name) + ext);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
    }
    return {};
}

} // namespace

process::command_result run_prompt_script(
    std::string_view name,
    std::string_view stdin_data,
    std::span<char const* const> args) noexcept {
    
    auto file = find_prompt_file(name);
    if (file.empty()) {
        return {"", "prompt: prompt '" + std::string(name) + "' not found", 3};
    }
    
    auto ext = file.extension().string();
    bool is_bash = (ext == ".sh");
    
    std::vector<std::string> cmd_storage;
    if (is_bash) {
        cmd_storage.emplace_back("bash");
        cmd_storage.push_back(file.string());
    } else {
        // Text/markdown: print the file, then append stdin (dispatcher parity).
        cmd_storage.emplace_back("cat");
        cmd_storage.push_back(file.string());
        if (!stdin_data.empty()) {
            cmd_storage.emplace_back("-");
        }
    }
    for (std::size_t i = 0; i < args.size() && args[i]; ++i) {
        cmd_storage.emplace_back(args[i]);
    }
    
    std::vector<char const*> argv;
    argv.reserve(cmd_storage.size() + 1);
    for (auto const& s : cmd_storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    
    return process::run_command(argv, stdin_data);
}

} // namespace prompt::legacy
