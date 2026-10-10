#pragma once

#include "prompt/tse/tse_models.hpp"
#include <expected>
#include <span>
#include <string_view>

namespace prompt::tse {

std::expected<collected_data, std::string> collect(
    std::string_view symbol,
    int days,
    int top,
    bool with_codal,
    bool adjusted) noexcept;

std::expected<std::string, std::string> render_markdown(
    nlohmann::json const& data) noexcept;

} // namespace prompt::tse