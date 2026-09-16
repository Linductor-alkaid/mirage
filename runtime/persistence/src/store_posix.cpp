#include <mirage/runtime/persistence/store.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace mirage::runtime::persistence {
namespace {

std::string io_error(const std::string &what, const std::filesystem::path &path, int errno_value) {
    return "io_error: " + what + " '" + path.string() + "': " + std::strerror(errno_value);
}

/// Creates `directory` and every missing parent with 0700, so a created
/// path never depends on the umask. Existing directories (for example the
/// user's ~/.local) are left untouched: only components this call creates
/// get the private mode, and the leaf is chmod'ed 0700 regardless.
bool ensure_private_directory(const std::filesystem::path &directory, std::string &error) {
    std::filesystem::path built;
    for (const std::filesystem::path &part : directory) {
        if (part == "/") {
            built = part;
            continue;
        }
        built = built.empty() ? part : built / part;
        if (::mkdir(built.c_str(), 0700) != 0 && errno != EEXIST) {
            const int errno_value = errno;
            error = io_error("cannot create directory", built, errno_value);
            return false;
        }
    }
    if (::chmod(directory.c_str(), 0700) != 0) {
        const int errno_value = errno;
        error = io_error("cannot chmod directory", directory, errno_value);
        return false;
    }
    return true;
}

/// Reads the whole file; a read that would exceed `max_bytes` reports
/// TooLarge instead of truncating (RULE-07).
LoadResult read_capped(const std::filesystem::path &path, std::size_t max_bytes) {
    LoadResult result;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            result.status = LoadStatus::Absent;
            return result;
        }
        result.status = LoadStatus::IoError;
        result.error = io_error("cannot open file", path, errno);
        return result;
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0) {
        const int errno_value = errno;
        ::close(fd);
        result.status = LoadStatus::IoError;
        result.error = io_error("cannot stat file", path, errno_value);
        return result;
    }
    if (info.st_size < 0 || static_cast<std::size_t>(info.st_size) > max_bytes) {
        ::close(fd);
        result.status = LoadStatus::TooLarge;
        return result;
    }
    std::string body;
    body.resize(static_cast<std::size_t>(info.st_size));
    std::size_t filled = 0;
    while (filled < body.size()) {
        const ssize_t chunk = ::read(fd, body.data() + filled, body.size() - filled);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int errno_value = errno;
            ::close(fd);
            result.status = LoadStatus::IoError;
            result.error = io_error("cannot read file", path, errno_value);
            return result;
        }
        if (chunk == 0) {
            break; // file shrank between fstat and read; keep what we got
        }
        filled += static_cast<std::size_t>(chunk);
    }
    ::close(fd);
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
    if (!ensure_private_directory(directory_, result.error)) {
        return result;
    }
    const std::filesystem::path target = file_path();
    const std::filesystem::path temp =
        directory_ / (file_name_ + ".tmp." + std::to_string(static_cast<long>(::getpid())));

    // O_NOFOLLOW + O_EXCL: the temp file is ours alone and never a symlink
    // planted in the directory; 0600 keeps the content private until the
    // rename publishes it.
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        result.error = io_error("cannot create temp file", temp, errno);
        return result;
    }
    std::size_t written = 0;
    while (written < body.size()) {
        const ssize_t chunk = ::write(fd, body.data() + written, body.size() - written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int errno_value = errno;
            ::close(fd);
            ::unlink(temp.c_str());
            result.error = io_error("cannot write temp file", temp, errno_value);
            return result;
        }
        written += static_cast<std::size_t>(chunk);
    }
    if (::fsync(fd) != 0) {
        const int errno_value = errno;
        ::close(fd);
        ::unlink(temp.c_str());
        result.error = io_error("cannot fsync temp file", temp, errno_value);
        return result;
    }
    if (::close(fd) != 0) {
        const int errno_value = errno;
        ::unlink(temp.c_str());
        result.error = io_error("cannot close temp file", temp, errno_value);
        return result;
    }
    if (::rename(temp.c_str(), target.c_str()) != 0) {
        const int errno_value = errno;
        ::unlink(temp.c_str());
        result.error = io_error("cannot publish file", target, errno_value);
        return result;
    }
    // Make the rename itself durable so a crash right after cannot lose
    // the snapshot silently.
    const int dir_fd = ::open(directory_.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        const int sync_result = ::fsync(dir_fd);
        const int sync_errno = errno;
        ::close(dir_fd);
        if (sync_result != 0) {
            result.error = io_error("cannot fsync directory", directory_, sync_errno);
            return result;
        }
    }
    result.ok = true;
    return result;
}

} // namespace mirage::runtime::persistence
