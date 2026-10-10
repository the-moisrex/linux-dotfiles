#include "prompt/tse/tse_python.hpp"
#include "prompt/core/python.hpp"
#include <nlohmann/json.hpp>

namespace prompt::tse {

collected_data parse_collected_data(nlohmann::json const& j) noexcept {
    collected_data data;
    data.raw_json = j;
    data.fetched_at = j.value("fetched_at", "");
    data.market_session_open = j.value("market_session_open", false);
    data.instrument = j.value("instrument", nlohmann::json::object());
    data.quote = j.value("quote", nlohmann::json::object());
    data.order_book = j.value("order_book", nlohmann::json::object());
    data.client_type = j.value("client_type", nlohmann::json::object());
    data.indexes = j.value("indexes", nlohmann::json::object());
    data.market_context = j.value("market_context", nlohmann::json::object());
    data.history = j.value("history", nlohmann::json::object());
    data.fundamentals = j.value("fundamentals", nlohmann::json::object());
    data.codal = j.value("codal", nlohmann::json::object());
    data.errors = j.value("errors", nlohmann::json::object());
    return data;
}

std::expected<collected_data, std::string> collect(
    std::string_view symbol,
    int days,
    int top,
    bool with_codal,
    bool adjusted) noexcept {
    
    std::string days_str = std::to_string(days);
    std::string top_str = std::to_string(top);
    std::string codal_str = with_codal ? "true" : "false";
    std::string adjusted_str = adjusted ? "true" : "false";
    
    std::array<std::string_view, 5> collect_args = {symbol, days_str, top_str, codal_str, adjusted_str};
    auto result = python::call("bin.tse", "collect_embed", collect_args);
    
    if (!result) {
        return std::unexpected(result.error());
    }
    
    auto json = nlohmann::json::parse(*result);
    return parse_collected_data(json);
}

std::expected<std::string, std::string> render_markdown(nlohmann::json const& data) noexcept {
    std::string data_json = data.dump();
    std::array<std::string_view, 1> markdown_args = {data_json};
    auto result = python::call("bin.tse", "markdown_embed", markdown_args);
    
    if (!result) {
        return std::unexpected(result.error());
    }
    
    // call() JSON-encodes the return value; markdown returns a str, so the
    // payload is a JSON string literal — decode it back to raw text.
    auto parsed = nlohmann::json::parse(*result, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_string()) {
        return parsed.get<std::string>();
    }
    return *result;
}

} // namespace prompt::tse