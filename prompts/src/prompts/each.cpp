#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/agent.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/policy.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <ostream>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

static void render_help_each(std::ostream& os);

namespace {

[[nodiscard]] std::optional<std::filesystem::path> git_root_of(view const& v) {
    if (v.env.git_root.empty()) return std::nullopt;
    return v.env.git_root;
}

struct each_options {
    std::size_t jobs = 0;
    std::size_t limit = 0;
    bool stdin_item = false;
    bool print_only = false;
    bool list = false;
    bool json = false;
    bool yes = false;
    bool no_header = false;
    bool keep_files = false;
    std::string on_error = "stop";
    std::chrono::seconds timeout{600};
    std::string selector;
};

[[nodiscard]] std::size_t positive_number(std::string_view value, std::string_view option) {
    unsigned long long number = 0;
    auto const* begin = value.data();
    auto const* end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, number);
    if (ec != std::errc{} || ptr != end || number == 0) {
        throw usage_error{std::format("prompt each: {} needs a positive number\n", option)};
    }
    return static_cast<std::size_t>(number);
}

[[nodiscard]] std::string_view value_after(std::string_view arg, std::string_view option) {
    return arg.substr(option.size());
}

[[nodiscard]] each_options parse_each_args(std::span<std::string_view const> args) {
    each_options out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view const arg = args[i];
        if (arg == "--jobs" || arg == "-j") {
            if (++i >= args.size()) throw usage_error{"prompt each: --jobs needs a value\n"};
            out.jobs = positive_number(args[i], "--jobs");
        } else if (arg.starts_with("--jobs=")) {
            out.jobs = positive_number(value_after(arg, "--jobs="), "--jobs");
        } else if (arg == "--limit") {
            if (++i >= args.size()) throw usage_error{"prompt each: --limit needs a value\n"};
            out.limit = positive_number(args[i], "--limit");
        } else if (arg.starts_with("--limit=")) {
            out.limit = positive_number(value_after(arg, "--limit="), "--limit");
        } else if (arg == "--stdin") {
            out.stdin_item = true;
        } else if (arg == "--print") {
            out.print_only = true;
        } else if (arg == "--list") {
            out.list = true;
        } else if (arg == "--json") {
            out.json = true;
        } else if (arg == "--yes") {
            out.yes = true;
        } else if (arg == "--no-header") {
            out.no_header = true;
        } else if (arg == "--keep-files") {
            out.keep_files = true;
        } else if (arg == "--on-item-error") {
            if (++i >= args.size()) throw usage_error{"prompt each: --on-item-error needs stop, skip or keep\n"};
            out.on_error = std::string(args[i]);
        } else if (arg.starts_with("--on-item-error=")) {
            out.on_error = std::string(value_after(arg, "--on-item-error="));
        } else if (arg == "--timeout") {
            if (++i >= args.size()) throw usage_error{"prompt each: --timeout needs a value\n"};
            out.timeout = std::chrono::seconds(positive_number(args[i], "--timeout"));
        } else if (arg.starts_with("--timeout=")) {
            out.timeout = std::chrono::seconds(positive_number(value_after(arg, "--timeout="), "--timeout"));
        } else if (!arg.empty() && arg.front() == '-') {
            throw usage_error{std::format("prompt each: unknown option {}\n{}", arg, help_text(render_help_each))};
        } else if (!out.selector.empty()) {
            throw usage_error{"prompt each: expected at most one selector\n" + help_text(render_help_each)};
        } else {
            out.selector = std::string(arg);
        }
    }
    if (out.on_error != "stop" && out.on_error != "skip" && out.on_error != "keep") {
        throw usage_error{"prompt each: --on-item-error must be stop, skip or keep\n"};
    }
    return out;
}

[[nodiscard]] std::string json_text(nlohmann::json const& value) {
    if (value.is_string()) return value.get<std::string>();
    return value.dump();
}

