#include "sanitize.hpp"
#include "strings.hpp"
#include <array>
#include <cstdlib>
#include <regex>
#include <string>
#include <string_view>
#include <unistd.h>

namespace prompt::sanitize {

std::string strip_osc(std::string_view input) {
    static const std::regex osc_regex(
        R"(\x1B(\[[0-9;]*[A-Za-z]|\][^\x07]*(\x07|\x1B\\)|[()][AB012]|[=>]|[NOM78HcDEFGIJKLMPQRSTUVWXYZ^_]|%[G8]|#[0-9]))");
    return std::regex_replace(std::string(input), osc_regex, "");
}

std::string clean_privacy_impl(std::string_view input, bool* redacted) {
    // $HOME/$USER/$TMPDIR are interpolated into regular expressions: any regex
    // metacharacter they contain would throw std::regex_error. Escaping keeps
    // a hostile environment from aborting the dispatcher.
    auto quote_re = [](std::string_view raw) { return prompt::text::regex_escape(raw); };

    std::string output(input);
    auto replace_all_re = [&output](std::string_view pattern, std::string_view replacement) {
        try {
            output = std::regex_replace(output, std::regex(std::string(pattern)), std::string(replacement));
        } catch (...) { // NOLINT(bugprone-empty-catch) — uncompilable pattern: leave the text untouched.
        }
    };

    char const* user = std::getenv("USER");
    char const* home = std::getenv("HOME");
    std::array<char, 256> hostname{};
    gethostname(hostname.data(), hostname.size());
    char const* tmpdir = std::getenv("TMPDIR");
    if (tmpdir == nullptr) tmpdir = "/tmp";

    if (std::string(tmpdir) != "/tmp") {
        replace_all_re(quote_re(tmpdir) + "/", "/tmp/");
        replace_all_re(quote_re(tmpdir), "/tmp");
    }
    if (home != nullptr) {
        replace_all_re(quote_re(home) + "/Projects/", "");
        replace_all_re(quote_re(home), "HOME");
    }
    if (user != nullptr) {
        replace_all_re(quote_re(user), "USER");
    }
    replace_all_re(quote_re(hostname.data()), "HOSTNAME");

    replace_all_re(R"(\b([0-9]{1,3}\.){3}[0-9]{1,3}\b)", "IP_ADDRESS");
    replace_all_re(R"(\b([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})\b)", "MAC_ADDRESS");

    replace_all_re(
        R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.(?:com|org|net|edu|gov|mil|biz|info|io|co|ai|de|uk|ca|fr|au|jp|ir)\b)",
        "EMAIL_ADDRESS");

    replace_all_re(R"(\([0-9]{3}\) [0-9]{3}-[0-9]{4})", "PHONE_NUMBER");
    replace_all_re(R"([0-9]{3}-[0-9]{3}-[0-9]{4})", "PHONE_NUMBER");

    replace_all_re(R"([0-9]{3}-[0-9]{2}-[0-9]{4})", "SSN");

    if (redacted != nullptr) *redacted = (output != input);
    return output;
}

std::string clean_privacy(std::string_view input) { return clean_privacy_impl(input, nullptr); }

result sanitize(std::string_view input) {
    bool redacted = false;
    std::string text = clean_privacy_impl(strip_osc(input), &redacted);
    return {.text = std::move(text), .redacted = redacted};
}

} // namespace prompt::sanitize