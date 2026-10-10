#include "prompt/core/clipboard.hpp"
#include "prompt/core/python.hpp"
#include "prompt/core/sanitize.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/prompt_context.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include "prompt/sdk/chaining.hpp"
#include "prompt/sdk/pipeline.hpp"
#include "prompt/prompts/prompt_entry.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

int main(int argc, char** argv) {
    // Initialize prompt registry (needed even for --help)
    prompt::init_prompt_registry();

    // Handle --help and --version early (only as first argument)
    if (argc > 1) {
        std::string_view arg(argv[1]);
        if (arg == "--help" || arg == "-h") {
            // Show general help
            std::cout << "Usage: prompt [--help] [--version] <prompt> [args...]\n";
            std::cout << "       prompt <.prompt> [args...] [.prompt] [args...]\n\n";
            std::cout << "Available prompts:\n";
            for (auto const& name : prompt::list_prompt_names()) {
                std::cout << "  " << name << "\n";
            }
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "prompt " << PROMPT_VERSION << "\n";
            return 0;
        }
    }
    
    // Initialize clipboard
    prompt::clipboard::init();
    
    // Initialize prompt registry
    prompt::init_prompt_registry();
    
    // Prepare base context
    auto base_ctx = prompt::make_base_context();
    
    // Wire up services
    base_ctx.clipboard_content_fn = &prompt::clipboard::paste;
    base_ctx.clipboard_identifier_fn = []() -> std::optional<std::string> {
        auto content = prompt::clipboard::paste();
        if (content.empty()) return std::nullopt;
        
        std::string text = content;
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
        
        static const std::regex isin_re(R"(IR[A-Za-z0-9]{10})");
        static const std::regex inscode_re(R"([0-9]{15,})");
        static const std::regex easytrader_re(R"(https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12})");
        static const std::regex tsetmc_re(R"(https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,})");
        static const std::regex codal_re(R"(https?://(www\.)?codal\.ir/ReportList\.aspx\?([^&]*&)?Symbol=[^&[:space:]]+)");
        
        std::smatch match;
        if (std::regex_search(text, match, easytrader_re) ||
            std::regex_search(text, match, tsetmc_re) ||
            std::regex_search(text, match, codal_re) ||
            std::regex_search(text, match, isin_re) ||
            std::regex_search(text, match, inscode_re)) {
            return match[0];
        }
        
        if (!text.empty() && text.size() <= 64) {
            bool has_latin = false;
            for (char c : text) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                    has_latin = true;
                    break;
                }
            }
            if (!has_latin) return text;
        }
        
        return std::nullopt;
    };
    
    base_ctx.select_files_fn = []() -> std::vector<std::filesystem::path> {
        std::vector<char const*> fzf_argv = {"fzf", "-m", nullptr};
        auto result = prompt::process::run_command(fzf_argv);
        std::vector<std::filesystem::path> files;
        std::string line;
        for (char c : result.stdout_data) {
            if (c == '\n') {
                if (!line.empty()) {
                    files.emplace_back(line);
                    line.clear();
                }
            } else {
                line += c;
            }
        }
        if (!line.empty()) files.emplace_back(line);
        return files;
    };
    
    base_ctx.resolve_input_file_fn = [](std::string_view name) -> std::optional<std::filesystem::path> {
        std::filesystem::path p(name);
        if (std::filesystem::exists(p)) return p;
        
        if (auto git_root = prompt::fs::find_git_root()) {
            auto full = *git_root / name;
            if (std::filesystem::exists(full)) return full;
        }
        
        std::string name_str(name);
        std::vector<char const*> fzf_argv = {"fzf", "-f", name_str.c_str(), nullptr};
        auto result = prompt::process::run_command(fzf_argv);
        if (!result.stdout_data.empty()) {
            std::string line = result.stdout_data.substr(0, result.stdout_data.find('\n'));
            if (!line.empty()) return std::filesystem::path(line);
        }
        
        return std::nullopt;
    };
    
    // Read stdin if available
    if (!isatty(STDIN_FILENO)) {
        std::string stdin_content;
        std::string line;
        while (std::getline(std::cin, line)) {
            stdin_content += line;
            stdin_content += '\n';
        }
        if (!stdin_content.empty()) {
            base_ctx.stdin_content = std::move(stdin_content);
            base_ctx.stdin_consumed = true;
        }
    }
    
    // Initialize prompt registry
    prompt::init_prompt_registry();
    
    // Parse chained prompts
    std::span<char const* const> argv_span(argv, static_cast<std::size_t>(argc));
    auto chain = prompt::parse_chain(argv_span, prompt::get_all_prompts());
    
    // If no prompts found, default to "auto"
    if (chain.count == 0) {
        chain.invocations[0].name = "auto";
        chain.count = 1;
    }
    
    // Pipeline for output accumulation
    prompt::pipeline pipeline;
    
    // Execute each prompt in chain
    for (std::size_t i = 0; i < chain.count; ++i) {
        auto const& inv = chain.invocations[i];
        auto* desc = prompt::find_prompt(inv.name);
        if (!desc) {
            std::cerr << "prompt: prompt '" << inv.name << "' not found\n";
            return 3;
        }
        
        // Per-prompt help: prompt <name> --help / -h renders that prompt's help
        bool wants_help = false;
        for (std::size_t j = 0; j < inv.args_count; ++j) {
            if (inv.args[j] == "--help" || inv.args[j] == "-h") {
                wants_help = true;
                break;
            }
        }
        if (wants_help && desc->render_help_fn) {
            desc->render_help_fn(std::cout);
            return 0;
        }
        
        // Build context for this prompt
        auto ctx = base_ctx;
        // Copy invocation args into context (handle size mismatch)
        for (std::size_t j = 0; j < inv.args_count && j < ctx.args.size(); ++j) {
            ctx.args[j] = inv.args[j];
        }
        ctx.args_count = inv.args_count;
        
        // Execute prompt
        auto result = desc->execute_fn(std::move(ctx));
        
        // Sanitize output
        std::string sanitized = prompt::sanitize::sanitize(result.output);
        
        // Accumulate
        pipeline.add_output(sanitized, result.no_clipboard);
        
        // Pass output as stdin to next prompt
        if (i + 1 < chain.count) {
            base_ctx.stdin_content = sanitized;
            base_ctx.stdin_consumed = true;
        }
        
        if (result.exit_code != 0) {
            std::cerr << sanitized;
            return result.exit_code;
        }
    }
    
    // Finalize and output
    std::string final_output = pipeline.finalize();
    std::cout << final_output;
    
    // Copy to clipboard (respecting per-prompt no-clipboard opt-outs)
    auto clip = pipeline.for_clipboard();
    if (!clip.empty()) {
        prompt::clipboard::copy(clip);
    }
    
    return 0;
}