[[nodiscard]] bool identifier_name(std::string_view name) {
    if (name.empty()) return false;
    auto const valid = [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '-';
    };
    if (std::isdigit(static_cast<unsigned char>(name.front())) != 0) return false;
    return std::ranges::all_of(name, valid);
}

[[nodiscard]] std::string substitute(std::string_view token, nlohmann::json const& item, std::size_t index,
                                     std::size_t total, std::string const& file) {
    std::string out;
    out.reserve(token.size());
    for (std::size_t i = 0; i < token.size();) {
        if (token[i] != '{') {
            out += token[i++];
            continue;
        }
        auto const close = token.find('}', i + 1);
        if (close == std::string_view::npos) {
            out += token[i++];
            continue;
        }
        std::string_view const name = token.substr(i + 1, close - i - 1);
        bool replaced = false;
        if (name == "index") {
            out += std::to_string(index);
            replaced = true;
        } else if (name == "n") {
            out += std::to_string(total);
            replaced = true;
        } else if (name == "file") {
            out += file;
            replaced = true;
        } else if (name == "item") {
            out += item.contains("item") ? json_text(item.at("item")) : item.dump();
            replaced = true;
        } else if (name == "body") {
            out += item.contains("body") ? json_text(item.at("body")) : item.dump();
            replaced = true;
        } else if (item.contains(std::string(name))) {
            out += json_text(item.at(std::string(name)));
            replaced = true;
        }
        if (!replaced) {
            if (identifier_name(name)) {
                throw usage_error{std::format("prompt each: unknown item field {{{}}} in item {}\n", name, index)};
            }
            out += token.substr(i, close - i + 1);
            i = close + 1;
            continue;
        }
        i = close + 1;
    }

    // Item data never introduces a chain segment: a whole-token placeholder
    // whose value names a prompt (or `.end`) is rejected before the child sees
    // it.
    bool const whole = token.size() >= 2 && token.front() == '{' && token.back() == '}' &&
                       token.find('{', 1) == std::string_view::npos;
    if (whole) {
        bool const prompt_like = out.starts_with('.') || (out.starts_with('-') && out.size() > 1 && out[1] != '-');
        if (out == ".end" || (prompt_like && find_prompt(std::string_view(out).substr(1)) != nullptr)) {
            throw usage_error{std::format(
                "prompt each: item {} produced the reserved token '{}'; item data cannot name a prompt or .end\n",
                index, out)};
        }
    }
    return out;
}

[[nodiscard]] std::string label_for(nlohmann::json const& item, std::size_t index) {
    if (item.contains("kind") && item.at("kind") == "command" && item.contains("command")) {
        auto command = json_text(item.at("command"));
        if (!command.empty()) return command;
    }
    for (auto const* key : {"symbol", "name", "title", "label"}) {
        if (item.contains(key) && item.at(key).is_string()) {
            auto value = item.at(key).get<std::string>();
            if (!value.empty()) return value;
        }
    }
    if (item.contains("_row")) return "row " + json_text(item.at("_row"));
    std::string text = item.contains("item") ? json_text(item.at("item")) : item.dump();
    auto const newline = text.find('\n');
    if (newline != std::string::npos) text.resize(newline);
    if (text.size() > 60) text.resize(60);
    return text.empty() ? std::format("item {}", index) : text;
}

[[nodiscard]] std::string join_strings(std::vector<std::string> const& values, std::string_view separator) {
    std::string out;
    for (auto const& value : values) {
        if (!out.empty()) out += separator;
        out += value;
    }
    return out;
}

class temp_workspace {
  public:
    temp_workspace() = default;
    temp_workspace(temp_workspace const&) = delete;
    temp_workspace& operator=(temp_workspace const&) = delete;
    temp_workspace(temp_workspace&&) = delete;
    temp_workspace& operator=(temp_workspace&&) = delete;
    ~temp_workspace() { cleanup(); }

    void keep() { keep_ = true; }

    [[nodiscard]] std::filesystem::path const& directory() const { return dir_; }

