// Shell completion (`prompt <name> --complete ...` and the whole-line
// `prompt --complete-line ...`): the dispatcher derives every prompt's flags
// from its own help text, a prompt's optional complete_fn adds positional/value
// candidates, and the line mode classifies the position itself. These tests
// exercise the helpers directly and check the end-to-end wiring through the
// registry (which prompts registered a callback, and what their help yields).
#include "../src/sdk/completion.hpp"
#include "../src/sdk/line_completion.hpp"
#include "../src/sdk/prompt_file.hpp"
#include "../src/sdk/prompt_registry.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

using namespace prompt;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// True when `line` appears as a whole line (`value` or `value<TAB>description`).
bool has_line(std::string_view text, std::string_view line) {
    std::string const needle = "\n" + std::string(line) + "\n";
    std::string const prefix = std::string(line) + "\n";
    std::string const body(text);
    return body.contains(needle) || text.starts_with(prefix);
}

// True when `value` starts a line, with or without a description (`value` alone
// or `value<TAB>description`). Unlike `has_line` this also matches candidates
// that carry a description, which is what every name and file candidate does.
bool has_candidate(std::string_view text, std::string_view value) {
    std::string const body(text);
    std::string const bare{value};
    return body.starts_with(bare + "\t") || body.contains("\n" + bare + "\t") || body.contains("\n" + bare + "\n");
}

template <typename Fn>
std::string render(Fn&& fn) {
    std::ostringstream os;
    fn(os);
    return os.str();
}

