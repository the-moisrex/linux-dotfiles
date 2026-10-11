#include "prompt/prompts/gdb_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_gdb(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::vector<std::string> files;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        files.emplace_back(ctx.args[i]);
    }

    // gdb.sh: a fixed instruction block (echoes + a quoted heredoc) followed by
    // the sample `gdb ... | grep` probe, whose stdout is streamed verbatim. The
    // three markdown hard-break list items end with two trailing spaces, so
    // they are emitted as ordinary literals (a raw string would drop them).
    std::string output;
    output += R"GDBEOF(Write a short GDB script to help debug the core function or algorithm in the provided code.
IMPORTANT: Do NOT provide a generic or generalized GDB template. You must specifically target the code provided below, using the actual function names, variable names, and logic present in these exact snippets.

To ensure the script actually works and doesn't fail due to minor line number mismatches or hanging, use the following techniques:
1. **Breakpoints**: Avoid guessing exact line numbers. Prefer function names (`tbreak actual_func_name`), relative line numbers (`tbreak +5`), or `rbreak`.
2. **Tracing Execution**: Instead of standard breakpoints that halt execution, highly prefer `dprintf` for tracing loops and states (e.g., `dprintf actual_func_name, "actual_var=%d\n", actual_var`). If using standard breakpoints, remember to add a `commands` block ending with `continue`.
3. **Loop Debugging**: Use conditional breakpoints (`break my_loop if i == 50`) or the `ignore <bnum> <count>` command to skip the noise of early loop iterations.
4. **Loop Debugging**: Use tbreaks and other logging breaks or conditional breaks to log and print anomolies or verifiy the loops outputs.
5. **Data Inspection**: Use GDB's array slice syntax to print dynamically allocated arrays (e.g., `p *actual_array@10`).

Be mindful of errors like 'No symbol ... in current context.' Try to break at locations where the variables are definitely in scope.
Avoid using the Python GDB API unless you are absolutely confident; stick to native GDB commands.

**Important GDB reliability requirement:**
Do not use `commands` immediately after `rbreak` unless you first verify that `rbreak` actually created at least one breakpoint. `rbreak` can print `No breakpoints made` for templated C++ functions if no matching instantiation exists in the current binary/debug symbols, and then a following `commands` block fails with `Argument required`.

For templated functions either:

)GDBEOF";
    output +=
        " 1. use `set breakpoint pending on` with a concrete mangled/demangled instantiated symbol if known, or  \n";
    output += " 2. use `break file:function` only if supported, or  \n";
    output += " 3. use `rbreak ...` only as a standalone optional command and do **not** attach a `commands` block to "
              "it, or  \n";
    output +=
        R"GDBEOF( 4. break on a nearby caller or on a non-template helper function that is actually emitted, then step into the templated function.

The script must be robust when the breakpoint pattern matches nothing. It should not contain a `commands` block that depends on a possibly nonexistent `rbreak` result.

Avoid `rbreak ...` followed by `commands` for templated functions. If `rbreak` finds no instantiated symbol, the script will fail with `Argument required`. Use a concrete existing symbol or make the `rbreak` optional and do not attach commands unless a breakpoint number is guaranteed.

We probably run it using `gdb -batch` as well.


Running the command: `gdb -q --nh -batch -ex "help" -ex "help data" -ex "help breakpoints" -ex "help tracepoint" | grep -Ev "^(Type|Command name|Making program|set )"``
)GDBEOF";

    std::vector<std::string> storage;
    storage.push_back("bash");
    storage.push_back("-c");
    storage.push_back("gdb -q --nh -batch -ex \"help\" -ex \"help data\" -ex \"help breakpoints\" -ex "
                      "\"help tracepoint\" | grep -Ev \"^(Type|Command name|Making program|set )\"");
    std::vector<char const*> argv;
    argv.reserve(storage.size() + 1);
    for (auto const& s : storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    output += prompt::process::run_command(argv).stdout_data;

    std::string error;
    for (auto const& file : files) {
        std::filesystem::path p(file);
        if (std::filesystem::is_regular_file(p)) {
            std::string file_name = p.filename().string();
            output += "File: " + file_name + "\n\n";
            output += "```" + infer_lang(p) + "\n";
            output += trim_context_nl(read_file(p), head_lines);
            output += "\n```\n\n";
        } else {
            error += "Warning: File '" + file + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_gdb(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt gdb [--head N] [FILE...]
       some-command | prompt gdb [--head N] [FILE...]

Generates a prompt asking the LLM to write a GDB script that helps debug the specified code (e.g., tracking variables in loops, pretty-printing).

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