    [[nodiscard]] std::string write(std::string_view content, std::size_t index) {
        ensure();
        auto const path = dir_ / std::format("item-{:04}.txt", index);
        std::ofstream file(path, std::ios::binary);
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.close();
        if (!file) throw soft_error{std::format("prompt each: cannot write {}\n", path.string())};
        return path.string();
    }

  private:
    void ensure() {
        if (!dir_.empty()) return;
        std::filesystem::path base;
        if (char const* tmp = std::getenv("TMPDIR"); tmp != nullptr && *tmp != '\0') {
            base = tmp;
        } else {
            base = std::filesystem::temp_directory_path();
        }
        auto const text = (base / "prompt-each-XXXXXX").string();
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back('\0');
        char* const made = ::mkdtemp(buffer.data());
        if (made == nullptr) {
            throw soft_error{std::format("prompt each: mkdtemp failed: {}\n", std::strerror(errno))};
        }
        dir_ = made;
    }

    void cleanup() {
        if (keep_ || dir_.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    std::filesystem::path dir_;
    bool keep_ = false;
};

struct job_result {
    int exit_code = 0;
    bool timed_out = false;
    std::string out;
    std::string err;
};

void set_nonblocking(int fd) {
    int const flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        auto const combined =
            static_cast<int>(static_cast<unsigned int>(flags) | static_cast<unsigned int>(O_NONBLOCK));
        ::fcntl(fd, F_SETFL, combined);
    }
}

void drain_fd(int& fd, bool& done, std::string& text) {
    if (done || fd < 0) return;
    std::array<char, 65536> buffer{};
    for (;;) {
        ssize_t const count = ::read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            text.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            done = true;
            ::close(fd);
            fd = -1;
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        done = true;
        ::close(fd);
        fd = -1;
        return;
    }
}

[[nodiscard]] std::filesystem::path temp_base() {
    if (char const* tmp = std::getenv("TMPDIR"); tmp != nullptr && *tmp != '\0') return tmp;
    return std::filesystem::temp_directory_path();
}

[[nodiscard]] int make_stdin_file(std::string_view text) {
    auto const pattern = (temp_base() / "prompt-each-stdin-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    int const fd = ::mkstemp(buffer.data());
    if (fd < 0) throw soft_error{std::format("prompt each: mkstemp failed: {}\n", std::strerror(errno))};
    ::unlink(buffer.data());
    std::size_t offset = 0;
    while (offset < text.size()) {
        ssize_t const count = ::write(fd, text.data() + offset, text.size() - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            throw soft_error{std::format("prompt each: cannot write item stdin: {}\n", std::strerror(errno))};
        }
        offset += static_cast<std::size_t>(count);
    }
    ::lseek(fd, 0, SEEK_SET);
    return fd;
}

[[nodiscard]] pid_t launch_job(std::vector<std::string> const& argv, std::string const& executable,
                               std::vector<std::string> const& extra_env, std::string_view stdin_text, bool feed_stdin,
                               int& out_fd, int& err_fd) {
    std::array<int, 2> out_pipe{-1, -1};
    std::array<int, 2> err_pipe{-1, -1};
    if (::pipe(out_pipe.data()) != 0) {
        throw soft_error{std::format("prompt each: pipe failed: {}\n", std::strerror(errno))};
    }
    if (::pipe(err_pipe.data()) != 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        throw soft_error{std::format("prompt each: pipe failed: {}\n", std::strerror(errno))};
    }

    int stdin_fd = -1;
    if (feed_stdin) {
        try {
            stdin_fd = make_stdin_file(stdin_text);
        } catch (...) {
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
            throw;
        }
    }

    pid_t const pid = ::fork();
    if (pid < 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        if (stdin_fd >= 0) ::close(stdin_fd);
        throw soft_error{std::format("prompt each: fork failed: {}\n", std::strerror(errno))};
    }

    if (pid == 0) {
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        if (stdin_fd >= 0) {
            ::dup2(stdin_fd, STDIN_FILENO);
        } else {
            int const devnull = ::open("/dev/null", O_RDONLY);
            if (devnull >= 0) ::dup2(devnull, STDIN_FILENO);
        }
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        if (stdin_fd >= 0) ::close(stdin_fd);

        for (auto const& entry : extra_env) {
            auto const equals = entry.find('=');
            if (equals == std::string::npos) continue;
            std::string const key = entry.substr(0, equals);
            std::string const value = entry.substr(equals + 1);
            ::setenv(key.c_str(), value.c_str(), 1);
        }

        std::vector<char*> raw;
        raw.reserve(argv.size() + 1);
        for (auto const& arg : argv) raw.push_back(const_cast<char*>(arg.c_str()));
        raw.push_back(nullptr);
        ::execv(executable.c_str(), raw.data());
        ::_exit(127);
    }

    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    if (stdin_fd >= 0) ::close(stdin_fd);
    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);
    out_fd = out_pipe[0];
    err_fd = err_pipe[0];
    return pid;
}

[[nodiscard]] std::vector<job_result> run_jobs(std::vector<std::vector<std::string>> const& argvs,
                                               std::string const& executable, std::size_t jobs,
                                               std::chrono::seconds timeout, std::vector<std::string> const& extra_env,
                                               std::vector<std::string> const* stdin_texts) {
    std::vector<job_result> results(argvs.size());
    if (argvs.empty()) return results;
    jobs = std::clamp<std::size_t>(jobs == 0 ? 1 : jobs, 1, argvs.size());

    struct running_job {
        pid_t pid = -1;
        int out_fd = -1;
        int err_fd = -1;
        std::string out;
        std::string err;
        bool out_done = false;
        bool err_done = false;
        bool timed_out = false;
        std::chrono::steady_clock::time_point started;
        std::size_t index = 0;
    };

    std::vector<running_job> active;
    active.reserve(jobs);
    std::size_t next = 0;

    auto launch = [&](std::size_t index) {
        int out_fd = -1;
        int err_fd = -1;
        std::string_view const stdin_text =
            stdin_texts == nullptr ? std::string_view{} : std::string_view((*stdin_texts)[index]);
        pid_t const pid =
            launch_job(argvs[index], executable, extra_env, stdin_text, stdin_texts != nullptr, out_fd, err_fd);
        active.push_back(running_job{
            .pid = pid,
            .out_fd = out_fd,
            .err_fd = err_fd,
            .out = {},
            .err = {},
            .started = std::chrono::steady_clock::now(),
            .index = index,
        });
    };

    while (next < argvs.size() || !active.empty()) {
        while (active.size() < jobs && next < argvs.size()) launch(next++);

        std::vector<pollfd> fds;
        fds.reserve(active.size() * 2);
        for (auto const& job : active) {
            if (!job.out_done) fds.push_back(pollfd{.fd = job.out_fd, .events = POLLIN, .revents = 0});
            if (!job.err_done) fds.push_back(pollfd{.fd = job.err_fd, .events = POLLIN, .revents = 0});
        }
        if (!fds.empty()) ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 100);

        for (auto& job : active) {
            drain_fd(job.out_fd, job.out_done, job.out);
            drain_fd(job.err_fd, job.err_done, job.err);
        }

        auto const now = std::chrono::steady_clock::now();
        for (auto& job : active) {
            if (!job.timed_out && now - job.started >= timeout) {
                ::kill(job.pid, SIGKILL);
                job.timed_out = true;
            }
        }

        for (auto it = active.begin(); it != active.end();) {
            if (it->out_done && it->err_done) {
                int status = 0;
                while (::waitpid(it->pid, &status, 0) < 0 && errno == EINTR) {
                }
                int code = 0;
                if (WIFEXITED(status)) {
                    code = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    code = 128 + WTERMSIG(status);
                }
                job_result& result = results[it->index];
                result.exit_code = code;
                result.timed_out = it->timed_out;
                result.out = std::move(it->out);
                result.err = std::move(it->err);
                it = active.erase(it);
            } else {
                ++it;
            }
        }
    }
    return results;
}

