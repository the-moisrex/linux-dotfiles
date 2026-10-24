#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace prompt::clipboard {

enum class backend : std::uint8_t { wayland, x11, kde, none };

backend detect_backend();

std::string paste();

// The instrument a pasted text identifies, or nullopt when it identifies
// nothing: an easytrader/TSETMC/Codal URL, an ISIN, an insCode (15+ digits), or
// a short symbol written without Latin letters. CR/LF are stripped first, so a
// terminal copy works. Pure, so the policy is testable without a clipboard.
std::optional<std::string> detect_identifier(std::string_view pasted);

bool copy(std::string_view data);

bool clear();

void init(std::chrono::seconds timeout = std::chrono::seconds(3));

} // namespace prompt::clipboard