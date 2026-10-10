#include "prompt/prompts/auto_prompt.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include "prompt/legacy/legacy_runner.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/clipboard.hpp"
#include "prompt/core/python.hpp"
#include "prompt/tse/tse_python.hpp"
#include "prompt/core/process.hpp"
#include <regex>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

bool is_compiler_output(std::string_view content) noexcept {
    static const std::regex compiler_regex(
        R"(:([0-9]+:)?[0-9]+: (error|warning|fatal error|note):|In file included from|undefined reference to|no matching function|ld: symbol\(s\) not found|FAILED:|ninja: build stopped|make\[[0-9]+\]: \*\*\*)"
    );
    std::string content_str(content);
    return std::regex_search(content_str, compiler_regex);
}

bool is_stock_identifier(std::string_view content, std::string& out_identifier) noexcept {
    static const std::regex isin_re(R"(^IR[A-Za-z0-9]{10}$)");
    static const std::regex inscode_re(R"(^[0-9]{15,}$)");
    static const std::regex easytrader_re(R"(^https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12}(/|\?|$))");
    static const std::regex tsetmc_re(R"(^https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,}(/|\?|$))");
    static const std::regex codal_re(R"(^https?://(www\.)?codal\.ir/ReportList\.aspx\?([^&]*&)?Symbol=[^&[:space:]]+)");
    
    auto start = content.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return false;
    auto end = content.find_last_not_of(" \t\r\n");
    content = content.substr(start, end - start + 1);
    
    if (content.find('\n') != std::string_view::npos) return false;
    
    std::string content_str(content);
    if (std::regex_match(content_str, isin_re) ||
        std::regex_match(content_str, inscode_re) ||
        std::regex_match(content_str, easytrader_re) ||
        std::regex_match(content_str, tsetmc_re) ||
        std::regex_match(content_str, codal_re)) {
        out_identifier = content_str;
        return true;
    }
    return false;
}

bool extract_stock_identifier(std::string_view content, std::string& out_identifier) noexcept {
    static const std::regex combined_re(
        R"(https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12}|https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,}|https?://(www\.)?codal\.ir/ReportList\.aspx\?([^&]*&)?Symbol=[^&[:space:]]+|IR[A-Za-z0-9]{10}|[0-9]{15,})"
    );
    std::smatch match;
    std::string content_str(content);
    if (std::regex_search(content_str, match, combined_re)) {
        out_identifier = match[0];
        return true;
    }
    return false;
}

bool should_translate_to_english(std::string_view content) noexcept {
    for (char c : content) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 0x80) return true;
    }
    return false;
}

} // namespace