void write_file(std::filesystem::path const& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// A scratch tree for the prompt-file candidates, removed on the way out.
struct temp_tree {
    std::filesystem::path root;

    explicit temp_tree(std::string_view name)
        : root(std::filesystem::temp_directory_path() /
               (std::string(name) + "-" + std::to_string(static_cast<long>(::getpid())))) {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root);
    }
    temp_tree(temp_tree const&) = delete;
    temp_tree& operator=(temp_tree const&) = delete;
    ~temp_tree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

completion_request request(std::string_view prefix, std::vector<std::string_view> const& prior) {
    return completion_request{.prefix = prefix, .prior_args = std::span<std::string_view const>{prior}};
}

// `matches` is a plain prefix test, with the empty prefix matching everything.
void test_matches() {
    check(completion::matches("", "--head"), "empty prefix matches everything");
    check(completion::matches("--", "--head"), "flag prefix matches a flag");
    check(!completion::matches("--x", "--head"), "unrelated prefix does not match");
}

// Flag completion is derived from the prompt's own `Options:` block: both
// spellings of a comma-separated option, at most the matching ones.
void test_help_flags() {
    std::string_view help = "Usage: prompt demo [--head N]\n"
                            "\n"
                            "Options:\n"
                            "  --head N     Keep only the first N lines\n"
                            "  --type, -t   Diagram type\n"
                            "  -h, --help   Show this help\n"
                            "\n"
                            "Examples:\n"
                            "  prompt demo --head 5\n";
    std::string all = render([&](std::ostream& os) { completion::write_help_flags(os, "--", help); });
    check(has_line(all, "--head"), "help flags: --head");
    check(has_line(all, "--type"), "help flags: --type");
    check(has_line(all, "--help"), "help flags: --help");

    std::string short_flags = render([&](std::ostream& os) { completion::write_help_flags(os, "-t", help); });
    check(has_line(short_flags, "-t"), "help flags: -t");
    check(!has_line(short_flags, "--type"), "help flags: -t does not match --type");

    // A non-dash prefix never returns flags, and a help without Options yields
    // nothing.
    check(render([&](std::ostream& os) { completion::write_help_flags(os, "f", help); }).empty(),
          "help flags need a dash prefix");
    check(render([&](std::ostream& os) { completion::write_help_flags(os, "--", "Usage: prompt x\n"); }).empty(),
          "help flags need an Options block");
}

// `write_values_after_flag` completes a flag's value only right after the flag.
void test_value_after_flag() {
    static constexpr std::array<std::string_view, 3> kValues = {"json", "xml", "yaml"};
    std::vector<std::string_view> after{"--format"};
    std::string out = render(
        [&](std::ostream& os) { completion::write_values_after_flag(os, request("", after), "--format", kValues); });
    check(has_line(out, "json") && has_line(out, "xml") && has_line(out, "yaml"), "value completion lists every value");

    std::string filtered = render(
        [&](std::ostream& os) { completion::write_values_after_flag(os, request("y", after), "--format", kValues); });
    check(has_line(filtered, "yaml") && !has_line(filtered, "json"), "value completion filters by prefix");

    std::vector<std::string_view> elsewhere{"--head"};
    check(render([&](std::ostream& os) {
              completion::write_values_after_flag(os, request("", elsewhere), "--format", kValues);
          }).empty(),
          "value completion requires the flag immediately before");
}

// Values carry an optional description after a tab, exactly how fish reads them.
void test_items_with_descriptions() {
    static constexpr std::array<completion::item, 2> kItems = {
        {
            {.value = "list", .description = "List cached skills"},
            {.value = "install", .description = "Download a skill"},
        },
    };
    std::string out = render([&](std::ostream& os) { completion::write_items(os, "i", kItems); });
    check(out == "install\tDownload a skill\n", "items keep their tab-separated description");
}

// End-to-end wiring: the prompts that know their own operands registered a
// complete_fn, and their help text feeds the flag parser. `tse`/`tse.intraday`/
// `tse.codal` are only checked for registration — invoking them would shell out
// to `bin/tse.resolve`. `tse.analyze`/`tse.intraday.analysis` take no arguments,
// so they register no complete_fn.
void test_registry_wiring() {
    for (char const* name : {
             "tse",
             "tse.intraday",
             "tse.codal",
             "run",
             "skill",
             "diagram",
             "metadata",
             "opportunities",
             "overengineering",
         }) {
        auto const* desc = find_prompt(name);
        check(desc != nullptr, "prompt is registered");
        if (desc != nullptr) {
            check(desc->complete_fn != nullptr, "prompt registered a complete_fn");
        }
    }

    check(find_prompt("tse.analyze") != nullptr, "tse.analyze is registered");
    check(find_prompt("tse.analyze") != nullptr && find_prompt("tse.analyze")->complete_fn == nullptr,
          "tse.analyze takes no arguments, so it registers no complete_fn");

    check(find_prompt("tse.analyze.history") != nullptr, "tse.analyze.history is registered");
    check(find_prompt("tse.analyze.history") != nullptr && find_prompt("tse.analyze.history")->complete_fn == nullptr,
          "tse.analyze.history takes no arguments, so it registers no complete_fn");

    check(find_prompt("tse.intraday.analysis") != nullptr, "tse.intraday.analysis is registered");
    check(find_prompt("tse.intraday.analysis") != nullptr &&
              find_prompt("tse.intraday.analysis")->complete_fn == nullptr,
          "tse.intraday.analysis takes no arguments, so it registers no complete_fn");

    auto const* metadata = find_prompt("metadata");
    if (metadata != nullptr && metadata->render_help_fn != nullptr) {
        std::string help = render([&](std::ostream& os) { metadata->render_help_fn(os); });
        std::string flags = render([&](std::ostream& os) { completion::write_help_flags(os, "--", help); });
        check(has_line(flags, "--format"), "metadata help advertises --format");
        check(has_line(flags, "--head"), "metadata help advertises --head");
    }

    auto const* diagram = find_prompt("diagram");
    if (diagram != nullptr && diagram->complete_fn != nullptr) {
        std::vector<std::string_view> after{"--type"};
        std::string out = render([&](std::ostream& os) { diagram->complete_fn(request("mer", after), os); });
        check(has_line(out, "mermaid"), "diagram completes mermaid after --type");
    }
}

// The catalog in all three spellings the chain accepts, each described by the
// prompt's own summary, filtered by prefix.
void test_prompt_names() {
    std::string all = render([&](std::ostream& os) { write_prompt_names(os, ""); });
    check(has_candidate(all, "list"), "names: the bare spelling");
    check(has_candidate(all, ".list"), "names: the `.name` spelling");
    check(has_candidate(all, "-list"), "names: the `-name` spelling");
    auto const pos = all.find("list\t");
    check(pos != std::string::npos && all.find('\n', pos) > pos + 5, "names: the help summary is the description");

    std::string filtered = render([&](std::ostream& os) { write_prompt_names(os, ".met"); });
    check(has_candidate(filtered, ".metadata"), "names: the prefix keeps the matching spelling");
    check(!has_candidate(filtered, ".files"), "names: the prefix drops unrelated names");
    check(render([&](std::ostream& os) { write_prompt_names(os, "zz-no-such-prefix"); }).empty(),
          "names: a prefix nothing matches yields nothing");
}

// `write_prompt_candidates` is the one place both completion forms answer from:
// the flags derived from the prompt's own help plus its complete_fn.
void test_shared_prompt_candidates() {
    std::vector<std::string_view> const none;
    if (auto const* metadata = find_prompt("metadata"); metadata != nullptr) {
        std::string flags = render([&](std::ostream& os) { write_prompt_candidates(*metadata, "--", none, os); });
        check(has_line(flags, "--format"), "shared: flags come from the prompt's own help");
        check(has_line(flags, "--head"), "shared: the shared options block contributes too");
    }
    if (auto const* diagram = find_prompt("diagram"); diagram != nullptr) {
        std::vector<std::string_view> after{"--type"};
        std::string values = render([&](std::ostream& os) { write_prompt_candidates(*diagram, "", after, os); });
        check(has_line(values, "mermaid"), "shared: complete_fn values are offered");
    }
}

// `*.prompt` candidates: a token with a separator lists that directory, a bare
// one lists the three sources the resolver reads, nearest first.
void test_prompt_file_candidates(temp_tree const& tree) {
    std::string const dir = (tree.root / "listing").string() + "/";
    write_file(tree.root / "listing" / "alpha.prompt", ".note a\n");
    write_file(tree.root / "listing" / "beta.prompt", ".note b\n");
    write_file(tree.root / "listing" / "notes.txt", "not a prompt\n");
    std::filesystem::create_directories(tree.root / "listing" / "sub");

    std::string all = render([&](std::ostream& os) { write_prompt_file_candidates(os, dir, tree.root); });
    check(has_candidate(all, dir + "alpha.prompt"), "files: `*.prompt` files are listed");
    check(has_candidate(all, dir + "beta.prompt"), "files: every `*.prompt` file is listed");
    check(!all.contains("notes.txt"), "files: other files are skipped");
    check(has_candidate(all, dir + "sub/"), "files: subdirectories are offered so the user can descend");

    std::string filtered = render([&](std::ostream& os) { write_prompt_file_candidates(os, dir + "al", tree.root); });
    check(has_candidate(filtered, dir + "alpha.prompt"), "files: the typed prefix filters the listing");
    check(!filtered.contains("beta.prompt"), "files: the typed prefix drops the other entries");

    write_file(tree.root / "empty" / "local.prompt", ".note l\n");
    write_file(tree.root / "empty" / "shared.prompt", ".note s\n");
    write_file(tree.root / "repo" / ".prompt" / "shared.prompt", ".note s\n");
    write_file(tree.root / "repo" / ".prompt" / "project.prompt", ".note p\n");
    write_file(tree.root / "cfg" / "prompt" / "prompts" / "user.prompt", ".note u\n");

    auto const cwd = std::filesystem::current_path();
    char const* const previous_xdg = std::getenv("XDG_CONFIG_HOME");
    std::string const saved = previous_xdg != nullptr ? previous_xdg : "";
    ::setenv("XDG_CONFIG_HOME", (tree.root / "cfg").c_str(), 1);
    std::filesystem::current_path(tree.root / "empty");
    std::string bare = render([&](std::ostream& os) { write_prompt_file_candidates(os, "", tree.root / "repo"); });
    std::string no_project = render([&](std::ostream& os) { write_prompt_file_candidates(os, "", {}); });
    std::filesystem::current_path(cwd);
    if (previous_xdg == nullptr) {
        ::unsetenv("XDG_CONFIG_HOME");
    } else {
        ::setenv("XDG_CONFIG_HOME", saved.c_str(), 1);
    }

    check(bare.contains("local.prompt\tprompt file"), "files: the current directory is the nearest source");
    check(bare.contains("project.prompt\tprompt file (project)"), "files: the project `.prompt/` dir is named");
    check(bare.contains("user.prompt\tprompt file (config)"), "files: the user config dir is named");
    check(bare.find("shared.prompt") == bare.rfind("shared.prompt"), "files: a name two sources share is emitted once");
    check(!no_project.contains("project.prompt"), "files: without a git root there is no project source");
    check(no_project.contains("user.prompt\tprompt file (config)"), "files: the config source needs no git root");
}

// The whole-line mode classifies the position: head, a `.`/`-` step boundary, an
// argument of the open prompt, or a tail it leaves to the shell.
void test_complete_line_positions() {
    std::filesystem::path const no_root;

    std::string head = render([&](std::ostream& os) { complete_line("", {}, no_root, os); });
    check(has_candidate(head, "--help"), "line: the head offers the dispatcher's own flags");
    check(has_candidate(head, "list"), "line: the head offers prompt names");
    check(has_candidate(head, ".list"), "line: the head offers the shorthand spellings");

    std::vector<std::string_view> diagram{"diagram", "--type"};
    std::string values = render([&](std::ostream& os) { complete_line("", diagram, no_root, os); });
    check(has_line(values, "mermaid"), "line: the open prompt's values are completed");

    std::vector<std::string_view> note{".note", "hi"};
    std::string boundary = render([&](std::ostream& os) { complete_line(".met", note, no_root, os); });
    check(has_candidate(boundary, ".metadata"), "line: a `.` token is completed as the next step");
    std::string dashed = render([&](std::ostream& os) { complete_line("-met", note, no_root, os); });
    check(has_candidate(dashed, "-metadata"), "line: a `-` token is completed as the next step");
    check(render([&](std::ostream& os) { complete_line("--met", note, no_root, os); }).empty(),
          "line: a dispatcher option is not a step");
    check(render([&](std::ostream& os) { complete_line("h", note, no_root, os); }).empty(),
          "line: a bare word after a prompt is not a step");

    std::vector<std::string_view> exec{".exec", "--"};
    check(render([&](std::ostream& os) { complete_line("", exec, no_root, os); }).empty(),
          "line: nothing after `.exec --`");
    std::vector<std::string_view> body{".each", "--print", "table:symbol"};
    check(render([&](std::ostream& os) { complete_line("", body, no_root, os); }).empty(),
          "line: nothing inside an `.each` body");
    std::vector<std::string_view> closed{".each", ".end"};
    check(has_candidate(render([&](std::ostream& os) { complete_line(".met", closed, no_root, os); }), ".metadata"),
          "line: completion resumes at a step boundary after `.end`");
}

// Functions in the line mode: the `.func` keyword at a step boundary, the
// `.func … .end` body as a chain, and the names visible at this point — the
// global `funcs` files, the inline definitions and those in an inlined
// `*.prompt` file.
void test_complete_line_functions(temp_tree const& tree) {
    std::filesystem::path const no_root;

    std::string head = render([&](std::ostream& os) { complete_line(".f", {}, no_root, os); });
    check(has_candidate(head, ".func"), "func: `.f` offers the `.func` keyword");

    std::vector<std::string_view> after_note{".note", "hi"};
    check(has_candidate(render([&](std::ostream& os) { complete_line(".f", after_note, no_root, os); }), ".func"),
          "func: a step boundary offers the `.func` keyword");

    // An inline definition becomes a candidate after `.end`.
    std::vector<std::string_view> defined{".func", "one", ".note", "hi", ".end"};
    check(has_candidate(render([&](std::ostream& os) { complete_line(".o", defined, no_root, os); }), ".one"),
          "func: an inline definition is completed after `.end`");

    // Inside the body `.end` closes it, `.func` is top level only, and the body
    // itself completes as a chain.
    std::vector<std::string_view> in_body{".func", "one", ".note", "hi"};
    std::string body = render([&](std::ostream& os) { complete_line(".", in_body, no_root, os); });
    check(has_candidate(body, ".end"), "func: `.end` closes the open body");
    check(!has_candidate(body, ".func"), "func: `.func` is top level only");
    check(has_candidate(render([&](std::ostream& os) { complete_line(".no", in_body, no_root, os); }), ".note"),
          "func: the body completes as a chain");

    // A function call opens a step (a following dispatcher option is not a step),
    // and the next step boundary still offers names.
    std::vector<std::string_view> invoked{".func", "one", ".end", ".one", "arg"};
    check(render([&](std::ostream& os) { complete_line("--f", invoked, no_root, os); }).empty(),
          "func: a dispatcher option is not a step after a function call");
    check(has_candidate(render([&](std::ostream& os) { complete_line(".n", invoked, no_root, os); }), ".note"),
          "func: a step boundary after a function call completes names");

    // A function defined in an inlined `*.prompt` file is discovered.
    write_file(tree.root / ".prompt" / "functions.prompt", ".func filefn .note x .end\n");
    std::vector<std::string_view> from_file{"functions.prompt"};
    check(has_candidate(render([&](std::ostream& os) { complete_line(".file", from_file, tree.root, os); }), ".filefn"),
          "func: a definition in an inlined *.prompt file is completed");

    // `--no-prompt-files` suppresses the inline pass, so the file is not read.
    std::vector<std::string_view> suppressed{"--no-prompt-files", "functions.prompt"};
    check(
        !has_candidate(render([&](std::ostream& os) { complete_line(".file", suppressed, tree.root, os); }), ".filefn"),
        "func: --no-prompt-files suppresses a file-defined function");

    // A function in a global `funcs` file is offered with its signature.
    write_file(tree.root / ".prompt" / "funcs", ".func globfn(who) .note \"hi {who}\" .end\n");
    char const* const previous = std::getenv("PROMPT_FUNCS");
    std::string const saved = previous != nullptr ? previous : "";
    ::setenv("PROMPT_FUNCS", (tree.root / ".prompt" / "funcs").c_str(), 1);
    std::string global = render([&](std::ostream& os) { complete_line(".glob", {}, tree.root, os); });
    if (previous == nullptr) {
        ::unsetenv("PROMPT_FUNCS");
    } else {
        ::setenv("PROMPT_FUNCS", saved.c_str(), 1);
    }
    check(global.contains(".globfn\tfunction(who)"), "func: a global funcs definition is offered with its signature");
}

} // namespace

int main() {
    test_matches();
    test_help_flags();
    test_value_after_flag();
    test_items_with_descriptions();
    test_registry_wiring();
    test_prompt_names();
    test_shared_prompt_candidates();
    temp_tree const tree{"prompt-completion-test"};
    test_prompt_file_candidates(tree);
    test_complete_line_positions();
    test_complete_line_functions(tree);
    if (failures == 0) {
        std::printf("all completion tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d completion checks failed\n", failures);
    return 1;
}
