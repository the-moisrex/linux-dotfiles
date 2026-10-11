#include "prompt/prompts/overengineering_prompt.hpp"
#include "prompt/core/process.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

struct overengineering_pattern {
    std::string_view name;
    std::string_view text;
};

// overengineering.sh's PATTERNS map, in the script's declaration order.
constexpr overengineering_pattern kPatterns[] = {
    {"general", "General over-engineering critique. Identify unnecessary complexity, premature abstractions, "
                "speculative flexibility, and violations of YAGNI. Prefer straightforward solutions, concrete "
                "over abstract, inline simple functions, remove unused capabilities. Provide specific "
                "simplifications as git diffs where possible."},
    {"premature-abstraction",
     "Premature abstraction critique. Flag interfaces with single implementations, base classes with "
     "no derived classes yet, template parameters used in only one way, strategy patterns where a "
     "function pointer or lambda would do, visitor patterns for simple type switches. Suggest "
     "inlining, removing indirection, using concrete types."},
    {"yagni", "YAGNI (You Aren't Gonna Need It) critique. Identify unused parameters, speculative hooks, "
              "plugin architectures not yet needed, configuration options never used, feature flags for "
              "undeployed features, generic repositories for single entity types, event systems with no "
              "subscribers. Suggest removing dead code paths and simplifying to current requirements."},
    {"pattern-abuse", "Design pattern overuse critique. Flag Factory/AbstractFactory for simple object creation, "
                      "Strategy for single-algorithm cases, Visitor for simple type dispatch, Observer for direct "
                      "calls, Command for simple callbacks, Builder for <4 parameters, Singleton where dependency "
                      "injection works, Decorator for single decoration. Suggest replacing with direct calls, "
                      "functions, or simpler constructs."},
    {"excessive-di", "Excessive dependency injection critique. Flag service locators, containers for simple apps, "
                     "constructor injection with 5+ params, interfaces created solely for mocking, "
                     "provider/manager/factory indirection chains, scoped lifetimes where transient works. Suggest "
                     "manual wiring, simple structs, or removing unnecessary abstraction layers."},
    {"cpp-templates", "C++ template metaprogramming critique. Flag excessive template parameters, CRTP used where "
                      "virtual works, policy classes with single policy, expression templates for simple ops, SFINAE "
                      "mazes replaceable with concepts/if constexpr, variadic templates for fixed arity, template "
                      "specialization explosion. Suggest runtime polymorphism, std::variant, plain functions, or "
                      "concepts."},
    {"cpp-inheritance", "C++ inheritance hierarchy critique. Flag deep hierarchies (>3 levels), virtual functions with "
                        "single override, base classes with no virtual destructor, multiple inheritance without "
                        "diamond, virtual where CRTP/static polymorphism works, interface segregation violations. "
                        "Suggest composition, CRTP, concepts, or flattening hierarchy."},
    {"cpp-overloads", "C++ overload/overload set critique. Flag excessive overloads (>4), ambiguous overload sets, "
                      "SFINAE-based enable_if chains, tag dispatching where if constexpr works, perfect forwarding "
                      "for non-template functions, concept overuse for simple constraints. Suggest default "
                      "parameters, single function with branches, or concepts."},
    {"cpp-header-only", "C++ header-only bloat critique. Flag header-only libraries forcing full recompilation, inline "
                        "on large functions, missing compilation firewalls (pimpl), template implementation in headers "
                        "for non-templates, heavy includes in public headers. Suggest explicit instantiation, pimpl, "
                        "moving impl to .cpp, forward declarations."},
    {"enterprise-java", "Enterprise Java pattern critique. Flag AbstractFactoryBean, Manager/Provider/Service suffixes "
                        "for simple objects, Builder for <4 fields, DTO/Entity mapping boilerplate, @Autowired "
                        "everywhere, @Configuration for simple beans, Repository for single table, "
                        "Specification/QueryDSL for simple queries. Suggest records, plain functions, constructor "
                        "injection, direct SQL."},
    {"microservice-premature",
     "Premature microservice critique. Flag distributed transactions for simple consistency, service "
     "mesh for 2-3 services, event sourcing for CRUD, CQRS where read=write, saga pattern for local "
     "transactions, API gateway for internal services, distributed tracing for monolith. Suggest "
     "modular monolith, shared library, or direct calls."},
    {"config-over-code",
     "Configuration over code critique. Flag YAML/JSON/TOML configs for logic, rule engines for "
     "<10 rules, DSLs for simple expressions, feature flags for permanent features, database-driven "
     "config for static values, complex config validation schemas. Suggest hardcoded constants, "
     "simple structs, compile-time config, or code."},
};

