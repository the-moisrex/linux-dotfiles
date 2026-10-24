#include "sanitize.hpp"
#include "strings.hpp"
#include <array>
#include <cstdlib>
#include <regex>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace prompt::sanitize {

std::string strip_osc(std::string_view input) {
    // An ESC byte is what every branch of the pattern starts with, so text
    // without one is already clean and skips the regex engine entirely (the
    // common case: file contents, model answers, plain tool output).
    if (!input.contains('\x1B')) return std::string(input);

    static const std::regex osc_regex(
        R"(\x1B(\[[0-9;]*[A-Za-z]|\][^\x07]*(\x07|\x1B\\)|[()][AB012]|[=>]|[NOM78HcDEFGIJKLMPQRSTUVWXYZ^_]|%[G8]|#[0-9]))");
    return std::regex_replace(std::string(input), osc_regex, "");
}

namespace {

struct redaction_rule {
    std::regex pattern;
    std::string replacement;
};

// Compiled once per process: the environment the rules interpolate ($HOME,
// $USER, $TMPDIR, the hostname) does not change while the dispatcher runs, so
// building the table here replaces compiling every pattern on every call —
// std::regex construction is far more expensive than the matches themselves.
std::vector<redaction_rule> const& redaction_rules() {
    static std::vector<redaction_rule> const rules = [] {
        std::vector<redaction_rule> built;
        auto add = [&built](std::string const& pattern, std::string replacement) {
            // $HOME/$USER/$TMPDIR are interpolated into regular expressions:
            // any regex metacharacter they contain would throw std::regex_error.
            // Escaping keeps a hostile environment from aborting the
            // dispatcher, and a pattern that still cannot compile is skipped.
            try {
                built.push_back({.pattern = std::regex(pattern), .replacement = std::move(replacement)});
            } catch (...) { // NOLINT(bugprone-empty-catch) — uncompilable pattern: ignored.
            }
        };
        auto quote = [](std::string_view raw) { return prompt::text::regex_escape(raw); };

        char const* user = std::getenv("USER");
        char const* home = std::getenv("HOME");
        char const* tmpdir = std::getenv("TMPDIR");
        if (tmpdir == nullptr) tmpdir = "/tmp";
        std::array<char, 256> hostname{};
        gethostname(hostname.data(), hostname.size());

        if (std::string(tmpdir) != "/tmp") {
            add(quote(tmpdir) + "/", "/tmp/");
            add(quote(tmpdir), "/tmp");
        }
        if (home != nullptr) {
            add(quote(home) + "/Projects/", "");
            add(quote(home), "HOME");
        }
        if (user != nullptr) {
            add(quote(user), "USER");
        }
        add(quote(hostname.data()), "HOSTNAME");

        add(R"(\b([0-9]{1,3}\.){3}[0-9]{1,3}\b)", "IP_ADDRESS");
        add(R"(\b([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})\b)", "MAC_ADDRESS");
        add(R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.(?:com|org|net|edu|gov|mil|biz|info|io|co|ai|de|uk|ca|fr|au|jp|ir)\b)",
            "EMAIL_ADDRESS");
        add(R"(\([0-9]{3}\) [0-9]{3}-[0-9]{4})", "PHONE_NUMBER");
        add(R"([0-9]{3}-[0-9]{3}-[0-9]{4})", "PHONE_NUMBER");
        add(R"([0-9]{3}-[0-9]{2}-[0-9]{4})", "SSN");
        return built;
    }();
    return rules;
}

} // namespace

static std::string clean_privacy_impl(std::string_view input, bool* redacted) {
    std::string output(input);
    for (auto const& rule : redaction_rules()) {
        output = std::regex_replace(output, rule.pattern, rule.replacement);
    }

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
