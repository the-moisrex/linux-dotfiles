#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// The keys the dispatcher and the built-in prompts agree on. A key is a plain
// string, so a prompt family can publish its own without touching the SDK; the
// constants below exist only to keep the names spelled once.
namespace keys {

// The basename stem a later `.save` should derive its filename from.
inline constexpr std::string_view save_stem = "save.stem";

// The response shape the AI was asked for, as a plan: newline-separated
// `[name=]selector` entries (`command:prompt`, `save.stem=field:filename`).
// Published by `.expect` and `.delegate`, read by `.extract` (all entries) and
// by `.each`/`.exec` (the unnamed primary entry only) when they are given no
// selector of their own. See `sdk/agent.hpp`'s `parse_expect_plan`.
inline constexpr std::string_view expect = "prompt.expect";

// Declared by `.save` (when it has no explicit target) so `.delegate` knows to
// ask the AI for a file name and publish the `save.stem` binding for it. The
// value is a marker ("1"); presence is what matters.
inline constexpr std::string_view save_wants_name = "save.want_name";

// The resolved TSE instrument, published once per chain by whichever `tse*`
// prompt resolved it first.
inline constexpr std::string_view tse_symbol = "tse.instrument.symbol";

// The chain's intent, published as an artifact by `.fix`/`.debug`/`.run` (and
// overridable with `.compress --for`). Read by `.compress` so the same input
// compresses differently when it is being repaired rather than merely read.
// Values are `general`, `fix`, `debug`, `review` (`prompt::signals::intent`).
inline constexpr std::string_view mode = "prompt.mode";

// What the chain is *about*: newline-separated focal items — failed Google Test
// cases, `path:line` error sites, or the files under repair. `.compress` never
// compresses a focal region and its `gtest.units` compressor keeps the focal
// test bodies while sampling the rest.
inline constexpr std::string_view focus = "prompt.focus";

// The detected project shape, published by `.project` and re-derived on demand
// by `.compress` through `prompt::project::detect`. `build_dir` is a path, the
// rest are `prompt::project` enum names.
inline constexpr std::string_view project_language = "project.language";
inline constexpr std::string_view project_build = "project.build";
inline constexpr std::string_view project_build_dir = "project.build_dir";
inline constexpr std::string_view project_package = "project.package";
inline constexpr std::string_view project_tests = "project.tests";

} // namespace keys

// The only inter-prompt channel. Ordered key/value pairs of strings; a value
// may contain anything (the wire writes it length-prefixed).
//
// Two merge rules, both one line:
//   * declarations (cheap, argv/env-derived, published before a batch runs)
//     keep the FIRST writer — order-independent;
//   * artifacts (published by a finished segment) keep the LAST writer — the
//     nearest successful segment, which is exactly what `.save` means.
class facts {
  public:
    struct entry {
        std::string key;
        std::string value;
    };

    void set(std::string_view key, std::string_view value);

    [[nodiscard]] std::optional<std::string_view> get(std::string_view key) const;

    [[nodiscard]] bool empty() const { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); }

    // Merge `other` in. overwrite=false keeps an existing value (declarations,
    // first writer wins); overwrite=true replaces it (artifacts, last wins).
    void merge(facts const& other, bool overwrite);

    // Wire codec: count, then length-prefixed key/value pairs. Values may
    // contain anything, including NUL, so the list encoding survives a fork.
    [[nodiscard]] std::string encode() const;
    static facts decode(std::string_view blob);

  private:
    std::vector<entry> entries_;
};

} // namespace prompt
