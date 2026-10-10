#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace prompt::clipboard {

enum class backend { wayland, x11, kde, none };

backend detect_backend() noexcept;

std::string paste() noexcept;

bool copy(std::string_view data) noexcept;

bool clear() noexcept;

void init(std::chrono::seconds timeout = std::chrono::seconds(3)) noexcept;

} // namespace prompt::clipboard