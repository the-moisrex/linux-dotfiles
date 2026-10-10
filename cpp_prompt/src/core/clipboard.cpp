#include "prompt/core/clipboard.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace prompt::clipboard {

namespace {
backend current_backend = backend::none;
std::chrono::seconds clipboard_timeout = std::chrono::seconds(3);
bool initialized = false;

struct stdin_tmpfile {
    std::filesystem::path path;
    explicit stdin_tmpfile(std::string_view data) {
        char tmpl[] = "/tmp/prompt_clip_XXXXXX";
        int fd = mkstemp(tmpl);
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

backend detect_backend() noexcept {
    if (std::getenv("WAYLAND_DISPLAY")) return backend::wayland;
    if (std::getenv("KDE_SESSION_VERSION")) return backend::kde;
    if (std::getenv("DISPLAY")) return backend::x11;
    return backend::none;
}

std::string run_with_timeout(char const* const* argv, std::string_view stdin_data = {},
                             bool discard_output = false) noexcept {
    std::string cmd;
    for (int i = 0; argv[i]; ++i) {
        if (i > 0) cmd += ' ';
        cmd += "'";
        cmd += argv[i];
        cmd += "'";
    }

    std::unique_ptr<stdin_tmpfile> tmp;
    if (!stdin_data.empty()) {
        tmp = std::make_unique<stdin_tmpfile>(stdin_data);
        cmd += " < '";
        cmd += tmp->path.string();
        cmd += "'";
    }

    // Copy backends (wl-copy, xclip) daemonize and inherit our pipe, so
    // popen would never see EOF — discard their output instead.
    if (discard_output) {
        cmd += " >/dev/null 2>&1";
        int rc = std::system(cmd.c_str());
        (void)rc;
        return "";
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    std::array<char, 4096> buffer;
    std::string output;
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        output += buffer.data();
    }
    pclose(pipe);
    return output;
}

void init(std::chrono::seconds timeout) noexcept {
    if (initialized) return;
    clipboard_timeout = timeout;
    current_backend = detect_backend();
    initialized = true;
}

std::string paste() noexcept {
    if (!initialized) init();

    switch (current_backend) {
    case backend::wayland: {
        char const* argv[] = {"wl-paste", "--no-newline", nullptr};
        return run_with_timeout(argv);
    }
    case backend::x11: {
        char const* argv[] = {"xclip", "-selection", "clipboard", "-o", nullptr};
        auto result = run_with_timeout(argv);
        if (result.empty()) {
            char const* argv2[] = {"xsel", "--clipboard", "--output", nullptr};
            result = run_with_timeout(argv2);
        }
        return result;
    }
    case backend::kde: {
        char const* argv[] = {"qdbus6", "org.kde.klipper", "/klipper", "org.kde.klipper.klipper.getClipboardContents",
                              nullptr};
        auto result = run_with_timeout(argv);
        if (!result.empty() && result.back() == '\n') result.pop_back();
        return result;
    }
    default:
        return "";
    }
}

bool copy(std::string_view data) noexcept {
    if (!initialized) init();

    switch (current_backend) {
    case backend::wayland: {
        char const* argv[] = {"wl-copy", "--type", "text/plain", nullptr};
        run_with_timeout(argv, data, true);
        return true;
    }
    case backend::x11: {
        char const* argv[] = {"xclip", "-selection", "clipboard", "-i", nullptr};
        run_with_timeout(argv, data, true);
        return true;
    }
    case backend::kde: {
        char const* py_script = R"PY(
import sys, dbus
iface = dbus.Interface(dbus.SessionBus().get_object("org.kde.klipper", "/klipper"),
                       "org.kde.klipper.klipper")
iface.setClipboardContents(sys.stdin.buffer.read().decode("utf-8", "replace"))
)PY";
        char const* argv[] = {"python3", "-c", py_script, nullptr};
        run_with_timeout(argv, data);
        return true;
    }
    default:
        return false;
    }
}

bool clear() noexcept {
    if (!initialized) init();

    switch (current_backend) {
    case backend::wayland: {
        char const* argv[] = {"wl-copy", "--clear", nullptr};
        run_with_timeout(argv, {}, true);
        return true;
    }
    case backend::x11: {
        char const* argv[] = {"xclip", "-selection", "clipboard", "-i", "/dev/null", nullptr};
        run_with_timeout(argv, {}, true);
        return true;
    }
    case backend::kde: {
        char const* argv[] = {"qdbus6", "org.kde.klipper", "/klipper", "org.kde.klipper.klipper.clearClipboardContents",
                              nullptr};
        run_with_timeout(argv);
        return true;
    }
    default:
        return false;
    }
}

} // namespace prompt::clipboard
