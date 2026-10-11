#include "prompt/prompts/ai_said_prompt.hpp"
#include "prompt/core/clipboard.hpp"
#include "prompt/core/fs.hpp"
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

namespace {

// SIGINT/SIGTERM request a graceful stop: the watch loop notices and the
// EXIT-equivalent finalize prints the collected entries then exits 130.
volatile std::sig_atomic_t g_ai_said_stop = 0;
void ai_said_on_signal(int) { g_ai_said_stop = 1; }

// Port of ai-said.sh trim_context applied to an entry and captured through
// $(...): first N lines joined by '\n', no trailing newline. head==0 → no cap.
std::string head_n_lines(std::string const& s, std::size_t head) noexcept {
    if (head == 0) return s;
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(std::string_view(s).substr(start));
            break;
        }
        lines.push_back(std::string_view(s).substr(start, nl - start));
        start = nl + 1;
        if (start == s.size()) break;
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size() && i < head; ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

std::string strip_trailing_nl(std::string s) noexcept {
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

bool only_whitespace(std::string const& s) noexcept {
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    return true;
}

std::string ordinal_word(long n) noexcept {
    if (n == 1) return "Another";
    if (n == 2) return "A second";
    if (n == 3) return "A third";
    long mod100 = n % 100;
    long mod10 = n % 10;
    std::string suffix = "th";
    if (mod100 < 11 || mod100 > 13) {
        if (mod10 == 1) suffix = "st";
        else if (mod10 == 2)
            suffix = "nd";
        else if (mod10 == 3)
            suffix = "rd";
    }
    return "A " + std::to_string(n) + suffix;
}

std::string entry_label(long n) noexcept { return ordinal_word(n) + " AI said:"; }

std::string build_output(std::vector<std::string> const& entries, std::size_t head_lines) noexcept {
    std::string out;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::string content = head_n_lines(entries[i], head_lines);
        out += entry_label(static_cast<long>(i) + 1);
        out += "\n\n";
        out += content;
        out += "\n\n";
        if (i + 1 < entries.size()) {
            out += "\n\n\n";
            out += std::string(66, '-');
            out += "\n";
        }
    }
    return out;
}

// ^[0-9]+(\.[0-9]+)?$ — an integer or a dotted decimal (nothing else).
bool is_number_string(std::string const& s) noexcept {
    if (s.empty()) return false;
    std::size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i == 0) return false;
    if (i < s.size()) {
        if (s[i] != '.') return false;
        ++i;
        std::size_t frac = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i == frac) return false;
    }
    return i == s.size();
}

double to_seconds(std::string const& s) noexcept { return std::strtod(s.c_str(), nullptr); }

std::string ai_said_help() noexcept {
    return "Usage: prompt ai-said [--interval N] [--timeout N] [--no-clear] [--head N]\n"
           "\n"
           "Collect multiple AI chat outputs via the clipboard and combine them into a single prompt.\n"
           "\n"
           "Flow:\n"
           "  1. Run `prompt ai-said` (clears the clipboard by default).\n"
           "  2. Copy each AI chatbot output from the browser, one after another.\n"
           "     Each clipboard change is captured, prefixed with\n"
           "     \"The first AI said:\", \"The second AI said:\", ... and the\n"
           "     combined result is copied back to the clipboard.\n"
           "  3. Paste the combined result into your AI agent.\n"
           "  4. Press Ctrl+C to stop; the final combined text is printed to\n"
           "     stdout (and auto-copied by the `prompt` dispatcher).\n"
           "\n"
           "Options:\n"
           "  --interval N   Poll the clipboard every N seconds (default: 0.5)\n"
           "  --timeout N    Give up on a single clipboard read/write after N seconds (default: 3)\n"
           "  --no-clear     Keep the existing clipboard content instead of clearing it\n"
           "  --head N       Keep only the first N lines of each captured entry\n";
}

} // namespace

