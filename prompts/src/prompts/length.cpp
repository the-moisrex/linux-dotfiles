#include "../core/strings.hpp"
#include "../sdk/completion.hpp"
#include "../sdk/prompt_registry.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::prompts {

namespace {

enum class length_mode : std::uint8_t { rough, exact, max, min };

enum class length_unit : std::uint8_t { words, lines, sentences, paragraphs, characters, tokens };

enum class length_preset : std::uint8_t {
    none,
    tiny,
    short_answer,
    medium,
    long_answer,
    one_liner,
    sentence,
    paragraph,
    bullets,
    title,
    tweet,
    sms,
    email,
    letter,
    blog,
    article,
    report,
    essay,
    abstract,
    outline,
    notes,
    caption,
    bio,
    commit,
    changelog,
    thread,
    tldr,
};

struct length_spec {
    std::optional<std::size_t> amount;
    length_unit unit = length_unit::words;
    length_mode mode = length_mode::rough;
    length_preset preset = length_preset::none;
};

// Words that name a unit after a number (`200 words`, `10 lines`). Single
// letters are the compact suffixes (`200w`, `10l`, `3p`).
constexpr auto kUnitAliases = std::to_array<std::pair<std::string_view, length_unit>>({
    {"words", length_unit::words},
    {"word", length_unit::words},
    {"w", length_unit::words},
    {"lines", length_unit::lines},
    {"line", length_unit::lines},
    {"l", length_unit::lines},
    {"sentences", length_unit::sentences},
    {"sentence", length_unit::sentences},
    {"s", length_unit::sentences},
    {"paragraphs", length_unit::paragraphs},
    {"paragraph", length_unit::paragraphs},
    {"paras", length_unit::paragraphs},
    {"para", length_unit::paragraphs},
    {"p", length_unit::paragraphs},
    {"characters", length_unit::characters},
    {"character", length_unit::characters},
    {"chars", length_unit::characters},
    {"char", length_unit::characters},
    {"c", length_unit::characters},
    {"tokens", length_unit::tokens},
    {"token", length_unit::tokens},
    {"t", length_unit::tokens},
});

constexpr auto kUnitFlags = std::to_array<std::pair<std::string_view, length_unit>>({
    {"--words", length_unit::words},
    {"--word", length_unit::words},
    {"--lines", length_unit::lines},
    {"--line", length_unit::lines},
    {"--sentences", length_unit::sentences},
    {"--sentence", length_unit::sentences},
    {"--paragraphs", length_unit::paragraphs},
    {"--paragraph", length_unit::paragraphs},
    {"--para", length_unit::paragraphs},
    {"--chars", length_unit::characters},
    {"--characters", length_unit::characters},
    {"--char", length_unit::characters},
    {"--tokens", length_unit::tokens},
    {"--token", length_unit::tokens},
});

constexpr auto kPresetAliases = std::to_array<std::pair<std::string_view, length_preset>>({
    {"tiny", length_preset::tiny},
    {"minimal", length_preset::tiny},
    {"short", length_preset::short_answer},
    {"brief", length_preset::short_answer},
    {"concise", length_preset::short_answer},
    {"medium", length_preset::medium},
    {"normal", length_preset::medium},
    {"default", length_preset::medium},
    {"standard", length_preset::medium},
    {"long", length_preset::long_answer},
    {"detailed", length_preset::long_answer},
    {"thorough", length_preset::long_answer},
    {"one-liner", length_preset::one_liner},
    {"oneliner", length_preset::one_liner},
    {"one_liner", length_preset::one_liner},
    {"line", length_preset::one_liner},
    {"sentence", length_preset::sentence},
    {"paragraph", length_preset::paragraph},
    {"para", length_preset::paragraph},
    {"bullets", length_preset::bullets},
    {"bullet-points", length_preset::bullets},
    {"bullet_points", length_preset::bullets},
    {"list", length_preset::bullets},
    {"title", length_preset::title},
    {"heading", length_preset::title},
    {"tweet", length_preset::tweet},
    {"sms", length_preset::sms},
    {"text", length_preset::sms},
    {"message", length_preset::sms},
    {"email", length_preset::email},
    {"e-mail", length_preset::email},
    {"letter", length_preset::letter},
    {"blog", length_preset::blog},
    {"blog-post", length_preset::blog},
    {"blogpost", length_preset::blog},
    {"article", length_preset::article},
    {"report", length_preset::report},
    {"essay", length_preset::essay},
    {"abstract", length_preset::abstract},
    {"outline", length_preset::outline},
    {"notes", length_preset::notes},
    {"caption", length_preset::caption},
    {"bio", length_preset::bio},
    {"biography", length_preset::bio},
    {"commit", length_preset::commit},
    {"commit-message", length_preset::commit},
    {"changelog", length_preset::changelog},
    {"changelog-entry", length_preset::changelog},
    {"thread", length_preset::thread},
    {"twitter-thread", length_preset::thread},
    {"tldr", length_preset::tldr},
    {"tl-dr", length_preset::tldr},
    {"tl;dr", length_preset::tldr},
});

constexpr auto kPresetNames = std::to_array<std::string_view>({
    "abstract", "article", "bio",  "blog",   "bullets", "caption",   "changelog", "commit",    "email",
    "essay",    "letter",  "long", "medium", "notes",   "one-liner", "outline",   "paragraph", "report",
    "sentence", "short",   "sms",  "thread", "tiny",    "title",     "tldr",      "tweet",
});

std::optional<length_unit> unit_from_word(std::string_view word) {
    for (auto const& [alias, unit] : kUnitAliases) {
        if (alias == word) return unit;
    }
    return std::nullopt;
}

std::optional<length_unit> unit_from_flag(std::string_view flag) {
    for (auto const& [alias, unit] : kUnitFlags) {
        if (alias == flag) return unit;
    }
    return std::nullopt;
}

std::optional<length_preset> preset_from_word(std::string_view word) {
    for (auto const& [alias, preset] : kPresetAliases) {
        if (alias == word) return preset;
    }
    return std::nullopt;
}

// "a couple of paragraphs", "a few sentences", "a dozen lines".
std::optional<std::size_t> number_word(std::string_view word) {
    constexpr auto kNumberWords = std::to_array<std::pair<std::string_view, std::size_t>>({
        {"one", 1},      {"two", 2},        {"three", 3},     {"four", 4},      {"five", 5},
        {"six", 6},      {"seven", 7},      {"eight", 8},     {"nine", 9},      {"ten", 10},
        {"eleven", 11},  {"twelve", 12},    {"thirteen", 13}, {"fourteen", 14}, {"fifteen", 15},
        {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19}, {"twenty", 20},
        {"couple", 2},   {"few", 3},        {"several", 4},   {"dozen", 12},    {"hundred", 100},
    });
    for (auto const& [name, value] : kNumberWords) {
        if (name == word) return value;
    }
    return std::nullopt;
}

struct parsed_number {
    std::size_t value;
    length_unit unit;
};

// A number with an optional thousands suffix and an optional unit attached:
// "200", "2k", "200w", "10lines". Returns nullopt for anything else.
std::optional<parsed_number> parse_number(std::string_view token, length_unit default_unit) {
    std::size_t pos = 0;
    std::size_t value = 0;
    while (pos < token.size() && std::isdigit(static_cast<unsigned char>(token[pos])) != 0) {
        auto const digit = static_cast<std::size_t>(token[pos] - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10) return std::nullopt;
        value = (value * 10) + digit;
        ++pos;
    }
    if (pos == 0) return std::nullopt;

    if (pos < token.size() && (token[pos] == 'k' || token[pos] == 'K')) {
        if (value > std::numeric_limits<std::size_t>::max() / 1000) return std::nullopt;
        value *= 1000;
        ++pos;
    }

    if (pos == token.size()) return parsed_number{.value = value, .unit = default_unit};

    auto const unit = unit_from_word(token.substr(pos));
    if (!unit) return std::nullopt;
    return parsed_number{.value = value, .unit = *unit};
}

std::string_view unit_name(length_unit unit, bool plural) {
    switch (unit) {
    case length_unit::words:
        return plural ? "words" : "word";
    case length_unit::lines:
        return plural ? "lines" : "line";
    case length_unit::sentences:
        return plural ? "sentences" : "sentence";
    case length_unit::paragraphs:
        return plural ? "paragraphs" : "paragraph";
    case length_unit::characters:
        return plural ? "characters" : "character";
    case length_unit::tokens:
        return plural ? "tokens" : "token";
    }
    return "words";
}

// Counting is unambiguous for words, sentences and paragraphs; the other units
// get a one-clause clarifier so the model and the reader agree on what counts.
std::string_view unit_clarifier(length_unit unit) {
    switch (unit) {
    case length_unit::lines:
        return " (Markdown lines, blank lines included)";
    case length_unit::characters:
        return " (spaces and punctuation included)";
    case length_unit::tokens:
        return " (roughly 4 characters per token)";
    default:
        return {};
    }
}

std::string_view preset_instruction(length_preset preset) {
    switch (preset) {
    case length_preset::tiny:
        return "Give the shortest possible answer: one short sentence, with no elaboration and no filler.";
    case length_preset::short_answer:
        return "Keep your answer short: about 2 to 4 sentences, with no padding or repetition.";
    case length_preset::medium:
        return "Keep your answer to about one or two paragraphs, with no padding or repetition.";
    case length_preset::long_answer:
        return "Give a long, thorough answer of several substantive paragraphs, without padding or repetition.";
    case length_preset::one_liner:
        return "Keep your answer to a single line.";
    case length_preset::sentence:
        return "Keep your answer to a single sentence.";
    case length_preset::paragraph:
        return "Keep your answer to a single paragraph.";
    case length_preset::bullets:
        return "Answer as a short bullet list of about 3 to 6 items.";
    case length_preset::title:
        return "Answer with a single title line and nothing else.";
    case length_preset::tweet:
        return "Keep your answer within a tweet: at most 280 characters.";
    case length_preset::sms:
        return "Keep your answer to a short text message: one or two sentences.";
    case length_preset::email:
        return "Answer as a short email: a greeting, a compact body of about 2 to 4 short paragraphs, and a sign-off.";
    case length_preset::letter:
        return "Answer as a short letter: a salutation, a compact body of about 3 to 5 short paragraphs, and a "
               "closing.";
    case length_preset::blog:
        return "Answer as a short blog post of roughly 500 to 800 words: a title, a short introduction, a few headed "
               "sections, and a takeaway.";
    case length_preset::article:
        return "Answer as an article of roughly 800 to 1200 words: a title, an introduction, a few headed sections, "
               "and "
               "a conclusion.";
    case length_preset::report:
        return "Answer as a structured report of roughly 800 to 1200 words: a short summary, headed sections, and a "
               "conclusion.";
    case length_preset::essay:
        return "Answer as a short essay of roughly 500 to 800 words: an introduction, a developed argument, and a "
               "conclusion.";
    case length_preset::abstract:
        return "Answer with a single-paragraph abstract of roughly 150 to 250 words.";
    case length_preset::outline:
        return "Answer as a short outline: headed sections with brief bullet points, no prose paragraphs.";
    case length_preset::notes:
        return "Answer as concise notes: short bullet points grouped under a few headings.";
    case length_preset::caption:
        return "Keep your answer to a one- or two-sentence caption.";
    case length_preset::bio:
        return "Keep your answer to a short bio of two or three sentences.";
    case length_preset::commit:
        return "Answer with a Conventional Commits message: a short imperative subject line, plus a short "
               "bullet-point body only if it adds information.";
    case length_preset::changelog:
        return "Answer with a changelog entry: one short summary line and a bullet list of the changes.";
    case length_preset::thread:
        return "Answer as a short social thread of three to five numbered posts, each under 280 characters.";
    case length_preset::tldr:
        return "Answer with a one- or two-sentence TL;DR.";
    case length_preset::none:
        break;
    }
    return {};
}

std::string render_amount(std::size_t amount, length_unit unit, length_mode mode) {
    std::string const target = std::format("{} {}", amount, unit_name(unit, amount != 1));
    std::string_view const clarifier = unit_clarifier(unit);

    switch (mode) {
    case length_mode::rough:
        return std::format("Keep your answer to about {}{}: trim it if it runs longer and expand it if it comes up "
                           "short, but never pad with filler or repeat yourself.\n",
                           target, clarifier);
    case length_mode::exact:
        return std::format("Your answer must be exactly {}{}: add or cut content to hit the target, keep it "
                           "substantive, and count before you finish.\n",
                           target, clarifier);
    case length_mode::max:
        return std::format("Your answer must be at most {}{}: cut anything that is not needed and never exceed the "
                           "target.\n",
                           target, clarifier);
    case length_mode::min:
        return std::format("Your answer must run to at least {}{}: expand it with substantive detail, examples and "
                           "reasoning, never with filler.\n",
                           target, clarifier);
    }
    return {};
}

void complete_length(completion_request const& request, std::ostream& os) {
    completion::write_values(os, request.prefix, kPresetNames);
}

} // namespace

