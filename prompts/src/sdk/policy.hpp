#pragma once

#include "agent.hpp"
#include "stage.hpp"
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::policy {

// The command policy. Prompts are trusted code the user typed; the gate is for
// *commands* (`.exec`, and the `.exec` steps of a `.each` body), because that
// is the only path from model output to execution.
//
// Rules are tri-state (`allow`, `ask`, `deny`) and match a program plus its
// arguments. The most specific rule wins; `deny` wins a specificity tie; a
// higher-priority source wins a remaining tie:
//
//   PROMPT_POLICY_ALLOW/PROMPT_POLICY_DENY env (3)
//   <git-root>/.prompt/policy                  (2)
//   $XDG_CONFIG_HOME/prompt/policy             (1)
//   built-ins                                  (0)
//
// A parent `.each` pre-approves its static `.exec` programs by passing them in
// PROMPT_INNER_ALLOW; those grants are ordinary allow rules at source 0, so an
// explicit deny still wins and a dynamic command (one the AI has not written
// yet) still asks when it first exists.
class engine {
  public:
    explicit engine(environment const& env);

    // Throws soft_error when the command is denied or the user cancels. `label`
    // overrides the display line (a generated prompt chain shows all its
    // segments).
    void require(std::span<std::string const> argv, std::string_view label = {});
    void require_chain(std::span<std::string const> chain_tokens);

    void set_yes(bool yes) { yes_ = yes; }
    [[nodiscard]] bool yes() const { return yes_; }

  private:
    enum class action : unsigned char { allow, ask, deny };

    struct rule {
        action verdict = action::ask;
        std::string program;
        std::vector<std::string> args;
        int source = 0;
        std::size_t specificity = 0;
    };

    [[nodiscard]] action decide(std::span<std::string const> argv) const;
    void add(action verdict, std::string program, std::vector<std::string> args, int source);
    void load_file(std::filesystem::path const& path, int source);
    void parse_env(std::string_view name, action verdict, int source);

    bool yes_ = false;
    std::vector<rule> rules_;
};

} // namespace prompt::policy
