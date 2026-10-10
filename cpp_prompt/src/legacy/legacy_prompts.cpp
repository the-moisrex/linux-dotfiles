#include "prompt/legacy/legacy_prompts.hpp"
#include "prompt/legacy/legacy_runner.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace prompt::legacy {

namespace {

// Stable storage for the dynamically registered descriptors' string_views
// (deque elements are never invalidated).
std::deque<std::string> prompt_strings;

std::string_view store(std::string s) {
    prompt_strings.push_back(std::move(s));
    return prompt_strings.back();
}

// Whether a script pulls the piped stdin in (bash's read_stdin/embed_stdin):
// the chain's shared pipe is spent once such a prompt has run. Text prompts
// always consume it (the dispatcher appends stdin after the file contents).
bool script_reads_stdin(std::string_view name) noexcept {
    auto file = prompt_file(name);
    if (file.empty()) return false;
    if (file.extension() != ".sh") return true;
    std::string content = read_file(file);
    return content.find("read_stdin") != std::string::npos || content.find("embed_stdin") != std::string::npos;
}

} // namespace

prompt_result execute_legacy_prompt(prompt_context&& ctx) noexcept {
    std::vector<std::string> arg_store;
    arg_store.reserve(ctx.args_count);
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        arg_store.emplace_back(ctx.args[i]);
    }

    std::vector<char const*> argv;
    argv.reserve(arg_store.size() + 1);
    for (auto const& s : arg_store) argv.push_back(s.c_str());
    argv.push_back(nullptr);

    auto result = run_prompt_script(ctx.name, ctx.stdin_content, argv);
    bool consumed = ctx.stdin_consumed && script_reads_stdin(ctx.name);
    return {std::move(result.stdout_data), result.exit_code, result.no_clipboard, std::move(result.stderr_data),
            consumed};
}

void register_legacy_prompts() noexcept {
    namespace fs = std::filesystem;

    std::unordered_set<std::string> seen;
    for (auto const& desc : get_all_prompts()) {
        seen.insert(std::string(desc.name));
    }

    for (auto const& dir : search_dirs()) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;

        // name -> (extension priority, file); mirrors _find_prompt_file's
        // .sh > .txt > .md > bare preference within a directory.
        std::map<std::string, std::pair<int, fs::path>> best;

        fs::directory_iterator it(dir, ec), end;
        if (ec) continue;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            auto const& p = it->path();
            std::error_code tec;
            if (!fs::is_regular_file(p, tec)) continue;
            std::string fname = p.filename().string();
            if (fname.starts_with('_')) continue;

            std::string name = fname;
            for (char const* ext : {".sh", ".txt", ".md"}) {
                std::string_view sv(name);
                std::string_view ev(ext);
                if (sv.ends_with(ev)) name.resize(name.size() - ev.size());
            }
            if (!name.empty() && name.back() == '.') name.pop_back();
            if (name.empty()) continue;

            int prio = 3;
            if (fname.ends_with(".sh")) prio = 0;
            else if (fname.ends_with(".txt"))
                prio = 1;
            else if (fname.ends_with(".md"))
                prio = 2;

            auto [ins, inserted] = best.try_emplace(name, prio, p);
            if (!inserted && prio < ins->second.first) ins->second = {prio, p};
        }

        for (auto const& [name, prio_and_file] : best) {
            std::string_view path = store(prio_and_file.second.string());
            set_sort_file(name, path);

            auto help = extract_static_help(prio_and_file.second);
            std::string summary = help.summary;
            if (summary.empty() && !help.found) summary = "(no help)";
            std::string full = std::move(help.full);

            if (seen.count(name)) {
                // Native prompt shadowed by a script: bash's list,
                // list-prompts and --help all read the script, so mirror
                // its help text over the embedded native one.
                set_help_texts(name, store(std::move(summary)), store(std::move(full)));
                continue;
            }
            seen.insert(name);

            register_prompt({store(name), store(std::move(summary)), store(std::move(full)), &execute_legacy_prompt,
                             nullptr,
                             path,   // script: legacy execution file
                             path}); // sort_file: bash list sort key
        }
    }
}

} // namespace prompt::legacy
