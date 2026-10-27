#include "render.hpp"
#include "../core/process.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::extract {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return text;
}

std::string rstrip_newlines(std::string_view text) {
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return std::string(text);
}

std::string heading_of(match const& m) {
    std::string out;
    for (auto const& [level, text] : m.heading_path) {
        (void)level;
        if (!out.empty()) out += " > ";
        out += text;
    }
    if (m.kind == "section" && !m.title.empty()) {
        if (!out.empty()) out += " > ";
        out += m.title;
    }
    return out;
}

std::string display_lang(match const& m, bool guess) {
    if (!m.lang.empty()) return m.lang;
    if (guess && m.kind == "code") {
        auto const guessed = infer_family(m.source);
        if (!guessed.empty()) return guessed + "?";
    }
    return {};
}

std::string drop_first_line(std::string_view text) {
    auto const newline = text.find('\n');
    return newline == std::string_view::npos ? std::string{} : std::string(text.substr(newline + 1));
}

std::string join_blocks(std::vector<std::string> const& parts) {
    std::string out;
    for (auto const& part : parts) {
        if (!out.empty()) out += "\n\n";
        out += part;
    }
    if (!out.empty()) out += '\n';
    return out;
}

std::optional<std::filesystem::path> which(std::string_view program) {
    char const* path_env = std::getenv("PATH");
    if (path_env == nullptr) return std::nullopt;
    std::string_view paths(path_env);
    std::size_t begin = 0;
    while (begin <= paths.size()) {
        auto const end = paths.find(':', begin);
        auto dir = paths.substr(begin, (end == std::string_view::npos ? paths.size() : end) - begin);
        auto candidate = std::filesystem::path(dir.empty() ? "." : std::string(dir)) / program;
        if (std::filesystem::exists(candidate)) return candidate;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return std::nullopt;
}

} // namespace

match match_from(block const& b) {
    match m;
    m.source = b.body;
    m.kind = b.kind;
    m.lang = b.lang;
    m.lang_raw = b.lang_raw;
    m.info = b.info_raw;
    m.fence = b.fence;
    m.title = b.title;
    m.id = b.ident;
    m.file = b.file;
    m.program = b.program;
    m.before = b.before;
    m.after = b.after;
    m.line = b.start_line;
    m.end_line = b.end_line;
    m.index = b.index;
    m.heading_level = b.heading_level;
    m.closed = b.closed;
    m.heading_path = b.heading_path;
    if (b.kind == "section") {
        std::string heading =
            b.heading_level > 0 ? std::string(static_cast<std::size_t>(b.heading_level), '#') + " " + b.title : b.title;
        m.text = b.body.empty() ? heading : heading + "\n\n" + b.body;
    } else if (b.kind == "command") {
        m.text = command_line(b);
    } else {
        m.text = b.body;
    }
    if (b.kind == "table") {
        auto const headers = table_headers(b);
        for (auto const& row : table_rows(b)) {
            cells record;
            for (std::size_t i = 0; i < headers.size(); ++i) {
                record.emplace_back(headers[i], i < row.size() ? row[i] : std::string{});
            }
            m.rows.push_back(std::move(record));
        }
    }
    return m;
}

std::string render_block(match const& m, render_options const& options) {
    std::vector<std::string> parts;
    if (options.with_heading) {
        for (auto const& [level, text] : m.heading_path) {
            parts.push_back(std::string(static_cast<std::size_t>(level), '#') + " " + text);
        }
        if (m.kind == "section" && m.heading_level > 0) {
            parts.push_back(std::string(static_cast<std::size_t>(m.heading_level), '#') + " " + m.title);
        }
    }
    if (options.with_intro && !m.before.empty()) parts.push_back(m.before);

    if (m.kind == "frontmatter" && options.fence) {
        std::string const mark = m.info.empty() ? "---" : std::string(trim(m.info));
        parts.push_back(mark + "\n" + m.source + "\n" + mark);
    } else if (m.kind == "section" && options.fence) {
        if (!options.with_heading) {
            parts.push_back(std::string(static_cast<std::size_t>(m.heading_level > 0 ? m.heading_level : 1), '#') +
                            " " + m.title);
        }
        parts.push_back(m.source);
    } else if (m.kind == "command") {
        parts.push_back(m.text);
    } else if (options.fence && m.kind != "table") {
        std::string const marker = m.fence.empty() ? "```" : m.fence;
        parts.push_back(marker + rstrip_newlines(m.info) + "\n" + m.source + "\n" + marker);
    } else {
        parts.push_back(m.source);
    }

    std::string out;
    for (auto const& part : parts) {
        if (part.empty()) continue;
        if (!out.empty()) out += "\n\n";
        out += part;
    }
    return std::string(trim(out));
}

std::string render_many(std::span<match const> matches, render_options const& options) {
    std::string out;
    for (auto const& m : matches) {
        auto const rendered = rstrip_newlines(render_block(m, options));
        if (rendered.empty()) continue;
        if (!out.empty()) out += "\n\n";
        out += rendered;
    }
    if (!out.empty()) out += '\n';
    return out;
}