std::string_view pattern_text(std::string_view name) noexcept {
    for (auto const& pattern : kPatterns) {
        if (pattern.name == name) return pattern.text;
    }
    return {};
}

bool is_known_pattern(std::string_view name) noexcept { return !pattern_text(name).empty(); }

// `overengineering list`: printf "  %-25s %s\n" over names sorted by `sort`
// (the padded names are unique, so byte order over names is the order).
std::string render_pattern_list() noexcept {
    std::vector<std::string_view> names;
    for (auto const& pattern : kPatterns) names.push_back(pattern.name);
    std::sort(names.begin(), names.end());

    std::string output = "Available over-engineering patterns:\n";
    for (auto const& name : names) {
        std::string_view text = pattern_text(name);
        std::string brief(text.substr(0, text.find('.')));
        output += "  ";
        output += name;
        if (name.size() < 25) output.append(25 - name.size(), ' ');
        output += " ";
        output += brief;
        output += ".\n";
    }
    return output;
}

// overengineering.sh never validates --head: it hands the raw value to
// `head -n <value> -- file`, so run exactly that (head reports its own error).
std::string run_head(std::string_view spec, std::filesystem::path const& file) noexcept {
    std::vector<std::string> storage;
    storage.push_back("bash");
    storage.push_back("-c");
    storage.push_back("head -n \"$1\" -- \"$2\"");
    storage.push_back(std::string());     // $0
    storage.push_back(std::string(spec)); // $1
    storage.push_back(file.string());     // $2
    std::vector<char const*> argv;
    argv.reserve(storage.size() + 1);
    for (auto const& s : storage) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    return prompt::process::run_command(argv).stdout_data;
}

} // namespace

prompt_result execute_overengineering(prompt_context&& ctx) noexcept {
    // overengineering.sh parses "$@" itself (never init_prompt), so its loop
    // sees the unprocessed arguments.
    std::vector<std::string_view> selected;
    std::vector<std::string_view> files;
    std::string head_spec;

    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        std::string_view arg = ctx.args[i];
        if (arg == "--help" || arg == "-h") {
            std::ostringstream help;
            render_help_overengineering(help);
            return {help.str(), 0, false, std::string{}};
        }
        if (arg == "--head") {
            if (i + 1 >= ctx.args_count) {
                return {std::string{}, 2, false, "Missing value for --head\n"};
            }
            head_spec = std::string(ctx.args[i + 1]);
            ++i;
            continue;
        }
        if (arg == "list") {
            return {render_pattern_list(), 0, false, std::string{}};
        }
        if (arg.starts_with('-')) {
            std::ostringstream help;
            render_help_overengineering(help);
            return {help.str(), 2, false, "Unknown option: " + std::string(arg) + "\n"};
        }
        if (is_known_pattern(arg)) selected.push_back(arg);
        else
            files.push_back(arg);
    }

    if (selected.empty()) selected.push_back("general");

    std::string output;
    for (auto const& pattern : selected) {
        output += pattern_text(pattern);
        output += "\n";
        output += "\n";
    }

    std::string error;
    for (auto const& arg : files) {
        std::filesystem::path file(arg);
        if (std::filesystem::is_regular_file(file)) {
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += head_spec.empty() ? read_file(file) : run_head(head_spec, file);
            output += "```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(arg) + "' not found or is not a regular file.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_overengineering(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt overengineering [PATTERN...] [--head N] [FILE...]
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
)EOF";
}

} // namespace prompt::prompts
