#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace prompt::python {

void init() noexcept;

std::expected<std::string, std::string> call(
    std::string_view module,
    std::string_view function,
    std::span<std::string_view const> args) noexcept;

bool available() noexcept;

} // namespace prompt::python