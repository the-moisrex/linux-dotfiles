#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt overengineering [PATTERN...] [--head N] [FILE...]
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
EOF
}

declare -A PATTERNS=(
  ["general"]="General over-engineering critique. Identify unnecessary complexity, premature abstractions, speculative flexibility, and violations of YAGNI. Prefer straightforward solutions, concrete over abstract, inline simple functions, remove unused capabilities. Provide specific simplifications as git diffs where possible."
  ["premature-abstraction"]="Premature abstraction critique. Flag interfaces with single implementations, base classes with no derived classes yet, template parameters used in only one way, strategy patterns where a function pointer or lambda would do, visitor patterns for simple type switches. Suggest inlining, removing indirection, using concrete types."
  ["yagni"]="YAGNI (You Aren't Gonna Need It) critique. Identify unused parameters, speculative hooks, plugin architectures not yet needed, configuration options never used, feature flags for undeployed features, generic repositories for single entity types, event systems with no subscribers. Suggest removing dead code paths and simplifying to current requirements."
  ["pattern-abuse"]="Design pattern overuse critique. Flag Factory/AbstractFactory for simple object creation, Strategy for single-algorithm cases, Visitor for simple type dispatch, Observer for direct calls, Command for simple callbacks, Builder for <4 parameters, Singleton where dependency injection works, Decorator for single decoration. Suggest replacing with direct calls, functions, or simpler constructs."
  ["excessive-di"]="Excessive dependency injection critique. Flag service locators, containers for simple apps, constructor injection with 5+ params, interfaces created solely for mocking, provider/manager/factory indirection chains, scoped lifetimes where transient works. Suggest manual wiring, simple structs, or removing unnecessary abstraction layers."
  ["cpp-templates"]="C++ template metaprogramming critique. Flag excessive template parameters, CRTP used where virtual works, policy classes with single policy, expression templates for simple ops, SFINAE mazes replaceable with concepts/if constexpr, variadic templates for fixed arity, template specialization explosion. Suggest runtime polymorphism, std::variant, plain functions, or concepts."
  ["cpp-inheritance"]="C++ inheritance hierarchy critique. Flag deep hierarchies (>3 levels), virtual functions with single override, base classes with no virtual destructor, multiple inheritance without diamond, virtual where CRTP/static polymorphism works, interface segregation violations. Suggest composition, CRTP, concepts, or flattening hierarchy."
  ["cpp-overloads"]="C++ overload/overload set critique. Flag excessive overloads (>4), ambiguous overload sets, SFINAE-based enable_if chains, tag dispatching where if constexpr works, perfect forwarding for non-template functions, concept overuse for simple constraints. Suggest default parameters, single function with branches, or concepts."
  ["cpp-header-only"]="C++ header-only bloat critique. Flag header-only libraries forcing full recompilation, inline on large functions, missing compilation firewalls (pimpl), template implementation in headers for non-templates, heavy includes in public headers. Suggest explicit instantiation, pimpl, moving impl to .cpp, forward declarations."
  ["enterprise-java"]="Enterprise Java pattern critique. Flag AbstractFactoryBean, Manager/Provider/Service suffixes for simple objects, Builder for <4 fields, DTO/Entity mapping boilerplate, @Autowired everywhere, @Configuration for simple beans, Repository for single table, Specification/QueryDSL for simple queries. Suggest records, plain functions, constructor injection, direct SQL."
  ["microservice-premature"]="Premature microservice critique. Flag distributed transactions for simple consistency, service mesh for 2-3 services, event sourcing for CRUD, CQRS where read=write, saga pattern for local transactions, API gateway for internal services, distributed tracing for monolith. Suggest modular monolith, shared library, or direct calls."
  ["config-over-code"]="Configuration over code critique. Flag YAML/JSON/TOML configs for logic, rule engines for <10 rules, DSLs for simple expressions, feature flags for permanent features, database-driven config for static values, complex config validation schemas. Suggest hardcoded constants, simple structs, compile-time config, or code."
)

source "$(dirname "$0")/_common.sh"

# Parse arguments manually since we need to handle patterns positionally
PATTERNS_SELECTED=()
ARGS=()
head_lines=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      show_help
      exit 0
      ;;
    --head)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --head" >&2
        exit 2
      fi
      head_lines="$2"
      shift 2
      ;;
    list)
      echo "Available over-engineering patterns:"
      for p in "${!PATTERNS[@]}"; do
        printf "  %-25s %s\n" "$p" "${PATTERNS[$p]%%.*}."
      done | sort
      exit 0
      ;;
    -*)
      echo "Unknown option: $1" >&2
      show_help
      exit 2
      ;;
    *)
      # Check if it's a known pattern
      if [[ -n "${PATTERNS[$1]:-}" ]]; then
        PATTERNS_SELECTED+=("$1")
      else
        ARGS+=("$1")
      fi
      shift
      ;;
  esac
done

# Default to general if no pattern specified
if [[ ${#PATTERNS_SELECTED[@]} -eq 0 ]]; then
  PATTERNS_SELECTED=("general")
fi

# Print instructions for each selected pattern
for pattern in "${PATTERNS_SELECTED[@]}"; do
  echo "${PATTERNS[$pattern]}"
  echo
done

# Process all collected files
for file in "${ARGS[@]}"; do
  if [[ -f "$file" ]]; then
    file_name="$(basename "$file")"
    echo "File: $file_name"
    echo
    echo "\`\`\`$(infer_lang "$file_name")"
    if [[ -n "$head_lines" ]]; then
      head -n "$head_lines" -- "$file"
    else
      cat -- "$file"
    fi
    echo '```'
    echo
  else
    echo "Warning: File '$file' not found or is not a regular file." >&2
  fi
done