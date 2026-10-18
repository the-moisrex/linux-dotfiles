#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

static void render_help_overengineering(std::ostream& os);

namespace {

struct overengineering_pattern {
    std::string_view name;
    std::string_view text;
};

// overengineering.sh's PATTERNS map, in the script's declaration order.
constexpr std::array<overengineering_pattern, 12> kPatterns = {
    {
        {
            .name = "general",
            .text = "General over-engineering critique. Identify unnecessary complexity, premature abstractions, "
                    "speculative flexibility, and violations of YAGNI. Prefer straightforward solutions, concrete "
                    "over abstract, inline simple functions, remove unused capabilities. Provide specific "
                    "simplifications as git diffs where possible.",
        },
        {
            .name = "premature-abstraction",
            .text = "Premature abstraction critique. Flag interfaces with single implementations, base classes with "
                    "no derived classes yet, template parameters used in only one way, strategy patterns where a "
                    "function pointer or lambda would do, visitor patterns for simple type switches. Suggest "
                    "inlining, removing indirection, using concrete types.",
        },
        {
            .name = "yagni",
            .text = "YAGNI (You Aren't Gonna Need It) critique. Identify unused parameters, speculative hooks, "
                    "plugin architectures not yet needed, configuration options never used, feature flags for "
                    "undeployed features, generic repositories for single entity types, event systems with no "
                    "subscribers. Suggest removing dead code paths and simplifying to current requirements.",
        },
        {
            .name = "pattern-abuse",
            .text = "Design pattern overuse critique. Flag Factory/AbstractFactory for simple object creation, "
                    "Strategy for single-algorithm cases, Visitor for simple type dispatch, Observer for direct "
                    "calls, Command for simple callbacks, Builder for <4 parameters, Singleton where dependency "
                    "injection works, Decorator for single decoration. Suggest replacing with direct calls, "
                    "functions, or simpler constructs.",
        },
        {
            .name = "excessive-di",
            .text = "Excessive dependency injection critique. Flag service locators, containers for simple apps, "
                    "constructor injection with 5+ params, interfaces created solely for mocking, "
                    "provider/manager/factory indirection chains, scoped lifetimes where transient works. Suggest "
                    "manual wiring, simple structs, or removing unnecessary abstraction layers.",
        },
        {
            .name = "cpp-templates",
            .text = "C++ template metaprogramming critique. Flag excessive template parameters, CRTP used where "
                    "virtual works, policy classes with single policy, expression templates for simple ops, SFINAE "
                    "mazes replaceable with concepts/if constexpr, variadic templates for fixed arity, template "
                    "specialization explosion. Suggest runtime polymorphism, std::variant, plain functions, or "
                    "concepts.",
        },
        {
            .name = "cpp-inheritance",
            .text = "C++ inheritance hierarchy critique. Flag deep hierarchies (>3 levels), virtual functions with "
                    "single override, base classes with no virtual destructor, multiple inheritance without "
                    "diamond, virtual where CRTP/static polymorphism works, interface segregation violations. "
                    "Suggest composition, CRTP, concepts, or flattening hierarchy.",
        },
        {
            .name = "cpp-overloads",
            .text = "C++ overload/overload set critique. Flag excessive overloads (>4), ambiguous overload sets, "
                    "SFINAE-based enable_if chains, tag dispatching where if constexpr works, perfect forwarding "
                    "for non-template functions, concept overuse for simple constraints. Suggest default "
                    "parameters, single function with branches, or concepts.",
        },
        {
            .name = "cpp-header-only",
            .text = "C++ header-only bloat critique. Flag header-only libraries forcing full recompilation, inline "
                    "on large functions, missing compilation firewalls (pimpl), template implementation in headers "
                    "for non-templates, heavy includes in public headers. Suggest explicit instantiation, pimpl, "
                    "moving impl to .cpp, forward declarations.",
        },
        {
            .name = "enterprise-java",
            .text = "Enterprise Java pattern critique. Flag AbstractFactoryBean, Manager/Provider/Service suffixes "
                    "for simple objects, Builder for <4 fields, DTO/Entity mapping boilerplate, @Autowired "
                    "everywhere, @Configuration for simple beans, Repository for single table, "
                    "Specification/QueryDSL for simple queries. Suggest records, plain functions, constructor "
                    "injection, direct SQL.",
        },
        {
            .name = "microservice-premature",
            .text = "Premature microservice critique. Flag distributed transactions for simple consistency, service "
                    "mesh for 2-3 services, event sourcing for CRUD, CQRS where read=write, saga pattern for local "
                    "transactions, API gateway for internal services, distributed tracing for monolith. Suggest "
                    "modular monolith, shared library, or direct calls.",
        },
        {
            .name = "config-over-code",
            .text = "Configuration over code critique. Flag YAML/JSON/TOML configs for logic, rule engines for "
                    "<10 rules, DSLs for simple expressions, feature flags for permanent features, database-driven "
                    "config for static values, complex config validation schemas. Suggest hardcoded constants, "
                    "simple structs, compile-time config, or code.",
        },
    },
};

std::string_view pattern_text(std::string_view name) {
    for (auto const& pattern : kPatterns) {
        if (pattern.name == name) return pattern.text;
    }
    return {};
}

bool is_known_pattern(std::string_view name) { return !pattern_text(name).empty(); }

// `overengineering list`: printf "  %-25s %s\n" over names sorted by `sort`
// (the padded names are unique, so byte order over names is the order).
std::string render_pattern_list() {
    std::vector<std::string_view> names;
    names.reserve(kPatterns.size());
    for (auto const& pattern : kPatterns) names.push_back(pattern.name);
    std::sort(names.begin(), names.end());

    std::string output = "Available over-engineering patterns:\n";
    for (auto const& name : names) {
        std::string_view text = pattern_text(name);
        std::string brief(text.substr(0, text.find('.')));
        output += "  ";
        output += name;
        if (name.size() < 25) output.append(25 - name.size(), ' ');
        output += ' ';
        output += brief;
        output += ".\n";
    }
    return output;
}

// overengineering.sh never validates --head: it hands the raw value to
// `head -n <value> -- file`, so run exactly that (head reports its own error).
std::string run_head(std::string_view spec, std::filesystem::path const& file) {
    std::vector<std::string> storage = {"bash", "-c", R"(head -n "$1" -- "$2")"};
    storage.emplace_back();           // $0
    storage.emplace_back(spec);       // $1
    storage.push_back(file.string()); // $2
    return prompt::process::run_command(storage).stdout_data;
}

} // namespace