static void execute_length(view const& v, result& r) {
    // Any argument may hold a whole phrase ("200 words", "a letter"), so every
    // argv token is split into words and lower-cased before matching.
    std::vector<std::string> tokens;
    for (std::string_view arg : v.args) {
        for (auto& word : prompt::text::words(arg)) {
            std::ranges::transform(word, word.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            tokens.push_back(std::move(word));
        }
    }

    length_spec spec;
    bool have_target = false;
    std::optional<length_mode> requested_mode;

    auto add_mode = [&](length_mode mode) {
        if (requested_mode.has_value() && *requested_mode != mode) {
            throw usage_error{"prompt length: conflicting length modes\n"};
        }
        requested_mode = mode;
    };

    auto set_amount = [&](std::size_t amount, length_unit unit) {
        if (have_target) throw usage_error{"prompt length: more than one target given\n"};
        spec.amount = amount;
        spec.unit = unit;
        have_target = true;
    };

    auto set_preset = [&](length_preset preset) {
        if (have_target) throw usage_error{"prompt length: more than one target given\n"};
        spec.preset = preset;
        have_target = true;
    };

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        std::string_view token = tokens[i];

        if (token == "--exact" || token == "-e" || token == "exactly") {
            add_mode(length_mode::exact);
            continue;
        }
        if (token == "--rough" || token == "-r" || token == "roughly" || token == "about" || token == "around" ||
            token == "approximately" || token == "approx" || token == "circa") {
            add_mode(length_mode::rough);
            continue;
        }
        if (token == "--max" || token == "--at-most") {
            add_mode(length_mode::max);
            continue;
        }
        if (token == "--min" || token == "--at-least") {
            add_mode(length_mode::min);
            continue;
        }
        if (token == "at" && i + 1 < tokens.size() && (tokens[i + 1] == "most" || tokens[i + 1] == "least")) {
            add_mode(tokens[++i] == "most" ? length_mode::max : length_mode::min);
            continue;
        }

        if (auto const unit = unit_from_flag(token)) {
            if (i + 1 >= tokens.size()) {
                throw usage_error{"prompt length: " + std::string(token) + " needs a number\n"};
            }
            auto const number = parse_number(tokens[++i], *unit);
            if (!number) throw usage_error{"prompt length: invalid length: " + tokens[i] + "\n"};
            set_amount(number->value, number->unit);
            continue;
        }

        // "=200" / "~200": the mode prefix written onto the number itself.
        if (token.starts_with('=') || token.starts_with('~')) {
            add_mode(token.front() == '=' ? length_mode::exact : length_mode::rough);
            token.remove_prefix(1);
            if (token.empty()) {
                if (i + 1 >= tokens.size()) {
                    throw usage_error{"prompt length: " + std::string(tokens[i]) + " needs a number\n"};
                }
                token = tokens[++i];
            }
        }

        if (!token.empty() && std::isdigit(static_cast<unsigned char>(token.front())) != 0) {
            auto const number = parse_number(token, length_unit::words);
            if (!number) throw usage_error{"prompt length: invalid length: " + std::string(token) + "\n"};
            // The unit may follow as its own word: "10 lines" (the number
            // itself may already carry it: "10l", "2k words").
            length_unit unit = number->unit;
            if (i + 1 < tokens.size()) {
                if (auto const next_unit = unit_from_word(tokens[i + 1])) {
                    unit = *next_unit;
                    ++i;
                }
            }
            set_amount(number->value, unit);
            continue;
        }

        // Articles: "a paragraph" is one paragraph, "a letter"/"an email" is
        // the article before a preset, "a couple of paragraphs" a number word.
        if (token == "a" || token == "an") {
            if (i + 1 >= tokens.size()) throw usage_error{"prompt length: 'a' needs a unit or a preset\n"};
            if (auto const unit = unit_from_word(tokens[i + 1])) {
                set_amount(1, *unit);
                ++i;
                continue;
            }
            if (number_word(tokens[i + 1]).has_value()) continue;
            if (preset_from_word(tokens[i + 1]).has_value()) continue;
            throw usage_error{"prompt length: 'a' needs a unit or a preset\n"};
        }

        if (auto const value = number_word(token)) {
            if (i + 1 < tokens.size() && tokens[i + 1] == "of") ++i;
            if (i + 1 < tokens.size()) {
                if (auto const unit = unit_from_word(tokens[i + 1])) {
                    set_amount(*value, *unit);
                    ++i;
                    continue;
                }
            }
            set_amount(*value, length_unit::words);
            continue;
        }

        if (auto const preset = preset_from_word(token)) {
            set_preset(*preset);
            continue;
        }

        throw usage_error{"prompt length: unknown argument: " + std::string(token) + "\n"};
    }

    if (!have_target && requested_mode.has_value()) {
        throw usage_error{"prompt length: no target given; --exact/--max/--min need a number\n"};
    }
    // No target: a balanced answer is the default (`medium`, about one or two
    // paragraphs), so a bare `.length` still emits a useful instruction.
    if (!have_target) spec.preset = length_preset::medium;
    if (spec.preset != length_preset::none && requested_mode.has_value()) {
        throw usage_error{"prompt length: presets are approximate; give a number for an exact or bounded target\n"};
    }

    spec.mode = requested_mode.value_or(length_mode::rough);
    if (spec.amount.has_value()) {
        r.out = render_amount(*spec.amount, spec.unit, spec.mode);
    } else {
        r.out = std::string(preset_instruction(spec.preset)) + '\n';
    }
}

