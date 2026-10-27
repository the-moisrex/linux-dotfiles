#include "../compress/compress.hpp"
#include "../core/fs.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/project.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/signals.hpp"
#include "../sdk/stage.hpp"
#include <array>
#include <filesystem>
#include <format>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prompt::prompts {

namespace {

std::vector<std::string> split_list(std::string_view value) {
    std::vector<std::string> items;
    std::size_t start = 0;
    while (start <= value.size()) {
        std::size_t const end = value.find(',', start);
        std::string_view const piece = value.substr(start, end == std::string_view::npos ? end : end - start);
        if (!piece.empty()) items.emplace_back(piece);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return items;
}

std::size_t parse_size(std::string_view text, std::string_view flag) {
    if (text.empty()) throw usage_error{"compress: " + std::string(flag) + " needs a number"};
    std::size_t value = 0;
    for (char const c : text) {
        if (c < '0' || c > '9') {
            throw usage_error{"compress: " + std::string(flag) + " needs a number, got '" + std::string(text) + "'"};
        }
        value = (value * 10) + static_cast<std::size_t>(c - '0');
    }
    return value;
}

void publish_project(project::info const& p, result& r) {
    if (!p.languages.empty()) r.published.set(keys::project_language, project::to_string(p.languages.front()));
    r.published.set(keys::project_build, project::to_string(p.build));
    if (p.build_dir) r.published.set(keys::project_build_dir, p.build_dir->string());
    r.published.set(keys::project_package, project::to_string(p.package));
    r.published.set(keys::project_tests, project::to_string(p.tests));
}

// The intent and focus the engine reads, from the `keys::mode`/`keys::focus`
// facts a preceding `.fix`/`.debug`/`.run` published. `--for` wins over the fact.
compress::signals base_signals(view const& v, std::string_view mode_override) {
    compress::signals base;
    if (auto const prior = v.prior.get(keys::mode)) base.mode = signals::intent_from(*prior);
    if (!mode_override.empty()) base.mode = signals::intent_from(mode_override);
    if (auto const prior = v.prior.get(keys::focus)) base.focus = signals::parse_focus(*prior);
    return base;
}

// A piped source file carries no fence and no path, so the fence-less input is
// wrapped in one with a language guessed from its first lines. A guess of ""
// leaves the content sniffers (`.csv.sample`) free to claim it.
std::string guess_lang(std::string_view text) {
    if (text.starts_with("#!")) {
        if (text.contains("python")) return "python";
        if (text.contains("node")) return "javascript";
        return "bash";
    }
    static constexpr std::array<std::string_view, 9> kCpp = {
        "#include", "#pragma", "std::", "namespace ", "template<", "template <", "TEST(", "TEST_F(", "TEST_P(",
    };
    for (auto const needle : kCpp) {
        if (text.contains(needle)) return "cpp";
    }
    if (text.contains("def ") || text.contains("import ")) return "python";
    if (text.contains("package ") && text.contains("func ")) return "go";
    return {};
}

// The exact document `compress_files` builds, so `--explain` can describe the
// same regions without compressing them.
std::string files_markdown(std::span<std::filesystem::path const> files, project::info const& p) {
    std::string document;
    for (auto const& file : files) {
        std::string const content = read_file(file);
        if (content.empty()) continue;
        auto const rel =
            fs::relative_path(file, p.root.empty() ? std::nullopt : std::optional<std::filesystem::path>(p.root));
        document += std::format("### {}\n\n{}\n", rel.string(), compress::fence_for(infer_lang(file)));
        document += content;
        if (content.back() != '\n') document.push_back('\n');
        document += "```\n\n";
    }
    return document;
}

} // namespace

static void execute_compress(view const& v, result& r) {
    compress::options opt;
    std::string mode_override;
    bool want_project = false;
    bool explain = false;
    std::vector<std::filesystem::path> files;

    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--force" || arg == "-f") {
            opt.force = true;
        } else if (arg == "--project") {
            want_project = true;
        } else if (arg == "--explain") {
            explain = true;
        } else if (arg == "--for") {
            if (++i >= v.args.size()) throw usage_error{"compress: --for needs a value"};
            mode_override = std::string(v.args[i]);
        } else if (arg == "--level") {
            if (++i >= v.args.size()) throw usage_error{"compress: --level needs a value"};
            opt.level = static_cast<int>(parse_size(v.args[i], "--level"));
        } else if (arg == "--min-lines") {
            if (++i >= v.args.size()) throw usage_error{"compress: --min-lines needs a value"};
            opt.min_lines = parse_size(v.args[i], "--min-lines");
        } else if (arg == "--budget") {
            if (++i >= v.args.size()) throw usage_error{"compress: --budget needs a value"};
            opt.budget_lines = parse_size(v.args[i], "--budget");
        } else if (arg == "--only") {
            if (++i >= v.args.size()) throw usage_error{"compress: --only needs a value"};
            opt.only = split_list(v.args[i]);
        } else if (arg == "--skip") {
            if (++i >= v.args.size()) throw usage_error{"compress: --skip needs a value"};
            opt.skip = split_list(v.args[i]);
        } else if (arg.size() > 1 && arg.front() == '-') {
            throw usage_error{"compress: unknown option " + std::string(arg)};
        } else {
            std::filesystem::path const file(arg);
            std::error_code ec;
            if (std::filesystem::is_directory(file, ec)) {
                throw usage_error{"compress: " + std::string(arg) +
                                  " is a directory, not a file; pass the files, e.g. " + std::string(arg) + "/*.cpp"};
            }
            if (!std::filesystem::is_regular_file(file, ec)) {
                throw usage_error{"compress: no such file: " + std::string(arg)};
            }
            files.push_back(file);
        }
    }

    compress::signals base = base_signals(v, mode_override);
    if (want_project || base.mode != signals::intent::general || !base.focus.empty()) {
        base.project = project::detect(v.env.git_root, v.env);
        publish_project(base.project, r);
    }

    bool const have_stdin = v.in.available();
    if (files.empty() && !have_stdin) throw soft_error{"compress: no FILE arguments and no piped input"};

    std::string document;
    if (files.empty()) {
        document = std::string(v.in.text());
        v.in.spend();
        if (!document.contains("```") && !document.contains("~~~")) {
            std::string wrapped = "```" + guess_lang(document) + "\n";
            wrapped += document;
            if (wrapped.back() != '\n') wrapped.push_back('\n');
            wrapped += "```\n";
            document = std::move(wrapped);
        }
    } else if (explain) {
        document = files_markdown(files, base.project);
    }

    if (explain) {
        for (auto const& line : compress::explain(document, base, opt)) {
            r.out += line;
            r.out.push_back('\n');
        }
        return;
    }

    compress::report const rep =
        files.empty() ? compress::compress_document(document, base, opt) : compress::compress_files(files, base, opt);
    r.out = rep.text;
}

