#include "process.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <future>
#include <memory>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace prompt::process {

namespace {
// Feeds stdin_data to the child through a pipe in the parent (no temp files,
// no half-duplex popen). Reads the child's streams until EOF.
struct child_io {
    std::string in;
    std::string out;
    std::string err;
    stderr_mode err_mode = stderr_mode::inherit;
};

struct pump_entry {
    int fd = -1;
    bool open = true;
    std::string* data = nullptr;
    command::chunk_callback const* callback = nullptr;
};

// Reads every stream until EOF, invoking the tee callback as chunks arrive.
// The poll loop also enforces `timeout` (0 = wait forever); the caller kills
// and reaps the child when timed_out is set.
bool pump_streams(std::vector<pump_entry>& entries, std::chrono::milliseconds timeout) {
    std::vector<pollfd> fds;
    fds.reserve(entries.size());
    for (auto const& entry : entries) fds.push_back(pollfd{.fd = entry.fd, .events = POLLIN, .revents = 0});

    auto const deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        bool any_open = false;
        for (auto const& entry : entries) any_open = any_open || entry.open;
        if (!any_open) return false;

        int wait_ms = -1;
        if (timeout.count() > 0) {
            auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                    .count();
            if (remaining <= 0) return true;
            wait_ms = remaining > 2'147'483'647LL ? 2'147'483'647 : static_cast<int>(remaining);
        }

        int const ready = ::poll(fds.data(), fds.size(), wait_ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (ready == 0) return true;

        for (std::size_t i = 0; i < entries.size(); ++i) {
            auto& entry = entries[i];
            if (!entry.open || fds[i].revents == 0) continue;
            std::array<char, 8192> buffer{};
            auto const n = ::read(entry.fd, buffer.data(), buffer.size());
            if (n > 0) {
                entry.data->append(buffer.data(), static_cast<std::size_t>(n));
                if (*entry.callback) (*entry.callback)(std::string_view(buffer.data(), static_cast<std::size_t>(n)));
            } else if (n == 0 || errno != EINTR) {
                entry.open = false;
                fds[i].fd = -1; // poll ignores negative descriptors
            }
        }
    }
}

} // namespace

command& command::arg(std::string_view a) {
    argv_.emplace_back(a);
    return *this;
}

command& command::arg(std::string const& a) {
    argv_.push_back(a);
    return *this;
}

command& command::args(std::vector<std::string> const& as) {
    argv_.insert(argv_.end(), as.begin(), as.end());
    return *this;
}

command& command::stdin_data(std::string data) {
    stdin_data_ = std::move(data);
    return *this;
}

command& command::env(std::vector<std::string> const& extra) {
    env_.insert(env_.end(), extra.begin(), extra.end());
    return *this;
}

command& command::timeout(std::chrono::milliseconds t) {
    timeout_ = t;
    return *this;
}

command& command::stderr(stderr_mode mode) {
    stderr_mode_ = mode;
    return *this;
}

command& command::capture_stderr() {
    stderr_mode_ = stderr_mode::capture;
    return *this;
}

command& command::stderr_to_stdout() {
    stderr_mode_ = stderr_mode::to_stdout;
    return *this;
}

command& command::discard_stderr() {
    stderr_mode_ = stderr_mode::discard;
    return *this;
}

command& command::inherit_stderr() {
    stderr_mode_ = stderr_mode::inherit;
    return *this;
}

command& command::on_stdout(chunk_callback callback) {
    stdout_callback_ = std::move(callback);
    return *this;
}

command& command::on_stderr(chunk_callback callback) {
    stderr_callback_ = std::move(callback);
    return *this;
}

