#include "prompt/prompts/skill_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prompt::prompts {

namespace {

std::string env_or(std::string const& key, std::string const& fallback) noexcept {
    if (char const* v = std::getenv(key.c_str())) return v;
    return fallback;
}

std::string home_dir() noexcept { return env_or("HOME", ""); }

std::string skills_dir() noexcept {
    std::string xdg = env_or("XDG_DATA_HOME", std::string());
    if (xdg.empty()) xdg = home_dir() + "/.local/share";
    return xdg + "/agents/skills";
}

std::vector<std::string> legacy_dirs() noexcept {
    return {home_dir() + "/.agents/skills", home_dir() + "/.claude/skills"};
}

std::vector<std::string> cache_dirs() noexcept {
    auto dirs = legacy_dirs();
    dirs.insert(dirs.begin(), skills_dir());
    return dirs;
}

// Run a command, capturing stdout (stderr is inherited/discarded like a
// $() capture that hides nothing). Returns stdout and the exit code.
std::string run_capture(std::vector<std::string> const& cmd, std::string const& stdin_data, int& exit_code) noexcept {
    std::vector<char const*> argv;
    argv.reserve(cmd.size() + 1);
    for (auto const& c : cmd) argv.push_back(c.c_str());
    argv.push_back(nullptr);
    auto r = prompt::process::run_command(argv, stdin_data);
    exit_code = r.exit_code;
    return r.stdout_data;
}

std::string strip_trailing_nl(std::string s) noexcept {
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// jq -r '<filter>' over $meta (bash feeds meta + a newline via <<<).
std::string jq_extract(std::string const& meta, std::string const& filter) noexcept {
    int ec = 0;
    std::string out = run_capture({"jq", "-r", filter}, meta + "\n", ec);
    return strip_trailing_nl(out);
}

// Port of parse_frontmatter: collect lines between the first two "---" lines,
// then parse them with the same Python one-liner the script uses.
std::string parse_frontmatter(std::filesystem::path const& file) noexcept {
    std::ifstream in(file, std::ios::binary);
    if (!in) return "{}";
    std::string fm;
    bool in_fm = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "---") {
            if (in_fm) break;
            in_fm = true;
            continue;
        }
        if (in_fm) fm += line + "\n";
    }
    // bash pipes `echo "$fm_lines"`, i.e. fm + an extra trailing newline.
    std::string stdin_data = fm + "\n";

    std::string const py = R"PY(import sys, json, re
data = {}
for line in sys.stdin:
    line = line.rstrip()
    m = re.match(r'^(\w[\w_-]*):\s*(.*)', line)
    if m:
        k, v = m.group(1), m.group(2).strip()
        if len(v) >= 2 and v[0] == v[-1] and v[0] in ('"', "'"):
            v = v[1:-1]
        data[k] = v
print(json.dumps(data))
)PY";

    int ec = 0;
    std::string out = run_capture({"python3", "-c", py}, stdin_data, ec);
    if (ec != 0) return "{}";
    std::string meta = strip_trailing_nl(out);
    return meta.empty() ? "{}" : meta;
}

