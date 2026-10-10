#include "prompt/core/process.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

namespace prompt::process {

namespace {
// Temp file holding stdin data so we can both feed the child and capture
// its stdout (popen is unidirectional).
struct stdin_tmpfile {
    std::filesystem::path path;
    explicit stdin_tmpfile(std::string_view data) {
        char tmpl[] = "/tmp/prompt_stdin_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) {
            auto n = ::write(fd, data.data(), data.size());
            (void)n;
            ::close(fd);
            path = tmpl;
        }
    }
    ~stdin_tmpfile() {
        if (!path.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};
} // namespace

command_result run_command(std::span<char const* const> argv, std::string_view stdin_data,
                           std::span<char const* const> env, std::chrono::milliseconds timeout) noexcept {

    (void)env;
    (void)timeout;

    command_result result;

    std::string cmd_str;
    for (auto arg : argv) {
        if (!arg) break;
        if (!cmd_str.empty()) cmd_str += ' ';
        // Single-quote the argument so the shell passes it through verbatim
        // (embedded quotes, $, backticks and newlines must survive).
        cmd_str += '\'';
        for (char const* p = arg; *p != '\0'; ++p) {
            char c = *p;
            if (c == '\'') {
                cmd_str += "'\\''";
            } else {
                cmd_str += c;
            }
        }
        cmd_str += '\'';
    }

    if (cmd_str.empty()) {
        result.exit_code = -1;
        result.stderr_data = "Empty command";
        return result;
    }

    std::unique_ptr<stdin_tmpfile> tmp;
    if (!stdin_data.empty()) {
        tmp = std::make_unique<stdin_tmpfile>(stdin_data);
        cmd_str += " < '";
        cmd_str += tmp->path.string();
        cmd_str += "'";
    }

    FILE* pipe = popen(cmd_str.c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        result.stderr_data = "Failed to execute command";
        return result;
    }

    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        result.stdout_data += buffer.data();
    }

    int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return result;
}

command_result run_pipeline(std::vector<std::vector<std::string>> const& commands, std::string_view stdin_data,
                            std::chrono::milliseconds timeout) noexcept {

    if (commands.empty()) {
        return {"", "No commands in pipeline", -1};
    }

    std::string current_input(stdin_data);
    command_result last_result;

    for (auto const& cmd : commands) {
        std::vector<char const*> argv;
        argv.reserve(cmd.size() + 1);
        for (auto const& arg : cmd) argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        last_result = run_command(argv, current_input, {}, timeout);
        current_input = last_result.stdout_data;

        if (last_result.exit_code != 0) break;
    }

    return last_result;
}

std::vector<command_result> run_async(std::span<async_command const> commands) noexcept {

    std::vector<std::future<command_result>> futures;
    futures.reserve(commands.size());

    for (auto const& cmd : commands) {
        futures.push_back(std::async(std::launch::async, [&cmd]() {
            std::vector<char const*> argv;
            argv.reserve(cmd.cmd.size() + 1);
            for (auto const& arg : cmd.cmd) argv.push_back(arg.c_str());
            argv.push_back(nullptr);
            return run_command(argv, cmd.stdin_data, {}, cmd.timeout);
        }));
    }

    std::vector<command_result> results;
    results.reserve(commands.size());
    for (auto& f : futures) {
        results.push_back(f.get());
    }

    return results;
}

} // namespace prompt::process