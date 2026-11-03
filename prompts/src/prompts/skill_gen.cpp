#include "../sdk/args.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/stage.hpp"
#include <ostream>
#include <string>
#include <string_view>

namespace prompt::prompts {

namespace {

// The instruction text appended to the chain context. It teaches the AI the
// SKILL.md shape the `skill` prompt loads and the shape `.extract` expects back.
std::string skill_instructions(bool want_name) {
    std::string out = R"EOF(# Skill generation

You are generating a reusable **skill**: one self-contained Markdown file an AI
agent loads when a matching task comes up. Produce exactly one skill and nothing
else.

## Format

The file begins with YAML frontmatter delimited by `---`:

---
name: <kebab-case-name>
description: <one sentence saying what the skill does and when to use it>
---

The body follows: concrete, step-by-step instructions, the exact commands to
run, file conventions, common pitfalls and one or two short examples. Prefer
imperative, testable guidance over prose, and keep the skill focused on a single
capability.

## Output

Return the whole `SKILL.md` in a single fenced ```` ```markdown ```` block. The
block must start with the `---` frontmatter and contain no surrounding
commentary.
)EOF";

    if (want_name) {
        out += R"EOF(
Just before the code block, add one line naming the skill:

filename: <kebab-case-name>

Use the same name as the frontmatter `name` entry. It becomes the directory the
skill is saved under, so keep it short and kebab-case.
)EOF";
    }
    return out;
}

} // namespace

static void execute_skill_gen(view const& v, result& r) {
    (void)head_lines_or_throw(v); // no embedded files to cap here

    // `.save` with no explicit target wants a derived name, so ask the AI for
    // one and publish the binding; `.save` itself is skill-aware because of the
    // `save.kind` fact below.
    bool const want_name = v.prior.get(keys::save_wants_name).has_value();

    std::string out;
    if (v.in.available()) {
        std::string_view const task = v.in.text();
        v.in.spend();
        if (!task.empty()) {
            out += "The skill should cover the following task:\n\n";
            out += std::string(task);
            out += "\n\n";
        }
    }
    out += skill_instructions(want_name);

    std::string plan = "code:markdown";
    if (want_name) {
        plan += '\n';
        plan += std::string(keys::save_stem) + "=field:filename";
    }
    r.published.set(keys::expect, plan);
    r.published.set(keys::save_kind, "skill");
    r.out = std::move(out);
}

static void render_help_skill_gen(std::ostream& os) {
    os << R"EOF(Usage: prompt .note "<task>" .skill.gen [--head N] .ai.<provider> .extract .save

Generate a reusable skill with an AI. Appends the SKILL.md format and an output
contract to the chain context, asks the AI for one skill, and marks the chain as
a skill save: a nameless `.save` then writes
`$XDG_DATA_HOME/agents/skills/<name>/SKILL.md` (the layout `prompt skill`
loads) instead of a file in the working directory.

The AI returns the whole SKILL.md in a ```` ```markdown ```` block plus a
`filename:` line, so `.extract` publishes the name and `.save` uses it. When
`.save` has an explicit target the name request is suppressed.

Examples:
  prompt .note "how to bisect a regression with git" .skill.gen .ai.dseek .extract .save
  cat notes.md | prompt .skill.gen .ai.grok .extract .save

Options:
  --head N   Accepted for symmetry with the file prompts; unused here
  -h, --help Show this help message

See also: `.skill` (load and manage cached skills), `.delegate` (generate a
`prompt` command) and `.expect`/`.extract` (the plan this prompt publishes).
)EOF";
}

bool const registered_skill_gen =
    prompt::register_prompt("skill.gen",
                            "Generate a reusable skill with an AI and save it into the skills directory "
                            "($XDG_DATA_HOME/agents/skills/<name>/SKILL.md).",
                            &execute_skill_gen, &render_help_skill_gen);

} // namespace prompt::prompts