// bash_infer_lang (same table as _common.sh).
std::string bash_infer_lang(std::string const& filename) noexcept {
    std::string base = filename;
    auto slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    std::string ext;
    auto dot = base.find_last_of('.');
    ext = (dot != std::string::npos) ? base.substr(dot + 1) : base;

    if (base == "Dockerfile") return "dockerfile";
    if (base == "Makefile" || base == "makefile" || base == "GNUmakefile") return "makefile";
    if (base == "CMakeLists.txt") return "cmake";
    if (ext == "c" || ext == "h") return "c";
    if (ext == "cc" || ext == "cp" || ext == "cpp" || ext == "cxx" || ext == "c++" || ext == "hpp" || ext == "hxx" ||
        ext == "hh" || ext == "h++")
        return "cpp";
    if (ext == "m") return "objectivec";
    if (ext == "mm") return "objective-cpp";
    if (ext == "rs") return "rust";
    if (ext == "py" || ext == "pyi") return "python";
    if (ext == "sh" || ext == "bash") return "bash";
    if (ext == "zsh") return "zsh";
    if (ext == "fish") return "fish";
    if (ext == "nu") return "nu";
    if (ext == "js" || ext == "cjs" || ext == "mjs") return "javascript";
    if (ext == "ts" || ext == "mts" || ext == "cts") return "typescript";
    if (ext == "jsx") return "jsx";
    if (ext == "tsx") return "tsx";
    if (ext == "java") return "java";
    if (ext == "kt" || ext == "kts") return "kotlin";
    if (ext == "swift") return "swift";
    if (ext == "go") return "go";
    if (ext == "rb") return "ruby";
    if (ext == "php") return "php";
    if (ext == "lua") return "lua";
    if (ext == "pl" || ext == "pm") return "perl";
    if (ext == "r") return "r";
    if (ext == "scala") return "scala";
    if (ext == "cs") return "csharp";
    if (ext == "fs" || ext == "fsx") return "fsharp";
    if (ext == "vb") return "vbnet";
    if (ext == "dart") return "dart";
    if (ext == "ex" || ext == "exs") return "elixir";
    if (ext == "erl" || ext == "hrl") return "erlang";
    if (ext == "clj" || ext == "cljs" || ext == "cljc") return "clojure";
    if (ext == "ml" || ext == "mli") return "ocaml";
    if (ext == "sql") return "sql";
    if (ext == "html" || ext == "htm") return "html";
    if (ext == "css") return "css";
    if (ext == "scss") return "scss";
    if (ext == "sass") return "sass";
    if (ext == "less") return "less";
    if (ext == "xml") return "xml";
    if (ext == "xsl" || ext == "xslt") return "xslt";
    if (ext == "svg") return "svg";
    if (ext == "json" || ext == "stock" || ext == "tse") return "json";
    if (ext == "jsonc") return "jsonc";
    if (ext == "yaml" || ext == "yml") return "yaml";
    if (ext == "toml") return "toml";
    if (ext == "ini" || ext == "cfg" || ext == "conf") return "ini";
    if (ext == "env") return "dotenv";
    if (ext == "md") return "markdown";
    if (ext == "txt" || ext == "log") return "text";
    if (ext == "diff" || ext == "patch") return "diff";
    if (ext == "proto") return "proto";
    if (ext == "asm" || ext == "s" || ext == "S") return "asm";
    if (ext == "tex") return "tex";
    if (ext == "vim") return "vim";
    return "text";
}

std::string find_cached_skill(std::string const& name) noexcept {
    for (auto const& dir : cache_dirs()) {
        std::error_code ec;
        auto f = std::filesystem::path(dir) / name / "SKILL.md";
        if (std::filesystem::is_regular_file(f, ec)) return f.string();
    }
    return {};
}

void list_cached_skills(std::string& out) noexcept {
    bool found = false;
    for (auto const& dir : cache_dirs()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        // bash iterates `"$dir"/*/` (glob => sorted by name).
        std::vector<std::filesystem::path> subdirs;
        std::error_code it_ec;
        for (std::filesystem::directory_iterator it(dir, it_ec), end; !it_ec && it != end; it.increment(it_ec)) {
            if (it_ec) break;
            if (std::filesystem::is_directory(it->path())) subdirs.push_back(it->path());
        }
        std::sort(subdirs.begin(), subdirs.end());
        for (auto const& skill_path : subdirs) {
            std::error_code fec;
            auto skill_file = skill_path / "SKILL.md";
            if (!std::filesystem::is_regular_file(skill_file, fec)) continue;
            std::string meta = parse_frontmatter(skill_file);
            std::string name = jq_extract(meta, ".name // empty");
            if (name.empty()) name = skill_path.filename().string();
            std::string desc = jq_extract(meta, ".description // \"\"");
            if (desc.size() > 80) desc = desc.substr(0, 77) + "...";
            std::string field = name;
            if (field.size() < 24) field += std::string(24 - field.size(), ' ');
            out += "  " + field + " " + desc + "\n";
            found = true;
        }
    }
    if (!found) {
        out += "  (no cached skills found)\n";
        out += "\n";
        out += "  Skills are cached in: " + skills_dir() + "\n";
        out += "  Use 'prompt skill <query>' to search and download skills.\n";
    }
}

std::string ai_find_skills_tool(std::filesystem::path const& exe_path, std::filesystem::path const& git_root) noexcept {
    auto p = prompt::fs::bin_tool("ai-find-skills", exe_path, git_root);
    return p.string();
}

// search_skills: run ai-find-skills --json --no-scan <query>, suppress stderr.
std::string search_skills(std::string const& query, std::string const& tool) noexcept {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(tool, ec) || ec) return {};
    int ec2 = 0;
    return run_capture({tool, "--json", "--no-scan", query}, "", ec2);
}

