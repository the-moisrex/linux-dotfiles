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
    std::size_t head_lines = 0;
    std::string stdin_content;
    bool stdin_consumed = false;
    std::filesystem::path git_root;

    std::string (*clipboard_content_fn)() = nullptr;
    std::optional<std::string> (*clipboard_identifier_fn)() = nullptr;
    std::vector<std::filesystem::path> (*select_files_fn)() = nullptr;
    std::optional<std::filesystem::path> (*resolve_input_file_fn)(std::string_view) = nullptr;

    [[nodiscard]] std::string clipboard_content() const noexcept {
        return clipboard_content_fn ? clipboard_content_fn() : "";
    }

    [[nodiscard]] std::optional<std::string> clipboard_identifier() const noexcept {
        return clipboard_identifier_fn ? clipboard_identifier_fn() : std::nullopt;
    }

    [[nodiscard]] std::vector<std::filesystem::path> select_files() const noexcept {
        return select_files_fn ? select_files_fn() : std::vector<std::filesystem::path>{};
    }

    [[nodiscard]] std::optional<std::filesystem::path> resolve_input_file(std::string_view wanted) const noexcept {
        return resolve_input_file_fn ? resolve_input_file_fn(wanted) : std::nullopt;
    }
};

prompt_context make_base_context() noexcept;

} // namespace prompt