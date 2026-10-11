#include "prompt/prompts/docker_prompt.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_docker(prompt_context&& ctx) noexcept {
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

    std::string output;
    output += "You are a Docker and container infrastructure expert.\n";
    output += "Review the provided Dockerfiles, docker-compose files, or container configurations.\n";
    output += "\n";
    output += "Focus on:\n";
    output += "- Security: non-root users, minimal base images, secrets exposure, COPY vs ADD\n";
    output += "- Layer optimization: ordering, caching, multi-stage builds, .dockerignore\n";
    output += "- Image size: unnecessary packages, build artifacts, dev dependencies in production\n";
    output += "- docker-compose: service dependencies, health checks, resource limits, networking\n";
    output += "- Runtime: signal handling, logging, volume mounts, environment variable management\n";
    output += "- Best practices: LABEL metadata, ENTRYPOINT vs CMD, ARG vs ENV\n";
    output += "\n";
    output += "Provide specific fixes as a git diff. Prioritize security and size improvements.\n";
    output += "\n";

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

void render_help_docker(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt docker [--head N] [FILE...]
       echo "task description" | prompt docker [--head N] [FILE...]

Docker and container review prompt.
Analyzes Dockerfiles, docker-compose files, and container configurations for best practices.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
