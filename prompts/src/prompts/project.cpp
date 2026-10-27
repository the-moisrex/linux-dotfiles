#include "../sdk/project.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/stage.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace prompt::prompts {

namespace {

void publish_project(project::info const& p, result& r) {
    if (!p.languages.empty()) {
        r.published.set(keys::project_language, project::to_string(p.languages.front()));
    }
    r.published.set(keys::project_build, project::to_string(p.build));
    if (p.build_dir) r.published.set(keys::project_build_dir, p.build_dir->string());
    r.published.set(keys::project_package, project::to_string(p.package));
    r.published.set(keys::project_tests, project::to_string(p.tests));
}

} // namespace

static void execute_project(view const& v, result& r) {
    std::filesystem::path root = v.env.git_root;
    if (root.empty()) root = std::filesystem::current_path();

    for (auto const& arg : v.args) {
        if (arg == "-h" || arg == "--help") {
            r.out = "Usage: prompt project [ROOT]";
            return;
        }
        if (!arg.starts_with('-') && !arg.empty()) {
            root = std::filesystem::path(std::string(arg));
            break;
        }
    }

    project::info const info = project::detect(root, v.env);
    publish_project(info, r);

    r.out = project::summary_line(info);
    r.out.push_back('\n');

    if (!info.languages.empty()) {
        r.out += "languages: ";
        for (std::size_t i = 0; i < info.languages.size(); ++i) {
            if (i != 0) r.out += ", ";
            r.out += project::to_string(info.languages[i]);
        }
        r.out.push_back('\n');
    }
    if (info.build_dir) r.out += "build dir: " + info.build_dir->string() + "\n";
    if (info.tools.universal_ctags) r.out += "ctags: yes\n";
    if (info.tools.compile_commands) r.out += "compile commands: yes\n";
}

static void render_help_project(std::ostream& os) {
    os << R"EOF(Usage: prompt project [ROOT]

Detects the project shape at ROOT (the git root by default) and publishes it as
the `project.*` facts for later segments. Prints a compact summary.

Detected: languages (by tracked-file count), build system, package manager, test
framework, build directory and the presence of ctags/clangd tooling.

Options:
  -h, --help  Show this help
)EOF";
}

bool const registered_project =
    prompt::register_prompt("project", "Detects the project shape and publishes the `project.*` facts.",
                            &execute_project, &render_help_project);

} // namespace prompt::prompts
