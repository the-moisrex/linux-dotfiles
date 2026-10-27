#include "../sdk/errors.hpp"
#include "../sdk/extract.hpp"
#include "common.hpp"
#include "document.hpp"
#include "render.hpp"
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::extract {

namespace {

std::string lowered(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char const c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::size_t header_index(std::vector<std::string> const& headers, std::string_view name) {
    auto const wanted = lowered(name);
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (lowered(headers[i]) == wanted) return i;
    }
    return headers.size();
}

std::vector<match> run_table(std::string_view text, std::string_view qualifier) {
    auto blocks = parse_document(text, "");
    assign_indices(blocks);
    std::vector<block> tables;
    for (auto const& b : blocks) {
        if (b.kind == "table") tables.push_back(b);
    }

    auto const columns = split_commas(qualifier);
    if (!columns.empty()) {
        std::vector<block> kept;
        for (auto const& b : tables) {
            auto const headers = table_headers(b);
            bool all = true;
            for (auto const& column : columns) {
                if (header_index(headers, column) >= headers.size()) {
                    all = false;
                    break;
                }
            }
            if (all) kept.push_back(b);
        }
        if (kept.empty()) {
            std::vector<std::string> available;
            for (auto const& b : tables) {
                for (auto const& h : table_headers(b)) available.push_back(h);
            }
            std::string list;
            for (auto const& column : columns) {
                if (!list.empty()) list += ", ";
                list += column;
            }
            std::string have;
            for (auto const& column : available) {
                if (!have.empty()) have += ", ";
                have += column;
            }
            throw soft_error{
                std::format("no table has {} (available columns: {})\n", list, have.empty() ? "none" : have)};
        }
        tables = std::move(kept);
    }

    std::vector<match> out;
    for (auto const& b : tables) {
        auto m = match_from(b);
        m.rows.clear();
        auto const headers = table_headers(b);
        auto const rows = table_rows(b);

        std::vector<std::string> chosen;
        if (columns.empty()) {
            chosen = headers;
        } else {
            for (auto const& column : columns) {
                auto const index = header_index(headers, column);
                if (index < headers.size()) chosen.push_back(headers[index]);
            }
        }

        for (auto const& row : rows) {
            cells record;
            for (auto const& name : chosen) {
                auto const index = header_index(headers, name);
                record.emplace_back(name, index < row.size() ? row[index] : std::string{});
            }
            m.rows.push_back(std::move(record));
        }

        if (!columns.empty()) {
            std::string projected;
            for (std::size_t i = 0; i < chosen.size(); ++i) {
                if (i != 0) projected += '\t';
                projected += chosen[i];
            }
            for (auto const& row : rows) {
                projected += '\n';
                for (std::size_t i = 0; i < chosen.size(); ++i) {
                    if (i != 0) projected += '\t';
                    auto const index = header_index(headers, chosen[i]);
                    if (index < row.size()) projected += row[index];
                }
            }
            m.text = std::move(projected);
        }
        out.push_back(std::move(m));
    }
    return out;
}

contract_text contract_table(std::string_view qualifier) {
    auto const columns = split_commas(qualifier);
    if (columns.empty()) {
        return make_contract("Return exactly one Markdown table with a header row. "
                             "Put no prose before or after the table.\n",
                             "one Markdown table with a header row");
    }
    std::string list;
    for (auto const& column : columns) {
        if (!list.empty()) list += ", ";
        list += std::format("`{}`", column);
    }
    return make_contract(std::format("Return exactly one Markdown table with a header row and these columns: {}. "
                                     "Put no prose before or after the table.\n",
                                     list),
                         std::format("one Markdown table with a header row and these columns: {}", list));
}

} // namespace

bool const registered_table = register_extractor(extractor{
    .name = "table",
    .summary = "GFM tables; table:col1,col2 projects named columns",
    .run = &run_table,
    .contract = &contract_table,
});

} // namespace prompt::extract
