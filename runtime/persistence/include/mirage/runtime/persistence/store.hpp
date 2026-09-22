#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace mirage::runtime::persistence {

/// What load() observed on disk.
enum class LoadStatus {
    /// `body` carries the full file content.
    Loaded,
    /// No file at the store path (a fresh installation is normal, not an
    /// error).
    Absent,
    /// The file exceeds the store's byte budget; nothing is loaded and the
    /// content is never silently truncated (RULE-07).
    TooLarge,
    /// The file exists but could not be read; `error` says why.
    IoError,
};

struct LoadResult {
    LoadStatus status = LoadStatus::Absent;
    std::string body;  ///< meaningful only for Loaded
    std::string error; ///< meaningful only for IoError
};

/// Outcome of save(); `error` carries a stable "io_error: ..." summary the
/// caller can surface verbatim.
struct SaveResult {
    bool ok = false;
    std::string error;
};

/// One bounded JSON document on disk, addressed as `directory`/`file_name`.
///
/// The store is format-agnostic: the typed codecs live in settings.hpp and
/// recovery.hpp, the pinned JSON model stays confined to their translation
/// units (DEC-011 item 2). save() is atomic and durable per the POSIX
/// discipline: the directory is created 0700, the body goes to a fresh
/// 0600 temp file, fsync, rename over the target, fsync of the directory.
/// A failed save leaves the previous file intact. load() refuses files
/// larger than the byte budget with TooLarge instead of truncating.
///
/// Platform-selected implementations of this one surface:
/// store_posix.cpp (0700/0600 + fsync + rename) on Linux and
/// store_windows.cpp (CREATE_NEW temp + FlushFileBuffers +
/// MoveFileEx WRITE_THROUGH) on Windows (M4-06); the recorded platform
/// difference is directory hardening (default profile ACLs instead of
/// 0700) and directory-durability (no per-directory flush).
class LocalStateStore {
  public:
    /// `file_name` must be a plain name (no directory separators); the
    /// store never escapes `directory`.
    LocalStateStore(std::filesystem::path directory, std::string file_name,
                    std::size_t max_file_bytes);

    LoadResult load() const;
    SaveResult save(std::string_view body) const;

    /// Full path of the managed document (diagnostics and tests).
    std::filesystem::path file_path() const;
    std::size_t max_file_bytes() const { return max_file_bytes_; }

  private:
    std::filesystem::path directory_;
    std::string file_name_;
    std::size_t max_file_bytes_;
};

} // namespace mirage::runtime::persistence
