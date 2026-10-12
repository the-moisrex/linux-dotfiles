#pragma once

#include <string>
#include <string_view>

namespace prompt::sanitize {

std::string strip_osc(std::string_view input);

std::string clean_privacy(std::string_view input);

std::string sanitize(std::string_view input);

} // namespace prompt::sanitize