[[nodiscard]] std::string item_text(nlohmann::json const& item) {
    if (item.contains("kind") && item.at("kind") == "command" && item.contains("command"))
        return json_text(item.at("command"));
    if (item.contains("body")) return json_text(item.at("body"));
    if (item.contains("item")) return json_text(item.at("item"));
    return item.dump();
}

[[nodiscard]] int current_depth() {
    char const* value = std::getenv("PROMPT_INNER_DEPTH");
    if (value == nullptr) return 0;
    int parsed = 0;
    auto const* end = value + std::strlen(value);
    auto [ptr, ec] = std::from_chars(value, end, parsed);
    return ec == std::errc{} && ptr == end ? parsed : 0;
}

[[nodiscard]] std::vector<std::string> inherited_grants() {
    std::vector<std::string> grants;
    char const* value = std::getenv("PROMPT_INNER_ALLOW");
    if (value == nullptr) return grants;
    std::string_view text(value);
    while (!text.empty()) {
        auto const comma = text.find(',');
        std::string_view part = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
        if (!part.empty()) grants.emplace_back(part);
    }
    return grants;
}

} // namespace

static void execute_each(view const& v, result& r) {
    auto const opts = parse_each_args(v.args);
    int const depth = current_depth();
    if (depth >= 2) throw usage_error{"prompt each: loops nested more than 2 deep (max 2)\n"};

    if (v.body.empty() && !opts.list) {
        throw usage_error{"prompt each: a body is required (end it with .end)\n" + help_text(render_help_each)};
    }
    if (v.body.empty() && opts.print_only) {
        throw usage_error{"prompt each: --print needs a body\n" + help_text(render_help_each)};
    }

    std::string selector = opts.selector;
    if (selector.empty()) {
        if (auto expected = v.prior.get(keys::expect)) selector = agent::primary_selector(*expected);
    }
    // `--list` is the inventory path: show every candidate instead of making
    // the ambiguous `auto` choice.
    if (selector.empty()) selector = opts.list ? "any" : "auto";
    if (v.text.empty()) throw soft_error{"prompt each: no accumulated context to take items from\n"};

    auto const tool = fs::bin_tool("md.extract", v.env.exe_path, git_root_of(v));
    std::vector<std::string> extract_argv{"python3", tool.string(), "--items", selector};
    auto const extracted = process::command{extract_argv}
                               .stdin_data(std::string(v.text))
                               .capture_stderr()
                               .timeout(std::chrono::minutes(5))
                               .run();
    if (extracted.exit_code != 0) {
        std::string message = extracted.stderr_data;
        if (message.empty()) message = std::format("prompt each: md.extract failed for {}\n", selector);
        if (extracted.exit_code == 2) throw usage_error{std::move(message)};
        throw soft_error{std::move(message)};
    }

    nlohmann::json items;
    try {
        items = nlohmann::json::parse(extracted.stdout_data);
    } catch (nlohmann::json::exception const& error) {
        throw soft_error{std::format("prompt each: md.extract returned invalid JSON: {}\n", error.what())};
    }
    if (!items.is_array()) throw soft_error{"prompt each: md.extract --items did not return an array\n"};
    if (opts.limit != 0 && items.size() > opts.limit) {
        items.erase(items.begin() + static_cast<nlohmann::json::difference_type>(opts.limit), items.end());
    }
    std::size_t const total = items.size();
    if (total == 0) throw soft_error{std::format("prompt each: selector {} produced no items\n", selector)};

    if (opts.list) {
        r.err += std::format("{}\n", total == 1 ? "1 item:" : std::format("{} items:", total));
        for (std::size_t i = 0; i < total; ++i) {
            r.err += std::format("{:>3}  {}\n", i + 1, label_for(items[i], i + 1));
        }
        r.no_clipboard = true;
        return;
    }

    std::vector<std::string> body_tokens;
    body_tokens.reserve(v.body.size());
    for (auto token : v.body) body_tokens.emplace_back(token);

    // Reject a malformed `.exec` before any child runs. A `{field}` program is
    // allowed but stays dynamic: the child's policy checks the resolved value.
    agent::validate_exec_template(body_tokens);

    bool const needs_file =
        std::ranges::any_of(body_tokens, [](std::string const& token) { return token.contains("{file}"); });

    temp_workspace workspace;
    if (opts.keep_files) workspace.keep();

    std::vector<std::string> files(total);
    if (needs_file) {
        for (std::size_t i = 0; i < total; ++i) files[i] = workspace.write(item_text(items[i]), i + 1);
        if (opts.keep_files)
            r.err += std::format("prompt each: kept item files in {}\n", workspace.directory().string());
    }

    std::vector<std::string> grants = inherited_grants();
    if (!opts.print_only) {
        policy::engine policy_engine(v.env);
        policy_engine.set_yes(opts.yes);
        for (auto const& command : agent::static_exec_commands(body_tokens)) {
            if (command.empty() || command.front().contains('{')) continue;
            policy_engine.require(command);
            grants.push_back(agent::program_name(command.front()));
        }
    }
    std::ranges::sort(grants);
    grants.erase(std::unique(grants.begin(), grants.end()), grants.end());

    std::vector<std::vector<std::string>> argvs;
    argvs.reserve(total);
    for (std::size_t i = 0; i < total; ++i) {
        std::vector<std::string> argv;
        argv.reserve(body_tokens.size() + 1);
        argv.push_back(v.env.exe_path.string());
        for (auto const& token : body_tokens) {
            argv.push_back(substitute(token, items[i], i + 1, total, files[i]));
        }
        argvs.push_back(std::move(argv));
    }

    if (opts.print_only) {
        for (auto const& argv : argvs) {
            std::vector<std::string> display(argv.begin() + 1, argv.end());
            r.out += "prompt ";
            r.out += agent::join_argv(display);
            r.out += '\n';
        }
        return;
    }

    std::vector<std::string> stdin_texts;
    if (opts.stdin_item) {
        stdin_texts.reserve(total);
        for (std::size_t i = 0; i < total; ++i) stdin_texts.push_back(item_text(items[i]));
    }

    std::vector<std::string> child_env{
        "PROMPT_INNER=1",
        std::format("PROMPT_INNER_DEPTH={}", depth + 1),
    };
    if (!grants.empty()) child_env.push_back("PROMPT_INNER_ALLOW=" + join_strings(grants, ","));
    if (opts.yes) child_env.emplace_back("PROMPT_INNER_YES=1");
    // Tag every child run with the parent's history id, so `.history.list` can
    // show which invocation a loop item belongs to.
    if (char const* current = std::getenv("PROMPT_HISTORY_CURRENT"); current != nullptr && *current != '\0')
        child_env.push_back(std::format("PROMPT_HISTORY_PARENT={}", current));

    std::size_t const jobs = std::min<std::size_t>(opts.jobs != 0 ? opts.jobs : std::min<std::size_t>(total, 8), 64);
    auto const results = run_jobs(argvs, v.env.exe_path.string(), jobs, opts.timeout, child_env,
                                  opts.stdin_item ? &stdin_texts : nullptr);

    bool failed = false;
    std::string failure;
    int failure_code = 1;
    for (std::size_t i = 0; i < total; ++i) {
        bool const bad = results[i].timed_out || results[i].exit_code != 0;
        if (!bad || opts.on_error != "stop") continue;
        failed = true;
        failure_code = results[i].exit_code != 0 ? results[i].exit_code : 1;
        failure = results[i].timed_out
                      ? std::format("prompt each: item {} timed out after {}s\n", i + 1, opts.timeout.count())
                      : std::format("prompt each: item {} failed with exit {}\n", i + 1, results[i].exit_code);
        break;
    }

    if (opts.json) {
        nlohmann::json array = nlohmann::json::array();
        for (std::size_t i = 0; i < total; ++i) {
            array.push_back({
                {"index", i + 1},
                {"exit", results[i].exit_code},
                {"timed_out", results[i].timed_out},
                {"stdout", results[i].out},
                {"stderr", results[i].err},
            });
        }
        r.out = array.dump(2) + "\n";
        if (failed) throw prompt_error{failure_code, failure, r.out};
        return;
    }

    for (std::size_t i = 0; i < total; ++i) {
        bool const bad = results[i].timed_out || results[i].exit_code != 0;
        std::string const note =
            results[i].timed_out
                ? std::format("prompt each: item {} timed out after {}s\n", i + 1, opts.timeout.count())
                : std::format("prompt each: item {} failed with exit {}\n", i + 1, results[i].exit_code);
        if (bad && opts.on_error == "skip") {
            r.err += note;
            continue;
        }

        if (total > 1 && !opts.no_header) {
            if (!r.out.empty()) r.out += "\n\n";
            r.out += std::format("## {}\n\n", label_for(items[i], i + 1));
        } else if (!r.out.empty()) {
            r.out += "\n\n";
        }
        r.out += results[i].out;
        r.err += results[i].err;
        if (bad) r.err += note;
        if (bad && opts.on_error == "stop") {
            failed = true;
            failure = note;
            failure_code = results[i].exit_code != 0 ? results[i].exit_code : 1;
            break;
        }
    }

    if (failed) throw prompt_error{failure_code, failure, r.out};
}

