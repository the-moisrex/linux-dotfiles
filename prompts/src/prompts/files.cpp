#include "../core/fs.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace prompt::prompts {

namespace fs_std = std::filesystem;

static void collect_regular_files_from_dir(fs_std::path const& dir_path, std::vector<fs_std::path>& out_files,
                                           std::string& errors) {
    std::error_code ec;
    auto iter_opts = fs_std::directory_options::skip_permission_denied;
    fs_std::recursive_directory_iterator it(dir_path, iter_opts, ec);
    fs_std::recursive_directory_iterator end;

    if (ec) {
        errors += std::format("prompt files: cannot read directory: \"{}\" ({})\n", dir_path.string(), ec.message());
        return;
    }

    std::vector<fs_std::path> dir_files;
    while (it != end) {
        // Skip hidden directories like .git, .cache, etc.
        auto const& p = it->path();
        if (it->is_directory(ec) && p.filename().string().starts_with('.')) {
            it.disable_recursion_pending();
            it.increment(ec);
            continue;
        }

        if (!ec && it->is_regular_file(ec)) {
            if (::access(p.c_str(), R_OK) == 0) {
                dir_files.push_back(p);
            } else {
                errors += std::format("prompt files: file not readable: \"{}\"\n", p.string());
            }
        }
        it.increment(ec);
    }

    // Sort to guarantee deterministic output ordering across runs/filesystems
    std::sort(dir_files.begin(), dir_files.end());
    out_files.insert(out_files.end(), dir_files.begin(), dir_files.end());
}

static void execute_files(view const& v, result& r) {
    auto opts = file_options_or_throw(v);
    auto const file_args = positional_args(v);

    std::vector<fs_std::path> selected_files;
    std::string errors;

    if (file_args.empty()) [[unlikely]] {
        selected_files = select_files(v.env);
    } else {
        for (auto const& arg : file_args) {
            auto resolved = resolve_input_file(v.env, arg);
            if (!resolved) {
                errors += std::format("prompt files: file or directory not found: {}\n", std::string(arg));
                continue;
            }

            std::error_code ec;
            auto status = fs_std::status(*resolved, ec);
            if (ec) {
                errors += std::format("prompt files: cannot stat \"{}\": {}\n", resolved->string(), ec.message());
                continue;
            }

            if (fs_std::is_directory(status)) {
                collect_regular_files_from_dir(*resolved, selected_files, errors);
            } else if (fs_std::is_regular_file(status)) {
                if (::access(resolved->c_str(), R_OK) != 0) {
                    errors += "prompt files: file not readable: \"" + std::string(arg) + "\"; resolved to \"" +
                              resolved->string() + "\"\n";
                    continue;
                }
                selected_files.push_back(*resolved);
            } else {
                errors += std::format("prompt files: unsupported file type: \"{}\"\n", resolved->string());
            }
        }
    }

    std::string output;
    std::vector<std::string> embedded_paths;

    for (auto const& file : selected_files) {
        if (!fs_std::exists(file) || !fs_std::is_regular_file(file)) continue;

        auto rel = fs::relative_path(file, v.env.git_root.empty() ? std::nullopt : std::optional(v.env.git_root));
        auto lang = infer_lang(file);
        auto content = read_file(file);

        if (!content.empty()) {
            embedded_paths.push_back(file.string());
            if (opts.head_lines) {
                output += std::format("File {} (first {} lines)\n\n", rel.string(), std::to_string(*opts.head_lines));
            } else {
                output += std::format("File {}\n\n", rel.string());
            }
            output += std::format("```{}\n", lang);
            output += number_lines(trim_context(content, opts.head_lines), opts.numbered);
            output += "\n```\n\n";
        }
    }

    if (!errors.empty()) {
        r.err = std::move(errors);
    }
    r.out = std::move(output);
    if (!embedded_paths.empty()) {
        r.published.set(keys::save_stem, fs_std::path(embedded_paths.front()).stem().string());
    }
}

static void render_help_files(std::ostream& os) {
    os << file_usage_help("files [path...]") << R"EOF(
Appends the given fuzzily found files or directory contents as Markdown code blocks.
If a directory is passed, it is recursively traversed for regular files.
If you're inside a Git repository, file headings are printed relative to the
repository root.
If no files are provided, `fzf -m` is used to choose them interactively.

Options:
)EOF" << file_options_help("each embedded file");
}

// The tool declaration: a descriptor keeps a pointer to it, so it is a static.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
schema::tool const k_files_tool =
    schema::make_tool("files",
                      "Appends the given fuzzily found files or directory contents as Markdown code blocks. "
                      "If you're inside a Git repository, file headings are printed relative to the repository root.",
                      {
                          schema::int_flag("head", "Keep only the first N lines of each embedded file"),
                          schema::bool_flag("num", "Number the lines"),
                      },
                      {
                          schema::argument("FILE", "File or directory to embed", false, true),
                      });

bool const registered = prompt::register_prompt(
    "files",
    "Appends the given fuzzily found files or directory contents as Markdown code blocks. "
    "If you're inside a Git repository, file headings are printed relative to the repository root. "
    "If no files are provided, `fzf -m` is used to choose them interactively.",
    &execute_files, &render_help_files, nullptr, chain_role::normal, true, nullptr, nullptr, false, true,
    &k_files_tool);

} // namespace prompt::prompts
