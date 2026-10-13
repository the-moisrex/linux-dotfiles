#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace prompt::clipboard {

enum class backend { wayland, x11, kde, none };

backend detect_backend();

std::string paste();

bool copy(std::string_view data);

bool clear();

void init(std::chrono::seconds timeout = std::chrono::seconds(3));

} // namespace prompt::clipboard