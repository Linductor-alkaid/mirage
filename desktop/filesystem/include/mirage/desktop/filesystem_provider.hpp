#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

/// Version facts of one observed regular file (DEC-095, M10-03). The
/// identity pair pins the real file (not the path), so a rename/replace
/// between observation and commit is detected; `content_digest` is the
/// backend's bounded full-content digest, empty only when the file exceeded
/// the modify budget and no write/edit may target it anyway. The digest
/// algorithm is backend-internal: version tokens are compared only by the
/// backend that produced them.
struct FileVersion {
    std::uint64_t device = 0;
    std::uint64_t file_index = 0;
    std::uint64_t size = 0;
    std::string content_digest; ///< hex; backend-defined, empty when undigested

    [[nodiscard]] bool digest_available() const { return !content_digest.empty(); }

    [[nodiscard]] bool same_identity_as(const FileVersion &other) const {
        return device == other.device && file_index == other.file_index;
    }

    [[nodiscard]] bool operator==(const FileVersion &) const = default;
};

/// One raw byte range of a file the caller has actually seen (DEC-095:
/// fragment observations authorize only their own replacement, never a
/// whole-file overwrite).
struct ObservedByteRange {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

/// Host-held observation facts presented to authorize a replace/edit
/// (DEC-095 decision 3). The provider re-verifies identity, size and digest
/// against the current file before any commit; callers cannot forge "seen"
/// state past a file change, and eviction/restart simply re-reads.
struct FileObservation {
    FileVersion version;
    /// True when the whole current content was observed.
    bool full = false;
    /// Raw byte ranges observed (ascending, non-overlapping) when not full.
    std::vector<ObservedByteRange> ranges;

    /// True when every byte of [offset, offset+length) is covered.
    [[nodiscard]] bool covers(std::uint64_t offset, std::uint64_t length) const {
        if (full) {
            return true;
        }
        const std::uint64_t end = offset + length;
        for (const auto &range : ranges) {
            if (range.offset <= offset && end <= range.offset + range.length) {
                return true;
            }
        }
        return false;
    }
};

/// Budgets for one windowed text read (DEC-095 budget table, frozen M10-02).
struct TextWindowLimits {
    /// Maximum lines returned per window; the first limit reached ends the
    /// window with truncated facts, never a silent cut.
    std::size_t max_lines = 2000;
    /// Maximum rendered bytes (lines + line-number-independent view data)
    /// per window.
    std::size_t max_bytes = 50u << 10;
    /// Byte budget for the underlying scan that serves one window (position
    /// seek + window materialization). Exceeding it fails with
    /// "scan_budget_exceeded" instead of growing work without bound.
    std::size_t max_scan_bytes = 16u << 20;
    /// Wall-clock budget for the underlying scan; clamped callers keep their
    /// own operation deadline authority.
    std::chrono::milliseconds max_scan_time{5000};
};

/// Budgets for one write/edit (DEC-095: content and result each bounded;
/// oversize content is refused before any side effect).
struct FileModifyLimits {
    /// Upper bound for the original and the resulting content in bytes.
    /// Files beyond it are paginated read-only; writes and edits are
    /// refused with "file_too_large".
    std::size_t max_content_bytes = 1u << 20;
};

/// Result of one windowed text read (DEC-095 contract table). Lines are the
/// raw file bytes between terminators: a "\r\n" terminator contributes its
/// "\r" to the preceding line's view so byte facts stay reconstructable, and
/// a BOM stays in the first line. Line numbers are 1-based.
struct TextWindowOutcome {
    bool ok = false;
    ProviderError error; ///< meaningful only when ok is false
    /// 1-based line number of lines.front() (= requested offset + 1).
    std::uint64_t first_line = 0;
    std::vector<std::string> lines;
    /// True when the window reaches end of file.
    bool eof = false;
    /// Line-skip value for the continuation read (offset + lines.size());
    /// meaningful when !eof, and equals offset + size at eof.
    std::uint64_t next_offset = 0;
    /// Total line count, set only when eof was actually reached.
    std::optional<std::uint64_t> total_lines;
    /// Version facts of the observed file; the digest is available only for
    /// files within the modify budget (writes need it, oversized files are
    /// paginated read-only).
    FileVersion version;
    bool has_bom = false;
    bool has_crlf = false;
    /// Raw byte range of the window the caller may record as observed.
    ObservedByteRange observed_range;
};

/// Result of one file write (create or observed replace).
struct FileWriteOutcome {
    bool ok = false;
    ProviderError error; ///< meaningful only when ok is false
    bool created = false;  ///< new file was committed
    bool replaced = false; ///< observed file was atomically replaced
    /// True when the commit point was passed. A cancellation after the
    /// commit keeps applied=true: the write happened and must be reported.
    bool applied = false;
    std::uint64_t bytes_written = 0;
    FileVersion version; ///< version facts of the committed content
};

/// Result of one literal edit.
struct FileEditOutcome {
    bool ok = false;
    ProviderError error; ///< meaningful only when ok is false
    /// False for a well-formed no-op request (old == new, or zero-length
    /// match); the file is untouched and the caller must not dress the call
    /// up as a modification.
    bool applied = false;
    std::uint64_t replacements = 0;
    FileVersion version; ///< version facts of the committed content
};

/// What a FilesystemProvider implementation actually delivers (DEC-095:
/// capability honesty). The legacy whole-file read is always present; every
/// M10 capability defaults to false and backends opt in explicitly, so a
/// provider without write support is discoverable instead of failing at
/// first use.
struct FilesystemCapabilities {
    /// Bounded line-window reads with version facts.
    bool windowed_read = false;
    /// create-if-absent and observed replace via temp-file commit.
    bool write = false;
    /// Literal single-match edit with observation enforcement.
    bool edit = false;
};

/// Read-side access to the local filesystem (design doc section 5).
///
/// M1-05 hardening: implementations enforce a configured read scope — reads
/// outside the scope fail closed with "permission_denied" before any content
/// is touched — and the FileReadLimits budget caps how much one read may
/// return. The provider itself stays permission-agnostic: callers judge the
/// filesystem.read capability through the runtime permission gate
/// (RULE-05, DEC-010) before invoking. Methods are synchronous and bounded
/// by their limits; callers decide the execution context.
///
/// M10-03 (DEC-095): the M10 surface is additive — windowed reads with
/// version facts and create/replace/edit commits. Every new method defaults
/// to a fail-closed "unsupported_platform" result so existing implementors
/// keep compiling and behaving exactly as before; capabilities() reports
/// what is actually available and callers must not register tools a
/// provider cannot deliver.
class FilesystemProvider {
  public:
    virtual ~FilesystemProvider() = default;

