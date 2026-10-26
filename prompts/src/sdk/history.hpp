#pragma once

#include "terminal.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::history {

// The per-user record of `prompt` invocations. One run is one append-only
// Markdown file under <root>/runs, with HTML-comment markers framing every
// segment, so the store stays greppable, crash-safe and rewrite-free.
//
// The root is $PROMPT_HISTORY_DIR, else $XDG_STATE_HOME/prompt, else
// $HOME/.local/state/prompt (XDG state, not cache: the history is meant to
// survive a cache cleanup and is bounded by the retention caps instead).

struct stored_segment {
    std::size_t index = 0; // 1-based chain position
    std::string name;      // prompt name ("ai.dseek")
    std::string role;      // "prompt" | "sink"
    std::vector<std::string> args;
    std::vector<std::string> body; // the raw tokens of a `.each` body
    std::string merge;             // append | replace | passthrough | clear | skip
    std::string status;            // ok | failed | skipped | help
    std::string text;              // this segment's own output (never the whole context)
    std::string input;             // the barrier input, when the run had to keep it
    bool emitted = false;          // reached stdout
    bool redacted = false;
    std::size_t original_bytes = 0; // > text.size() when the middle was elided
};

struct stored_run {
    std::filesystem::path path;
    std::string id;
    long long ts = 0;
    std::string started;
    std::vector<std::string> argv;
    std::string cwd;
    std::string git_root;
    std::string parent;
    terminal::id terminal; // where the invocation ran (empty for older runs)
    std::size_t exit_code = 0;
    bool finished = false;
    bool redacted = false;
    bool incomplete = false;
    std::size_t output_bytes = 0;
    std::vector<stored_segment> segments;
    std::size_t bytes = 0; // file size on disk
};

// The directory rules (see above).
std::filesystem::path root();
std::filesystem::path runs_dir();

// A run file's path relative to the current directory when possible. The
// dispatcher redacts $HOME in everything it prints, so an absolute path under
// the home directory would come out unusable ("HOME/.local/…"); a relative
// path stays copy-pasteable.
std::string display_path(std::filesystem::path const& file);

// PROMPT_HISTORY=0/off/false/no disables recording for one invocation;
// PROMPT_HISTORY_INNER=0 also silences `.each` child runs.
bool enabled();
bool record_inner();

// Newest first, from the filenames alone (no parsing): cheap enough for
// listing, completion and the prune pass.
std::vector<std::filesystem::path> run_files();
std::vector<std::string> run_ids();

std::optional<stored_run> load(std::filesystem::path const& file);
// Every run, newest first.
std::vector<stored_run> load_all();
// The newest run whose id starts with `key` (empty = newest).
std::optional<stored_run> find_by_id(std::string_view key);
// 1-based rank counted from the newest run.
std::optional<stored_run> find_by_rank(std::size_t rank);

// True when the run happened in `who`'s terminal (see sdk/terminal.hpp). An
// older run with no recorded terminal never matches.
bool in_terminal(stored_run const& run, terminal::id const& who);
// The newest run from `who`'s terminal, or nullopt when it has none. Only the
// run marker is read while scanning, so a store full of foreign terminals is
// still cheap to skip.
std::optional<stored_run> find_last_in(terminal::id const& who);
// The `rank`-th newest run from `who`'s terminal (1-based), or nullopt.
std::optional<stored_run> find_rank_in(terminal::id const& who, std::size_t rank);

// What the run produced: the emitted segments joined the way the dispatcher
// joins them, with the redaction legend in front when any emitted segment was
// redacted (the clipboard's rule).
std::string final_output(stored_run const& run);
// The chain context a sink at `index` received: the stored barrier input when
// the run kept one, otherwise the recorded merges replayed.
std::string barrier_input(stored_run const& run, std::size_t index);

// "7d", "12h", "30m", "90s", "2w", or a bare number of days.
std::optional<std::chrono::seconds> parse_duration(std::string_view text);
// "256M", "1G", "512K", or plain bytes.
std::optional<std::uintmax_t> parse_size(std::string_view text);

// Retention defaults: PROMPT_HISTORY_KEEP (1000 runs) and PROMPT_HISTORY_SIZE
// (256M), with PROMPT_HISTORY_PRUNE=0 disabling the automatic pass.
std::size_t keep_runs();
std::uintmax_t keep_bytes();
bool auto_prune();

// Delete the oldest runs until the count/size caps hold, never touching `keep`
// (the run being written) or `exclude` (a file a caller wants preserved).
// Best effort and silent: history must never fail a prompt run.
void prune(std::filesystem::path const& keep = {});

// Write a run file for one invocation. Every operation is best-effort: a
// disabled or unwritable store is simply inactive.
class recorder {
  public:
    struct info {
        std::vector<std::string> argv;
        std::filesystem::path cwd;
        std::filesystem::path git_root;
        terminal::id terminal;
        bool tty = false;
        bool inner = false;
        std::string parent;
    };

    explicit recorder(info const& run_info);
    recorder(recorder const&) = delete;
    recorder& operator=(recorder const&) = delete;
    ~recorder();

    [[nodiscard]] bool enabled() const { return active_; }
    [[nodiscard]] bool active() const { return fd_ >= 0; }
    [[nodiscard]] std::string const& id() const { return id_; }

    // Append one segment. `barrier_input` is the context a sink actually
    // received; it is stored only when it differs from the recorded replay
    // (piped stdin, `--head`, redaction), so the usual case costs no bytes.
    void segment(stored_segment record, std::string_view barrier_input = {});
    // Write the `end` marker, close the file and prune. Idempotent.
    void finish(int exit_code, bool incomplete);

  private:
    void open();
    void write(std::string_view bytes) const;
    void frame(std::string_view kind, std::string_view payload, std::string_view first, std::string_view second);
    [[nodiscard]] std::string replay(std::size_t count, bool& redacted_out) const;

    info info_;
    bool active_ = false;
    int fd_ = -1;
    std::string id_;
    std::filesystem::path path_;
    std::vector<stored_segment> recorded_;
    std::chrono::system_clock::time_point started_;
    bool finished_ = false;
};

// Sanitize the digits-only/format helpers the prompts share with `.list`.
std::string format_time(long long epoch_seconds);
std::string human_bytes(std::uintmax_t bytes);

} // namespace prompt::history
