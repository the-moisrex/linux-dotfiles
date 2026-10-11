#include "prompt/prompts/benchmark_prompt.hpp"
#include "prompt/prompts/de_export_prompt.hpp"
#include "prompt/prompts/debug_prompt.hpp"
#include "prompt/prompts/diagram_prompt.hpp"
#include "prompt/prompts/metadata_prompt.hpp"
#include "prompt/prompts/migrate_prompt.hpp"
#include "prompt/prompts/optimize_prompt_prompt.hpp"
#include "prompt/prompts/overengineering_prompt.hpp"
#include "prompt/prompts/plan_prompt.hpp"
#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/prompts/readme_prompt.hpp"
#include "prompt/prompts/stupid_prompt.hpp"
#include "prompt/prompts/table_lookup_prompt.hpp"

// Natively ported prompt batch 2 (see register_batches.cpp).
namespace prompt {

void register_batch_text2() noexcept {
    register_prompt({"table-lookup", "Replace conditional logic with precomputed table lookups.",
                     R"EOF(Usage: prompt table-lookup [--head N] [FILE...]
       some-command | prompt table-lookup [--head N] [FILE...]

Replace conditional logic with precomputed table lookups.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_table_lookup, prompts::render_help_table_lookup});

    register_prompt({"overengineering", "Critique code for over-engineering patterns.",
                     R"EOF(Usage: prompt overengineering [PATTERN...] [--head N] [FILE...]
       prompt overengineering list

Critique code for over-engineering patterns.

Patterns:
  general                 General over-engineering critique (default)
  premature-abstraction   Premature interfaces, base classes, templates
  yagni                   You Aren't Gonna Need It - unused flexibility, speculative features
  pattern-abuse           Design pattern overuse (Factory, Strategy, Visitor where simple func works)
  excessive-di            Over-engineered dependency injection, service locators
  cpp-templates           C++: excessive template metaprogramming, CRTP abuse, policy classes
  cpp-inheritance         C++: deep hierarchies, virtual where static works, overuse of polymorphism
  cpp-overloads           C++: excessive overloads, SFINAE maze, concept overuse
  cpp-header-only         C++: header-only bloat, forcing inline, compilation firewalls ignored
  enterprise-java         Java-style: AbstractFactoryBean, Manager, Provider, Builder for simple tasks
  microservice-premature  Distributed system complexity where monolith/module suffices
  config-over-code        Excessive config files, DSLs, rule engines for simple logic

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_overengineering, prompts::render_help_overengineering});

    register_prompt({"migrate",
                     "Code migration assistance prompt. Helps migrate code between frameworks, "
                     "APIs, language versions, or patterns.",
                     R"EOF(Usage: prompt migrate [--head N] [FILE...]
       echo "migration task" | prompt migrate [--head N] [FILE...]

Code migration assistance prompt.
Helps migrate code between frameworks, APIs, language versions, or patterns.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_migrate, prompts::render_help_migrate});

    register_prompt({"optimize-prompt", "Ask the AI to analyze and improve an input prompt.",
                     R"EOF(Usage: prompt optimize-prompt [--head N] [FILE...]
       echo "my rough prompt" | prompt optimize-prompt [--head N] [FILE...]

Ask the AI to analyze and improve an input prompt.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_optimize_prompt, prompts::render_help_optimize_prompt});

    register_prompt({"plan",
                     "Generates a structured plan mode prompt for an AI assistant. The AI will be "
                     "instructed to ask clarifying questions, analyze provided code, and produce a "
                     "detailed implementation plan before writing any code. Designed for chat mode "
                     "where the AI cannot access files directly.",
                     R"EOF(Usage: prompt plan [--head N] [FILE...]
       echo "task description" | prompt plan [--head N] [FILE...]

Generates a structured plan mode prompt for an AI assistant.
The AI will be instructed to ask clarifying questions, analyze provided code,
and produce a detailed implementation plan before writing any code.
Designed for chat mode where the AI cannot access files directly.

Use this when you want the AI to plan before implementing.

Options:
  --head N   Keep only the first N lines of embedded context files
)EOF",
                     prompts::execute_plan, prompts::render_help_plan});

    register_prompt({"readme",
                     "Generates or improves a README.md for the provided code or project description. If code "
                     "files are provided, analyzes them to generate accurate documentation.",
                     R"EOF(Usage: prompt readme [--head N] [FILE...]
       echo "project description" | prompt readme [--head N] [FILE...]

Generates or improves a README.md for the provided code or project description.
If code files are provided, analyzes them to generate accurate documentation.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_readme, prompts::render_help_readme});

    register_prompt({"debug",
                     "General-purpose multi-language debugging prompt. Analyzes error messages, "
                     "stack traces, or suspicious code and identifies the root cause.",
                     R"EOF(Usage: prompt debug [--head N] [FILE...]
       some-command | prompt debug [--head N] [FILE...]

General-purpose multi-language debugging prompt.
Analyzes error messages, stack traces, or suspicious code and identifies the root cause.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_debug, prompts::render_help_debug});

    register_prompt({"diagram",
                     "Generates code diagrams (Mermaid, PlantUML, ASCII) from source code or "
                     "descriptions.",
                     R"EOF(Usage: prompt diagram [--head N] [--type TYPE] [FILE...]
       echo "task description" | prompt diagram [--head N] [--type TYPE] [FILE...]

Generates code diagrams (Mermaid, PlantUML, ASCII) from source code or descriptions.

Options:
  --head N       Keep only the first N lines of the embedded context
  --type, -t     Diagram type: mermaid (default), plantuml, ascii, flowchart, sequence, class
)EOF",
                     prompts::execute_diagram, prompts::render_help_diagram});

    register_prompt({"benchmark",
                     "Performance benchmarking prompt. Analyzes code and suggests concrete "
                     "benchmarks to measure performance.",
                     R"EOF(Usage: prompt benchmark [--head N] [FILE...]
       some-command | prompt benchmark [--head N] [FILE...]

Performance benchmarking prompt.
Analyzes code and suggests concrete benchmarks to measure performance.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_benchmark, prompts::render_help_benchmark});

    register_prompt({"de-export",
                     "Reduce unnecessary exports from C++20 modules and move implementations out "
                     "of headers.",
                     R"EOF(Usage: prompt de-export [--head N] [FILE...]
       some-command | prompt de-export [--head N] [FILE...]

Reduce unnecessary exports from C++20 modules and move implementations out of headers.

Only applies de-export advice if the project uses C++20 modules and contains `export`s.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_de_export, prompts::render_help_de_export});

    register_prompt({"metadata",
                     "Analyze the provided inputs and generate structured metadata representing "
                     "them.",
                     R"EOF(Usage: prompt metadata [--head N] [--format FORMAT] [FILE...]
       some-command | prompt metadata [--head N] [--format FORMAT] [FILE...]

Analyze the provided inputs and generate structured metadata representing them.

Options:
  --head N         Keep only the first N lines of the embedded context
  --format, -f     Output format for the metadata (e.g., json, xml, yaml, toml, conf). Default: json
)EOF",
                     prompts::execute_metadata, prompts::render_help_metadata});

    register_prompt({"stupid", "Find the stupid mistakes in this code.",
                     R"EOF(Usage: prompt stupid [--head N] [file...]
       some-command | prompt stupid [--head N] [file...]

Find the stupid mistakes in this code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF",
                     prompts::execute_stupid, prompts::render_help_stupid});
}

} // namespace prompt