prompt_result execute_ai_said(prompt_context&& ctx) noexcept {
    std::string interval = "0.5";
    std::string clip_timeout = "3";
    bool clear_on_start = true;
    bool head_set = false;
    std::string head_str;
    std::vector<std::string> positional;

    for (std::size_t i = 0; i < ctx.args_count;) {
        std::string a(ctx.args[i]);
        if (a == "--help" || a == "-h") {
            return {ai_said_help(), 0, false, std::string{}};
        } else if (a == "--head") {
            if (i + 1 >= ctx.args_count) return {std::string{}, 2, false, "prompt ai-said: Missing value for --head\n"};
            head_str = std::string(ctx.args[++i]);
            ++i;
            head_set = true;
        } else if (a == "--interval") {
            if (i + 1 >= ctx.args_count)
                return {std::string{}, 2, false, "prompt ai-said: Missing value for --interval\n"};
            interval = std::string(ctx.args[++i]);
            ++i;
        } else if (a == "--timeout") {
            if (i + 1 >= ctx.args_count)
                return {std::string{}, 2, false, "prompt ai-said: Missing value for --timeout\n"};
            clip_timeout = std::string(ctx.args[++i]);
            ++i;
        } else if (a == "--no-clear") {
            clear_on_start = false;
            ++i;
        } else if (a.size() >= 2 && a[0] == '-' && a[1] == '-') {
            return {std::string{}, 2, false, "prompt ai-said: Unknown option: " + a + "\n"};
        } else {
            positional.push_back(a);
            ++i;
        }
    }

    if (!positional.empty()) {
        return {std::string{}, 2, false, "prompt ai-said: This prompt takes no positional arguments.\n"};
    }
    if (!is_number_string(interval) || interval == "0") {
        return {std::string{}, 2, false, "prompt ai-said: --interval must be a positive number of seconds.\n"};
    }
    if (!is_number_string(clip_timeout) || clip_timeout == "0") {
        return {std::string{}, 2, false, "prompt ai-said: --timeout must be a positive number of seconds.\n"};
    }

    // head value is applied like `sed -n 1,Np`; a non-numeric value becomes a
    // 0 cap (never triggered in practice).
    std::size_t head_lines = 0;
    if (head_set) {
        bool numeric = !head_str.empty() && head_str.find_first_not_of("0123456789") == std::string::npos;
        if (numeric) head_lines = static_cast<std::size_t>(std::strtoull(head_str.c_str(), nullptr, 10));
    }

    // CLIPBOARD existence check (mirrors the script's -x test); the actual I/O
    // uses the C++ clipboard backend (same wl-clipboard family as bin/clipboard).
    std::string clipboard_path;
    if (char const* cmd = std::getenv("CLIPBOARD_CMD")) {
        clipboard_path = cmd;
    } else {
        clipboard_path = prompt::fs::bin_tool("clipboard", ctx.exe_path, ctx.git_root).string();
    }
    if (::access(clipboard_path.c_str(), X_OK) != 0) {
        return {std::string{}, 1, false, "prompt ai-said: Clipboard helper not found: " + clipboard_path + "\n"};
    }

    // Capture the paste like bash's $(...): trailing newlines are stripped.
    auto paste = []() -> std::string { return strip_trailing_nl(prompt::clipboard::paste()); };

    prompt::clipboard::init();

    std::string err;
    std::vector<std::string> entries;

    // Watchable sleep: short slices so a SIGINT/SIGTERM is noticed promptly.
    auto watch_sleep = [&](double secs) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(secs));
        while (!g_ai_said_stop && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    };

    std::string baseline;
    if (clear_on_start) {
        prompt::clipboard::clear();
        watch_sleep(0.2);
        baseline = paste(); // paste never signals failure here (backend absent == empty)
        if (!only_whitespace(baseline)) {
            err += "Warning: clipboard still holds " + std::to_string(baseline.size()) +
                   " chars after clear; that content will be ignored.\n";
        } else {
            err += "Clipboard cleared.\n";
        }
    } else {
        err += "Keeping existing clipboard content.\n";
        baseline = paste();
    }

    // Baseline so stale content is not captured as entry #1. hashed/trailing-
    // stripped the same way the poll loop treats each read.
    std::string last_raw = baseline;
    bool have_last_wrote = false;
    std::string last_wrote;

    err += "Watching clipboard every " + interval + "s. Copy AI outputs one by one;\n";
    err += "each change is collected and re-copied. Press Ctrl+C to finish.\n";

    // Install the interrupt handler for the duration of the watch loop.
    struct sigaction sa{};
    sa.sa_handler = &ai_said_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART: wake the polling sleeps on signals
    struct sigaction old_int, old_term;
    sigaction(SIGINT, &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);

    double interval_s = to_seconds(interval);

    while (!g_ai_said_stop) {
        // clipboard::paste cannot distinguish "empty" from "backend failed";
        // the failing-backend path is not deterministically reachable here.
        std::string raw = paste();

        if (raw == last_raw) {
            watch_sleep(interval_s);
            continue;
        }
        last_raw = raw;

        if (only_whitespace(raw)) {
            watch_sleep(interval_s);
            continue;
        }
        if (have_last_wrote && raw == last_wrote) {
            watch_sleep(interval_s);
            continue;
        }

        entries.push_back(raw);
        // combined = $(build_output): trailing newlines stripped before it is
        // copied back and hashed as the loop guard.
        std::string combined = strip_trailing_nl(build_output(entries, head_lines));
        if (prompt::clipboard::copy(combined)) {
            last_wrote = combined;
            have_last_wrote = true;
        } else {
            err += "Warning: failed to copy combined output to clipboard.\n";
        }

        err += "[ai-said] captured #" + std::to_string(entries.size()) + " (" + std::to_string(raw.size()) +
               " chars) \xE2\x80\x94 combined output copied to clipboard.\n";

        watch_sleep(interval_s);
    }

    // Finalize (bash EXIT trap): print the full combined output to stdout
    // (trailing newlines kept), the summary to stderr, exit 130.
    std::string out;
    if (!entries.empty()) {
        out = build_output(entries, head_lines);
        std::string noun = entries.size() == 1 ? "entry" : "entries";
        err += "Collected " + std::to_string(entries.size()) + " " + noun + ". Final prompt printed to stdout.\n";
    } else {
        err += "No entries collected. Nothing to output.\n";
    }

    sigaction(SIGINT, &old_int, nullptr);
    sigaction(SIGTERM, &old_term, nullptr);

    return {std::move(out), 130, false, std::move(err)};
}

void render_help_ai_said(std::ostream& os) noexcept { os << ai_said_help(); }

} // namespace prompt::prompts
