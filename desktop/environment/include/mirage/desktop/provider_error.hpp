#pragma once

#include <string>

namespace mirage::desktop {

/// Error surface shared by the DesktopEnvironment providers (design doc
/// section 5). `code` is a stable identifier the product layer can match on
/// ("invalid_argument", "not_found", "permission_denied", "deadline_exceeded",
/// "io_error", "unsupported_platform", ...); `message` is safe for logs and
/// UI and never echoes unredacted file content or command output.
struct ProviderError {
    std::string code;
    std::string message;
};

} // namespace mirage::desktop
