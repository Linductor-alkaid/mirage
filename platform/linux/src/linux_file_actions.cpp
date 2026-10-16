// M10-04/M10-05 (DEC-095): the Linux file and exec additions of the
// LinuxDesktopEnvironment — windowed text reads with version facts,
// create / observed-replace / literal-edit commits through same-directory
// temporary files, bounded single-writer leases, and shell execution with an
// explicit working directory plus output retention. The mechanics live here
// so linux_desktop_environment.cpp keeps the M1-M9 provider surface.

#include <mirage/desktop/input_provider.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

namespace mirage::platform::linux_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::FileEditOutcome;
using mirage::desktop::FileModifyLimits;
using mirage::desktop::FileObservation;
using mirage::desktop::FilesystemCapabilities;
using mirage::desktop::FileWriteOutcome;
using mirage::desktop::FileVersion;
using mirage::desktop::ObservedByteRange;
using mirage::desktop::ProcessLimits;
using mirage::desktop::ProcessOutcome;
using mirage::desktop::ProcessRequest;
using mirage::desktop::ProviderError;
using mirage::desktop::TextWindowLimits;
using mirage::desktop::TextWindowOutcome;

/// DEC-095 frozen budget: files above this bound stay paginated read-only;
/// their version carries no digest and every write/edit is refused. Read and
/// write paths share the constant so "digest available" implies "modify
/// possible".
constexpr std::size_t kModifyContentBytes = 1u << 20;
/// Chunk granularity for scans and writes; every chunk boundary observes the
/// cancellation token.
constexpr std::size_t kChunkBytes = 64u << 10;