static void render_help_each(std::ostream& os) {
    os << R"EOF(Usage: prompt <prompts...> .each [SELECTOR] [flags] .prompt ... .end

Loop the body over every item of a table, code block or section and run each
iteration as its own `prompt` child. The body is a normal chain (nested `.each`
allowed) written between `.each` and its matching `.end`; the prefix before the
first dot token is only this prompt's flags and selector.

The items come from `bin/md.extract --items SELECTOR`:
  table[:col1,col2]   one item per row (fields are the column names)
  command[:program]   one item per command block
  code[:lang]         one item per fenced block
  section[:heading]   one item per section
  auto                the only table, code block or section in the context
Without SELECTOR the shape `.expect`/`.delegate` published is used, then auto.

Field placeholders, substituted in every body argument:
  {symbol} {reason} …   a table column (any item key)
  {item} {body}         the item's rendered text
  {index} {n}           the 1-based item number and the item count
  {file}                a temporary file holding the item (for `.files`)
Item data only ever reaches argv; it can never become a prompt name or `.end`.
)EOF";
    render_sink_help(os);
    os << R"EOF(
Loop options:
  -j, --jobs N            Concurrent children (default min(items, 8))
  --limit N               Only the first N items
  --stdin                 Feed each item's text to the child on stdin
  --on-item-error MODE    stop (default), skip or keep a failed item
  --timeout SEC           Per-item timeout (default 600)
  --print                 Print each substituted `prompt` command, run nothing
  --list                  List the items on stderr and keep the context
  --json                  Emit one JSON object per item (index/exit/stdout/…)
  --no-header             Do not add `## <label>` separators
  --keep-files            Keep the {file} temporary files
  --yes                   Turn policy asks into allows (never overrides a deny)

Commands inside the body (`.exec`) are pre-approved once by the parent for the
static part of the body; a dynamic command still asks when it first exists.

Examples:
  prompt .note "screen these" .expect table:symbol .ai.dseek .each table:symbol .tse {symbol} .tse.analyze .end
  prompt .files a.cpp .expect command:prompt .ai.dseek .each command:prompt .exec -- {command} .end

)EOF";
}

bool const registered = prompt::register_sink(
    "each",
    "Loop a chain body over the rows of a table, code block or section, running each item as a child prompt in "
    "parallel.",
    &execute_each, &render_help_each, nullptr, true, nullptr, nullptr, true);

} // namespace prompt::prompts