static void execute_overengineering(prompt_context& ctx) {
    // overengineering.sh parses "$@" itself (never init_prompt), so its loop
    // sees the unprocessed arguments.
    std::vector<std::string_view> selected;
    std::vector<std::string_view> files;
    std::string head_spec;
    bool numbered = false;

    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        std::string_view arg = ctx.args[i];
        if (arg == "--help" || arg == "-h") {
            std::string help = help_text(render_help_overengineering);
            ctx.out = help;
            return;
        }
        if (arg == "--head") {
            if (i + 1 >= ctx.args_count) {
                throw usage_error{"Missing value for --head\n"};
            }
            head_spec = std::string(ctx.args[i + 1]);
            ++i;
            continue;
        }
        if (is_number_flag(arg)) {
            numbered = true;
            continue;
        }
        if (arg == "list") {
            ctx.out = render_pattern_list();
            return;
        }
        if (arg.starts_with('-')) {
            std::string help = help_text(render_help_overengineering);
            throw usage_error{"Unknown option: " + std::string(arg) + "\n", help};
        }
        if (is_known_pattern(arg)) selected.push_back(arg);
        else
            files.push_back(arg);
    }

    if (selected.empty()) selected.emplace_back("general");

    std::string output;
    for (auto const& pattern : selected) {
        output += pattern_text(pattern);
        output += "\n\n";
    }

    std::string error;
    for (auto const& arg : files) {
        std::filesystem::path file(arg);
        if (std::filesystem::is_regular_file(file)) {
            output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
            output += number_lines(head_spec.empty() ? read_file(file) : run_head(head_spec, file), numbered);
            output += "```\n\n";
        } else {
            error += std::format("Warning: File '{}' not found or is not a regular file.\n", std::string(arg));
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_overengineering(std::ostream& os) {
    os << file_usage_help("overengineering", "[FILE...]", "", "[PATTERN...]")
       << R"EOF(       prompt overengineering list

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
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("overengineering", "Critique code for over-engineering patterns.",
                                                &execute_overengineering, &render_help_overengineering);

} // namespace prompt::prompts
