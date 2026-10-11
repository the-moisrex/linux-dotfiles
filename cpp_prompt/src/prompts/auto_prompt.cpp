#include "prompt/prompts/auto_prompt.hpp"
#include "prompt/core/clipboard.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/core/python.hpp"
#include "prompt/sdk/embed.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include "prompt/tse/tse_python.hpp"
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

namespace prompt::prompts {

namespace {

bool is_compiler_output(std::string_view content) noexcept {
    // auto.sh tests with `grep -qiE`, so the match is case-insensitive and
    // can start on any line.
    static const std::regex compiler_regex(
        R"(:[0-9]+:([0-9]+:)? (error|warning|fatal error|note):|In file included from|undefined reference to|no matching function for call to|ld: symbol\(s\) not found|FAILED:|ninja: build stopped|make\[[0-9]+\]: \*\*\*)",
        std::regex::icase);
    std::string content_str(content);
    return std::regex_search(content_str, compiler_regex);
}

bool is_stock_identifier(std::string_view content, std::string& out_identifier) noexcept {
    static const std::regex isin_re(R"(^IR[A-Za-z0-9]{10}$)");
    static const std::regex inscode_re(R"(^[0-9]{15,}$)");
    static const std::regex easytrader_re(
        R"(^https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12}(/|\?|$))");
    static const std::regex tsetmc_re(R"(^https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,}(/|\?|$))");
    static const std::regex codal_re(R"(^https?://(www\.)?codal\.ir/ReportList\.aspx\?([^&]*&)?Symbol=[^&[:space:]]+)");

    auto start = content.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return false;
    auto end = content.find_last_not_of(" \t\r\n");
    content = content.substr(start, end - start + 1);

    if (content.find('\n') != std::string_view::npos) return false;

    std::string content_str(content);
    if (std::regex_match(content_str, isin_re) || std::regex_match(content_str, inscode_re) ||
        std::regex_match(content_str, easytrader_re) || std::regex_match(content_str, tsetmc_re) ||
        std::regex_match(content_str, codal_re)) {
        out_identifier = content_str;
        return true;
    }
    return false;
}

bool extract_stock_identifier(std::string_view content, std::string& out_identifier) noexcept {
    static const std::regex combined_re(
        R"(https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12}|https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,}|https?://(www\.)?codal\.ir/ReportList\.aspx\?([^&]*&)?Symbol=[^&[:space:]]+|IR[A-Za-z0-9]{10}|[0-9]{15,})");
    std::smatch match;
    std::string content_str(content);
    if (std::regex_search(content_str, match, combined_re)) {
        out_identifier = match[0];
        return true;
    }
    return false;
}

// Approximation of the perl `\p{L}` test auto.sh runs: only letter
// characters count, English letters and Arabic-script letters (outside the
// Arabic-only forms ة ي ك) stay untranslated, everything else translates.
bool is_letter_codepoint(std::uint32_t cp) noexcept {
    if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    auto in = [cp](std::uint32_t lo, std::uint32_t hi) { return cp >= lo && cp <= hi; };
    return in(0x00C0, 0x00D6) || in(0x00D8, 0x00F6) || in(0x00F8, 0x02AF) || // Latin
           in(0x0370, 0x03FF) || in(0x1F00, 0x1FFF) ||                       // Greek
           in(0x0400, 0x052F) ||                                             // Cyrillic
           in(0x0530, 0x058F) ||                                             // Armenian
           in(0x0590, 0x05FF) ||                                             // Hebrew
           in(0x0600, 0x06FF) || in(0x0750, 0x077F) || in(0x08A0, 0x08FF) || // Arabic
           in(0xFB50, 0xFDFF) || in(0xFE70, 0xFEFF) ||                       // Arabic forms
           in(0x0900, 0x097F) || in(0x0980, 0x09FF) || in(0x0A00, 0x0A7F) || // Indic
           in(0x0A80, 0x0AFF) || in(0x0B00, 0x0B7F) || in(0x0B80, 0x0BFF) || in(0x0C00, 0x0C7F) || in(0x0C80, 0x0CFF) ||
           in(0x0D00, 0x0D7F) || in(0x0D80, 0x0DFF) || in(0x0E00, 0x0E7F) ||                       // Thai
           in(0x10A0, 0x10FF) || in(0x2D00, 0x2D2F) ||                                             // Georgian
           in(0x1100, 0x11FF) || in(0x3130, 0x318F) || in(0xAC00, 0xD7AF) ||                       // Hangul
           in(0x2E80, 0x2EFF) || in(0x3005, 0x3007) || in(0x3400, 0x4DBF) ||                       // CJK
           in(0x4E00, 0x9FFF) || in(0xF900, 0xFAFF) || in(0x3040, 0x309F) || in(0x30A0, 0x30FF) || // Kana
           in(0xFF21, 0xFF3A) || in(0xFF41, 0xFF5A);                                               // Fullwidth
}

bool should_translate_to_english(std::string_view content) noexcept {
    std::size_t i = 0;
    while (i < content.size()) {
        unsigned char lead = static_cast<unsigned char>(content[i]);
        std::uint32_t cp = 0;
        std::size_t len = 1;
        if (lead < 0x80) {
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1F;
            len = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0F;
            len = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07;
            len = 4;
        } else {
            ++i;
            continue;
        }
        if (i + len > content.size()) break;
        bool valid = true;
        for (std::size_t k = 1; k < len; ++k) {
            unsigned char cont = static_cast<unsigned char>(content[i + k]);
            if ((cont & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cont & 0x3F);
        }
        i += len;
        if (!valid) continue;

        if (!is_letter_codepoint(cp)) continue;
        if (cp >= 'a' && cp <= 'z') continue;
        if (cp >= 'A' && cp <= 'Z') continue;
        // Arabic-script letters are Farsi except the Arabic-only forms.
        bool arabic = (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
                      (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
                      (cp >= 0xFE70 && cp <= 0xFEFF);
        if (arabic) {
            if (cp == 0x0629 || cp == 0x064A || cp == 0x0643) return true; // ة ي ك
            continue;
        }
        return true;
    }
    return false;
}

} // namespace

prompt_result execute_auto(prompt_context&& ctx) noexcept {
    std::string input_buffer;
    bool has_stdin = ctx.stdin_consumed;

    if (has_stdin) {
        // `$(cat)` strips every trailing newline, then auto.sh removes a
        // trailing carriage return.
        input_buffer = ctx.stdin_content;
        while (!input_buffer.empty() && input_buffer.back() == '\n') input_buffer.pop_back();
        if (!input_buffer.empty() && input_buffer.back() == '\r') input_buffer.pop_back();
    }

    if (input_buffer.empty()) {
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
        } else if (std::regex_search(input_buffer,
                                     std::regex(R"((^|\n)(FROM |RUN |COPY |CMD |ENTRYPOINT |docker-compose|services:))",
                                                std::regex::icase))) {
            target_script = "docker";
        } else if (std::regex_search(
                       input_buffer,
                       std::regex(R"((^|\n)\.github/workflows/|\.gitlab-ci\.yml|Jenkinsfile|stages:|jobs:)",
                                  std::regex::icase))) {
            target_script = "ci";
        } else if (
            std::regex_search(
                input_buffer,
                std::regex(
                    R"(Traceback \(most recent call last\)|File ".*", line|Fatal error|panic:|Segmentation fault|stack trace|Exception in)",
                    std::regex::icase))) {
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
        std::string arg_stock;
        if (is_stock_identifier(arg, arg_stock)) {
            // The identifier is passed along as this prompt's argument.
            target_script = "stock";
            extra_args.clear();
            break;
        }
        if (arg.find("youtube.com") != std::string_view::npos || arg.find("youtu.be") != std::string_view::npos) {
            target_script = "yt";
            break;
        }
        std::filesystem::path p(arg);
        if (std::filesystem::exists(p)) {
            auto ext = p.extension().string();
            auto fname = p.filename().string();
            if (ext == ".cpp" || ext == ".hpp" || ext == ".cxx" || ext == ".hxx" || ext == ".cc" || ext == ".c" ||
                ext == ".h") {
                target_script = "cpp-reviewer";
            } else if (ext == ".sh" || ext == ".bash") {
                target_script = "review";
            } else if (ext == ".py" || ext == ".rb" || ext == ".go" || ext == ".rs" || ext == ".java" || ext == ".kt" ||
                       ext == ".swift" || ext == ".ts" || ext == ".js") {
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
            } else if (fname == "Jenkinsfile" || fname == ".gitlab-ci.yml" || fname == "Makefile" ||
                       fname == "CMakeLists.txt") {
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

    // bash auto.sh echoes the buffered input (stdin or clipboard) before
    // running the chosen target, so the output shows what was detected.
    std::string echo = input_buffer + "\n";

    // The buffer-derived stock identifier is only handed over when the
    // target is still stock and the buffer came from a pipe (bash pipes
    // the buffer only when stdin was piped; interactively each prompt
    // reads the clipboard itself).
    bool pass_extra = has_stdin && target_script == "stock" && !extra_args.empty();

    // Dispatch in-process when the target prompt is implemented in this
    // binary; avoid re-spawning `prompt` which would recurse.
    if (auto* target = prompt::find_prompt(target_script)) {
        auto sub_ctx = std::move(ctx);
        sub_ctx.name = target_script;
        auto orig_args = sub_ctx.args;
        auto orig_count = sub_ctx.args_count;
        std::size_t n = 0;
        if (pass_extra) {
            for (auto const& a : extra_args) {
                if (n >= sub_ctx.args.size()) break;
                sub_ctx.args[n++] = a;
            }
        }
        for (std::size_t i = 0; i < orig_count && n < sub_ctx.args.size(); ++i) {
            sub_ctx.args[n++] = orig_args[i];
        }
        sub_ctx.args_count = n;
        // `printf '%s\n' "$input_buffer"` re-adds the trailing newline that
        // $(cat) stripped.
        sub_ctx.stdin_content = has_stdin ? input_buffer + "\n" : std::string{};
        sub_ctx.stdin_consumed = has_stdin;
        auto result = target->execute_fn(std::move(sub_ctx));
        result.output.insert(0, echo);
        // auto.sh reads the pipe itself (`input_buffer="$(cat)"`), so the
        // chain's stdin is spent regardless of what the target did.
        result.stdin_consumed = has_stdin;
        return result;
    }

    // Every dispatch target of this binary is implemented natively; the
    // dispatch above always resolves. (A missing target is a bug — report
    // it like the bash dispatcher reports an unknown prompt.)
    return {"", 3, false, "prompt auto: prompt '" + std::string(target_script) + "' not found\n", has_stdin};
}

void render_help_auto(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt auto [FILE...]
       some-command | prompt auto [FILE...]

Automatically chooses and executes the most appropriate prompt script based on the input.
For example, if it detects YouTube URLs, it delegates to the 'yt' prompt.
If it detects C++ files, it delegates to 'cpp-reviewer'.
A bare stock identifier (ISIN, insCode, or easytrader/tsetmc/codal URL) delegates to 'stock'.
Defaults to 'summarize' for English and Farsi text, or 'english' (translate) for other languages like Arabic.

Options:
  --help, -h   Show this help message
  (Any other options like --head are passed through to the selected script)
)EOF";
}

} // namespace prompt::prompts