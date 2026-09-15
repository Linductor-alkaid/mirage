#pragma once

#include <filesystem>
#include <string>

#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Outcome of a text-file read; `content` is meaningful only when ok is true.
struct FileReadOutcome {
    bool ok = false;
    std::string content;
    ProviderError error; ///< meaningful only when ok is false
};

/// Read-side access to the local filesystem (design doc section 5).
///
/// The M1 first version is read-only and unscoped: path range constraints
/// land with M1-05 and the Desktop Permission gate (RULE-05) with M1-06, so
/// until then this provider must only be bound in development and test
/// topologies (DEC-008). Methods are synchronous and bounded by the file
/// size; callers decide the execution context.
class FilesystemProvider {
public:
    virtual ~FilesystemProvider() = default;

    /// Reads a regular file as text. Fails closed with a stable ProviderError
    /// for missing files, directories and unreadable paths; the content is
    /// never truncated silently.
    virtual FileReadOutcome read_text_file(const std::filesystem::path& path) = 0;
};

} // namespace mirage::desktop