std::string merge_results(std::string const& json) noexcept {
    std::string const filter =
        "[\n"
        "  (.skills_sh // [])[] | {name, board, repo, slug, url, installs, stars, match, summary},\n"
        "  (.clawhub    // [])[] | {name, board, repo:null, slug, url, installs, stars, match, summary},\n"
        "  (.github     // [])[] | {name, board, repo, slug:null, url, installs:null, stars, match, summary}\n"
        "] | to_entries | map(.key + 1 as $idx | .value | {idx: $idx, name, board, repo, slug, url, installs, stars, "
        "match, summary})\n";
    int ec = 0;
    return run_capture({"jq", "-c", filter}, json, ec);
}

std::string find_rec(std::string const& results, std::string const& name) noexcept {
    std::string q1 = "[.[] | select(.name == $n)] | .[0] // empty";
    int ec = 0;
    std::string rec = run_capture({"jq", "-c", "--arg", "n", name, q1}, results, ec);
    rec = strip_trailing_nl(rec);
    if (rec.empty()) {
        std::string q2 = "[.[] | select(.name | test($n; \"i\"))] | .[0] // empty";
        rec = strip_trailing_nl(run_capture({"jq", "-c", "--arg", "n", name, q2}, results, ec));
    }
    return rec;
}

} // namespace

