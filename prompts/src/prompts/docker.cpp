#include "../core/fs.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_docker(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    auto files = positional_args(ctx);

    std::string output;
    output +=
        "You are a Docker and container infrastructure expert.\nReview the provided Dockerfiles, docker-compose files, "
        "or container configurations.\n\nFocus on:\n- Security: non-root users, minimal base images, secrets exposure, "
        "COPY vs ADD\n- Layer optimization: ordering, caching, multi-stage builds, .dockerignore\n- Image size: "
        "unnecessary packages, build artifacts, dev dependencies in production\n- docker-compose: service "
        "dependencies, health checks, resource limits, networking\n- Runtime: signal handling, logging, volume mounts, "
        "environment variable management\n- Best practices: LABEL metadata, ENTRYPOINT vs CMD, ARG vs ENV\n\nProvide "
        "specific fixes as a git diff. Prioritize security and size improvements.\n\n";

    std::string error;
    for (auto const& file : files) {
        std::filesystem::path p(file);
        if (std::filesystem::is_regular_file(p)) {
            std::string file_name = p.filename().string();
            output += std::format("File: {}\n\n```{}\n", file_name, infer_lang(p));
            output += trim_context_nl(read_file(p), head_lines);
            output += "\n```\n\n";
        } else {
            error += std::format("Warning: File '{}' not found.\n", file);
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_docker(std::ostream& os) {
    os << R"EOF(Usage: prompt docker [--head N] [FILE...]
       echo "task description" | prompt docker [--head N] [FILE...]

Docker and container review prompt.
Analyzes Dockerfiles, docker-compose files, and container configurations for best practices.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("docker",
                            "Docker and container review prompt. Analyzes Dockerfiles, docker-compose files, and "
                            "container configurations for best practices.",
                            &execute_docker, &render_help_docker);

} // namespace prompt::prompts