prompt_result execute_auto(prompt_context&& ctx) noexcept {
    std::string input_buffer = ctx.stdin_content;
    bool has_stdin = ctx.stdin_consumed;
    
    if (!has_stdin && input_buffer.empty()) {
        input_buffer = ctx.clipboard_content();
    }
    
    std::string target_script = "summarize";
    std::string stock_identifier;
    std::vector<std::string> extra_args;
    
    if (!input_buffer.empty()) {
        if (is_compiler_output(input_buffer)) {
            target_script = "cpp";
        } else if (input_buffer.find("youtube.com") != std::string::npos || 
                   input_buffer.find("youtu.be") != std::string::npos) {
            target_script = "yt";
        } else if (std::regex_search(input_buffer, std::regex(R"(^FROM |^RUN |^COPY |^CMD |^ENTRYPOINT |^docker-compose|^services:)"))) {
            target_script = "docker";
        } else if (std::regex_search(input_buffer, std::regex(R"(^\.github/workflows/|\.gitlab-ci\.yml|Jenkinsfile|stages:|jobs:)"))) {
            target_script = "ci";
        } else if (std::regex_search(input_buffer, std::regex(R"(Traceback \(most recent call last\)|File ".+", line|Fatal error|panic:|Segmentation fault|stack trace|Exception in)"))) {
            target_script = "debug";
        } else if (is_stock_identifier(input_buffer, stock_identifier)) {
            target_script = "stock";
            extra_args.push_back(stock_identifier);
        } else if (extract_stock_identifier(input_buffer, stock_identifier)) {
            target_script = "stock";
            extra_args.push_back(stock_identifier);
        } else if (input_buffer.find("class ") != std::string::npos || 
                   input_buffer.find("struct ") != std::string::npos || 
                   input_buffer.find("#include <") != std::string::npos) {
            target_script = "cpp-reviewer";
        } else if (should_translate_to_english(input_buffer)) {
            target_script = "english";
        } else {
            target_script = "summarize";
        }
    }
    
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        std::string_view arg = ctx.args[i];
        std::filesystem::path p(arg);
        if (std::filesystem::exists(p)) {
            auto ext = p.extension().string();
            auto fname = p.filename().string();
            if (ext == ".cpp" || ext == ".hpp" || ext == ".cxx" || ext == ".hxx" || 
                ext == ".cc" || ext == ".c" || ext == ".h") {
                target_script = "cpp-reviewer";
            } else if (ext == ".sh" || ext == ".bash") {
                target_script = "review";
            } else if (ext == ".py" || ext == ".rb" || ext == ".go" || ext == ".rs" || 
                       ext == ".java" || ext == ".kt" || ext == ".swift" || ext == ".ts" || ext == ".js") {
                target_script = "review";
            } else if (ext == ".yml" || ext == ".yaml") {
                if (fname.find("docker-compose") != std::string::npos || fname.find("compose") != std::string::npos) {
                    target_script = "docker";
                } else if (fname.find(".gitlab-ci") != std::string::npos) {
                    target_script = "ci";
                } else {
                    target_script = "review";
                }
            } else if (ext == ".json") {
                target_script = "review";
            } else if (ext == ".md") {
                target_script = "readme";
            } else if (p.filename() == "Dockerfile" || fname.find("Dockerfile") != std::string::npos) {
                target_script = "docker";
            } else if (fname == "Jenkinsfile" || fname == ".gitlab-ci.yml" || fname == "Makefile" || fname == "CMakeLists.txt") {
                target_script = "review";
            }
        }
    }
    
    if (!ctx.stdin_consumed && target_script != "stock") {
        if (auto id = ctx.clipboard_identifier()) {
            target_script = "stock";
            extra_args.push_back(*id);
        }
    }
    
    // Dispatch in-process when the target prompt is implemented in this
    // binary; avoid re-spawning `prompt` which would recurse.
    if (auto* target = prompt::find_prompt(target_script)) {
        auto sub_ctx = std::move(ctx);
        auto orig_args = sub_ctx.args;
        auto orig_count = sub_ctx.args_count;
        std::size_t n = 0;
        for (auto const& a : extra_args) {
            if (n >= sub_ctx.args.size()) break;
            sub_ctx.args[n++] = a;
        }
        for (std::size_t i = 0; i < orig_count && n < sub_ctx.args.size(); ++i) {
            sub_ctx.args[n++] = orig_args[i];
        }
        sub_ctx.args_count = n;
        sub_ctx.stdin_content = input_buffer;
        sub_ctx.stdin_consumed = !input_buffer.empty();
        return target->execute_fn(std::move(sub_ctx));
    }
    
    // Fallback: not yet ported — run the legacy bash prompt script
    // directly (never re-invoke the dispatcher: it may resolve back to
    // this binary and recurse).
    std::vector<std::string> arg_storage;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        arg_storage.emplace_back(ctx.args[i]);
    }
    for (auto const& a : extra_args) {
        arg_storage.push_back(a);
    }
    std::vector<char const*> arg_argv;
    arg_argv.reserve(arg_storage.size() + 1);
    for (auto const& s : arg_storage) arg_argv.push_back(s.c_str());
    arg_argv.push_back(nullptr);
    
    auto result = prompt::legacy::run_prompt_script(target_script, input_buffer, arg_argv);
    
    return {result.stdout_data, result.exit_code, false};
}

void render_help_auto(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt auto [FILE...]
       some-command | prompt auto [FILE...]

Automatically chooses and executes the most appropriate prompt script based on the input.
For example, if it detects YouTube URLs, it delegates to the 'yt' prompt.
If it detects C++ files, it delegates to 'cpp-reviewer'.
A bare stock identifier delegates to 'stock'.
Defaults to 'summarize' for English and Farsi text, or 'english' (translate) for other languages.

Options:
  --help, -h   Show this help message
  (Any other options like --head are passed through to the selected script)
)EOF";
}

} // namespace prompt::prompts