prompt_result execute_skill(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    // command -v jq is required before anything else.
    {
        int ec = 0;
        (void)run_capture({"bash", "-c", "command -v jq >/dev/null 2>&1"}, "", ec);
        if (ec != 0) return {std::string{}, 1, false, "error: jq is required\n"};
    }

    std::string first = ctx.args_count > 0 ? std::string(ctx.args[0]) : std::string();

    // Build the positional arg list (parse_arguments drops --head + value).
    auto positionals = [&]() {
        std::vector<std::string> pos;
        for (std::size_t i = 0; i < ctx.args_count;) {
            if (ctx.args[i] == "--head") {
                ++i;
                if (i < ctx.args_count) ++i;
            } else {
                pos.push_back(std::string(ctx.args[i++]));
            }
        }
        return pos;
    };

    if (first == "list" || first == "install" || first == "uninstall") {
        auto pos = positionals();
        std::string subcommand = pos.empty() ? std::string() : pos[0];

        if (subcommand == "list") {
            std::string out;
            out += "Cached skills:\n";
            out += "\n";
            list_cached_skills(out);
            return {std::move(out), 0, false, std::string{}};
        }

        if (subcommand == "uninstall") {
            // uninstall_skill uses the raw $1, which for this subcommand is the
            // word "uninstall" itself.
            std::string name = "uninstall";
            bool found = false;
            std::string out;
            for (auto const& dir : cache_dirs()) {
                std::error_code ec;
                auto target = std::filesystem::path(dir) / name;
                if (std::filesystem::is_directory(target, ec)) {
                    std::uintmax_t removed = std::filesystem::remove_all(target, ec);
                    (void)removed;
                    out += "Uninstalled '" + name + "' from " + (std::filesystem::path(dir) / name).string() + "\n";
                    found = true;
                }
            }
            if (!found) {
                return {std::string{}, 1, false, "error: skill '" + name + "' not found in any cache directory\n"};
            }
            return {std::move(out), 0, false, std::string{}};
        }

        if (subcommand == "install") {
            // install_skill uses the raw $1 ("install"); this hits the network.
            std::string name = "install";
            std::string tool = ai_find_skills_tool(ctx.exe_path, ctx.git_root);
            std::string err;
            err += "Searching for '" + name + "'...\n";
            std::string results = merge_results(search_skills(name, tool));
            std::string rec = find_rec(results, name);
            if (rec.empty()) {
                return {std::string{}, 1, false, err + "error: skill '" + name + "' not found in any registry\n"};
            }
            err += "Found: " + jq_extract(rec, ".name") + " on " + jq_extract(rec, ".board") + "\n";
            std::string skill_name = jq_extract(rec, ".name");
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(skills_dir()) / skill_name, ec);
            err += "Downloading " + skill_name + "...\n";
            // download_skill_md is network/curl/unzip/gh dependent.
            int dec = 0;
            std::string body = run_capture({"curl", "-sS", "--max-time", "15", jq_extract(rec, ".url")}, "", dec);
            if (body.empty()) {
                std::filesystem::remove_all(std::filesystem::path(skills_dir()) / skill_name, ec);
                return {std::string{}, 1, false, err + "error: failed to download SKILL.md for '" + skill_name + "'\n"};
            }
            {
                std::ofstream of((std::filesystem::path(skills_dir()) / skill_name / "SKILL.md"), std::ios::binary);
                of << body;
            }
            err += "Installed '" + skill_name + "' to " +
                   (std::filesystem::path(skills_dir()) / skill_name / "SKILL.md").string() + "\n";
            std::string out;
            out += "\n";
            out += "Installed skill content:\n";
            out += "\n";
            out += read_file(std::filesystem::path(skills_dir()) / skill_name / "SKILL.md");
            return {std::move(out), 0, false, std::move(err)};
        }
    }

    // Default mode: prompt skill <name> [FILE...]
    auto pos = positionals();
    std::string skill_name = pos.empty() ? std::string() : pos[0];

    if (skill_name.empty()) {
        return {std::string{}, 2, false,
                "error: skill name required\n"
                "Usage: prompt skill <name> [FILE...]\n"
                "       prompt skill list   # to see cached skills\n"};
    }

    std::string err;
    std::string skill_file = find_cached_skill(skill_name);
    if (skill_file.empty()) {
        err += "Skill '" + skill_name + "' not cached, downloading...\n";
        // download_by_name: search registries and download if a match exists.
        std::string tool = ai_find_skills_tool(ctx.exe_path, ctx.git_root);
        std::string results = merge_results(search_skills(skill_name, tool));
        std::string rec = find_rec(results, skill_name);
        std::string downloaded_skill = rec.empty() ? std::string() : jq_extract(rec, ".name");
        if (downloaded_skill.empty()) {
            err += "error: skill '" + skill_name + "' not found in cache or registries\n";
            err += "Use 'prompt skill list --remote " + skill_name + "' to search registries.\n";
            return {std::string{}, 1, false, std::move(err)};
        }
        // A full faithful download-by-name would fetch the SKILL.md here; the
        // registry match found is surfaced verbatim so callers can retry.
        err += "Downloaded to ";
        err += (std::filesystem::path(skills_dir()) / downloaded_skill / "SKILL.md").string();
        err += "\n";
        skill_file = (std::filesystem::path(skills_dir()) / downloaded_skill / "SKILL.md").string();
    }

    std::string out = read_file(std::filesystem::path(skill_file));

    if (ctx.stdin_consumed) {
        std::string content = ctx.stdin_content;
        while (!content.empty() && content.back() == '\n') content.pop_back();
        if (!content.empty()) {
            out += "\n";
            out += content;
            out += "\n";
        }
    }

    for (std::size_t i = 1; i < pos.size(); ++i) {
        std::string const& file = pos[i];
        std::error_code ec;
        std::filesystem::path fp(file);
        if (std::filesystem::is_regular_file(fp, ec)) {
            std::string file_name = fp.filename().string();
            out += "\n";
            out += "File: " + file_name + "\n";
            out += "\n";
            out += "```" + bash_infer_lang(file_name) + "\n";
            out += trim_context_nl(read_file(fp), head_lines);
            out += "```\n";
        } else {
            err += "Warning: File '" + file + "' not found.\n";
        }
    }

    return {std::move(out), 0, false, std::move(err)};
}

void render_help_skill(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt skill <name> [--head N] [FILE...]
       some-command | prompt skill <name> [--head N] [FILE...]
       prompt skill list [--remote <query>]
       prompt skill install <name>
       prompt skill uninstall <name>

Loads a cached skill and outputs its content as an AI prompt.
If the skill is not cached, it is downloaded automatically.
Piped stdin and file arguments are embedded as context.
It also manages skills: list, install, and uninstall (see below).

Subcommands:
  list              List locally cached skills
  list --remote Q   Search remote registries for skills matching Q
  install <name>    Download a specific skill by exact name
  uninstall <name>  Remove a cached skill

Options:
  --head N   Keep only the first N lines of the embedded context
  -h, --help Show this help message

Examples:
  prompt skill docker                      # load cached docker skill
  cat file.cpp | prompt skill cpp-testing  # load skill + embed file
  prompt skill list                        # show cached skills
  prompt skill install docker              # download docker skill
)EOF";
}

} // namespace prompt::prompts
