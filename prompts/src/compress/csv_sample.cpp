#include "../core/strings.hpp"
#include "compress.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::compress {

namespace {

std::string lowercase(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

std::vector<std::string> csv_lines(std::string_view text) {
    std::vector<std::string> lines = text::split_lines(text, false);
    for (auto& line : lines) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
    }
    return lines;
}

// Fields in a CSV record: delimiters outside double quotes, with `""` an
// escaped quote. Enough for the delimiter guess; a full CSV parser is not the
// point of a compressor.
std::size_t count_fields(std::string_view line, char delim) {
    std::size_t fields = 1;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char const c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                ++i;
                continue;
            }
            quoted = !quoted;
        } else if (c == delim && !quoted) {
            ++fields;
        }
    }
    return fields;
}

// The delimiter whose field count is at least two and holds for >= 80% of the
// lines (so a stray comma in prose does not turn a paragraph into a table).
bool detect_delimiter(std::vector<std::string> const& lines, char& delim, std::size_t& columns) {
    static constexpr std::array<char, 4> candidates = {',', '\t', ';', '|'};
    for (char const candidate : candidates) {
        std::size_t const cols = count_fields(lines.front(), candidate);
        if (cols < 2) continue;
        std::size_t consistent = 0;
        for (auto const& line : lines) {
            if (count_fields(line, candidate) == cols) ++consistent;
        }
        if (consistent * 10 >= lines.size() * 8) {
            delim = candidate;
            columns = cols;
            return true;
        }
    }
    return false;
}

bool csv_extension(std::string_view path) {
    std::size_t const dot = path.rfind('.');
    if (dot == std::string_view::npos) return false;
    std::string const ext = lowercase(std::string(path.substr(dot + 1)));
    return ext == "csv" || ext == "tsv";
}

bool applies_csv(region const& r, [[maybe_unused]] signals const& s) {
    if (r.lang == "csv" || r.lang == "tsv") return true;
    if (csv_extension(r.path)) return true;
    // Only sniff untagged/text regions: a `cpp` fence with commas in it is not a
    // table, and skipping it keeps `applies` cheap.
    if (!r.lang.empty() && r.lang != "text") return false;
    std::vector<std::string> const lines = csv_lines(r.text);
    if (lines.size() < 5) return false;
    char delim = 0;
    std::size_t columns = 0;
    return detect_delimiter(lines, delim, columns);
}

outcome compress_csv(region const& r, signals const& s) {
    std::size_t const total = count_lines(r.text);

    std::vector<std::string> const lines = csv_lines(r.text);
    if (lines.size() < 8) return {};

    char delim = 0;
    std::size_t columns = 0;
    if (!detect_delimiter(lines, delim, columns)) return {};

    std::size_t sample = 1;
    if (s.level <= 1) {
        sample = 5;
    } else if (s.level == 2) {
        sample = 3;
    }
    std::size_t const rows = lines.size() - 1; // the first line is the header
    if (rows <= sample) return {};

    outcome o;
    o.total = total;
    o.markdown = lines.front();
    o.markdown.push_back('\n');
    for (std::size_t i = 1; i <= sample; ++i) {
        o.markdown += lines[i];
        o.markdown.push_back('\n');
    }
    o.markdown += "…\n";
    o.kept = count_lines(o.markdown);
    if (o.kept >= o.total) return {};

    o.legend = "csv.sample: " + plural(columns, "column", "columns") + ", " + plural(rows, "row", "rows") + "; kept " +
               plural(sample, "sample row", "sample rows") + "." + expand_hint(r.path);
    o.changed = true;
    return o;
}

} // namespace

bool const registered_csv = register_compressor(compressor{
    .name = "csv.sample",
    .summary = "Replace a large CSV/TSV block with its header and a few sample rows.",
    .applies = &applies_csv,
    .compress = &compress_csv,
    .priority = 10,
});

} // namespace prompt::compress
