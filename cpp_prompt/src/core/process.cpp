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

// Blocks reading `fd` until EOF; returns when the writer closes or on error.
std::string drain(int fd) {
    std::string data;
    std::array<char, 8192> buf;
    for (;;) {
        auto n = ::read(fd, buf.data(), buf.size());
        if (n > 0) {
            data.append(buf.data(), static_cast<std::size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }
    return data;
}

std::string quote_for(std::string const& s) { return "'" + s + "'"; }

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

command& command::inherit_stderr() {
    stderr_mode_ = stderr_mode::inherit;
    return *this;
}

command_result command::run() {
    command_result result;
    if (argv_.empty()) {
        result.exit_code = -1;
        result.stderr_data = "Empty command";
        return result;
    }

    int in_pipe[2];  // parent -> child
    int out_pipe[2]; // child -> parent
    int err_pipe[2]; // child -> parent (capture mode only)
    if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0) {
        result.exit_code = -1;
        result.stderr_data = "pipe failed";
        return result;
    }
    bool capture_err = stderr_mode_ == stderr_mode::capture;
    if (capture_err && ::pipe(err_pipe) != 0) {
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
    result.stdout_data = drain(out_pipe[0]);
    if (capture_err) result.stderr_data = drain(err_pipe[0]);
    ::close(out_pipe[0]);
    if (capture_err) ::close(err_pipe[0]);
    if (writer.joinable()) writer.join();

    // Timeout: SIGKILL after `timeout_` (0 = wait forever).
    int status = 0;
    if (timeout_.count() > 0) {
        // waitpid with WNOHANG in a poll loop
        auto deadline = std::chrono::steady_clock::now() + timeout_;
        for (;;) {
            pid_t r = ::waitpid(pid, &status, WNOHANG);
            if (r == pid) break;
            if (r < 0) {
                status = -1;
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, &status, 0);
                result.timed_out = true;
                status = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } else {
        if (::waitpid(pid, &status, 0) < 0) status = -1;
    }

    if (status > 0) result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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