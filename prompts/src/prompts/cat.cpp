#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <fstream>
#include <iterator>
#include <string>

namespace prompt::prompts {

static void execute_cat(view const& v, result& r) {
    auto const opts = file_options_or_throw(v);
    auto const sources = collect_file_args(v);

    if (sources.files.size() + sources.missing.size() != 1) {
        throw usage_error{"prompt cat: expected exactly one FILE\n"};
    }
    if (!sources.missing.empty()) [[unlikely]] {
        throw soft_error{"prompt cat: file not found or not a regular file: " + std::string(sources.missing.front()) +
                         "\n"};
    }

    auto const& file = sources.files.front();
    std::ifstream input(file, std::ios::binary);
    if (!input) [[unlikely]] {
        throw soft_error{"prompt cat: cannot read file: " + file.string() + "\n"};
    }

    std::string const content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) [[unlikely]] {
        throw soft_error{"prompt cat: failed to read file: " + file.string() + "\n"};
    }

    r.out = number_lines(trim_context(content, opts.head_lines), opts.numbered);
}

static void render_help_cat(std::ostream& os) {
    os << file_usage_help("cat", " FILE") << R"EOF(
Append one file's contents as plain prompt text.

The file has no heading or code fence. Available piped stdin is placed before
the file and consumed. The file is unchanged unless --head or --num is used.

Options:
)EOF" << file_options_help("the file", true);
}

bool const registered =
    prompt::register_prompt("cat", "Append one file's contents as plain prompt text.", &execute_cat, &render_help_cat);

} // namespace prompt::prompts
