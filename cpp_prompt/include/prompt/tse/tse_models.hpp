#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace prompt::tse {

struct collected_data {
    nlohmann::json raw_json;
    std::string fetched_at;
    bool market_session_open = false;
    nlohmann::json instrument;
    nlohmann::json quote;
    nlohmann::json order_book;
    nlohmann::json client_type;
    nlohmann::json indexes;
    nlohmann::json market_context;
    nlohmann::json history;
    nlohmann::json fundamentals;
    nlohmann::json codal;
    nlohmann::json errors;
};

collected_data parse_collected_data(nlohmann::json const& j) noexcept;

} // namespace prompt::tse