ProviderError provider_error(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

/// Minimal descriptor owner: every helper exit closes what it opened.
struct FdGuard {
    int fd = -1;
    explicit FdGuard(int descriptor = -1) : fd(descriptor) {}
    ~FdGuard() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;
};

// ---------------------------------------------------------------------------
// SHA-256 (bounded content digest, DEC-095 decision 4). The algorithm is
// backend-internal: version tokens never cross backends. This is the one
// remaining local SHA-256 in the tree (REUSE-02/03): the platform layer
// depends only on Mirage::desktop, so pinned mira's digest_bytes is not
// reachable here; runtime/update consumes the mira implementation instead.
// ---------------------------------------------------------------------------

class Sha256 {
  public:
    void update(const std::uint8_t *data, std::size_t size) {
        processed_ += size;
        while (size > 0) {
            const std::size_t room = 64 - pending_;
            const std::size_t take = std::min(room, size);
            std::memcpy(buffer_.data() + pending_, data, take);
            pending_ += take;
            data += take;
            size -= take;
            if (pending_ == 64) {
                compress(buffer_.data());
                pending_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish() {
        const std::uint64_t bits = processed_ * 8;
        std::uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0x00;
        while (pending_ != 56) {
            update(&pad, 1);
        }
        std::array<std::uint8_t, 8> length{};
        for (int i = 0; i < 8; ++i) {
            length[7 - static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((bits >> (i * 8)) & 0xFFu);
        }
        update(length.data(), length.size());
        std::array<std::uint8_t, 32> digest{};
        for (int i = 0; i < 8; ++i) {
            const std::uint32_t word = state_[static_cast<std::size_t>(i)];
            digest[static_cast<std::size_t>(i) * 4] = static_cast<std::uint8_t>(word >> 24);
            digest[static_cast<std::size_t>(i) * 4 + 1] = static_cast<std::uint8_t>(word >> 16);
            digest[static_cast<std::size_t>(i) * 4 + 2] = static_cast<std::uint8_t>(word >> 8);
            digest[static_cast<std::size_t>(i) * 4 + 3] = static_cast<std::uint8_t>(word);
        }
        return digest;
    }

  private:
    static std::uint32_t rotr(std::uint32_t value, unsigned count) {
        return (value >> count) | (value << (32 - count));
    }

    void compress(const std::uint8_t *block) {
        static constexpr std::array<std::uint32_t, 64> kRoundConstants{
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t base = static_cast<std::size_t>(i) * 4;
            w[static_cast<std::size_t>(i)] = (static_cast<std::uint32_t>(block[base]) << 24) |
                                             (static_cast<std::uint32_t>(block[base + 1]) << 16) |
                                             (static_cast<std::uint32_t>(block[base + 2]) << 8) |
                                             static_cast<std::uint32_t>(block[base + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::size_t at = static_cast<std::size_t>(i);
            const std::uint32_t s0 = rotr(w[at - 15], 7) ^ rotr(w[at - 15], 18) ^ (w[at - 15] >> 3);
            const std::uint32_t s1 = rotr(w[at - 2], 17) ^ rotr(w[at - 2], 19) ^ (w[at - 2] >> 10);
            w[at] = w[at - 16] + s0 + w[at - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t upper = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + upper + ch + kRoundConstants[static_cast<std::size_t>(i)] +
                                     w[static_cast<std::size_t>(i)];
            const std::uint32_t lower = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = lower + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t pending_ = 0;
    std::uint64_t processed_ = 0;
};

std::string content_digest_hex(std::string_view content) {
    Sha256 hash;
    hash.update(reinterpret_cast<const std::uint8_t *>(content.data()), content.size());
    const auto digest = hash.finish();
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        hex.push_back(kHex[byte >> 4]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    return hex;
}

// ---------------------------------------------------------------------------
// File identity and containment helpers (DEC-095: anchor on real handles,
// verify the final target, never trust a re-opened path).
// ---------------------------------------------------------------------------

FileVersion version_from_stat(const struct stat &info, std::string_view digest) {
    FileVersion version;
    version.device = static_cast<std::uint64_t>(info.st_dev);
    version.file_index = static_cast<std::uint64_t>(info.st_ino);
    version.size = static_cast<std::uint64_t>(info.st_size);
    version.content_digest = std::string(digest);
    return version;
}

/// Resolves the real path of an open descriptor through /proc/self/fd; the
/// scope check runs on this resolved target so a check-then-reopen race
/// cannot smuggle an out-of-scope inode through a swapped path.
bool resolve_fd_path(int fd, std::filesystem::path &resolved) {
    std::string link(256, '\0');
    for (;;) {
        const std::string link_path = "/proc/self/fd/" + std::to_string(fd);
        const ssize_t written = ::readlink(link_path.c_str(), link.data(), link.size());
        if (written < 0) {
            return false;
        }
        if (static_cast<std::size_t>(written) < link.size()) {
            link.resize(static_cast<std::size_t>(written));
            resolved = link;
            return true;
        }
        link.resize(link.size() * 2);
    }
}

/// Opens every directory component of `parent` with O_NOFOLLOW (DEC-095:
/// write paths reject symbolic links instead of following them). Returns the
/// parent directory descriptor; the caller owns it.
bool open_parent_directory(const std::filesystem::path &parent, int &parent_fd,
                           ProviderError &error) {
    if (parent.empty() || !parent.is_absolute()) {
        error = provider_error("invalid_argument", "write path must be absolute");
        return false;
    }
    FdGuard dir(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (dir.fd < 0) {
        error = provider_error("io_error", "cannot open filesystem root");
        return false;
    }
    for (const auto &component : parent) {
        if (component.empty() || component == "/") {
            continue;
        }
        const int next =
            ::openat(dir.fd, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            if (errno == ELOOP || errno == ENOTDIR) {
                error = provider_error(
                    "invalid_argument",
                    "write path contains a symbolic link or non-directory component: " +
                        parent.string());
            } else if (errno == ENOENT) {
                error = provider_error("not_found",
                                       "parent directory does not exist: " + parent.string());
            } else if (errno == EACCES || errno == EPERM) {
                error = provider_error(
                    "permission_denied", "parent directory is not accessible: " + parent.string());
            } else {
                error = provider_error("io_error",
                                       "cannot open parent directory: " + parent.string());
            }
            return false;
        }
        dir.fd = next;
    }
    parent_fd = dir.fd;
    dir.fd = -1;
    return true;
}

/// Writes the whole buffer, chunk by chunk, observing the token per chunk.
bool write_all_bounded(int fd, const std::string &content, const CancelToken &cancel,
                       ProviderError &error) {
    std::size_t written = 0;
    while (written < content.size()) {
        if (cancel.cancelled()) {
            error = provider_error("cancelled", "write was cancelled before it finished");
            return false;
        }
        const std::size_t take = std::min(kChunkBytes, content.size() - written);
        const ssize_t chunk = ::write(fd, content.data() + written, take);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ENOSPC) {
                error = provider_error("io_error", "disk is full while writing content");
            } else {
                error = provider_error("io_error",
                                       std::string("write failed: ") + std::strerror(errno));
            }
            return false;
        }
        written += static_cast<std::size_t>(chunk);
    }
    return true;
}

enum class ReadAllStatus { Ok, TooLarge, Cancelled, IoError };

/// Reads an opened file up to `cap` bytes; `truncated` reports that the cap
/// was reached, which callers treat as "content exceeds the modify budget".
ReadAllStatus read_all_bounded(int fd, std::size_t cap, const CancelToken &cancel,
                               std::string &content, bool &truncated) {
    content.clear();
    truncated = false;
    std::vector<char> buffer(kChunkBytes);
    for (;;) {
        if (cancel.cancelled()) {
            return ReadAllStatus::Cancelled;
        }
        const ssize_t received = ::read(fd, buffer.data(), buffer.size());
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ReadAllStatus::IoError;
        }
        if (received == 0) {
            return ReadAllStatus::Ok;
        }
        if (content.size() + static_cast<std::size_t>(received) > cap) {
            truncated = true;
            return ReadAllStatus::TooLarge;
        }
        content.append(buffer.data(), static_cast<std::size_t>(received));
    }
}

/// DEC-095: text tools serve UTF-8 text; NUL bytes fail with
/// unsupported_encoding instead of feeding the model binary noise. Core
/// strictness rides the shared desktop validator (REUSE-05) — this wrapper
/// only adds the NUL policy.
bool is_valid_utf8_text(std::string_view content) {
    if (content.find('\0') != std::string_view::npos) {
        return false;
    }
    return mirage::desktop::is_valid_utf8(std::string(content));
}

/// Validates the shared mutation budgets (DEC-095: refuse before any side
/// effect, never grow past the frozen bound).
bool modify_limits_valid(const FileModifyLimits &limits, ProviderError &error) {
    if (limits.max_content_bytes == 0 || limits.max_content_bytes > kModifyContentBytes) {
        error = provider_error("invalid_argument",
                               "modify budget must be within " +
                                   std::to_string(kModifyContentBytes) + " bytes");
        return false;
    }
    return true;
}

/// Verifies the caller's observation still describes the content just read
/// (DEC-095: the host presents facts, the provider re-derives them).
bool observation_matches(const FileObservation &observation, const struct stat &info,
                         std::string_view content, ProviderError &error) {
    if (!observation.version.digest_available()) {
        error = provider_error("not_observed",
                               "observation carries no content digest; re-read the file");
        return false;
    }
    const FileVersion current = version_from_stat(info, content_digest_hex(content));
    if (observation.version != current) {
        error = provider_error("stale_file",
                               "file changed since it was observed; re-read before writing");
        return false;
    }
    return true;
}

FileWriteOutcome committed_write(const struct stat &temp_stat, const std::string &content,
                                 bool created) {
    FileWriteOutcome outcome;
    outcome.ok = true;
    outcome.applied = true;
    outcome.created = created;
    outcome.replaced = !created;
    outcome.bytes_written = content.size();
    outcome.version = version_from_stat(temp_stat, content_digest_hex(content));
    return outcome;
}

FileWriteOutcome failed_write(ProviderError error) {
    FileWriteOutcome outcome;
    outcome.error = std::move(error);
    return outcome;
}

FileEditOutcome failed_edit(ProviderError error) {
    FileEditOutcome outcome;
    outcome.error = std::move(error);
    return outcome;
}

/// Commits `content` by writing a same-directory temporary file and renaming
/// it into place (RENAME_NOREPLACE for creates). `preserved_mode` of zero
/// uses the default 0644. Every failure path removes the temporary file.
/// When `target_fd >= 0` the pinned target stat is re-checked at the last
/// pre-commit point: in-place rewrites are detected, while a path re-pointed
/// between check and rename stays the documented residual window (DEC-095).
FileWriteOutcome commit_via_temp(int parent_fd, const std::filesystem::path &path,
                                 const std::string &name, const std::string &content,
                                 mode_t preserved_mode, bool allow_replace,
                                 const CancelToken &cancel, int target_fd,
                                 const struct stat &target_stat) {
    if (target_fd >= 0) {
        struct stat current {};
        if (::fstat(target_fd, &current) != 0 || current.st_dev != target_stat.st_dev ||
            current.st_ino != target_stat.st_ino || current.st_size != target_stat.st_size ||
            current.st_mtim.tv_sec != target_stat.st_mtim.tv_sec ||
            current.st_mtim.tv_nsec != target_stat.st_mtim.tv_nsec) {
            return failed_write(provider_error(
                "stale_file", "file changed while preparing the write; re-read and retry"));
        }
    }
    static std::atomic_uint64_t temp_sequence{0};
    std::string temp_name;
    int temp_fd = -1;
    for (int attempt = 0; attempt < 64 && temp_fd < 0; ++attempt) {
        temp_name = ".mirage-tmp-" + std::to_string(::getpid()) + "-" +
                    std::to_string(temp_sequence.fetch_add(1)) + ".tmp";
        temp_fd = ::openat(parent_fd, temp_name.c_str(),
                           O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC,
                           preserved_mode == 0 ? static_cast<mode_t>(0644) : preserved_mode);
        if (temp_fd < 0 && errno != EEXIST) {
            return failed_write(
                provider_error("io_error",
                               "cannot create temporary file in " + path.parent_path().string()));
        }
    }
    if (temp_fd < 0) {
        return failed_write(
            provider_error("busy", "could not allocate a temporary file name; retry"));
    }
    const auto cleanup = [&] {
        ::close(temp_fd);
        ::unlinkat(parent_fd, temp_name.c_str(), 0);
    };
    if (preserved_mode != 0 && ::fchmod(temp_fd, preserved_mode) != 0) {
        ProviderError error = provider_error("io_error", "cannot preserve file mode");
        cleanup();
        return failed_write(std::move(error));
    }
    ProviderError error;
    if (!write_all_bounded(temp_fd, content, cancel, error)) {
        cleanup();
        return failed_write(std::move(error));
    }
    struct stat temp_stat {};
    if (::fstat(temp_fd, &temp_stat) != 0) {
        error = provider_error("io_error", "cannot stat temporary file");
        cleanup();
        return failed_write(std::move(error));
    }
    if (::close(temp_fd) != 0) {
        temp_fd = -1;
        ::unlinkat(parent_fd, temp_name.c_str(), 0);
        return failed_write(provider_error("io_error", "closing the temporary file failed"));
    }
    // Cancel at the last pre-commit point: the commit has not happened, the
    // target is untouched, and the temp file is gone (DEC-095 lifecycle).
    if (cancel.cancelled()) {
        ::unlinkat(parent_fd, temp_name.c_str(), 0);
        return failed_write(provider_error("cancelled", "write was cancelled before commit"));
    }
    const int result =
        allow_replace
            ? ::renameat(parent_fd, temp_name.c_str(), parent_fd, name.c_str())
            : static_cast<int>(::syscall(SYS_renameat2, parent_fd, temp_name.c_str(), parent_fd,
                                         name.c_str(), RENAME_NOREPLACE));
    if (result != 0) {
        const int saved = errno;
        ::unlinkat(parent_fd, temp_name.c_str(), 0);
        if (saved == EEXIST) {
            // Provider-specific code (ProviderError vocabulary): the create
            // path never overwrites; "already exists" is distinct from a
            // lease conflict so the caller can ask for a read first.
            return failed_write(provider_error(
                "already_exists", "target already exists; read it and replace it to overwrite"));
        }
        if (saved == EACCES || saved == EPERM) {
            return failed_write(provider_error("permission_denied", "commit was refused"));
        }
        return failed_write(
            provider_error("io_error", std::string("commit failed: ") + std::strerror(saved)));
    }
    return committed_write(temp_stat, content, !allow_replace);
}

// ---------------------------------------------------------------------------
// Windowed reads
// ---------------------------------------------------------------------------

struct LineSpan {
    std::size_t start = 0;  // raw offset of the first view byte
    std::size_t length = 0; // view bytes (terminator excluded, '\r' kept)
    bool terminated = true; // followed by '\n' in the raw content
};

/// Splits raw content into line spans: bytes between '\n' terminators; a
/// final unterminated line still counts (DEC-095 contract).
void split_lines(std::string_view content, std::vector<LineSpan> &lines) {
    lines.clear();
    std::size_t start = 0;
    while (start < content.size()) {
        const std::size_t at = content.find('\n', start);
        if (at == std::string_view::npos) {
            lines.push_back({start, content.size() - start, false});
            return;
        }
        lines.push_back({start, at - start, true});
        start = at + 1;
    }
}

/// Shared per-line admission of the window collection (DEC-095 budget
/// table): a spent budget stops the window with continuation facts; a single
/// line beyond the whole window budget is named line_too_long instead of
/// being cut.
enum class LineDecision { Collect, Stop, TooLong };

LineDecision admit_line(std::size_t length, std::uint64_t rendered_so_far, std::size_t collected,
                        const TextWindowLimits &limits) {
    if (collected >= limits.max_lines) {
        return LineDecision::Stop;
    }
    if (length > limits.max_bytes) {
        return LineDecision::TooLong;
    }
    if (rendered_so_far + length > limits.max_bytes) {
        return LineDecision::Stop;
    }
    return LineDecision::Collect;
}

/// RAII release of a single-writer file lease; the lease guards the whole
/// prepare-and-commit window of one mutation. Templated so it can hold the
/// environment's private lease table without naming it.
template <typename Table> class FileLeaseGuard {
  public:
    FileLeaseGuard(Table &table, std::uint64_t device, std::uint64_t index, bool acquired)
        : table_(table), device_(device), index_(index), held_(acquired) {}
    ~FileLeaseGuard() {
        if (held_) {
            table_.release(device_, index_);
        }
    }
    [[nodiscard]] bool held() const { return held_; }
    FileLeaseGuard(const FileLeaseGuard &) = delete;
    FileLeaseGuard &operator=(const FileLeaseGuard &) = delete;

  private:
    Table &table_;
    std::uint64_t device_;
    std::uint64_t index_;
    bool held_;
};

/// Serves the window [skip_lines, ...) of an in-memory line table. A
/// skip_lines at or beyond the line count is an invalid_argument: a stale
/// continuation is a caller bug, and eof windows carry next_offset facts.
/// An empty file with skip_lines zero is the legal empty eof window.
TextWindowOutcome window_from_lines(std::string_view content,
                                    const std::vector<LineSpan> &lines, std::uint64_t skip_lines,
                                    const TextWindowLimits &limits, FileVersion version) {
    TextWindowOutcome outcome;
    if (lines.empty() && skip_lines == 0) {
        outcome.ok = true;
        outcome.eof = true;
        outcome.total_lines = 0;
        outcome.version = std::move(version);
        outcome.has_bom = false;
        return outcome;
    }
    if (skip_lines >= lines.size()) {
        outcome.error = provider_error("invalid_argument",
                                       "offset beyond end of file (file has " +
                                           std::to_string(lines.size()) + " lines)");
        return outcome;
    }
    const std::size_t begin = static_cast<std::size_t>(skip_lines);
    std::size_t index = begin;
    std::uint64_t rendered = 0;
    std::size_t end_offset = lines[begin].start;
    bool window_has_crlf = false;
    while (index < lines.size()) {
        const LineSpan &line = lines[index];
        const LineDecision decision =
            admit_line(line.length, rendered, outcome.lines.size(), limits);
        if (decision == LineDecision::TooLong) {
            outcome.error = provider_error(
                "line_too_long",
                "line " + std::to_string(static_cast<std::uint64_t>(index) + 1) + " has " +
                    std::to_string(line.length) + " bytes, beyond the window budget of " +
                    std::to_string(limits.max_bytes) + " bytes");
            return outcome;
        }
        if (decision == LineDecision::Stop) {
            break;
        }
        rendered += line.length;
        // The view strips the "\r" of a "\r\n" pair (the view is what
        // edit_file matches on); raw facts keep the original bytes.
        std::string_view view = content.substr(line.start, line.length);
        if (line.terminated && !view.empty() && view.back() == '\r') {
            view.remove_suffix(1);
            window_has_crlf = true;
        }
        outcome.lines.emplace_back(view);
        end_offset = line.start + line.length + (line.terminated ? 1u : 0u);
        ++index;
    }
    outcome.ok = true;
    outcome.first_line = skip_lines + 1;
    outcome.eof = index == lines.size();
    outcome.next_offset = static_cast<std::uint64_t>(index);
    if (outcome.eof) {
        outcome.total_lines = lines.size();
    }
    outcome.has_bom = skip_lines == 0 && content.size() >= 3 &&
                      static_cast<unsigned char>(content[0]) == 0xEF &&
                      static_cast<unsigned char>(content[1]) == 0xBB &&
                      static_cast<unsigned char>(content[2]) == 0xBF;
    outcome.has_crlf = window_has_crlf;
    outcome.version = std::move(version);
    outcome.observed_range = ObservedByteRange{
        static_cast<std::uint64_t>(lines[begin].start),
        static_cast<std::uint64_t>(end_offset - lines[begin].start)};
    return outcome;
}

/// Windowed read over a whole in-memory content (files within the modify
/// budget): validates text, computes the version digest, serves the window.
/// The version binds the real file identity from the caller's fstat — the
/// digest alone cannot re-verify an observation (BUG-20261016-001).
TextWindowOutcome window_from_content(std::string_view content, std::uint64_t skip_lines,
                                      const TextWindowLimits &limits, const struct stat &info) {
    if (!is_valid_utf8_text(content)) {
        TextWindowOutcome outcome;
        outcome.error =
            provider_error("unsupported_encoding", "file is not valid UTF-8 text");
        return outcome;
    }
    FileVersion version = version_from_stat(info, content_digest_hex(content));
    version.size = static_cast<std::uint64_t>(content.size());
    std::vector<LineSpan> lines;
    split_lines(content, lines);
    return window_from_lines(content, lines, skip_lines, limits, std::move(version));
}

/// Windowed read over a file beyond the modify budget: a single bounded
/// forward scan positions the window start, then collects lines under the
/// line/byte budgets. No digest is computed (DEC-095: oversized files are
/// paginated read-only), so every write on them is refused upstream.
TextWindowOutcome window_from_stream(int fd, std::uint64_t skip_lines,
                                     const TextWindowLimits &limits, const CancelToken &cancel,
                                     const struct stat &info) {
    TextWindowOutcome outcome;
    const auto deadline = std::chrono::steady_clock::now() + limits.max_scan_time;
    const auto scan_exceeded = [&] {
        return provider_error("scan_budget_exceeded",
                              "scan exceeded its byte or time budget; eof position unknown");
    };
    std::uint64_t scanned = 0;
    std::uint64_t absolute = 0; // raw offset of the next unconsumed byte
    std::uint64_t line_index = 0;
    std::string pending; // bytes of the line currently being assembled
    std::vector<char> buffer(kChunkBytes);

    // Phase 1: position the window start after `skip_lines` terminators.
    while (line_index < skip_lines) {
        if (cancel.cancelled()) {
            outcome.error = provider_error("cancelled", "read was cancelled");
            return outcome;
        }
        if (scanned > limits.max_scan_bytes ||
            std::chrono::steady_clock::now() > deadline) {
            outcome.error = scan_exceeded();
            return outcome;
        }
        ssize_t received = ::read(fd, buffer.data(), buffer.size());
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received < 0) {
            outcome.error =
                provider_error("io_error", std::string("read failed: ") + std::strerror(errno));
            return outcome;
        }
        if (received == 0) {
            outcome.error =
                provider_error("invalid_argument", "offset beyond end of file (scanned " +
                                                       std::to_string(line_index) +
                                                       " complete lines)");
            return outcome;
        }
        scanned += static_cast<std::size_t>(received);
        const std::string_view chunk(buffer.data(), static_cast<std::size_t>(received));
        std::size_t consumed = 0;
        while (line_index < skip_lines) {
            const std::size_t at = chunk.find('\n', consumed);
            if (at == std::string_view::npos) {
                consumed = chunk.size();
                break;
            }
            ++line_index;
            consumed = at + 1;
        }
        absolute += consumed;
        pending.assign(chunk.substr(consumed));
    }

    // Phase 2: collect lines under the budgets.
    const std::uint64_t window_start = absolute;
    std::uint64_t rendered = 0;
    std::uint64_t window_bytes = 0; // raw bytes covered by collected lines
    bool window_has_crlf = false;
    bool hit_eof = false;
    bool stopped = false;
    for (;;) {
        // Drain complete lines already assembled in `pending`; at eof the
        // remaining bytes are the final unterminated line. The drain runs
        // at least once after hit_eof is set, so a file that does not end
        // in a newline still reports its final line (BUG-20261016-002).
        for (;;) {
            const std::size_t at = pending.find('\n');
            const bool terminated = at != std::string::npos;
            if (!terminated && (!hit_eof || pending.empty())) {
                break; // incomplete line needs more input, or nothing is left
            }
            const std::size_t length = terminated ? at : pending.size();
            const LineDecision decision =
                admit_line(length, rendered, outcome.lines.size(), limits);
            if (decision == LineDecision::TooLong) {
                outcome.error = provider_error(
                    "line_too_long",
                    "line " + std::to_string(line_index + 1) + " has " +
                        std::to_string(length) + " bytes, beyond the window budget of " +
                        std::to_string(limits.max_bytes) + " bytes");
                return outcome;
            }
            if (decision == LineDecision::Stop) {
                stopped = true;
                break;
            }
            const std::string_view raw_line(pending.data(), length);
            // The view strips the "\r" of a "\r\n" pair (the view is what
            // edit_file matches on); raw budget facts keep original bytes.
            std::string_view view = raw_line;
            if (terminated && !view.empty() && view.back() == '\r') {
                view.remove_suffix(1);
                window_has_crlf = true;
            }
            if (!is_valid_utf8_text(view)) {
                outcome.error = provider_error("unsupported_encoding",
                                               "file is not valid UTF-8 text");
                return outcome;
            }
            outcome.lines.emplace_back(view);
            rendered += length;
            window_bytes += length + (terminated ? 1u : 0u);
            ++line_index;
            pending.erase(0, length + (terminated ? 1u : 0u));
            if (!terminated) {
                break; // final unterminated line consumed
            }
        }
        if (stopped || hit_eof) {
            break;
        }
        if (cancel.cancelled()) {
            outcome.error = provider_error("cancelled", "read was cancelled");
            return outcome;
        }
        if (scanned > limits.max_scan_bytes ||
            std::chrono::steady_clock::now() > deadline) {
            outcome.error = scan_exceeded();
            return outcome;
        }
        const ssize_t received = ::read(fd, buffer.data(), buffer.size());
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received < 0) {
            outcome.error =
                provider_error("io_error", std::string("read failed: ") + std::strerror(errno));
            return outcome;
        }
        if (received == 0) {
            hit_eof = true;
            continue; // let the drain above settle the final pending line
        }
        scanned += static_cast<std::size_t>(received);
        pending.append(buffer.data(), static_cast<std::size_t>(received));
    }

    outcome.ok = true;
    outcome.first_line = skip_lines + 1;
    outcome.eof = hit_eof && !stopped;
    outcome.next_offset = skip_lines + outcome.lines.size();
    if (outcome.eof) {
        outcome.total_lines = outcome.next_offset;
    }
    // A continuation that lands exactly at eof of a newline-terminated file
    // is the same stale-offset bug the in-memory path rejects (the eof
    // window already carries next_offset for well-formed continuations).
    if (outcome.eof && outcome.lines.empty() && skip_lines > 0) {
        outcome = TextWindowOutcome{};
        outcome.error = provider_error(
            "invalid_argument", "offset beyond end of file (scanned " +
                                    std::to_string(line_index) + " complete lines)");
        return outcome;
    }
    outcome.has_bom = skip_lines == 0 && window_start == 0 && !outcome.lines.empty() &&
                      outcome.lines.front().size() >= 3 &&
                      static_cast<unsigned char>(outcome.lines.front()[0]) == 0xEF &&
                      static_cast<unsigned char>(outcome.lines.front()[1]) == 0xBB &&
                      static_cast<unsigned char>(outcome.lines.front()[2]) == 0xBF;
    outcome.has_crlf = window_has_crlf;
    outcome.version = version_from_stat(info, {});
    outcome.observed_range = ObservedByteRange{window_start, window_bytes};
    return outcome;
}

// ---------------------------------------------------------------------------
// Normalization mapping for literal edits (DEC-095: "\r\n" matches as "\n",
// unmodified bytes keep their original form).
// ---------------------------------------------------------------------------

/// Builds the "\r\n" -> "\n" view of `content` together with the raw offset
/// of every view byte (empty when the content has no "\r" and view positions
/// are raw positions directly).
std::string normalized_view(const std::string &content,
                            std::vector<std::uint32_t> &view_to_raw) {
    view_to_raw.clear();
    if (content.find('\r') == std::string::npos) {
        return content;
    }
    std::string view;
    view.reserve(content.size());
    view_to_raw.reserve(content.size());
    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r' && i + 1 < content.size() && content[i + 1] == '\n') {
            continue;
        }
        view.push_back(content[i]);
        view_to_raw.push_back(static_cast<std::uint32_t>(i));
    }
    return view;
}

/// Raw span [begin, end) of the view match [begin, end): view byte at
/// position p starts at map[p]; the exclusive end is one past the raw byte
/// of view position end - 1 (every view byte is one raw byte; the removed
/// "\r" bytes only precede later bytes).
struct RawSpan {
    std::uint64_t begin;
    std::uint64_t end;
};

RawSpan raw_span(const std::vector<std::uint32_t> &view_to_raw, std::size_t begin,
                 std::size_t end, std::size_t raw_length) {
    if (view_to_raw.empty()) {
        return {static_cast<std::uint64_t>(begin), static_cast<std::uint64_t>(end)};
    }
    const std::uint64_t raw_begin = view_to_raw[begin];
    const std::uint64_t raw_end =
        end == 0 ? 0
                 : static_cast<std::uint64_t>(view_to_raw[end - 1]) + 1;
    return {raw_begin, std::min<std::uint64_t>(raw_end, raw_length)};
}

/// Finds every occurrence of `needle` (non-overlapping, leftmost-first) as
/// [begin, end) positions in `haystack`.
std::vector<std::pair<std::size_t, std::size_t>>
find_occurrences(std::string_view haystack, std::string_view needle) {
    std::vector<std::pair<std::size_t, std::size_t>> matches;
    std::size_t from = 0;
    while (from + needle.size() <= haystack.size()) {
        const std::size_t at = haystack.find(needle, from);
        if (at == std::string_view::npos) {
            break;
        }
        matches.emplace_back(at, at + needle.size());
        from = at + needle.size();
    }
    return matches;
}

/// Shared mutation entry: budget validation, cancellation, symlink-free
/// parent walk, and write-scope containment of the resolved parent
/// directory. Returns the parent descriptor (owned via `guard`) and the
/// final component name.
bool mutation_prologue(const std::filesystem::path &path,
                       const mirage::desktop::PathScope &write_scope,
                       const FileModifyLimits &limits, const CancelToken &cancel, FdGuard &parent,
                       std::string &name, ProviderError &error) {
    if (path.empty() || !path.is_absolute()) {
        error = provider_error("invalid_argument", "path must be non-empty and absolute");
        return false;
    }
    if (!modify_limits_valid(limits, error)) {
        return false;
    }
    if (cancel.cancelled()) {
        error = provider_error("cancelled", "write was cancelled before it started");
        return false;
    }
    name = path.filename().string();
    if (name.empty() || name == "." || name == "..") {
        error = provider_error("invalid_argument", "path must name a file: " + path.string());
        return false;
    }
    if (!open_parent_directory(path.parent_path(), parent.fd, error)) {
        return false;
    }
    std::filesystem::path resolved;
    if (!resolve_fd_path(parent.fd, resolved) || !write_scope.contains(resolved)) {
        error = write_scope.roots().empty()
                    ? provider_error("permission_denied",
                                     "no write roots are configured; every write is denied")
                    : provider_error("permission_denied",
                                     "path is outside the allowed write scope: " + path.string());
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// LinuxDesktopEnvironment member implementations (M10 surface)
// ---------------------------------------------------------------------------

bool LinuxDesktopEnvironment::FileLeaseTable::try_acquire(std::uint64_t device,
                                                          std::uint64_t index) {
    const std::lock_guard lock(mutex);
    return held.emplace(std::pair{device, index}, true).second;
}

void LinuxDesktopEnvironment::FileLeaseTable::release(std::uint64_t device, std::uint64_t index) {
    const std::lock_guard lock(mutex);
    held.erase(std::pair{device, index});
}

FilesystemCapabilities LinuxDesktopEnvironment::capabilities() const {
    FilesystemCapabilities caps;
    caps.windowed_read = true;
    // Capability honesty (DEC-095): without declared write roots the
    // mutation surface does not exist at all, so callers must not register
    // write/edit tools instead of exposing per-call permission failures.
    caps.write = !write_scope_.roots().empty();
    caps.edit = caps.write;
    return caps;
}

TextWindowOutcome LinuxDesktopEnvironment::read_text_window(const std::filesystem::path &path,
                                                            std::uint64_t skip_lines,
                                                            const TextWindowLimits &limits,
                                                            const CancelToken &cancel) {
    if (path.empty() || !path.is_absolute()) {
        TextWindowOutcome outcome;
        outcome.error = provider_error("invalid_argument", "path must be non-empty and absolute");
        return outcome;
    }
    if (limits.max_lines == 0 || limits.max_bytes == 0 || limits.max_scan_bytes == 0 ||
        limits.max_scan_time <= std::chrono::milliseconds::zero()) {
        TextWindowOutcome outcome;
        outcome.error = provider_error("invalid_argument", "window budgets must be positive");
        return outcome;
    }
    if (!read_scope_.contains(path)) {
        TextWindowOutcome outcome;
        outcome.error = provider_error("permission_denied",
                                       "path is outside the allowed read scope: " + path.string());
        return outcome;
    }
    const FdGuard fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (fd.fd < 0) {
        TextWindowOutcome outcome;
        outcome.error = provider_error(errno == ENOENT ? "not_found" : "io_error",
                                       "cannot open file: " + path.string());
        return outcome;
    }
    struct stat info {};
    if (::fstat(fd.fd, &info) != 0) {
        TextWindowOutcome outcome;
        outcome.error = provider_error("io_error", "cannot stat file: " + path.string());
        return outcome;
    }
    if (!S_ISREG(info.st_mode)) {
        TextWindowOutcome outcome;
        outcome.error =
            provider_error("not_regular_file", "path is not a regular file: " + path.string());
        return outcome;
    }

    // Files within the modify budget take the exact path: read once, digest,
    // split. Anything larger (or grown past the budget mid-read) takes the
    // bounded scan with no digest — writes on it are refused upstream anyway.
    if (static_cast<std::uint64_t>(info.st_size) <= kModifyContentBytes) {
        std::string content;
        bool truncated = false;
        const auto status = read_all_bounded(fd.fd, kModifyContentBytes, cancel, content, truncated);
        if (status == ReadAllStatus::TooLarge) {
            if (::lseek(fd.fd, 0, SEEK_SET) < 0) {
                TextWindowOutcome outcome;
                outcome.error = provider_error("io_error", "cannot rewind a grown file");
                return outcome;
            }
            return window_from_stream(fd.fd, skip_lines, limits, cancel, info);
        }
        switch (status) {
        case ReadAllStatus::Cancelled: {
            TextWindowOutcome outcome;
            outcome.error = provider_error("cancelled", "read was cancelled");
            return outcome;
        }
        case ReadAllStatus::IoError: {
            TextWindowOutcome outcome;
            outcome.error = provider_error("io_error", "read failed: " + path.string());
            return outcome;
        }
        case ReadAllStatus::Ok:
        case ReadAllStatus::TooLarge:
            break;
        }
        return window_from_content(content, skip_lines, limits, info);
    }
    return window_from_stream(fd.fd, skip_lines, limits, cancel, info);
}

FileWriteOutcome LinuxDesktopEnvironment::create_file(const std::filesystem::path &path,
                                                      const std::string &content,
                                                      const FileModifyLimits &limits,
                                                      const CancelToken &cancel) {
    ProviderError error;
    FdGuard parent;
    std::string name;
    if (!mutation_prologue(path, write_scope_, limits, cancel, parent, name, error)) {
        return failed_write(std::move(error));
    }
    if (content.size() > limits.max_content_bytes) {
        return failed_write(provider_error("file_too_large",
                                           "content exceeds the modify budget of " +
                                               std::to_string(limits.max_content_bytes) +
                                               " bytes"));
    }
    const struct stat untouched {};
    auto outcome =
        commit_via_temp(parent.fd, path, name, content, 0, false, cancel, -1, untouched);
    return outcome;
}

FileWriteOutcome LinuxDesktopEnvironment::replace_file(const std::filesystem::path &path,
                                                       const std::string &content,
                                                       const FileObservation &observation,
                                                       const FileModifyLimits &limits,
                                                       const CancelToken &cancel) {
    ProviderError error;
    FdGuard parent;
    std::string name;
    if (!mutation_prologue(path, write_scope_, limits, cancel, parent, name, error)) {
        return failed_write(std::move(error));
    }
    if (!observation.full) {
        return failed_write(provider_error(
            "not_observed", "replacing a file requires a full observation of its content"));
    }
    if (content.size() > limits.max_content_bytes) {
        return failed_write(provider_error("file_too_large",
                                           "content exceeds the modify budget of " +
                                               std::to_string(limits.max_content_bytes) +
                                               " bytes"));
    }
    const FdGuard target(
        ::openat(parent.fd, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (target.fd < 0) {
        if (errno == ELOOP) {
            return failed_write(provider_error("invalid_argument",
                                               "write target is a symbolic link: " + path.string()));
        }
        if (errno == ENOENT) {
            return failed_write(
                provider_error("not_found", "file does not exist: " + path.string()));
        }
        return failed_write(provider_error("io_error", "cannot open file: " + path.string()));
    }
    struct stat info {};
    if (::fstat(target.fd, &info) != 0) {
        return failed_write(provider_error("io_error", "cannot stat file: " + path.string()));
    }
    if (!S_ISREG(info.st_mode)) {
        return failed_write(
            provider_error("not_regular_file", "path is not a regular file: " + path.string()));
    }
    if (info.st_nlink > 1) {
        return failed_write(provider_error(
            "invalid_argument",
            "target has multiple hard links; refusing to change link semantics: " +
                path.string()));
    }
    if (static_cast<std::uint64_t>(info.st_size) > limits.max_content_bytes) {
        return failed_write(provider_error("file_too_large",
                                           "file exceeds the modify budget of " +
                                               std::to_string(limits.max_content_bytes) +
                                               " bytes: " + path.string()));
    }
    std::string current;
    bool truncated = false;
    switch (read_all_bounded(target.fd, limits.max_content_bytes, cancel, current, truncated)) {
    case ReadAllStatus::Ok:
        break;
    case ReadAllStatus::TooLarge:
        return failed_write(provider_error("file_too_large", "file grew beyond the modify budget"));
    case ReadAllStatus::Cancelled:
        return failed_write(provider_error("cancelled", "read was cancelled"));
    case ReadAllStatus::IoError:
        return failed_write(provider_error("io_error", "read failed: " + path.string()));
    }
    if (!is_valid_utf8_text(current)) {
        return failed_write(
            provider_error("unsupported_encoding", "file is not valid UTF-8 text: " + path.string()));
    }
    if (!observation_matches(observation, info, current, error)) {
        return failed_write(std::move(error));
    }
    FileLeaseGuard lease(*file_leases_, info.st_dev, info.st_ino,
                         file_leases_->try_acquire(info.st_dev, info.st_ino));
    if (!lease.held()) {
        return failed_write(
            provider_error("busy", "another write to this file is in progress"));
    }
    return commit_via_temp(parent.fd, path, name, content, static_cast<mode_t>(info.st_mode & 07777),
                           true, cancel, target.fd, info);
}

FileEditOutcome LinuxDesktopEnvironment::edit_file(const std::filesystem::path &path,
                                                   const std::string &old_string,
                                                   const std::string &new_string, bool replace_all,
                                                   const FileObservation &observation,
                                                   const FileModifyLimits &limits,
                                                   const CancelToken &cancel) {
    ProviderError error;
    FdGuard parent;
    std::string name;
    if (!mutation_prologue(path, write_scope_, limits, cancel, parent, name, error)) {
        return failed_edit(std::move(error));
    }
    if (old_string.empty()) {
        return failed_edit(provider_error("invalid_argument", "old_string must not be empty"));
    }
    const FdGuard target(
        ::openat(parent.fd, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (target.fd < 0) {
        if (errno == ELOOP) {
            return failed_edit(provider_error("invalid_argument",
                                              "write target is a symbolic link: " + path.string()));
        }
        if (errno == ENOENT) {
            return failed_edit(
                provider_error("not_found", "file does not exist: " + path.string()));
        }
        return failed_edit(provider_error("io_error", "cannot open file: " + path.string()));
    }
    struct stat info {};
    if (::fstat(target.fd, &info) != 0) {
        return failed_edit(provider_error("io_error", "cannot stat file: " + path.string()));
    }
    if (!S_ISREG(info.st_mode)) {
        return failed_edit(
            provider_error("not_regular_file", "path is not a regular file: " + path.string()));
    }
    if (info.st_nlink > 1) {
        return failed_edit(provider_error(
            "invalid_argument",
            "target has multiple hard links; refusing to change link semantics: " + path.string()));
    }
    if (static_cast<std::uint64_t>(info.st_size) > limits.max_content_bytes) {
        return failed_edit(provider_error("file_too_large",
                                          "file exceeds the modify budget of " +
                                              std::to_string(limits.max_content_bytes) +
                                              " bytes: " + path.string()));
    }
    std::string content;
    bool truncated = false;
    switch (read_all_bounded(target.fd, limits.max_content_bytes, cancel, content, truncated)) {
    case ReadAllStatus::Ok:
        break;
    case ReadAllStatus::TooLarge:
        return failed_edit(provider_error("file_too_large", "file grew beyond the modify budget"));
    case ReadAllStatus::Cancelled:
        return failed_edit(provider_error("cancelled", "read was cancelled"));
    case ReadAllStatus::IoError:
        return failed_edit(provider_error("io_error", "read failed: " + path.string()));
    }
    if (!is_valid_utf8_text(content)) {
        return failed_edit(
            provider_error("unsupported_encoding", "file is not valid UTF-8 text: " + path.string()));
    }
    if (!observation_matches(observation, info, content, error)) {
        return failed_edit(std::move(error));
    }

    // Match in the "\r\n"-normalized view, act on raw bytes: every match
    // range must sit inside the caller's observation (DEC-095 decision 3).
    std::vector<std::uint32_t> view_to_raw;
    const std::string view = normalized_view(content, view_to_raw);
    const auto matches = find_occurrences(view, old_string);
    if (matches.empty()) {
        return failed_edit(provider_error("match_not_found",
                                          "old_string does not occur in the observed content"));
    }
    if (matches.size() > 1 && !replace_all) {
        return failed_edit(provider_error(
            "ambiguous_match", "old_string occurs " + std::to_string(matches.size()) +
                                   " times; re-read and provide more context or use replace_all"));
    }
    if (replace_all && !observation.full) {
        return failed_edit(provider_error(
            "not_observed", "replace_all requires a full observation of the current content"));
    }
    std::vector<RawSpan> spans;
    spans.reserve(matches.size());
    for (const auto &match : matches) {
        const RawSpan span = raw_span(view_to_raw, match.first, match.second, content.size());
        if (!observation.covers(span.begin, span.end - span.begin)) {
            return failed_edit(provider_error(
                "not_observed", "the match range was not observed; re-read the file region"));
        }
        spans.push_back(span);
    }
    FileEditOutcome outcome;
    outcome.replacements = matches.size();
    if (old_string == new_string) {
        // A well-formed no-op: the file stays byte-identical and the caller
        // must not dress the call up as a modification (DEC-095).
        outcome.ok = true;
        outcome.applied = false;
        outcome.version = version_from_stat(info, content_digest_hex(content));
        return outcome;
    }
    std::string updated;
    updated.reserve(content.size() + new_string.size() * matches.size());
    std::size_t cursor = 0;
    for (const RawSpan &span : spans) {
        updated.append(content, cursor, static_cast<std::size_t>(span.begin) - cursor);
        updated.append(new_string);
        cursor = static_cast<std::size_t>(span.end);
    }
    updated.append(content, cursor, content.size() - cursor);
    if (updated.size() > limits.max_content_bytes) {
        return failed_edit(provider_error("file_too_large",
                                          "edited content would exceed the modify budget of " +
                                              std::to_string(limits.max_content_bytes) +
                                              " bytes"));
    }
    FileLeaseGuard lease(*file_leases_, info.st_dev, info.st_ino,
                         file_leases_->try_acquire(info.st_dev, info.st_ino));
    if (!lease.held()) {
        return failed_edit(provider_error("busy", "another write to this file is in progress"));
    }
    const FileWriteOutcome committed =
        commit_via_temp(parent.fd, path, name, updated, static_cast<mode_t>(info.st_mode & 07777),
                        true, cancel, target.fd, info);
    if (!committed.ok) {
        return failed_edit(committed.error);
    }
    outcome.ok = true;
    outcome.applied = true;
    outcome.version = committed.version;
    return outcome;
}

ProcessOutcome LinuxDesktopEnvironment::execute_request(const ProcessRequest &request,
                                                        const ProcessLimits &limits,
                                                        const CancelToken &cancel) {
    if (request.command.empty()) {
        ProcessOutcome outcome;
        outcome.error = provider_error("invalid_argument", "command must not be empty");
        return outcome;
    }
    FdGuard working_dir;
    if (!request.working_directory.empty()) {
        if (!request.working_directory.is_absolute()) {
            ProcessOutcome outcome;
            outcome.error = provider_error("invalid_argument",
                                           "working directory must be absolute: " +
                                               request.working_directory.string());
            return outcome;
        }
        working_dir.fd = ::open(request.working_directory.c_str(),
                                O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (working_dir.fd < 0) {
            ProcessOutcome outcome;
            if (errno == ENOENT) {
                outcome.error = provider_error(
                    "not_found", "working directory does not exist: " +
                                     request.working_directory.string());
            } else if (errno == ENOTDIR) {
                outcome.error = provider_error(
                    "not_regular_file",
                    "working directory is not a directory: " + request.working_directory.string());
            } else if (errno == EACCES || errno == EPERM) {
                outcome.error = provider_error(
                    "permission_denied",
                    "working directory is not accessible: " + request.working_directory.string());
            } else {
                outcome.error = provider_error("io_error",
                                               "cannot open working directory: " +
                                                   request.working_directory.string());
            }
            return outcome;
        }
    }
    return execute_common(request.command, limits, cancel, working_dir.fd,
                          request.output_sink);
}

} // namespace mirage::platform::linux_backend