    /// Reads a regular file as text. Fails closed with a stable ProviderError
    /// for out-of-scope paths, missing files, directories, unreadable paths,
    /// files beyond the read budget and cancellation; the content is never
    /// truncated silently.
    virtual FileReadOutcome read_text_file(const std::filesystem::path &path,
                                           const FileReadLimits &limits,
                                           const CancelToken &cancel) = 0;

    /// Same read under default limits and without cancellation.
    FileReadOutcome read_text_file(const std::filesystem::path &path) {
        return read_text_file(path, FileReadLimits{}, CancelToken{});
    }

    /// Reports the M10 capabilities this implementation provides.
    [[nodiscard]] virtual FilesystemCapabilities capabilities() const { return {}; }

    /// Reads one bounded window of a UTF-8 text file, 1-based line numbers,
    /// version facts of the observed file and the raw byte range the caller
    /// may record as observed. A `skip_lines` beyond the line count is an
    /// invalid_argument, not an empty window. Content that is not valid
    /// UTF-8 (including NUL bytes) fails with "unsupported_encoding";
    /// directories and special files with "not_regular_file"; files beyond
    /// the modify budget stay readable but report no digest.
    virtual TextWindowOutcome read_text_window(const std::filesystem::path &path,
                                               std::uint64_t skip_lines,
                                               const TextWindowLimits &limits,
                                               const CancelToken &cancel) {
        (void)path;
        (void)skip_lines;
        (void)limits;
        (void)cancel;
        TextWindowOutcome outcome;
        outcome.error = {"unsupported_platform",
                         "windowed text read is not available on this provider"};
        return outcome;
    }

    /// Creates `path` with `content`; an existing target is never overwritten
    /// (create-if-absent, DEC-095 decision 3). The parent directory must
    /// exist; it is never created implicitly.
    virtual FileWriteOutcome create_file(const std::filesystem::path &path,
                                         const std::string &content,
                                         const FileModifyLimits &limits,
                                         const CancelToken &cancel) {
        (void)path;
        (void)content;
        (void)limits;
        (void)cancel;
        FileWriteOutcome outcome;
        outcome.error = {"unsupported_platform",
                         "file creation is not available on this provider"};
        return outcome;
    }

    /// Atomically replaces `path` with `content`. `observation` must pin the
    /// file's current version with a full observation; a stale or foreign
    /// observation fails with "stale_file" or "not_observed" and no side
    /// effect.
    virtual FileWriteOutcome replace_file(const std::filesystem::path &path,
                                          const std::string &content,
                                          const FileObservation &observation,
                                          const FileModifyLimits &limits,
                                          const CancelToken &cancel) {
        (void)path;
        (void)content;
        (void)observation;
        (void)limits;
        (void)cancel;
        FileWriteOutcome outcome;
        outcome.error = {"unsupported_platform",
                         "file replacement is not available on this provider"};
        return outcome;
    }

    /// Replaces every occurrence of `old_string` (exactly one match unless
    /// `replace_all`) with `new_string`. Match ranges must lie inside
    /// `observation`; zero matches fail with "match_not_found", several with
    /// "ambiguous_match". `old_string` must be non-empty; an empty
    /// `new_string` deletes. Matching normalizes "\r\n" to "\n" in the file
    /// view while the commit keeps every unmodified byte as-is.
    virtual FileEditOutcome edit_file(const std::filesystem::path &path,
                                      const std::string &old_string,
                                      const std::string &new_string, bool replace_all,
                                      const FileObservation &observation,
                                      const FileModifyLimits &limits,
                                      const CancelToken &cancel) {
        (void)path;
        (void)old_string;
        (void)new_string;
        (void)replace_all;
        (void)observation;
        (void)limits;
        (void)cancel;
        FileEditOutcome outcome;
        outcome.error = {"unsupported_platform", "file edit is not available on this provider"};
        return outcome;
    }
};

} // namespace mirage::desktop
