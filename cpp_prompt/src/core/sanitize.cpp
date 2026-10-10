#include "prompt/core/sanitize.hpp"
#include <regex>
#include <string>
#include <string_view>
#include <cstdlib>
#include <unistd.h>

namespace prompt::sanitize {

std::string strip_osc(std::string_view input) noexcept {
    static const std::regex osc_regex(
        R"(\x1B(\[[0-9;]*[A-Za-z]|\][^\x07]*(\x07|\x1B\\)|[()][AB012]|[=>]|[NOM78HcDEFGIJKLMPQRSTUVWXYZ^_]|%[G8]|#[0-9]))"
    );
    return std::regex_replace(std::string(input), osc_regex, "");
}

std::string clean_privacy(std::string_view input) noexcept {
    std::string output(input);
    
    char const* user = std::getenv("USER");
    char const* home = std::getenv("HOME");
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    char const* tmpdir = std::getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    
    auto replace_all = [&output](std::regex const& re, std::string_view replacement) {
        output = std::regex_replace(output, re, replacement.data());
    };
    
    if (std::string(tmpdir) != "/tmp") {
        replace_all(std::regex(std::string(tmpdir) + "/"), "/tmp/");
        replace_all(std::regex(std::string(tmpdir)), "/tmp");
    }
    if (home) {
        replace_all(std::regex(std::string(home) + "/Projects/"), "");
        replace_all(std::regex(std::string(home)), "HOME");
    }
    if (user) {
        replace_all(std::regex(std::string(user)), "USER");
    }
    replace_all(std::regex(std::string(hostname)), "HOSTNAME");
    
    replace_all(std::regex(R"(\b([0-9]{1,3}\.){3}[0-9]{1,3}\b)"), "IP_ADDRESS");
    replace_all(std::regex(R"(\b([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})\b)"), "MAC_ADDRESS");
    
    static const std::regex email_regex(
        R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.(?:com|org|net|edu|gov|mil|biz|info|io|co|ai|de|uk|ca|fr|au|jp|ir)\b)"
    );
    replace_all(email_regex, "EMAIL_ADDRESS");
    
    replace_all(std::regex(R"(\([0-9]{3}\) [0-9]{3}-[0-9]{4})"), "PHONE_NUMBER");
    replace_all(std::regex(R"([0-9]{3}-[0-9]{3}-[0-9]{4})"), "PHONE_NUMBER");
    
    replace_all(std::regex(R"([0-9]{3}-[0-9]{2}-[0-9]{4})"), "SSN");
    
    return output;
}

std::string sanitize(std::string_view input) noexcept {
    return clean_privacy(strip_osc(input));
}

} // namespace prompt::sanitize