static void render_help_length(std::ostream& os) {
    os << R"EOF(Usage: prompt length [--exact|--rough] [--max|--min] <TARGET>
       prompt <name> ... .length <TARGET>
       some-command | prompt length <TARGET>

Set the answer length (or shape) the AI should target, roughly or exactly.

TARGET is a number with an optional unit -- 200, 200 words, 10 lines,
500 chars, 3 paragraphs -- or a preset. Length presets: tiny, short,
medium, long. Shape presets: one-liner, sentence, paragraph, bullets,
title, tweet, sms, email, letter, blog, article, report, essay, abstract,
outline, notes, caption, bio, commit, changelog, thread, tldr. Words is the
default unit; compact suffixes (200w, 10l, 3p, 500c, 300t), unit flags
(--words 200, --lines 10), thousands ("2k") and number words ("a paragraph",
"a few sentences") work too.

With no TARGET, `medium` (about one or two paragraphs) is used. Targets are
approximate by default ("about 200 words"); prefix the number with '=' or
pass --exact for a hard target ("exactly 200 words"); --max only caps the
answer and --min only extends it. Presets are always approximate.

Options:
  --exact, -e         Hard target ('=200' is the short form)
  --rough, -r         Approximate target, the default ('~200' is the short form)
  --max, --at-most    Cap the answer; never expand it
  --min, --at-least   Extend the answer; never cut it
  --words N           Answer length in words (the default unit)
  --lines N           Answer length in lines
  --sentences N       Answer length in sentences
  --paragraphs N      Answer length in paragraphs
  --chars N           Answer length in characters
  --tokens N          Answer length in tokens
  -h, --help          Show this help

Examples:
  prompt .fix src/main.cpp .length 200
  prompt .tse فولاد .tse.analyze .length long
  prompt .note "summarise this" .files report.md .length =5 sentences
  prompt .note "explain the design" .files a.cpp .length blog
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("length", "Set the answer length (or shape) the AI should target, roughly or exactly.",
                            &execute_length, &render_help_length, &complete_length);

} // namespace prompt::prompts
