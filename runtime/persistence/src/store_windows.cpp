#include <mirage/runtime/persistence/store.hpp>

// The Win32 surface is included macro-neutral (DEC-017 decision 5): the
// file APIs are the explicit W forms.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>

namespace mirage::runtime::persistence {
namespace {

std::string io_error(const std::string &what, const std::filesystem::path &path, DWORD error) {
    return "io_error: " + what + " '" + path.string() + "': Win32 error " +
           std::to_string(static_cast<unsigned long>(error));
}

constexpr DWORD kExclusiveShare = 0; // no share flags: the handle is exclusive

/// Reads the whole file; a read that would exceed `max_bytes` reports
/// TooLarge instead of truncating (RULE-07).
LoadResult read_capped(const std::filesystem::path &path, std::size_t max_bytes) {
    LoadResult result;
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, kExclusiveShare, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            result.status = LoadStatus::Absent;
            return result;
        }
        result.status = LoadStatus::IoError;
        result.error = io_error("cannot open file", path, error);
        return result;
    }
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size)) {
        const DWORD error = ::GetLastError();
        ::CloseHandle(file);
        result.status = LoadStatus::IoError;
        result.error = io_error("cannot stat file", path, error);
        return result;
    }
    if (size.QuadPart < 0 || static_cast<std::size_t>(size.QuadPart) > max_bytes) {
        ::CloseHandle(file);
        result.status = LoadStatus::TooLarge;
        return result;
    }
    std::string body;
    body.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t filled = 0;
    while (filled < body.size()) {
        DWORD chunk = 0;
        const std::size_t remaining = body.size() - filled;
        if (!::ReadFile(file, body.data() + filled,
                        static_cast<DWORD>(remaining < 0xFFFFFFFF ? remaining : 0xFFFFFFFFu),
                        &chunk, nullptr)) {
            const DWORD error = ::GetLastError();
            ::CloseHandle(file);
            result.status = LoadStatus::IoError;
            result.error = io_error("cannot read file", path, error);
            return result;
        }
        if (chunk == 0) {
            break; // file shrank between size and read; keep what we got
        }
        filled += chunk;
    }
    ::CloseHandle(file);
    body.resize(filled);
    result.status = LoadStatus::Loaded;
    result.body = std::move(body);
    return result;
}

} // namespace

LocalStateStore::LocalStateStore(std::filesystem::path directory, std::string file_name,
                                 std::size_t max_file_bytes)
    : directory_(std::move(directory)), file_name_(std::move(file_name)),
      max_file_bytes_(max_file_bytes) {}

std::filesystem::path LocalStateStore::file_path() const { return directory_ / file_name_; }

LoadResult LocalStateStore::load() const { return read_capped(file_path(), max_file_bytes_); }

SaveResult LocalStateStore::save(std::string_view body) const {
    SaveResult result;
    if (body.size() > max_file_bytes_) {
        // Refuse before touching disk: an over-budget document is a caller
        // bug or a pathological record, never something to truncate.
        result.error =
            "io_error: document exceeds the " + std::to_string(max_file_bytes_) + " byte budget";
        return result;
    }
    // The POSIX store hardens created directories to 0700; Windows keeps
    // the profile's default ACLs instead (the store lives under the user's
    // profile by default), which is the recorded platform difference.
    std::error_code fs_error;
    std::filesystem::create_directories(directory_, fs_error);
    if (fs_error) {
        result.error = "io_error: cannot create directory '" + directory_.string() +
                       "': " + fs_error.message();
        return result;
    }
    const std::filesystem::path target = file_path();
    const std::filesystem::path temp =
        directory_ / (file_name_ + ".tmp." +
                      std::to_string(static_cast<unsigned long>(::GetCurrentProcessId())));

    // CREATE_NEW with no sharing is the O_EXCL analog: the temp file is
    // ours alone and never a link planted in the directory.
    HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, kExclusiveShare, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        result.error = io_error("cannot create temp file", temp, ::GetLastError());
        return result;
    }
    std::size_t offset = 0;
    while (offset < body.size()) {
        DWORD chunk = 0;
        const std::size_t remaining = body.size() - offset;
        if (!::WriteFile(file, body.data() + offset,
                         static_cast<DWORD>(remaining < 0xFFFFFFFF ? remaining : 0xFFFFFFFFu),
                         &chunk, nullptr)) {
            const DWORD error = ::GetLastError();
            ::CloseHandle(file);
            ::DeleteFileW(temp.c_str());
            result.error = io_error("cannot write temp file", temp, error);
            return result;
        }
        if (chunk == 0) {
            ::CloseHandle(file);
            ::DeleteFileW(temp.c_str());
            result.error = io_error("cannot write temp file", temp, ERROR_WRITE_FAULT);
            return result;
        }
        offset += chunk;
    }
    // FlushFileBuffers is the fsync analog: the content survives a crash
    // before the rename publishes it.
    if (!::FlushFileBuffers(file)) {
        const DWORD error = ::GetLastError();
        ::CloseHandle(file);
        ::DeleteFileW(temp.c_str());
        result.error = io_error("cannot flush temp file", temp, error);
        return result;
    }
    if (!::CloseHandle(file)) {
        const DWORD error = ::GetLastError();
        ::DeleteFileW(temp.c_str());
        result.error = io_error("cannot close temp file", temp, error);
        return result;
    }
    // MoveFileEx with REPLACE_EXISTING | WRITE_THROUGH publishes atomically
    // and makes the rename itself durable (the POSIX rename + directory
    // fsync pair; a per-directory flush does not exist on Windows — the
    // recorded platform difference).
    if (!::MoveFileExW(temp.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = ::GetLastError();
        ::DeleteFileW(temp.c_str());
        result.error = io_error("cannot publish file", target, error);
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace mirage::runtime::persistence
