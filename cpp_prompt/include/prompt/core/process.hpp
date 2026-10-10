#pragma once

#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::process {

struct command_result {
    std::string stdout_data;
    std::string stderr_data;
    int exit_code = 0;
    bool timed_out = false;
};

command_result run_command(
    std::span<char const* const> argv,
    std::string_view stdin_data = {},
    std::span<char const* const> env = {},
    std::chrono::milliseconds timeout = std::chrono::seconds(30)) noexcept;

command_result run_pipeline(
    std::vector<std::vector<std::string>> const& commands,
    std::string_view stdin_data = {},
    std::chrono::milliseconds timeout = std::chrono::seconds(60)) noexcept;

struct async_command {
    std::vector<std::string> cmd;
    std::string stdin_data;
    std::chrono::milliseconds timeout;
};

std::vector<command_result> run_async(
    std::span<async_command const> commands) noexcept;

} // namespace prompt::process