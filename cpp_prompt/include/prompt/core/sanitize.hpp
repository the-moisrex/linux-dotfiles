#pragma once

#include <string>
#include <string_view>

namespace prompt::sanitize {

std::string strip_osc(std::string_view input) noexcept;

std::string clean_privacy(std::string_view input) noexcept;

std::string sanitize(std::string_view input) noexcept;

} // namespace prompt::sanitize