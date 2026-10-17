#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

struct prompt_context {
    // Name of the prompt being executed (set by the dispatcher; used by
    // legacy scripts for $PROMPT_NAME and sub-dispatch in auto).
    std::string_view name;
    std::array<std::string_view, 32> args{};
    std::size_t args_count = 0;
    std::optional<std::size_t> head_lines;
    std::string stdin_content;
    // Set when stdin was piped, and by a prompt that reads it: the dispatcher
    // spends the shared pipe once either flag is set (see main.cpp).
    bool stdin_consumed = false;
    std::filesystem::path git_root;
    // Path of the running executable (for locating repo bin/ utilities).
    std::filesystem::path exe_path;

    std::string (*clipboard_content_fn)() = nullptr;
    std::optional<std::string> (*clipboard_identifier_fn)() = nullptr;
    std::vector<std::filesystem::path> (*select_files_fn)() = nullptr;
    std::optional<std::filesystem::path> (*resolve_input_file_fn)(std::string_view) = nullptr;

    // --- outputs ---------------------------------------------------------
    // A prompt fills these instead of returning a value: `out` is the prompt
    // text for stdout (sanitized, pipelined, clipboard), `err` is stderr on
    // the *success* path (bash's `>&2` output with exit 0), and the flags
    // tell the dispatcher how to treat the result. Failure does not use
    // them: it throws (see sdk/errors.hpp).
    std::string out;
    std::string err;
    bool no_clipboard = false;

    // In-process sub-dispatch (paths -> files, verify -> git-dirty) copies the
    // context; the child must not append to the parent's accumulated text.
    void clear_output() {
        out.clear();
        err.clear();
        no_clipboard = false;
    }

    [[nodiscard]] std::string clipboard_content() const {
        return (clipboard_content_fn != nullptr) ? clipboard_content_fn() : "";
    }

    [[nodiscard]] std::optional<std::string> clipboard_identifier() const {
        return (clipboard_identifier_fn != nullptr) ? clipboard_identifier_fn() : std::nullopt;
    }

    [[nodiscard]] std::vector<std::filesystem::path> select_files() const {
        return (select_files_fn != nullptr) ? select_files_fn() : std::vector<std::filesystem::path>{};
    }

    [[nodiscard]] std::optional<std::filesystem::path> resolve_input_file(std::string_view wanted) const {
        return (resolve_input_file_fn != nullptr) ? resolve_input_file_fn(wanted) : std::nullopt;
    }
};

prompt_context make_base_context();

} // namespace prompt