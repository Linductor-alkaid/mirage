#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Budget for one text-file read (RULE-07: every provider operation carries
/// a capacity or budget cap). The read is refused — never silently
/// truncated — when the file exceeds the cap.
struct FileReadLimits {
    /// Upper bound for the file size in bytes. Must be positive.
    std::size_t max_bytes = 1u << 20;
};

/// Outcome of a text-file read; `content` is meaningful only when ok is true.
struct FileReadOutcome {
    bool ok = false;
    std::string content;
    ProviderError error; ///< meaningful only when ok is false
};

/// Read-side access to the local filesystem (design doc section 5).
///
/// M1-05 hardening: implementations enforce a configured read scope — reads
/// outside the scope fail closed with "permission_denied" before any content
/// is touched — and the FileReadLimits budget caps how much one read may
/// return. The Desktop Permission gate (RULE-05) lands with M1-06. Methods
/// are synchronous and bounded by their limits; callers decide the execution
/// context.
class FilesystemProvider {
public:
    virtual ~FilesystemProvider() = default;

    /// Reads a regular file as text. Fails closed with a stable ProviderError
    /// for out-of-scope paths, missing files, directories, unreadable paths,
    /// files beyond the read budget and cancellation; the content is never
    /// truncated silently.
    virtual FileReadOutcome read_text_file(const std::filesystem::path& path,
                                           const FileReadLimits& limits,
                                           const CancelToken& cancel) = 0;

    /// Same read under default limits and without cancellation.
    FileReadOutcome read_text_file(const std::filesystem::path& path) {
        return read_text_file(path, FileReadLimits{}, CancelToken{});
    }
};

} // namespace mirage::desktop
