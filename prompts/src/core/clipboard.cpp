#include "clipboard.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace prompt::clipboard {

namespace {
backend current_backend = backend::none;
bool initialized = false;

struct stdin_tmpfile {
    std::filesystem::path path;
    explicit stdin_tmpfile(std::string_view data) {
        std::string tmpl = "/tmp/prompt_clip_XXXXXX";
        int fd = mkstemp(tmpl.data());
        if (fd >= 0) {
            auto n = ::write(fd, data.data(), data.size());
            (void)n;
            ::close(fd);
            path = tmpl;
        }
    }
    ~stdin_tmpfile() {
        if (!path.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};
} // namespace

backend detect_backend() {
    if (std::getenv("WAYLAND_DISPLAY") != nullptr) return backend::wayland;
    if (std::getenv("KDE_SESSION_VERSION") != nullptr) return backend::kde;
    if (std::getenv("DISPLAY") != nullptr) return backend::x11;
    return backend::none;
}

static std::string run_with_timeout(char const* const* argv, std::string_view stdin_data = {},
                                    bool discard_output = false) {
    std::string cmd;
    for (int i = 0; argv[i] != nullptr; ++i) {
        if (i > 0) cmd += ' ';
        cmd += '\'';
        cmd += argv[i];
        cmd += '\'';
    }

    std::unique_ptr<stdin_tmpfile> tmp;
    if (!stdin_data.empty()) {
        tmp = std::make_unique<stdin_tmpfile>(stdin_data);
        cmd += " < '";
        cmd += tmp->path.string();
        cmd += '\'';
    }

    // Copy backends (wl-copy, xclip) daemonize and inherit our pipe, so
    // popen would never see EOF — discard their output instead.
    if (discard_output) {
        cmd += " >/dev/null 2>&1";
        // The clipboard helpers are the user's session tools, not shell input.
        int rc = std::system(cmd.c_str()); // NOLINT(bugprone-command-processor)
        (void)rc;
        return "";
    }

    // The command is a fixed backend argv built above, never caller-supplied.
    FILE* pipe = popen(cmd.c_str(), "r"); // NOLINT(bugprone-command-processor)
    if (pipe == nullptr) return "";

    std::array<char, 4096> buffer;
    std::string output;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);
    return output;
}

void init([[maybe_unused]] std::chrono::seconds timeout) {
    if (initialized) return;
    current_backend = detect_backend();
    initialized = true;
}

std::string paste() {
    if (!initialized) init();

    switch (current_backend) {
    case backend::wayland: {
        std::array<char const*, 3> argv = {"wl-paste", "--no-newline", nullptr};
        return run_with_timeout(argv.data());
    }
    case backend::x11: {
        std::array<char const*, 5> argv = {"xclip", "-selection", "clipboard", "-o", nullptr};
        auto result = run_with_timeout(argv.data());
        if (result.empty()) {
            std::array<char const*, 4> argv2 = {"xsel", "--clipboard", "--output", nullptr};
            result = run_with_timeout(argv2.data());
        }
        return result;
    }
    case backend::kde: {
        std::array<char const*, 5> argv = {
            "qdbus6", "org.kde.klipper", "/klipper", "org.kde.klipper.klipper.getClipboardContents", nullptr,
        };
        auto result = run_with_timeout(argv.data());
        if (!result.empty() && result.back() == '\n') result.pop_back();
        return result;
    }
    default:
        return "";
    }
}

std::optional<std::string> detect_identifier(std::string_view pasted) {
    std::string text(pasted);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
    if (text.empty()) return std::nullopt;

    // Order matters: the URL forms are more specific than the bare ISIN and
    // insCode tokens they contain.
    static const std::regex easytrader_re(R"(https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12})");
    static const std::regex tsetmc_re(R"(https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,})");
    static const std::regex codal_re(R"(https?://(www\.)?codal\.ir/ReportList\.aspx\?([^&]*&)?Symbol=[^&[:space:]]+)");
    static const std::regex isin_re(R"(IR[A-Za-z0-9]{10})");
    static const std::regex inscode_re(R"([0-9]{15,})");

    std::smatch match;
    if (std::regex_search(text, match, easytrader_re) || std::regex_search(text, match, tsetmc_re) ||
        std::regex_search(text, match, codal_re) || std::regex_search(text, match, isin_re) ||
        std::regex_search(text, match, inscode_re)) {
        return std::string(match[0]);
    }

    // A copied Persian symbol ("فولاد") is an identifier; a sentence is not,
    // so cap the length and reject anything containing a Latin letter or digit.
    if (text.size() <= 64 && std::ranges::none_of(text, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        })) {
        return text;
    }

    return std::nullopt;
}

bool copy(std::string_view data) {
    if (!initialized) init();

    switch (current_backend) {
    case backend::wayland: {
        std::array<char const*, 4> argv = {"wl-copy", "--type", "text/plain", nullptr};
        run_with_timeout(argv.data(), data, true);
        return true;
    }
    case backend::x11: {
        std::array<char const*, 5> argv = {"xclip", "-selection", "clipboard", "-i", nullptr};
        run_with_timeout(argv.data(), data, true);
        return true;
    }
    case backend::kde: {
        // bash's bin/clipboard prefers the dbus-send/qdbus call over its
        // python-dbus client; klipper takes the text as an argv element.
        prompt::process::command cmd{
            "qdbus", "org.kde.klipper", "/klipper", "org.kde.klipper.klipper.setClipboardContents", std::string(data),
        };
        cmd.timeout(std::chrono::seconds(5));
        cmd.run();
        return true;
    }
    default:
        return false;
    }
}

bool clear() {
    if (!initialized) init();

    switch (current_backend) {
    case backend::wayland: {
        std::array<char const*, 3> argv = {"wl-copy", "--clear", nullptr};
        run_with_timeout(argv.data(), {}, true);
        return true;
    }
    case backend::x11: {
        std::array<char const*, 6> argv = {"xclip", "-selection", "clipboard", "-i", "/dev/null", nullptr};
        run_with_timeout(argv.data(), {}, true);
        return true;
    }
    case backend::kde: {
        std::array<char const*, 5> argv = {
            "qdbus6", "org.kde.klipper", "/klipper", "org.kde.klipper.klipper.clearClipboardContents", nullptr,
        };
        run_with_timeout(argv.data());
        return true;
    }
    default:
        return false;
    }
}

} // namespace prompt::clipboard
