#pragma once

#include <string>
#include <string_view>

namespace prompt::sanitize {

std::string strip_osc(std::string_view input);

std::string clean_privacy(std::string_view input);

// The sanitize pipeline (strip_osc then clean_privacy) plus whether clean_privacy
// actually rewrote anything.
struct result {
    std::string text;
    bool redacted = false;
};

result sanitize(std::string_view input);

// One-line note the dispatcher prepends to the output when `redacted` is set, so
// the model knows the placeholder tokens are redactions rather than the original
// file/log text. Native-only: the bash reference does not emit it.
inline constexpr std::string_view kRedactionLegend =
    "[prompt] Note: personal data below was redacted by the prompt pipeline — HOME, USER, HOSTNAME, IP_ADDRESS, "
    "MAC_ADDRESS, EMAIL_ADDRESS, PHONE_NUMBER and SSN are placeholders, not the original text.";

} // namespace prompt::sanitize