command_result command::run() {
    command_result result;
    if (argv_.empty()) {
        result.exit_code = -1;
        result.stderr_data = "Empty command";
        return result;
    }

    std::array<int, 2> in_pipe{};  // parent -> child
    std::array<int, 2> out_pipe{}; // child -> parent
    std::array<int, 2> err_pipe{}; // child -> parent (capture mode only)
    if (::pipe(in_pipe.data()) != 0 || ::pipe(out_pipe.data()) != 0) {
        result.exit_code = -1;
        result.stderr_data = "pipe failed";
        return result;
    }
    bool capture_err = stderr_mode_ == stderr_mode::capture || static_cast<bool>(stderr_callback_);
    if (capture_err && ::pipe(err_pipe.data()) != 0) {
        result.exit_code = -1;
        result.stderr_data = "pipe failed";
        return result;
    }

    std::vector<char> argv_buf;
    std::vector<char*> argv_c(argv_.size() + 1);
    for (std::size_t i = 0; i < argv_.size(); ++i) {
        argv_c[i] = const_cast<char*>(argv_[i].c_str());
    }
    argv_c[argv_.size()] = nullptr;
    // environ is the process environment; passing it through execvpe keeps the
    // child's environment identical to ours (bash scripts rely on PATH etc.).
    std::vector<char*> envp;
    for (char** e = ::environ; *e != nullptr; ++e) envp.push_back(*e);
    for (auto const& kv : env_) envp.push_back(const_cast<char*>(kv.c_str()));
    envp.push_back(nullptr);

    // NOTE: no threads exist yet — fork first, then feed the child's stdin
    // from a writer thread in the parent only. Forking while another thread
    // holds a libc lock deadlocks the child inside execvpe.
    pid_t pid = ::fork();
    if (pid < 0) {
        result.exit_code = -1;
        result.stderr_data = "fork failed";
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[1]);
        ::close(out_pipe[0]);
        if (capture_err) {
            ::close(err_pipe[0]);
            ::close(err_pipe[1]);
        }
        return result;
    }

    if (pid == 0) {
        // Child: stdin/stdout are pipes; stderr is inherited (bash's `$(...)`),
        // captured, or merged onto stdout (2>&1).
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        if (capture_err) {
            ::dup2(err_pipe[1], STDERR_FILENO);
        } else if (stderr_mode_ == stderr_mode::to_stdout) {
            ::dup2(out_pipe[1], STDERR_FILENO);
        } else if (stderr_mode_ == stderr_mode::discard) {
            int null_fd = ::open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                ::dup2(null_fd, STDERR_FILENO);
                ::close(null_fd);
            }
        }
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        if (capture_err) ::close(err_pipe[0]);
        ::execvpe(argv_c[0], argv_c.data(), envp.data());
        _exit(127);
    }

    // The child has the read ends it needs; close the parent's copies and
    // hand the child its stdin from a writer thread (or close the pipe right
    // away when there is none). EOF on that write end is what lets a child
    // that reads stdin exit while the parent is still draining stdout.
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    if (capture_err) ::close(err_pipe[1]);
    std::string in_copy = stdin_data_;
    std::thread writer;
    if (in_copy.empty()) {
        ::close(in_pipe[1]);
        in_pipe[1] = -1;
    } else {
        writer = std::thread([&] {
            std::size_t off = 0;
            while (off < in_copy.size()) {
                auto n = ::write(in_pipe[1], in_copy.data() + off, in_copy.size() - off);
                if (n <= 0) {
                    if (n < 0 && errno == EINTR) continue;
                    break;
                }
                off += static_cast<std::size_t>(n);
            }
            ::close(in_pipe[1]);
            in_pipe[1] = -1;
        });
    }
    std::vector<pump_entry> streams;
    streams.push_back(
        pump_entry{.fd = out_pipe[0], .open = true, .data = &result.stdout_data, .callback = &stdout_callback_});
    if (capture_err) {
        streams.push_back(
            pump_entry{.fd = err_pipe[0], .open = true, .data = &result.stderr_data, .callback = &stderr_callback_});
    }
    result.timed_out = pump_streams(streams, timeout_);
    ::close(out_pipe[0]);
    if (capture_err) ::close(err_pipe[0]);
    // Kill before joining the stdin writer: on a timeout the writer may still
    // be blocked writing to a child that stopped reading.
    if (result.timed_out) ::kill(pid, SIGKILL);
    if (writer.joinable()) writer.join();

    int status = 0;
    if (result.timed_out) {
        ::waitpid(pid, &status, 0);
        status = -1;
    } else {
        if (::waitpid(pid, &status, 0) < 0) status = -1;
    }

    if (result.timed_out) {
        result.exit_code = -1;
    } else if (status > 0) {
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return result;
}

command_result run_script(std::string const& script, std::vector<std::string> const& extras,
                          std::string_view stdin_data, std::chrono::milliseconds timeout) {
    // `bash -c <script> arg0 arg1 ...` replicates the prompt scripts' own
    // invocation ($0 = script name, $@ = extras).
    command cmd{"bash", "-c", script};
    for (auto const& e : extras) cmd.arg(e);
    cmd.stdin_data(std::string(stdin_data));
    cmd.timeout(timeout);
    cmd.inherit_stderr();
    return cmd.run();
}

command_result run_command(std::vector<std::string> const& argv, std::string_view stdin_data,
                           std::vector<std::string> const& env, std::chrono::milliseconds timeout) {
    command cmd{argv};
    cmd.stdin_data(std::string(stdin_data));
    cmd.env(env);
    cmd.timeout(timeout);
    cmd.inherit_stderr();
    return cmd.run();
}

command_result run_command(std::span<char const* const> argv, std::string_view stdin_data,
                           std::span<char const* const> env, std::chrono::milliseconds timeout) {
    std::vector<std::string> args;
    for (const auto* p : argv) {
        if (p == nullptr) break;
        args.emplace_back(p);
    }
    std::vector<std::string> envs;
    for (const auto* e : env) {
        if (e == nullptr) break;
        envs.emplace_back(e);
    }
    return run_command(args, stdin_data, envs, timeout);
}

} // namespace prompt::process