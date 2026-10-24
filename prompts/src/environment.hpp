#pragma once

#include "sdk/stage.hpp"

namespace prompt {

// Build the process-wide environment once, at startup: where the repository is,
// where this executable lives, and the clipboard/file-picker services a prompt
// may call. Built here rather than in the dispatcher so main.cpp keeps chain
// mechanics and the "what does this pasted text identify" policy stays next to
// the clipboard it reads (see clipboard::detect_identifier).
environment make_environment();

} // namespace prompt
