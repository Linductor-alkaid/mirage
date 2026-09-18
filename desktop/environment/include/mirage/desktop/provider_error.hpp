#pragma once

#include <string>

namespace mirage::desktop {

/// Error surface shared by the DesktopEnvironment providers (design doc
/// section 5). `code` is a stable identifier the product layer can match on;
/// `message` is safe for logs and UI and never echoes unredacted file content
/// or command output. Shared vocabulary (providers may add specific codes,
/// e.g. "file_too_large"): "invalid_argument", "not_found",
/// "permission_denied", "deadline_exceeded", "io_error", "cancelled",
/// "unsupported_platform", "unsupported_content", "unsupported_window",
/// "already_running", "result_too_large" (an enumeration or table exceeded
/// its declared budget and is refused, never silently truncated).
struct ProviderError {
    std::string code;
    std::string message;
};

} // namespace mirage::desktop