std::string render_selection(std::span<match const> matches, selection const& sel, bool no_header,
                             render_options const& options) {
    if (matches.empty()) return {};

    std::string const first = sel.steps.empty() ? std::string{} : sel.steps.front().name;
    std::string const qualifier = sel.steps.empty() ? std::string{} : sel.steps.front().qualifier;

    if (first == "table" && !qualifier.empty()) {
        std::vector<std::string> parts;
        for (auto const& m : matches) {
            auto text = no_header ? drop_first_line(m.text) : m.text;
            if (!text.empty()) parts.push_back(text);
        }
        return join_blocks(parts);
    }
    if (first == "section") {
        std::vector<std::string> parts;
        for (auto const& m : matches) {
            std::string heading;
            if (m.kind == "section" && m.heading_level > 0) {
                heading = std::string(static_cast<std::size_t>(m.heading_level), '#') + " " + m.title;
            }
            auto const body = m.kind == "section" ? m.source : render_block(m, options);
            if (heading.empty()) parts.push_back(body);
            else if (body.empty())
                parts.push_back(heading);
            else
                parts.push_back(std::format("{}\n\n{}", heading, body));
        }
        return join_blocks(parts);
    }
    if (first == "any" || (!matches.empty() && matches.front().kind == "command")) {
        std::vector<std::string> parts;
        for (auto const& m : matches) {
            if (m.kind == "section" && m.heading_level > 0) {
                parts.push_back(std::string(static_cast<std::size_t>(m.heading_level), '#') + " " + m.title + "\n\n" +
                                m.source);
            } else if (m.kind == "command") {
                parts.push_back(m.text);
            } else {
                parts.push_back(m.source);
            }
        }
        return join_blocks(parts);
    }
    return render_many(matches, options);
}

std::string join_texts(std::span<match const> matches) {
    std::string out;
    for (auto const& m : matches) {
        if (m.text.empty()) continue;
        if (!out.empty()) out += "\n\n";
        out += m.text;
    }
    if (!out.empty()) out += '\n';
    return out;
}

std::string render_list(std::span<match const> matches, bool guess) {
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"IDX", "KIND", "LANG", "LINES", "HEADING", "LABEL", "SOURCE"});
    for (auto const& m : matches) {
        std::string lang = display_lang(m, guess);
        if (lang.empty()) lang = m.kind == "code" ? "?" : "-";
        rows.push_back({
            std::to_string(m.index),
            m.kind,
            lang,
            std::to_string(count_lines(m.source)),
            first_line(heading_of(m), 36),
            first_line(m.before, 44),
            std::format("{}:{}", m.file, m.line),
        });
    }
    std::vector<std::size_t> widths(rows.front().size(), 0);
    for (auto const& row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) widths[i] = std::max(widths[i], row[i].size());
    }
    std::string out;
    for (auto const& row : rows) {
        std::string line;
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i != 0) line += "  ";
            line += row[i];
            if (i + 1 < row.size()) line += std::string(widths[i] - row[i].size(), ' ');
        }
        out += rstrip_newlines(line);
        out.push_back('\n');
    }
    return out;
}

std::vector<match> pick(std::vector<match> const& matches, bool guess) {
    auto const executable = which("fzf");
    if (!executable.has_value()) throw usage_error{"--pick needs fzf on PATH\n"};
    std::string rows;
    for (std::size_t sequence = 0; sequence < matches.size(); ++sequence) {
        auto const& m = matches[sequence];
        auto const lang = display_lang(m, guess);
        rows +=
            std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}:{}\n", sequence, m.index, m.kind, lang.empty() ? "?" : lang,
                        count_lines(m.source), first_line(heading_of(m), 60), first_line(m.before, 60), m.file, m.line);
    }
    auto const result = process::command{std::vector<std::string>{
                                             executable->string(),
                                             "--delimiter=\t",
                                             "--with-nth=2..",
                                             "--prompt=block> ",
                                             "--height=60%",
                                         }}
                            .stdin_data(rows)
                            .capture_stderr()
                            .run();
    if (result.exit_code != 0 || result.stdout_data.find_first_not_of(" \t\n") == std::string::npos) {
        throw soft_error{"selection cancelled\n"};
    }
    auto const first = result.stdout_data.substr(0, result.stdout_data.find('\t'));
    return {matches.at(static_cast<std::size_t>(std::stoul(first)))};
}

nlohmann::ordered_json block_record(match const& m, bool guess) {
    nlohmann::ordered_json heading_path = nlohmann::ordered_json::array();
    for (auto const& [level, text] : m.heading_path) heading_path.push_back({{"level", level}, {"text", text}});
    return nlohmann::ordered_json{
        {"file", m.file},
        {"kind", m.kind},
        {"index", m.index},
        {"lang", display_lang(m, guess)},
        {"lang_raw", m.lang_raw},
        {"info", std::string(trim(m.info))},
        {"title", m.title},
        {"id", m.id},
        {"heading", heading_of(m)},
        {"heading_path", heading_path},
        {"before", m.before},
        {"after", m.after},
        {"line", m.line},
        {"end_line", m.end_line},
        {"lines", count_lines(m.source)},
        {"closed", m.closed},
        {"body", m.source},
        {"program", m.program},
    };
}

nlohmann::ordered_json items_to_json(std::span<match const> matches, bool guess) {
    nlohmann::ordered_json items = nlohmann::ordered_json::array();
    for (auto const& m : matches) {
        if (m.kind == "table") {
            for (std::size_t row_index = 0; row_index < m.rows.size(); ++row_index) {
                nlohmann::ordered_json record = nlohmann::ordered_json::object();
                std::string text;
                for (auto const& [column, value] : m.rows[row_index]) {
                    record[column] = value;
                    if (!text.empty()) text += '\t';
                    text += value;
                }
                record["item"] = text;
                record["body"] = text;
                record["_row"] = row_index + 1;
                record["_table"] = m.index;
                items.push_back(std::move(record));
            }
            continue;
        }
        auto record = block_record(m, guess);
        std::string const text = m.kind == "command" ? m.text : m.source;
        if (!record.contains("item")) record["item"] = text;
        if (!record.contains("body")) record["body"] = text;
        if (m.kind == "command") {
            if (!record.contains("command")) record["command"] = text;
            if (!record.contains("program")) record["program"] = m.program;
        }
        items.push_back(std::move(record));
    }
    return items;
}

} // namespace prompt::extract
