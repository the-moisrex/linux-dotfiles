#include "prompt/tse/tse_markdown.hpp"
#include <charconv>
#include <nlohmann/json.hpp>
#include <sstream>

namespace prompt::tse {

namespace {

// Python's csv.DictWriter with QUOTE_MINIMAL: a field is quoted only when it
// contains the delimiter, a quote, CR or LF; quotes are doubled.
void csv_row(std::string& out, std::vector<std::string> const& cols, nlohmann::json const& row) {
    bool first = true;
    for (auto const& col : cols) {
        if (!first) out += ',';
        first = false;
        std::string value;
        if (row.is_object() && row.contains(col)) {
            auto const& cell = row.at(col);
            if (cell.is_string()) {
                value = cell.get<std::string>();
            } else if (cell.is_null()) {
                value = "";
            } else {
                value = cell.dump();
            }
        }
        bool needs_quotes = value.find_first_of(",\"\r\n") != std::string::npos;
        if (needs_quotes) {
            out += '"';
            for (char c : value) {
                if (c == '"') out += '"'; // doubled
                out += c;
            }
            out += '"';
        } else {
            out += value;
        }
    }
    out += '\n';
}

std::string csv_table(std::vector<std::string> const& cols, nlohmann::json const& rows) {
    std::string out;
    csv_row(out, cols, nlohmann::json::object()); // header
    if (rows.is_array()) {
        for (auto const& row : rows) csv_row(out, cols, row);
    }
    return out;
}

std::string rstrip(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

std::string number4(double v) {
    std::ostringstream os;
    os.precision(4);
    os << std::fixed << v;
    return os.str();
}

std::string number0(double v) {
    std::ostringstream os;
    os.precision(0);
    os << std::fixed << v;
    return os.str();
}

// Python's f"{value}" for a float: shortest round-trip repr (never scientific
// for these price-sized values), e.g. 12345.0 -> "12345.0", 1.5 -> "1.5".
std::string py_float_str(double v) {
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, res.ptr);
}

} // namespace

std::string render_markdown(nlohmann::json const& data) {
    std::vector<std::string> lines;
    lines.push_back("Fetched at (Asia/Tehran): " + data.value("fetched_at", std::string{}));
    lines.push_back(
        "Market session open by schedule: " + std::string(data.value("market_session_open", false) ? "True" : "False") +
        " (does not guarantee an instrument is trading)");
    lines.push_back("Prices and traded value are in rial, not toman. Missing values are unknown, not zero.");

    // Each section: blank line, "## <heading>", the JSON dump in a json fence.
    struct section {
        char const* heading;
        char const* key;
    };
    static constexpr section sections[] = {
        {"Symbol / نماد", "instrument"},
        {"Live quote / قیمت و وضعیت", "quote"},
        {"Order book / صف خرید و فروش", "order_book"},
        {"Money flow / حقیقی و حقوقی", "client_type"},
        {"Market benchmarks / شاخص‌های بازار", "indexes"},
        {"Market context / دلار، اقتصاد و عمق بازار", "market_context"},
        {"Fundamentals / اطلاعات بنیادی", "fundamentals"},
        {"Codal / اطلاعیه‌ها و صورت‌های مالی", "codal"},
    };
    for (auto const& [heading, key] : sections) {
        if (data.contains(key) && !data.at(key).is_null()) {
            lines.emplace_back("");
            lines.push_back(std::string("## ") + heading);
            lines.emplace_back("```json");
            lines.push_back(data.at(key).dump());
            lines.emplace_back("```");
        }
    }

    if (data.contains("history") && !data.at("history").is_null()) {
        auto const& hist = data.at("history");
        if (hist.is_object() && hist.contains("unadjusted")) {
            // Adjusted snapshot: the adjusted rows already carry the raw
            // columns plus the adj_* ones; otherwise the unadjusted rows.
            auto a_rows = hist.contains("adjusted") ? hist.at("adjusted") : nlohmann::json::array();
            std::vector<std::string> cols;
            std::string heading;
            if (!a_rows.empty()) {
                cols = {"date",          "first_rial",     "high_rial",        "low_rial",      "closing_rial",
                        "last_rial",     "adj_first_rial", "adj_high_rial",    "adj_low_rial",  "adj_closing_rial",
                        "adj_last_rial", "adj_factor",     "corporate_action", "volume_shares", "value_rial",
                        "trade_count"};
                heading = "## Price history / تاریخچه قیمت";
            } else {
                cols = {"date",      "first_rial",    "high_rial",  "low_rial",   "closing_rial",
                        "last_rial", "volume_shares", "value_rial", "trade_count"};
                heading = "## Price history / تاریخچه قیمت (بدون تعدیل)";
            }
            lines.emplace_back("");
            lines.push_back(heading);
            lines.emplace_back("```csv");
            lines.push_back(rstrip(csv_table(cols, a_rows.empty() ? hist.at("unadjusted") : a_rows)));
            lines.emplace_back("```");

            // Corporate action events table.
            nlohmann::json pa;
            nlohmann::json sc;
            if (hist.contains("adjustment_events")) {
                auto const& events = hist.at("adjustment_events");
                pa = events.contains("price_adjustments") ? events.at("price_adjustments") : nlohmann::json::array();
                sc = events.contains("share_changes") ? events.at("share_changes") : nlohmann::json::array();
            }
            if (!pa.empty() || !sc.empty()) {
                std::string table;
                csv_row(table, {"date", "type", "details"}, nlohmann::json::object());
                for (auto const& a : pa) {
                    nlohmann::json row;
                    row["date"] = a.value("date", std::string{});
                    row["type"] = "price_adjustment";
                    row["details"] = "adj_ratio=" + number4(a.value("adj_ratio", 0.0)) +
                                     " (pClosing_adj=" + py_float_str(a.value("pClosing_adj", 0.0)) +
                                     ", pClosing_raw=" + py_float_str(a.value("pClosing_raw", 0.0)) + ")";
                    csv_row(table, {"date", "type", "details"}, row);
                }
                for (auto const& s : sc) {
                    nlohmann::json row;
                    row["date"] = s.value("date", std::string{});
                    row["type"] = "share_change";
                    row["details"] = "ratio=" + number4(s.value("ratio", 0.0)) +
                                     " (old=" + number0(s.value("old_shares", 0.0)) +
                                     ", new=" + number0(s.value("new_shares", 0.0)) + ")";
                    csv_row(table, {"date", "type", "details"}, row);
                }
                lines.emplace_back("");
                lines.push_back("## Corporate action events / رویدادهای اقدامات شرکتی");
                lines.emplace_back("```csv");
                lines.push_back(rstrip(table));
                lines.emplace_back("```");
            }
        } else {
            lines.emplace_back("");
            lines.push_back("## Price history / تاریخچه قیمت (unadjusted)");
            lines.emplace_back("```csv");
            std::vector<std::string> cols = {"date",      "first_rial",    "high_rial",  "low_rial",   "closing_rial",
                                             "last_rial", "volume_shares", "value_rial", "trade_count"};
            lines.push_back(rstrip(csv_table(cols, hist)));
            lines.emplace_back("```");
        }
    }

    if (data.contains("errors") && !data.at("errors").empty()) {
        lines.emplace_back("");
        lines.push_back("## Missing or failed sources / داده‌های ناموجود");
        lines.emplace_back("```json");
        lines.push_back(data.at("errors").dump());
        lines.emplace_back("```");
    }

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

} // namespace prompt::tse