static void render_help_compress(std::ostream& os) {
    os << R"EOF(Usage: prompt compress [options] [FILE...]

Compress the given FILEs (or the piped input) for AI consumption: C/C++ function
bodies are elided while declarations, doc comments and an include map of the
project-local headers are kept, a Google Test file keeps its failed bodies and a
sample of the rest, and a large CSV block collapses to its header and a few rows.

With no FILE, the piped input is compressed: a document already carrying fenced
blocks (for example the output of `prompt .files`) is scanned for regions, and a
fence-less source file is wrapped in one with a guessed language.

The chain's intent (`keys::mode`) and focus (`keys::focus`) facts, published by
`.fix`, `.debug` and `.run`, select what to keep and which regions are focal (a
focal region is never compressed). `--for` overrides the intent; the project is
re-detected whenever the intent is not `general` or `--project` is given.

Examples:
  prompt compress src/big.cpp .ai.dseek
  prompt .files src/a.cpp src/b.cpp | prompt compress .ai.dseek
  prompt compress --for fix --level 2 src/big.cpp

Options:
  --for MODE        Intent override: general, fix, debug or review
  --level N         Compression level (1-3; higher drops doc comments/macros)
  --min-lines N     Leave regions shorter than N lines alone (default 120)
  --budget N        Raise the level until the result fits N lines
  --only NAMES      Restrict to these comma-separated compressor names
  --skip NAMES      Disable these comma-separated compressor names
  --project         Detect the project shape and publish the `project.*` facts
  --force, -f       Compress even regions shorter than `--min-lines`
  --explain         Print what each region would do, without compressing
  -h, --help        Show this help message
)EOF";
}

static void complete_compress(completion_request const& req, std::ostream& os) {
    static constexpr std::array<std::string_view, 4> kModes = {"general", "fix", "debug", "review"};
    if (completion::write_values_after_flag(os, req, "--for", kModes)) return;

    if (!req.prior_args.empty()) {
        std::string_view const prev = req.prior_args.back();
        if (prev == "--only" || prev == "--skip") {
            for (auto const& unit : compress::get_all_compressors())
                completion::write_item(os, unit.name, unit.summary);
        }
    }
}

static void execute_compress_list(view const& v, result& r) {
    for (auto const& arg : v.args) {
        if (!arg.empty()) throw usage_error{"compress.list: takes no arguments"};
    }

    if (auto const lang = v.prior.get(keys::project_language)) {
        r.out = "language: " + std::string(*lang);
        if (auto const b = v.prior.get(keys::project_build)) r.out += " · build: " + std::string(*b);
        if (auto const d = v.prior.get(keys::project_build_dir)) r.out += " (" + std::string(*d) + ")";
        if (auto const p = v.prior.get(keys::project_package)) r.out += " · pkg: " + std::string(*p);
        if (auto const t = v.prior.get(keys::project_tests)) r.out += " · tests: " + std::string(*t);
    } else {
        r.out = project::summary_line(project::detect(v.env.git_root, v.env));
    }
    r.out += "\n\n";

    auto const units = compress::get_all_compressors();
    if (units.empty()) {
        r.out += "no compressors registered\n";
        return;
    }
    r.out += std::format("{} compressors:\n", units.size());
    for (auto const& unit : units) r.out += std::format("  {:<14} {}\n", unit.name, unit.summary);
}

static void render_help_compress_list(std::ostream& os) {
    os << R"EOF(Usage: prompt compress.list

List the project shape and every registered compressor with its one-line summary.
The project line comes from the `project.*` facts when `.project` (or `.compress`)
published them, and is re-detected otherwise, so the list reflects the project
`.compress` would see.

Options:
  -h, --help  Show this help
)EOF";
}

// `.compress` and its catalog lens `.compress.list` share one file: the lens is
// the catalog face of the same subsystem.
bool const registered_compress =
    prompt::register_prompt("compress", "Compress the given files (or the piped input) for AI consumption.",
                            &execute_compress, &render_help_compress, &complete_compress);

bool const registered_compress_list = prompt::register_prompt(
    "compress.list", "List the project shape and every registered compressor with its one-line summary.",
    &execute_compress_list, &render_help_compress_list);

} // namespace prompt::prompts
