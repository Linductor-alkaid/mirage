#pragma once

#include <string>

namespace mirage::platform::pet_window {
// Called only on EUI's owning UI thread before its first visible frame.
// Public signatures intentionally contain no GLFW or OS types.
bool prepare(std::string &diagnostic);
void request_close();
// Parent-owned graceful exit request. Only the pet process installs this hook.
bool install_exit_request(std::string &diagnostic);
bool exit_requested();
} // namespace mirage::platform::pet_window
