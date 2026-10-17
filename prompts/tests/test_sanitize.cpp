#include "../src/core/sanitize.hpp"
#include <cstdio>
#include <string>
#include <string_view>

using prompt::sanitize::sanitize;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_no_redaction() {
    auto r = sanitize("a perfectly ordinary line with no personal data\n");
    check(!r.redacted, "plain text is not marked redacted");
    check(r.text == "a perfectly ordinary line with no personal data\n", "plain text is unchanged");
}

void test_ip_is_redacted() {
    auto r = sanitize("server at 10.0.0.1 answered\n");
    check(r.redacted, "an IP address marks the text redacted");
    check(r.text == "server at IP_ADDRESS answered\n", "the IP is replaced by IP_ADDRESS");
}

void test_email_is_redacted() {
    auto r = sanitize("mail someone@example.com now\n");
    check(r.redacted, "an email marks the text redacted");
    check(r.text == "mail EMAIL_ADDRESS now\n", "the email is replaced by EMAIL_ADDRESS");
}

void test_legend_mentions_the_placeholders() {
    std::string_view legend = prompt::sanitize::kRedactionLegend;
    check(legend.starts_with("[prompt] "), "the legend carries the harness-visible marker");
    check(legend.contains("IP_ADDRESS"), "the legend lists IP_ADDRESS");
    check(legend.contains("EMAIL_ADDRESS"), "the legend lists EMAIL_ADDRESS");
}

} // namespace

int main() {
    test_no_redaction();
    test_ip_is_redacted();
    test_email_is_redacted();
    test_legend_mentions_the_placeholders();

    if (failures != 0) {
        std::fprintf(stderr, "%d sanitize check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
