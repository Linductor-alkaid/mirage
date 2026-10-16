// M10-04/M10-05 (DEC-095) Linux backend verification (independent
// verification pass). Covers the windowed text read matrix (paging, eof,
// empty file, offset bounds, CRLF, BOM, oversize lines, non-UTF-8,
// directories, scan budgets, digest availability across the 1 MiB modify
// budget), the version/staleness facts, the create/replace/edit commit
// matrix (success byte comparisons, every documented rejection, symlink and
// hardlink fail-closed paths, write-scope containment, temp-file hygiene,
// pre-commit cancellation, mode preservation), the SHA-256 known vectors,
// and the shell execution additions (explicit working directory including
// space and non-ASCII names, exit-code-as-result, dual streams, preview
// truncation, whole-group timeout teardown, cancellation, the retention
// sink seam) plus the legacy execute() regression.

#include "../support/test.hpp"

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/path_scope.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace desktop = mirage::desktop;
namespace linux_backend = mirage::platform::linux_backend;

using clock_ = std::chrono::steady_clock;

// --- fixtures ------------------------------------------------------------------

class TempWorkspace {
  public:
    TempWorkspace() {
        std::error_code ec;
        root_ = std::filesystem::temp_directory_path(ec) /
                ("mirage-m10-files-" + std::to_string(::getpid()) + "-" +
                 std::to_string(clock_::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_, ec);
    }
    ~TempWorkspace() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    TempWorkspace(const TempWorkspace &) = delete;
    TempWorkspace &operator=(const TempWorkspace &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

std::unique_ptr<linux_backend::LinuxDesktopEnvironment>
make_env(const std::vector<std::filesystem::path> &read_roots,
         const std::vector<std::filesystem::path> &write_roots) {
    return std::make_unique<linux_backend::LinuxDesktopEnvironment>(
        read_roots, linux_backend::X11Options{}, linux_backend::AtspiOptions{},
        linux_backend::ApplicationOptions{}, linux_backend::NotificationsOptions{},
        linux_backend::FilesystemWriteOptions{write_roots});
}

void spew(const std::filesystem::path &path, const std::string &content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
    MIRAGE_CHECK(stream.good());
}

std::string slurp(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

desktop::TextWindowOutcome read_window(linux_backend::LinuxDesktopEnvironment &env,
                                       const std::filesystem::path &path, std::uint64_t skip = 0,
                                       std::size_t max_lines = 2000,
                                       std::size_t max_bytes = 50u << 10,
                                       std::size_t max_scan_bytes = 16u << 20) {
    desktop::TextWindowLimits limits;
    limits.max_lines = max_lines;
    limits.max_bytes = max_bytes;
    limits.max_scan_bytes = max_scan_bytes;
    return env.read_text_window(path, skip, limits, desktop::CancelToken{});
}

/// The observation a full windowed read authorizes (the toolset's rule).
desktop::FileObservation full_observation(const desktop::TextWindowOutcome &window) {
    desktop::FileObservation observation;
    observation.version = window.version;
    observation.full = true;
    return observation;
}

std::size_t count_temp_files(const std::filesystem::path &dir) {
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(".mirage-tmp-", 0) == 0) {
            ++count;
        }
    }
    return count;
}

bool process_is_gone(pid_t pid, std::chrono::milliseconds grace) {
    const auto deadline = clock_::now() + grace;
    while (clock_::now() < deadline) {
        if (::kill(pid, 0) != 0 && errno == ESRCH) {
            return true;
        }
        ::usleep(20000);
    }
    return ::kill(pid, 0) != 0 && errno == ESRCH;
}

// --- capabilities and scope isolation -------------------------------------------

void scenario_capabilities_and_scope_isolation(const std::filesystem::path &root) {
    std::filesystem::create_directories(root / "allowed");
    spew(root / "plain.txt", "readable\n");

    // Read + write roots: the full M10 surface.
    {
        auto env = make_env({root}, {root});
        const auto caps = env->capabilities();
        MIRAGE_CHECK(caps.windowed_read);
        MIRAGE_CHECK(caps.write);
        MIRAGE_CHECK(caps.edit);
        MIRAGE_CHECK(env->supports_working_directory());
        MIRAGE_CHECK(env->supports_output_retention());
    }
    // Read-only environment: windowed read exists, every mutation is absent —
    // capability honesty, not a per-call permission error.
    {
        auto env = make_env({root}, {});
        const auto caps = env->capabilities();
        MIRAGE_CHECK(caps.windowed_read);
        MIRAGE_CHECK(!caps.write);
        MIRAGE_CHECK(!caps.edit);
        const auto created = env->create_file(root / "plain.txt", "x", desktop::FileModifyLimits{},
                                              desktop::CancelToken{});
        MIRAGE_CHECK(!created.ok);
        MIRAGE_CHECK(created.error.code == std::string("permission_denied"));
        MIRAGE_CHECK(slurp(root / "plain.txt") == "readable\n");
    }
    // No roots at all: nothing is readable, and the Linux backend still
    // reports windowed_read as its delivered capability (the scope refusal is
    // per call), but reports no write/edit capability.
    {
        auto env = make_env({}, {});
        const auto caps = env->capabilities();
        MIRAGE_CHECK(caps.windowed_read);
        MIRAGE_CHECK(!caps.write);
        MIRAGE_CHECK(!caps.edit);
        const auto window = read_window(*env, root / "plain.txt");
        MIRAGE_CHECK(!window.ok);
        MIRAGE_CHECK(window.error.code == std::string("permission_denied"));
    }
    // Split roots: the read root never implies write authority.
    {
        auto env = make_env({root}, {root / "allowed"});
        const auto outside = env->create_file(root / "outside.txt", "x",
                                              desktop::FileModifyLimits{}, desktop::CancelToken{});
        MIRAGE_CHECK(!outside.ok);
        MIRAGE_CHECK(outside.error.code == std::string("permission_denied"));
        MIRAGE_CHECK(!std::filesystem::exists(root / "outside.txt"));
        const auto inside = env->create_file(root / "allowed" / "inside.txt", "x",
                                             desktop::FileModifyLimits{}, desktop::CancelToken{});
        MIRAGE_CHECK(inside.ok && inside.applied);
        MIRAGE_CHECK(slurp(root / "allowed" / "inside.txt") == "x");
    }
}

// --- SHA-256 known vectors (backend-internal digest) ------------------------------

void scenario_sha256_known_vectors(const std::filesystem::path &root,
                                   linux_backend::LinuxDesktopEnvironment &env) {
    // SHA-256("") = e3b0c442... (empty file window, skip 0).
    spew(root / "empty.txt", "");
    const auto empty_window = read_window(env, root / "empty.txt");
    MIRAGE_CHECK(empty_window.ok);
    MIRAGE_CHECK(empty_window.version.content_digest ==
                 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    MIRAGE_CHECK(empty_window.version.size == 0);

    // SHA-256("abc") = ba7816bf...
    spew(root / "abc.txt", "abc");
    const auto abc_window = read_window(env, root / "abc.txt");
    MIRAGE_CHECK(abc_window.ok);
    MIRAGE_CHECK(abc_window.version.content_digest ==
                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    MIRAGE_CHECK(abc_window.version.size == 3);

    // Digest stability: equal content digests equally, unequal content does
    // not, and re-reading one file is stable.
    spew(root / "same-a.txt", "abc");
    const auto again = read_window(env, root / "same-a.txt");
    MIRAGE_CHECK(again.version.content_digest == abc_window.version.content_digest);
    const auto reread = read_window(env, root / "abc.txt");
    MIRAGE_CHECK(reread.version.content_digest == abc_window.version.content_digest);
}

// --- windowed read matrix ---------------------------------------------------------

void scenario_window_paging_and_facts(const std::filesystem::path &root,
                                      linux_backend::LinuxDesktopEnvironment &env) {
    const auto path = root / "paged.txt";
    spew(path, "l1\nl2\nl3\nl4\nl5\n");

    const auto first = read_window(env, path, 0, 2);
    MIRAGE_CHECK(first.ok);
    MIRAGE_CHECK(first.first_line == 1);
    MIRAGE_CHECK(first.lines.size() == 2);
    MIRAGE_CHECK(first.lines[0] == "l1");
    MIRAGE_CHECK(first.lines[1] == "l2");
    MIRAGE_CHECK(!first.eof);
    MIRAGE_CHECK(first.next_offset == 2);
    MIRAGE_CHECK(!first.total_lines.has_value());
    MIRAGE_CHECK(first.observed_range.offset == 0);
    MIRAGE_CHECK(first.observed_range.length == 6);
    MIRAGE_CHECK(first.version.size == 15);
    MIRAGE_CHECK(first.version.digest_available());
    if (first.version.device == 0 && first.version.file_index == 0) {
        struct stat identity {};
        MIRAGE_CHECK(::stat(path.c_str(), &identity) == 0);
        std::fprintf(stderr,
                     "[linux_file_actions_test] window version lacks identity: dev=%llu ino=%llu "
                     "but fstat says dev=%llu ino=%llu (the observation could never re-verify)\n",
                     static_cast<unsigned long long>(first.version.device),
                     static_cast<unsigned long long>(first.version.file_index),
                     static_cast<unsigned long long>(identity.st_dev),
                     static_cast<unsigned long long>(identity.st_ino));
    }
    MIRAGE_CHECK(first.version.device != 0 || first.version.file_index != 0);

    const auto middle = read_window(env, path, 2, 2);
    MIRAGE_CHECK(middle.ok);
    MIRAGE_CHECK(middle.first_line == 3);
    MIRAGE_CHECK(middle.lines.size() == 2);
    MIRAGE_CHECK(!middle.eof);
    MIRAGE_CHECK(middle.next_offset == 4);
    MIRAGE_CHECK(middle.observed_range.offset == 6);
    MIRAGE_CHECK(middle.observed_range.length == 6);
    MIRAGE_CHECK(middle.version == first.version);

    const auto last = read_window(env, path, 4, 10);
    MIRAGE_CHECK(last.ok);
    MIRAGE_CHECK(last.first_line == 5);
    MIRAGE_CHECK(last.lines.size() == 1);
    MIRAGE_CHECK(last.eof);
    MIRAGE_CHECK(last.next_offset == 5);
    MIRAGE_CHECK(last.total_lines.value_or(0) == 5);
    MIRAGE_CHECK(last.observed_range.offset == 12);
    MIRAGE_CHECK(last.observed_range.length == 3);

    // A window past the last line is a caller bug, not an empty window.
    const auto beyond = read_window(env, path, 5);
    MIRAGE_CHECK(!beyond.ok);
    MIRAGE_CHECK(beyond.error.code == std::string("invalid_argument"));
    const auto far_beyond = read_window(env, path, 6);
    MIRAGE_CHECK(!far_beyond.ok);

    // Non-positive budgets are refused before any read.
    desktop::TextWindowLimits zero_lines;
    zero_lines.max_lines = 0;
    const auto zero = env.read_text_window(path, 0, zero_lines, desktop::CancelToken{});
    MIRAGE_CHECK(!zero.ok);
    MIRAGE_CHECK(zero.error.code == std::string("invalid_argument"));

    // Path hygiene.
    const auto relative = env.read_text_window(std::filesystem::path{"relative.txt"}, 0,
                                               desktop::TextWindowLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!relative.ok);
    MIRAGE_CHECK(relative.error.code == std::string("invalid_argument"));
    const auto missing = read_window(env, root / "absent.txt");
    MIRAGE_CHECK(!missing.ok);
    MIRAGE_CHECK(missing.error.code == std::string("not_found"));
    const auto directory = read_window(env, root);
    MIRAGE_CHECK(!directory.ok);
    MIRAGE_CHECK(directory.error.code == std::string("not_regular_file"));

    // A pre-cancelled token fails the read without content.
    desktop::CancelToken cancelled;
    cancelled.request_cancel();
    const auto cancelled_outcome =
        env.read_text_window(path, 0, desktop::TextWindowLimits{}, cancelled);
    MIRAGE_CHECK(!cancelled_outcome.ok);
    MIRAGE_CHECK(cancelled_outcome.error.code == std::string("cancelled"));

    // Read containment: a path outside the read scope is refused.
    auto narrow = make_env({root / "paged-roots"}, {});
    std::filesystem::create_directories(root / "paged-roots");
    spew(root / "outside-scope.txt", "secret\n");
    const auto outside = read_window(*narrow, root / "outside-scope.txt");
    MIRAGE_CHECK(!outside.ok);
    MIRAGE_CHECK(outside.error.code == std::string("permission_denied"));
}

void scenario_window_text_facts(const std::filesystem::path &root,
                                linux_backend::LinuxDesktopEnvironment &env) {
    // Empty file: the legal empty eof window.
    {
        const auto path = root / "empty-window.txt";
        spew(path, "");
        const auto window = read_window(env, path, 0);
        MIRAGE_CHECK(window.ok);
        MIRAGE_CHECK(window.eof);
        MIRAGE_CHECK(window.lines.empty());
        MIRAGE_CHECK(window.total_lines.value_or(-1) == 0);
        MIRAGE_CHECK(window.next_offset == 0);
        MIRAGE_CHECK(!window.has_bom);
        MIRAGE_CHECK(!window.has_crlf);
        const auto beyond = read_window(env, path, 1);
        MIRAGE_CHECK(!beyond.ok);
        MIRAGE_CHECK(beyond.error.code == std::string("invalid_argument"));
    }
    // Unterminated final line still counts and ends the window at eof.
    {
        const auto path = root / "unterminated.txt";
        spew(path, "a\nb");
        const auto window = read_window(env, path, 1);
        MIRAGE_CHECK(window.ok);
        MIRAGE_CHECK(window.lines.size() == 1);
        MIRAGE_CHECK(window.lines[0] == "b");
        MIRAGE_CHECK(window.eof);
        MIRAGE_CHECK(window.total_lines.value_or(0) == 2);
        MIRAGE_CHECK(window.observed_range.offset == 2);
        MIRAGE_CHECK(window.observed_range.length == 1);
    }
    // CRLF terminators read as "\n" in the line view (the view is what
    // edit_file matches on); the digest covers the raw bytes.
    {
        const auto path = root / "crlf.txt";
        spew(path, "a\r\nb\r\n");
        const auto window = read_window(env, path, 0);
        MIRAGE_CHECK(window.ok);
        MIRAGE_CHECK(window.lines.size() == 2);
        MIRAGE_CHECK(window.lines[0] == std::string("a"));
        MIRAGE_CHECK(window.lines[1] == std::string("b"));
        MIRAGE_CHECK(window.has_crlf);
        MIRAGE_CHECK(window.version.size == 6);
        MIRAGE_CHECK(window.eof);
    }
    // BOM stays in the first line and is reported only for the first window.
    {
        const auto path = root / "bom.txt";
        spew(path, "\xEF\xBB\xBFhi\nsecond\n");
        const auto first = read_window(env, path, 0);
        MIRAGE_CHECK(first.ok);
        MIRAGE_CHECK(first.has_bom);
        MIRAGE_CHECK(first.lines[0] == std::string("\xEF\xBB\xBFhi"));
        const auto second = read_window(env, path, 1);
        MIRAGE_CHECK(second.ok);
        MIRAGE_CHECK(!second.has_bom);
        MIRAGE_CHECK(second.lines[0] == "second");
    }
    // A single line beyond the window budget is named line_too_long with the
    // length fact; a line exactly at the budget is collected.
    {
        const auto path = root / "giant.txt";
        spew(path, "ok\n" + std::string(12, 'x') + "\n");
        desktop::TextWindowLimits tight;
        tight.max_lines = 2000;
        tight.max_bytes = 8;
        const auto refused = env.read_text_window(path, 0, tight, desktop::CancelToken{});
        MIRAGE_CHECK(!refused.ok);
        MIRAGE_CHECK(refused.error.code == std::string("line_too_long"));
        MIRAGE_CHECK(refused.error.message.find("12") != std::string::npos);

        const auto boundary_path = root / "boundary.txt";
        spew(boundary_path, "abcde\nz\n");
        desktop::TextWindowLimits exact;
        exact.max_lines = 2000;
        exact.max_bytes = 5;
        const auto collected =
            env.read_text_window(boundary_path, 0, exact, desktop::CancelToken{});
        MIRAGE_CHECK(collected.ok);
        MIRAGE_CHECK(collected.lines.size() == 1);
        MIRAGE_CHECK(collected.lines[0] == "abcde");
        MIRAGE_CHECK(!collected.eof);
        MIRAGE_CHECK(collected.next_offset == 1);
    }
    // A spent byte budget stops the window with continuation facts, never a
    // silent cut or an error.
    {
        const auto path = root / "budget-stop.txt";
        spew(path, "aaaa\nbbbb\ncccc\n");
        desktop::TextWindowLimits tight;
        tight.max_lines = 2000;
        tight.max_bytes = 9;
        const auto window = env.read_text_window(path, 0, tight, desktop::CancelToken{});
        MIRAGE_CHECK(window.ok);
        MIRAGE_CHECK(window.lines.size() == 2);
        MIRAGE_CHECK(window.lines[1] == "bbbb");
        MIRAGE_CHECK(!window.eof);
        MIRAGE_CHECK(window.next_offset == 2);
    }
    // NUL bytes and invalid UTF-8 are unsupported_encoding, never mangled.
    {
        const auto nul_path = root / "nul.txt";
        spew(nul_path, std::string("a\0b\n", 4)); // \0 ends the octal escape; b stays literal
        const auto nul = read_window(env, nul_path);
        MIRAGE_CHECK(!nul.ok);
        MIRAGE_CHECK(nul.error.code == std::string("unsupported_encoding"));

        const auto latin1_path = root / "latin1.txt";
        spew(latin1_path, "caf\xe9\n");
        const auto latin1 = read_window(env, latin1_path);
        MIRAGE_CHECK(!latin1.ok);
        MIRAGE_CHECK(latin1.error.code == std::string("unsupported_encoding"));
    }
}

void scenario_window_oversize_stream(const std::filesystem::path &root,
                                     linux_backend::LinuxDesktopEnvironment &env) {
    // Exactly 1 MiB: still the in-memory path, digest available.
    {
        std::string content;
        content.reserve(1u << 20);
        for (std::size_t i = 0; i < (1u << 20) / 64; ++i) {
            content += std::string(63, 'a') + "\n";
        }
        MIRAGE_CHECK(content.size() == 1u << 20);
        const auto path = root / "exact-1mib.txt";
        spew(path, content);
        const auto window = read_window(env, path, 0, 4);
        MIRAGE_CHECK(window.ok);
        MIRAGE_CHECK(window.version.digest_available());
        MIRAGE_CHECK(window.version.size == 1u << 20);
    }
    // 1 MiB + 1: the bounded streaming scan, no digest, correct paging.
    {
        std::string content;
        content.reserve((1u << 20) + 1);
        for (std::size_t i = 0; i < (1u << 20) / 64; ++i) {
            content += std::string(63, 'a') + "\n";
        }
        content += "y";
        MIRAGE_CHECK(content.size() == (1u << 20) + 1);
        const auto path = root / "over-1mib.txt";
        spew(path, content);
        const auto window = read_window(env, path, 0, 4);
        MIRAGE_CHECK(window.ok);
        MIRAGE_CHECK(!window.version.digest_available());
        MIRAGE_CHECK(window.version.size == (1u << 20) + 1);
        MIRAGE_CHECK(window.lines.size() == 4);
        MIRAGE_CHECK(!window.eof);
        MIRAGE_CHECK(window.first_line == 1);
        MIRAGE_CHECK(window.observed_range.offset == 0);
        MIRAGE_CHECK(window.observed_range.length == 256);
        const auto tail = read_window(env, path, (1u << 20) / 64);
        MIRAGE_CHECK(tail.ok);
        MIRAGE_CHECK(tail.lines.size() == 1);
        if (!tail.lines.empty()) {
            MIRAGE_CHECK(tail.lines[0] == "y");
        } else {
            // Diagnostic evidence for the streamed-tail finding; guarded so a
            // wrong line count cannot turn into a segfault.
            std::fprintf(stderr,
                         "[linux_file_actions_test] streamed tail: lines=0 eof=%d "
                         "next_offset=%llu total=%s (expected the final unterminated line)\n",
                         tail.eof, static_cast<unsigned long long>(tail.next_offset),
                         tail.total_lines ? std::to_string(*tail.total_lines).c_str() : "-");
        }
        MIRAGE_CHECK(tail.eof);
        MIRAGE_CHECK(tail.total_lines.value_or(0) == (1u << 20) / 64 + 1);
        MIRAGE_CHECK(!tail.version.digest_available());
        const auto beyond = read_window(env, path, (1u << 20) / 64 + 2);
        MIRAGE_CHECK(!beyond.ok);
        MIRAGE_CHECK(beyond.error.code == std::string("invalid_argument"));
    }
    // Numbered-line oversize file: far skips land on the right lines and the
    // eof window reports the total.
    {
        constexpr std::size_t kLines = 140000;
        std::string content;
        content.reserve(kLines * 8);
        char buffer[16];
        for (std::size_t i = 0; i < kLines; ++i) {
            std::snprintf(buffer, sizeof(buffer), "%07zu\n", i);
            content += buffer;
        }
        MIRAGE_CHECK(content.size() > 1u << 20);
        const auto path = root / "numbered.txt";
        spew(path, content);
        const auto head = read_window(env, path, 2, 3);
        MIRAGE_CHECK(head.ok);
        MIRAGE_CHECK(head.first_line == 3);
        MIRAGE_CHECK(head.lines.size() == 3);
        MIRAGE_CHECK(head.lines[0] == "0000002");
        MIRAGE_CHECK(!head.version.digest_available());
        const auto tail = read_window(env, path, kLines - 2);
        MIRAGE_CHECK(tail.ok);
        MIRAGE_CHECK(tail.lines.size() == 2);
        MIRAGE_CHECK(tail.lines[1] == "0139999"); // %07zu zero padding
        MIRAGE_CHECK(tail.eof);
        MIRAGE_CHECK(tail.total_lines.value_or(0) == kLines);
        // A stale continuation landing exactly at eof of a newline-terminated
        // file follows the in-memory rule: invalid_argument, not an empty
        // window (the eof window already carried next_offset).
        const auto stale_offset = read_window(env, path, kLines);
        MIRAGE_CHECK(!stale_offset.ok);
        MIRAGE_CHECK(stale_offset.error.code == std::string("invalid_argument"));
        // Scan budget: a scan past its byte budget is refused, not
        // truncated. The line/byte budgets are set high enough that the
        // drain cannot stop first, so the byte budget itself trips.
        desktop::TextWindowLimits scan_starved;
        scan_starved.max_lines = 1000000;
        scan_starved.max_bytes = 1u << 20;
        scan_starved.max_scan_bytes = 4;
        const auto refused = env.read_text_window(path, 0, scan_starved, desktop::CancelToken{});
        MIRAGE_CHECK(!refused.ok);
        MIRAGE_CHECK(refused.error.code == std::string("scan_budget_exceeded"));
    }
    // Oversize CRLF + BOM: the streamed window still reports both facts.
    {
        std::string content = "\xEF\xBB\xBF";
        for (std::size_t i = 0; i < 300000; ++i) {
            content += "ab\r\n";
        }
        MIRAGE_CHECK(content.size() > 1u << 20);
        const auto path = root / "oversize-crlf.txt";
        spew(path, content);
        const auto window = read_window(env, path, 0, 3);
        MIRAGE_CHECK(window.ok);
        MIRAGE_CHECK(window.has_crlf);
        MIRAGE_CHECK(window.has_bom);
        MIRAGE_CHECK(window.lines[0] == std::string("\xEF\xBB\xBF"
                                                    "ab"));
        MIRAGE_CHECK(!window.version.digest_available());
    }
}

// --- create -----------------------------------------------------------------------

void scenario_create_matrix(const std::filesystem::path &root,
                            linux_backend::LinuxDesktopEnvironment &env) {
    const auto dir = root / "create";
    std::filesystem::create_directories(dir);

    const auto created = env.create_file(dir / "new.txt", "hello\n", desktop::FileModifyLimits{},
                                         desktop::CancelToken{});
    MIRAGE_CHECK(created.ok);
    MIRAGE_CHECK(created.applied);
    MIRAGE_CHECK(created.created);
    MIRAGE_CHECK(!created.replaced);
    MIRAGE_CHECK(created.bytes_written == 6);
    MIRAGE_CHECK(slurp(dir / "new.txt") == "hello\n");
    MIRAGE_CHECK(count_temp_files(dir) == 0);
    // The committed version re-verifies against a windowed read of the file.
    const auto window = read_window(env, dir / "new.txt");
    MIRAGE_CHECK(window.ok);
    MIRAGE_CHECK(window.version == created.version);

    // The create-if-absent refusal: an existing target is never overwritten.
    const auto existing = env.create_file(dir / "new.txt", "over!", desktop::FileModifyLimits{},
                                          desktop::CancelToken{});
    MIRAGE_CHECK(!existing.ok);
    MIRAGE_CHECK(existing.error.code == std::string("already_exists"));
    MIRAGE_CHECK(slurp(dir / "new.txt") == "hello\n");
    MIRAGE_CHECK(count_temp_files(dir) == 0);

    // A symlink at the target path is an existing directory entry: refused
    // without following it, and the link is untouched.
    std::filesystem::create_symlink("new.txt", dir / "link.txt");
    const auto over_link = env.create_file(dir / "link.txt", "over!", desktop::FileModifyLimits{},
                                           desktop::CancelToken{});
    MIRAGE_CHECK(!over_link.ok);
    MIRAGE_CHECK(over_link.error.code == std::string("already_exists"));
    MIRAGE_CHECK(std::filesystem::is_symlink(dir / "link.txt"));
    MIRAGE_CHECK(slurp(dir / "new.txt") == "hello\n");

    // Parent directory must exist; it is never created implicitly.
    const auto missing = env.create_file(dir / "absent-dir" / "f.txt", "x",
                                         desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!missing.ok);
    MIRAGE_CHECK(missing.error.code == std::string("not_found"));

    // Path hygiene.
    const auto relative = env.create_file(std::filesystem::path{"rel.txt"}, "x",
                                          desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!relative.ok);
    MIRAGE_CHECK(relative.error.code == std::string("invalid_argument"));
    const auto dot_dot =
        env.create_file(dir / "..", "x", desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!dot_dot.ok);
    MIRAGE_CHECK(dot_dot.error.code == std::string("invalid_argument"));
    const auto empty = env.create_file(std::filesystem::path{}, "x", desktop::FileModifyLimits{},
                                       desktop::CancelToken{});
    MIRAGE_CHECK(!empty.ok);

    // A symbolic link inside the parent chain is rejected, not followed.
    std::filesystem::create_directories(dir / "real-dir");
    std::filesystem::create_directory_symlink(dir / "real-dir", dir / "link-dir");
    const auto through_link = env.create_file(dir / "link-dir" / "f.txt", "x",
                                              desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!through_link.ok);
    MIRAGE_CHECK(through_link.error.code == std::string("invalid_argument"));
    MIRAGE_CHECK(!std::filesystem::exists(dir / "real-dir" / "f.txt"));

    // Content beyond the budget is refused before any side effect.
    desktop::FileModifyLimits tiny;
    tiny.max_content_bytes = 4;
    const auto oversize = env.create_file(dir / "big.txt", "12345", tiny, desktop::CancelToken{});
    MIRAGE_CHECK(!oversize.ok);
    MIRAGE_CHECK(oversize.error.code == std::string("file_too_large"));
    MIRAGE_CHECK(!std::filesystem::exists(dir / "big.txt"));

    // Pre-cancelled token: cancelled, zero side effects.
    desktop::CancelToken cancelled;
    cancelled.request_cancel();
    const auto cancelled_outcome =
        env.create_file(dir / "cancelled.txt", "x", desktop::FileModifyLimits{}, cancelled);
    MIRAGE_CHECK(!cancelled_outcome.ok);
    MIRAGE_CHECK(cancelled_outcome.error.code == std::string("cancelled"));
    MIRAGE_CHECK(!std::filesystem::exists(dir / "cancelled.txt"));

    // Default mode honors the process umask on the 0644 base.
    const mode_t umask_bits = ::umask(0);
    ::umask(umask_bits);
    struct stat info {};
    MIRAGE_CHECK(::stat((dir / "new.txt").c_str(), &info) == 0);
    MIRAGE_CHECK((info.st_mode & 07777) == (static_cast<mode_t>(0644) & ~umask_bits));

    MIRAGE_CHECK(count_temp_files(dir) == 0);
}

// --- replace ----------------------------------------------------------------------

void scenario_replace_matrix(const std::filesystem::path &root,
                             linux_backend::LinuxDesktopEnvironment &env) {
    const auto dir = root / "replace";
    std::filesystem::create_directories(dir);
    const auto path = dir / "target.txt";
    spew(path, "v1\n");

    // Happy path: full observation authorizes the atomic replace; mode is
    // preserved through the same-directory temp commit.
    const auto before = read_window(env, path);
    MIRAGE_CHECK(before.ok);
    ::chmod(path.c_str(), 0640);
    const auto replaced = env.replace_file(path, "v2-much-longer\n", full_observation(before),
                                           desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(replaced.ok);
    MIRAGE_CHECK(replaced.applied);
    MIRAGE_CHECK(replaced.replaced);
    MIRAGE_CHECK(!replaced.created);
    MIRAGE_CHECK(replaced.bytes_written == 15); // "v2-much-longer\n" is 15 bytes
    MIRAGE_CHECK(slurp(path) == "v2-much-longer\n");
    MIRAGE_CHECK(count_temp_files(dir) == 0);
    const auto after = read_window(env, path);
    MIRAGE_CHECK(after.ok);
    MIRAGE_CHECK(after.version == replaced.version);
    MIRAGE_CHECK(!(after.version == before.version));
    struct stat mode_info {};
    MIRAGE_CHECK(::stat(path.c_str(), &mode_info) == 0);
    MIRAGE_CHECK((mode_info.st_mode & 07777) == 0640);

    // A partial observation never authorizes a whole-file replace.
    desktop::FileObservation partial;
    partial.version = before.version;
    partial.full = false;
    partial.ranges.push_back({0, 3});
    const auto not_full =
        env.replace_file(path, "x", partial, desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!not_full.ok);
    MIRAGE_CHECK(not_full.error.code == std::string("not_observed"));
    MIRAGE_CHECK(slurp(path) == "v2-much-longer\n");

    // An observation without a digest authorizes nothing.
    desktop::FileObservation undigested;
    undigested.version = before.version;
    undigested.version.content_digest.clear();
    undigested.full = true;
    const auto no_digest = env.replace_file(path, "x", undigested, desktop::FileModifyLimits{},
                                            desktop::CancelToken{});
    MIRAGE_CHECK(!no_digest.ok);
    MIRAGE_CHECK(no_digest.error.code == std::string("not_observed"));

    // Staleness: an external rewrite invalidates the old observation.
    const auto stale_window = read_window(env, path);
    MIRAGE_CHECK(stale_window.ok);
    spew(path, "externally rewritten\n");
    const auto stale = env.replace_file(path, "x", full_observation(stale_window),
                                        desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!stale.ok);
    MIRAGE_CHECK(stale.error.code == std::string("stale_file"));
    MIRAGE_CHECK(slurp(path) == "externally rewritten\n");
    MIRAGE_CHECK(count_temp_files(dir) == 0);

    // A tampered digest fails the re-derivation even against an unchanged
    // file.
    const auto current = read_window(env, path);
    MIRAGE_CHECK(current.ok);
    desktop::FileObservation forged = full_observation(current);
    forged.version.content_digest = std::string(forged.version.content_digest.size(), '0');
    const auto forged_outcome =
        env.replace_file(path, "x", forged, desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!forged_outcome.ok);
    MIRAGE_CHECK(forged_outcome.error.code == std::string("stale_file"));

    // Symbolic link target: refused, the link's destination is untouched.
    spew(path, "real content\n");
    std::filesystem::create_symlink("target.txt", dir / "alias.txt");
    const auto through = read_window(env, path);
    MIRAGE_CHECK(through.ok);
    const auto link_replace =
        env.replace_file(dir / "alias.txt", "hijacked", full_observation(through),
                         desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!link_replace.ok);
    MIRAGE_CHECK(link_replace.error.code == std::string("invalid_argument"));
    MIRAGE_CHECK(slurp(path) == "real content\n");
    MIRAGE_CHECK(std::filesystem::is_symlink(dir / "alias.txt"));
    MIRAGE_CHECK(count_temp_files(dir) == 0);

    // Multiple hard links: refused to avoid changing link semantics.
    std::filesystem::create_hard_link(path, dir / "hard.txt");
    const auto hard_replace = env.replace_file(path, "x", full_observation(through),
                                               desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!hard_replace.ok);
    MIRAGE_CHECK(hard_replace.error.code == std::string("invalid_argument"));
    MIRAGE_CHECK(slurp(path) == "real content\n");
    MIRAGE_CHECK(slurp(dir / "hard.txt") == "real content\n");
    MIRAGE_CHECK(count_temp_files(dir) == 0);
    std::filesystem::remove(dir / "hard.txt");
    std::filesystem::remove(dir / "alias.txt");

    // Oversize target and oversize content are refused with file_too_large.
    const auto current2 = read_window(env, path);
    MIRAGE_CHECK(current2.ok);
    desktop::FileModifyLimits tiny;
    tiny.max_content_bytes = 4;
    const auto oversize_target =
        env.replace_file(path, "x", full_observation(current2), tiny, desktop::CancelToken{});
    MIRAGE_CHECK(!oversize_target.ok);
    MIRAGE_CHECK(oversize_target.error.code == std::string("file_too_large"));
    const auto oversize_content =
        env.replace_file(path, "12345", full_observation(current2), tiny, desktop::CancelToken{});
    MIRAGE_CHECK(!oversize_content.ok);
    MIRAGE_CHECK(oversize_content.error.code == std::string("file_too_large"));

    // Cancellation before the commit leaves the file byte-identical.
    desktop::CancelToken cancelled;
    cancelled.request_cancel();
    const auto cancelled_outcome = env.replace_file(path, "x", full_observation(current2),
                                                    desktop::FileModifyLimits{}, cancelled);
    MIRAGE_CHECK(!cancelled_outcome.ok);
    MIRAGE_CHECK(cancelled_outcome.error.code == std::string("cancelled"));
    MIRAGE_CHECK(!cancelled_outcome.applied);
    MIRAGE_CHECK(slurp(path) == "real content\n");
    MIRAGE_CHECK(count_temp_files(dir) == 0);

    // Missing target.
    const auto missing = env.replace_file(dir / "absent.txt", "x", full_observation(current2),
                                          desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!missing.ok);
    MIRAGE_CHECK(missing.error.code == std::string("not_found"));

    // Relative path.
    const auto relative =
        env.replace_file(std::filesystem::path{"rel.txt"}, "x", full_observation(current2),
                         desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!relative.ok);
    MIRAGE_CHECK(relative.error.code == std::string("invalid_argument"));
}

// --- edit -------------------------------------------------------------------------

void scenario_edit_matrix(const std::filesystem::path &root,
                          linux_backend::LinuxDesktopEnvironment &env) {
    const auto dir = root / "edit";
    std::filesystem::create_directories(dir);
    const auto path = dir / "code.txt";

    // Single unique match.
    spew(path, "hello world\n");
    const auto observed = read_window(env, path);
    MIRAGE_CHECK(observed.ok);
    const auto edited = env.edit_file(path, "world", "there", false, full_observation(observed),
                                      desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(edited.ok);
    MIRAGE_CHECK(edited.applied);
    MIRAGE_CHECK(edited.replacements == 1);
    MIRAGE_CHECK(slurp(path) == "hello there\n");
    MIRAGE_CHECK(count_temp_files(dir) == 0);
    const auto after = read_window(env, path);
    MIRAGE_CHECK(after.ok);
    MIRAGE_CHECK(after.version == edited.version);

    // Zero matches.
    const auto no_match = env.edit_file(path, "absent", "x", false, full_observation(after),
                                        desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!no_match.ok);
    MIRAGE_CHECK(no_match.error.code == std::string("match_not_found"));
    MIRAGE_CHECK(slurp(path) == "hello there\n");

    // Ambiguity without replace_all.
    spew(path, "dup dup\n");
    const auto dup = read_window(env, path);
    MIRAGE_CHECK(dup.ok);
    const auto ambiguous = env.edit_file(path, "dup", "x", false, full_observation(dup),
                                         desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!ambiguous.ok);
    MIRAGE_CHECK(ambiguous.error.code == std::string("ambiguous_match"));
    MIRAGE_CHECK(slurp(path) == "dup dup\n");
    // ... and replace_all replaces both occurrences.
    const auto all = env.edit_file(path, "dup", "x", true, full_observation(dup),
                                   desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(all.ok);
    MIRAGE_CHECK(all.applied);
    MIRAGE_CHECK(all.replacements == 2);
    MIRAGE_CHECK(slurp(path) == "x x\n");

    // replace_all demands a full observation even when the match is unique.
    const auto after_all = read_window(env, path);
    MIRAGE_CHECK(after_all.ok);
    desktop::FileObservation partial;
    partial.version = after_all.version;
    partial.full = false;
    partial.ranges.push_back(after_all.observed_range);
    const auto partial_all = env.edit_file(path, "x", "y", true, partial,
                                           desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!partial_all.ok);
    MIRAGE_CHECK(partial_all.error.code == std::string("not_observed"));
    MIRAGE_CHECK(slurp(path) == "x x\n");

    // A match outside the observed ranges is refused; a covered match goes
    // through on the same partial observation.
    spew(path, "alpha\nbeta\n");
    desktop::TextWindowLimits one_line;
    one_line.max_lines = 1;
    const auto first_line_window = env.read_text_window(path, 0, one_line, desktop::CancelToken{});
    MIRAGE_CHECK(first_line_window.ok && !first_line_window.eof);
    desktop::FileObservation line_one;
    line_one.version = first_line_window.version;
    line_one.full = false;
    line_one.ranges.push_back(first_line_window.observed_range);
    const auto unobserved = env.edit_file(path, "beta", "BETA", false, line_one,
                                          desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!unobserved.ok);
    MIRAGE_CHECK(unobserved.error.code == std::string("not_observed"));
    MIRAGE_CHECK(slurp(path) == "alpha\nbeta\n");
    const auto observed_edit = env.edit_file(path, "alpha", "ALPHA", false, line_one,
                                             desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(observed_edit.ok && observed_edit.applied);
    MIRAGE_CHECK(slurp(path) == "ALPHA\nbeta\n");

    // Well-formed no-op: old == new reports applied=false and writes nothing.
    const auto state = read_window(env, path);
    MIRAGE_CHECK(state.ok);
    struct stat before_stat {};
    MIRAGE_CHECK(::stat(path.c_str(), &before_stat) == 0);
    const auto noop = env.edit_file(path, "ALPHA", "ALPHA", false, full_observation(state),
                                    desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(noop.ok);
    MIRAGE_CHECK(!noop.applied);
    MIRAGE_CHECK(noop.replacements == 1);
    struct stat after_stat {};
    MIRAGE_CHECK(::stat(path.c_str(), &after_stat) == 0);
    MIRAGE_CHECK(before_stat.st_mtim.tv_sec == after_stat.st_mtim.tv_sec &&
                 before_stat.st_mtim.tv_nsec == after_stat.st_mtim.tv_nsec);
    MIRAGE_CHECK(slurp(path) == "ALPHA\nbeta\n");

    // Empty old_string is a caller bug, not a match.
    const auto empty_old = env.edit_file(path, "", "x", false, full_observation(state),
                                         desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!empty_old.ok);
    MIRAGE_CHECK(empty_old.error.code == std::string("invalid_argument"));

    // Deletion with an empty new_string.
    const auto deleted = env.edit_file(path, "beta\n", "", false, full_observation(state),
                                       desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(deleted.ok && deleted.applied);
    MIRAGE_CHECK(slurp(path) == "ALPHA\n");

    // CRLF files match in the normalized view while unmodified bytes keep
    // their original form.
    spew(path, "a\r\nb\r\n");
    const auto crlf = read_window(env, path);
    MIRAGE_CHECK(crlf.ok && crlf.has_crlf);
    const auto crlf_edit = env.edit_file(path, "a\nb", "x\ny", false, full_observation(crlf),
                                         desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(crlf_edit.ok && crlf_edit.applied);
    MIRAGE_CHECK(slurp(path) == "x\ny\r\n");
    MIRAGE_CHECK(count_temp_files(dir) == 0);

    // Stale observation, missing file, symlink and hardlink targets.
    const auto stale_window = read_window(env, path);
    MIRAGE_CHECK(stale_window.ok);
    spew(path, "changed\n");
    const auto stale = env.edit_file(path, "x\ny", "z", false, full_observation(stale_window),
                                     desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!stale.ok);
    MIRAGE_CHECK(stale.error.code == std::string("stale_file"));

    const auto current = read_window(env, path);
    MIRAGE_CHECK(current.ok);
    const auto missing =
        env.edit_file(dir / "absent.txt", "a", "b", false, full_observation(current),
                      desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!missing.ok);
    MIRAGE_CHECK(missing.error.code == std::string("not_found"));

    std::filesystem::create_symlink("code.txt", dir / "alias.txt");
    const auto link_edit =
        env.edit_file(dir / "alias.txt", "changed", "evil", false, full_observation(current),
                      desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!link_edit.ok);
    MIRAGE_CHECK(link_edit.error.code == std::string("invalid_argument"));
    MIRAGE_CHECK(slurp(path) == "changed\n");

    std::filesystem::create_hard_link(path, dir / "hard.txt");
    const auto hard_edit = env.edit_file(path, "changed", "x", false, full_observation(current),
                                         desktop::FileModifyLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(!hard_edit.ok);
    MIRAGE_CHECK(hard_edit.error.code == std::string("invalid_argument"));
    MIRAGE_CHECK(slurp(path) == "changed\n");
    std::filesystem::remove(dir / "hard.txt");
    std::filesystem::remove(dir / "alias.txt");
    MIRAGE_CHECK(count_temp_files(dir) == 0);
}

// --- single-writer lease (best effort; see report) ---------------------------------

void scenario_concurrent_replace_reports_busy_without_queueing(
    const std::filesystem::path &root, linux_backend::LinuxDesktopEnvironment &env) {
    const auto dir = root / "race";
    std::filesystem::create_directories(dir);
    const auto path = dir / "contended.txt";
    const std::string original(64, 'o');
    const std::string payload_a(384u << 10, 'a');
    const std::string payload_b(384u << 10, 'b');

    int busy_reports = 0;
    int stale_reports = 0;
    for (int round = 0; round < 6; ++round) {
        spew(path, original);
        const auto window = read_window(env, path);
        MIRAGE_CHECK(window.ok);
        const auto observation = full_observation(window);
        std::atomic<bool> go{false};
        desktop::FileWriteOutcome outcome_a, outcome_b;
        auto make_writer = [&](const std::string &payload, desktop::FileWriteOutcome &into) {
            return [&env, &path, &observation, payload, &into, &go]() {
                while (!go.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                into = env.replace_file(path, payload, observation, desktop::FileModifyLimits{},
                                        desktop::CancelToken{});
            };
        };
        std::thread writer_a = std::thread(make_writer(payload_a, outcome_a));
        std::thread writer_b = std::thread(make_writer(payload_b, outcome_b));
        go.store(true, std::memory_order_release);
        writer_a.join();
        writer_b.join();

        // At most one commit can win: the loser must fail closed with busy
        // (lease held by the winner) or stale_file (winner committed first),
        // and never queue behind it.
        const bool a_applied = outcome_a.ok && outcome_a.applied;
        const bool b_applied = outcome_b.ok && outcome_b.applied;
        MIRAGE_CHECK(!(a_applied && b_applied));
        if (a_applied || b_applied) {
            const auto &loser = a_applied ? outcome_b : outcome_a;
            MIRAGE_CHECK(!loser.ok);
            MIRAGE_CHECK(loser.error.code == std::string("busy") ||
                         loser.error.code == std::string("stale_file"));
            if (loser.error.code == "busy") {
                ++busy_reports;
            } else {
                ++stale_reports;
            }
            // The committed content is exactly one writer's payload (atomic
            // visibility, no interleaved bytes).
            const std::string &winner = a_applied ? payload_a : payload_b;
            MIRAGE_CHECK(slurp(path) == winner);
        } else {
            // Both failed closed: the file is untouched.
            MIRAGE_CHECK(slurp(path) == original);
        }
        MIRAGE_CHECK(count_temp_files(dir) == 0);
    }
    std::fprintf(stderr, "[linux_file_actions_test] lease race: %d busy, %d stale over 6 rounds\n",
                 busy_reports, stale_reports);
}

// --- exec -------------------------------------------------------------------------

class RecordingSink final : public desktop::ProcessOutputSink {
  public:
    bool retain(bool standard_output, std::string_view chunk) override {
        (standard_output ? stdout_ : stderr_).append(chunk);
        return true;
    }
    [[nodiscard]] const std::string &stdout_text() const { return stdout_; }
    [[nodiscard]] const std::string &stderr_text() const { return stderr_; }

  private:
    std::string stdout_, stderr_;
};

class RejectingSink final : public desktop::ProcessOutputSink {
  public:
    bool retain(bool, std::string_view) override {
        ++rejected_;
        return false;
    }
    [[nodiscard]] int rejected() const { return rejected_; }

  private:
    int rejected_ = 0;
};

void scenario_exec_working_directory(const std::filesystem::path &root,
                                     linux_backend::LinuxDesktopEnvironment &env) {
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{10000};

    // Space-bearing and non-ASCII directory names; the service cwd never
    // moves.
    const auto spaced = root / "dir with spaces";
    const auto unicode = root / "\xE7\x9B\xAE\xE5\xBD\x95-\xce\xBE"; // 目录-ξ
    MIRAGE_CHECK(std::filesystem::create_directories(spaced));
    MIRAGE_CHECK(std::filesystem::create_directories(unicode));
    char service_cwd[4096];
    MIRAGE_CHECK(::getcwd(service_cwd, sizeof(service_cwd)) != nullptr);

    desktop::ProcessRequest spaced_run;
    spaced_run.command = "pwd";
    spaced_run.working_directory = spaced;
    const auto spaced_outcome = env.execute_request(spaced_run, limits, desktop::CancelToken{});
    MIRAGE_CHECK(spaced_outcome.ok && spaced_outcome.exited_normally);
    MIRAGE_CHECK(spaced_outcome.exit_code == 0);
    MIRAGE_CHECK(spaced_outcome.standard_output == spaced.string() + "\n");
    char cwd_after[4096];
    MIRAGE_CHECK(::getcwd(cwd_after, sizeof(cwd_after)) != nullptr);
    MIRAGE_CHECK(std::string_view(service_cwd) == std::string_view(cwd_after));

    desktop::ProcessRequest unicode_run;
    unicode_run.command = "pwd";
    unicode_run.working_directory = unicode;
    const auto unicode_outcome = env.execute_request(unicode_run, limits, desktop::CancelToken{});
    MIRAGE_CHECK(unicode_outcome.ok);
    MIRAGE_CHECK(unicode_outcome.standard_output == unicode.string() + "\n");

    // An empty working directory keeps the caller's cwd (legacy semantics).
    desktop::ProcessRequest legacy_run;
    legacy_run.command = "pwd";
    const auto legacy_outcome = env.execute_request(legacy_run, limits, desktop::CancelToken{});
    MIRAGE_CHECK(legacy_outcome.ok);
    MIRAGE_CHECK(legacy_outcome.standard_output == std::string(service_cwd) + "\n");

    // Broken working directories map onto stable provider errors.
    desktop::ProcessRequest missing_dir;
    missing_dir.command = "true";
    missing_dir.working_directory = root / "absent-dir";
    const auto not_found = env.execute_request(missing_dir, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!not_found.ok);
    MIRAGE_CHECK(not_found.error.code == std::string("not_found"));

    desktop::ProcessRequest file_dir;
    file_dir.command = "true";
    file_dir.working_directory = root / "plain.txt";
    const auto not_dir = env.execute_request(file_dir, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!not_dir.ok);
    MIRAGE_CHECK(not_dir.error.code == std::string("not_regular_file"));

    desktop::ProcessRequest relative_dir;
    relative_dir.command = "true";
    relative_dir.working_directory = std::filesystem::path{"relative/cwd"};
    const auto relative = env.execute_request(relative_dir, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!relative.ok);
    MIRAGE_CHECK(relative.error.code == std::string("invalid_argument"));

    if (::geteuid() != 0) {
        const auto locked = root / "locked-dir";
        MIRAGE_CHECK(std::filesystem::create_directories(locked));
        MIRAGE_CHECK(::chmod(locked.c_str(), 0000) == 0);
        desktop::ProcessRequest locked_run;
        locked_run.command = "true";
        locked_run.working_directory = locked;
        const auto denied = env.execute_request(locked_run, limits, desktop::CancelToken{});
        MIRAGE_CHECK(::chmod(locked.c_str(), 0755) == 0);
        MIRAGE_CHECK(!denied.ok);
        MIRAGE_CHECK(denied.error.code == std::string("permission_denied"));
    }
}

void scenario_exec_results_and_budgets(const std::filesystem::path &root,
                                       linux_backend::LinuxDesktopEnvironment &env) {
    (void)root;
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{10000};

    // A non-zero exit code is a structured result, not a provider failure.
    desktop::ProcessRequest exit_code;
    exit_code.command = "exit 3";
    const auto exit_outcome = env.execute_request(exit_code, limits, desktop::CancelToken{});
    MIRAGE_CHECK(exit_outcome.ok);
    MIRAGE_CHECK(exit_outcome.exited_normally);
    MIRAGE_CHECK(exit_outcome.exit_code == 3);

    // Both streams are captured independently.
    desktop::ProcessRequest streams;
    streams.command = "printf out-payload; printf err-payload >&2";
    const auto stream_outcome = env.execute_request(streams, limits, desktop::CancelToken{});
    MIRAGE_CHECK(stream_outcome.ok);
    MIRAGE_CHECK(stream_outcome.standard_output == "out-payload");
    MIRAGE_CHECK(stream_outcome.standard_error == "err-payload");
    MIRAGE_CHECK(!stream_outcome.output_truncated);

    // Preview truncation reports the fact instead of growing without bound.
    desktop::ProcessLimits tight = limits;
    tight.max_output_bytes = 4;
    desktop::ProcessRequest chatty;
    chatty.command = "printf ABCDEFGH";
    const auto truncated = env.execute_request(chatty, tight, desktop::CancelToken{});
    MIRAGE_CHECK(truncated.ok);
    MIRAGE_CHECK(truncated.output_truncated);
    MIRAGE_CHECK(truncated.standard_output == "ABCD");

    // Over-budget command lines are refused before any process exists.
    desktop::ProcessRequest huge;
    huge.command = "true # " + std::string(70u << 10, 'x');
    const auto refused = env.execute_request(huge, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == std::string("invalid_argument"));

    // An empty command is invalid input.
    desktop::ProcessRequest empty;
    empty.command = "";
    const auto empty_outcome = env.execute_request(empty, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!empty_outcome.ok);
    MIRAGE_CHECK(empty_outcome.error.code == std::string("invalid_argument"));

    // A signal death is ok=false with the signal fact.
    desktop::ProcessRequest self_term;
    self_term.command = "kill -TERM $$";
    const auto signalled = env.execute_request(self_term, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!signalled.ok);
    MIRAGE_CHECK(!signalled.exited_normally);
    MIRAGE_CHECK(!signalled.timed_out && !signalled.cancelled);
    MIRAGE_CHECK(signalled.error.code == std::string("process_signalled"));
}

void scenario_exec_timeout_and_cancel(const std::filesystem::path &root,
                                      linux_backend::LinuxDesktopEnvironment &env) {
    // Budget expiry: the call is bounded and reports the timeout fact.
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{600};
    desktop::ProcessRequest sleeper;
    sleeper.command = "sleep 30";
    const auto start = clock_::now();
    const auto timed_out = env.execute_request(sleeper, limits, desktop::CancelToken{});
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock_::now() - start);
    MIRAGE_CHECK(!timed_out.ok);
    MIRAGE_CHECK(timed_out.timed_out);
    MIRAGE_CHECK(!timed_out.cancelled);
    MIRAGE_CHECK(timed_out.error.code == std::string("deadline_exceeded"));
    MIRAGE_CHECK(elapsed.count() < 10000);

    // Whole-group teardown: a background child of the killed group does not
    // survive the call.
    const auto pidfile = root / "bg.pid";
    desktop::ProcessRequest group;
    group.command = "sleep 30 & echo $! > " + pidfile.string() + "; sleep 30";
    group.working_directory = root;
    desktop::ProcessLimits short_limits;
    short_limits.timeout = std::chrono::milliseconds{600};
    const auto group_outcome = env.execute_request(group, short_limits, desktop::CancelToken{});
    MIRAGE_CHECK(group_outcome.timed_out);
    std::ifstream pid_stream(pidfile);
    pid_t background = -1;
    if (pid_stream >> background) {
        MIRAGE_CHECK(process_is_gone(background, std::chrono::milliseconds{3000}));
    } else {
        MIRAGE_CHECK(false); // the fixture command must have written the pid
    }

    // Cooperative cancellation mid-run: bounded teardown, cancelled fact,
    // already-captured output preserved.
    desktop::CancelToken cancel;
    desktop::ProcessRequest cancellable;
    cancellable.command = "printf partial; sleep 30";
    desktop::ProcessLimits generous;
    generous.timeout = std::chrono::milliseconds{30000};
    std::thread canceller([&cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds{150});
        cancel.request_cancel();
    });
    const auto cancel_start = clock_::now();
    const auto cancelled_outcome = env.execute_request(cancellable, generous, cancel);
    const auto cancel_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock_::now() - cancel_start);
    canceller.join();
    MIRAGE_CHECK(!cancelled_outcome.ok);
    MIRAGE_CHECK(cancelled_outcome.cancelled);
    MIRAGE_CHECK(!cancelled_outcome.timed_out);
    MIRAGE_CHECK(cancelled_outcome.error.code == std::string("cancelled"));
    MIRAGE_CHECK(cancelled_outcome.standard_output == "partial");
    MIRAGE_CHECK(cancel_elapsed.count() < 5000);
}

void scenario_exec_retention_sink(const std::filesystem::path &root,
                                  linux_backend::LinuxDesktopEnvironment &env) {
    (void)root;
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{10000};

    // A working sink mirrors every chunk of both streams.
    RecordingSink sink;
    desktop::ProcessRequest mirrored;
    mirrored.command = "printf 'o1\\no2'; printf 'e1\\n' >&2";
    mirrored.output_sink = &sink;
    const auto mirrored_outcome = env.execute_request(mirrored, limits, desktop::CancelToken{});
    MIRAGE_CHECK(mirrored_outcome.ok);
    MIRAGE_CHECK(!mirrored_outcome.retained_truncated);
    MIRAGE_CHECK(sink.stdout_text() == "o1\no2");
    MIRAGE_CHECK(sink.stderr_text() == "e1\n");
    MIRAGE_CHECK(mirrored_outcome.standard_output == "o1\no2");

    // A rejecting sink marks retained_truncated, never blocks the command and
    // never stops the drain; the preview stays complete on its own terms.
    RejectingSink failing;
    desktop::ProcessRequest rejected;
    rejected.command = "printf captured-anyway";
    rejected.output_sink = &failing;
    const auto rejected_outcome = env.execute_request(rejected, limits, desktop::CancelToken{});
    MIRAGE_CHECK(rejected_outcome.ok);
    MIRAGE_CHECK(rejected_outcome.retained_truncated);
    MIRAGE_CHECK(failing.rejected() > 0);
    MIRAGE_CHECK(rejected_outcome.standard_output == "captured-anyway");
}

void scenario_legacy_execute_regression(const std::filesystem::path &root,
                                        linux_backend::LinuxDesktopEnvironment &env) {
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{10000};
    const auto outcome = env.execute("printf legacy-ok; printf legacy-err >&2; exit 2", limits,
                                     desktop::CancelToken{});
    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.exited_normally);
    MIRAGE_CHECK(outcome.exit_code == 2);
    MIRAGE_CHECK(outcome.standard_output == "legacy-ok");
    MIRAGE_CHECK(outcome.standard_error == "legacy-err");
    MIRAGE_CHECK(!outcome.retained_truncated);

    // The two-argument convenience still routes through the same path.
    const auto convenience = env.execute("printf hello", desktop::ProcessLimits{});
    MIRAGE_CHECK(convenience.ok);
    MIRAGE_CHECK(convenience.standard_output == "hello");

    char service_cwd[4096];
    MIRAGE_CHECK(::getcwd(service_cwd, sizeof(service_cwd)) != nullptr);
    desktop::ProcessRequest workdir_run;
    workdir_run.command = "pwd";
    workdir_run.working_directory = root;
    MIRAGE_CHECK(env.execute_request(workdir_run, limits, desktop::CancelToken{}).ok);
    char cwd_after[4096];
    MIRAGE_CHECK(::getcwd(cwd_after, sizeof(cwd_after)) != nullptr);
    MIRAGE_CHECK(std::string_view(service_cwd) == std::string_view(cwd_after));
}

// --- crash injection (SIGKILL at the commit point, M10 fault injection) -------------
//
// Contract under test (DEC-095): a process SIGKILLed at any stage of a file
// commit must leave the target byte-identical to the OLD or the NEW content
// — never a half-written mixture — and any residue must be a
// ".mirage-tmp-*.tmp" temporary, never the target's name. Each forked child
// is an independent product instance (the single-writer lease table is
// per-process), so a kill is a faithful crash; the child _exits so no parent
// cleanup or flush runs in it.

enum class CrashOp { Create, Replace, Edit };

/// The child body of one injected operation. Runs on the forked copy of the
/// parent's address space (the environment, contents and observation are all
/// inherited) and _exits so no atexit/flush machinery of the parent runs.
[[noreturn]] void run_crash_child(linux_backend::LinuxDesktopEnvironment &env, CrashOp op,
                                  const std::filesystem::path &target, const std::string &content,
                                  const desktop::FileObservation &observation,
                                  const std::string &old_string, const std::string &new_string) {
    const desktop::FileModifyLimits limits;
    switch (op) {
    case CrashOp::Create: {
        const auto outcome = env.create_file(target, content, limits, desktop::CancelToken{});
        ::_exit(outcome.ok ? 0 : 3);
    }
    case CrashOp::Replace: {
        const auto outcome =
            env.replace_file(target, content, observation, limits, desktop::CancelToken{});
        ::_exit(outcome.ok ? 0 : 3);
    }
    case CrashOp::Edit: {
        const auto outcome = env.edit_file(target, old_string, new_string, false, observation,
                                           limits, desktop::CancelToken{});
        ::_exit(outcome.ok ? 0 : 3);
    }
    }
    ::_exit(4);
}

/// One fork + staggered SIGKILL + waitpid. Returns the child status word.
int kill_child_after(const std::function<void()> &body, long delay_us) {
    const pid_t pid = ::fork();
    MIRAGE_CHECK(pid >= 0);
    if (pid == 0) {
        body();
        ::_exit(4);
    }
    if (delay_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }
    // The child may already be gone: kill reports ESRCH and waitpid still
    // reaps it, which the caller reads from the status word.
    ::kill(pid, SIGKILL);
    int status = 0;
    const auto waited = ::waitpid(pid, &status, 0);
    MIRAGE_CHECK(waited == pid);
    return status;
}

/// Forks the body and waits for a normal exit — no kill. Calibration uses
/// this to measure the operation's un-cut runtime.
int run_child_to_completion(const std::function<void()> &body) {
    const pid_t pid = ::fork();
    MIRAGE_CHECK(pid >= 0);
    if (pid == 0) {
        body();
        ::_exit(4);
    }
    int status = 0;
    MIRAGE_CHECK(::waitpid(pid, &status, 0) == pid);
    return status;
}

/// Everything allowed to exist in the injection directory: the target, the
/// sibling, the calibration file — and nothing else but kill residue.
bool residue_free_names(const std::filesystem::path &dir,
                        std::initializer_list<std::string> allowed, std::size_t &residue) {
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        bool known = false;
        for (const auto &ok : allowed) {
            known = known || name == ok;
        }
        if (known) {
            continue;
        }
        if (name.rfind(".mirage-tmp-", 0) == 0 && name.size() >= 4 &&
            name.compare(name.size() - 4, 4, ".tmp") == 0) {
            ++residue;
            continue;
        }
        std::fprintf(stderr, "[linux_file_actions_test] unexpected directory entry: %s\n",
                     name.c_str());
        return false; // an unknown name occupies the directory
    }
    return true;
}

void scenario_crash_injection_commit_atomicity(const std::filesystem::path &root,
                                               linux_backend::LinuxDesktopEnvironment &env) {
    const auto dir = root / "crash-injection";
    const auto refs = root / "crash-refs";
    MIRAGE_CHECK(std::filesystem::create_directories(dir));
    MIRAGE_CHECK(std::filesystem::create_directories(refs));

    const auto digest_via_read = [&env](const std::filesystem::path &path) {
        const auto window =
            env.read_text_window(path, 0, desktop::TextWindowLimits{}, desktop::CancelToken{});
        return window.ok ? window.version.content_digest : std::string{};
    };
    // Reference digests come from reading payload files through the same
    // windowed-read surface, outside the injected directory.
    const auto digest_of_content = [&](const std::string &content, const char *name) {
        const auto ref = refs / name;
        spew(ref, content);
        return digest_via_read(ref);
    };

    const std::string sibling_bytes = "crash sibling stays untouched\n";
    const auto sibling = dir / "sibling.txt";
    spew(sibling, sibling_bytes);

    struct Matrix {
        CrashOp op;
        const char *label;
        std::string old_content;
        std::string new_content;
        std::string old_string;
        std::string new_string;
    };
    // Small: a few hundred bytes. Large: 512 KiB so the write phase is wide
    // enough to sample. Both printable UTF-8 (the reads validate text).
    auto line_block = [](char fill) {
        std::string block;
        block.reserve(512u << 10);
        for (std::size_t i = 0; i < (1u << 19) / 64; ++i) {
            block.append(63, fill);
            block.push_back('\n');
        }
        return block;
    };
    const std::string marker = "M10-EDIT-TARGET";
    const std::string marker_new = "M10-EDIT-TARGET-REPLACED-WITH-A-LONGER-PAYLOAD";
    auto with_marker = [&](std::string base, std::size_t line, const std::string &text) {
        const std::size_t at = line * 64;
        std::copy(text.begin(), text.end(), base.begin() + static_cast<long>(at));
        base[at + text.size()] = '\n';
        return base;
    };
    const std::string small_old = "prefix\n" + std::string(200, 'a') + "\n" + marker + "\nsuffix\n";
    const std::string small_new =
        "replaced-prefix\n" + std::string(200, 'b') + "\n" + marker + "\nsuffix\n";
    const std::string large_old = with_marker(line_block('a'), 4096, marker);
    const std::string large_new = with_marker(line_block('b'), 4096, marker);

    const Matrix matrices[] = {
        {CrashOp::Create, "create-small", "", small_new, "", ""},
        {CrashOp::Create, "create-large", "", large_new, "", ""},
        {CrashOp::Replace, "replace-small", small_old, small_new, "", ""},
        {CrashOp::Replace, "replace-large", large_old, large_new, "", ""},
        {CrashOp::Edit, "edit-small", small_old, small_new, marker, marker_new},
        {CrashOp::Edit, "edit-large", large_old, large_new, marker, marker_new},
    };

    int total_killed_running = 0;
    int total_already_done = 0;
    for (const auto &matrix : matrices) {
        const auto target = dir / (std::string(matrix.label) + "-target.txt");
        const std::string new_digest =
            digest_of_content(matrix.new_content, (std::string(matrix.label) + "-new.bin").c_str());
        const std::string old_digest =
            matrix.op == CrashOp::Create
                ? std::string{}
                : digest_of_content(matrix.old_content,
                                    (std::string(matrix.label) + "-old.bin").c_str());
        // The edit's committed bytes are the old content with the single
        // marker spliced (raw splice, one occurrence).
        const std::string committed_new = matrix.op == CrashOp::Edit ? [&] {
            std::string spliced = matrix.old_content;
            const auto at = spliced.find(matrix.old_string);
            spliced.replace(at, matrix.old_string.size(), matrix.new_string);
            return spliced;
        }()
                                                                     : matrix.new_content;
        const std::string committed_digest =
            matrix.op == CrashOp::Edit
                ? digest_of_content(committed_new,
                                    (std::string(matrix.label) + "-committed.bin").c_str())
                : new_digest;

        // Calibration: one clean child run on a throwaway target in a
        // separate directory bounds the operation's wall time so the kill
        // delays sample inside the commit window.
        const auto calib_dir = root / "crash-calib";
        std::filesystem::create_directories(calib_dir);
        long duration_us = 0;
        for (int run = 0; run < 2; ++run) {
            const auto calib_target = calib_dir / "calib.bin";
            std::error_code remove_ec;
            std::filesystem::remove(calib_target, remove_ec);
            if (matrix.op != CrashOp::Create) {
                spew(calib_target, matrix.old_content);
            }
            desktop::FileObservation calib_observation;
            if (matrix.op != CrashOp::Create) {
                calib_observation = full_observation(read_window(env, calib_target));
            }
            const auto started = clock_::now();
            const int status = run_child_to_completion([&] {
                run_crash_child(env, matrix.op, calib_target, matrix.new_content, calib_observation,
                                matrix.old_string, matrix.new_string);
            });
            duration_us = std::max(
                duration_us,
                std::chrono::duration_cast<std::chrono::microseconds>(clock_::now() - started)
                    .count());
            MIRAGE_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }

        constexpr int kIterations = 24;
        std::mt19937 rng(0x5EEDC0DEu);
        int killed_running = 0;
        int already_done = 0;
        int residue_total = 0;
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            // Reset: the target holds the old bytes (create: nothing).
            std::error_code remove_ec;
            std::filesystem::remove(target, remove_ec);
            if (matrix.op != CrashOp::Create) {
                spew(target, matrix.old_content);
            }
            // The observation is taken in the parent, per iteration, before
            // the fork; the child inherits it and a crash leaves the
            // parent's copy untouched.
            desktop::FileObservation observation;
            if (matrix.op != CrashOp::Create) {
                const auto window = read_window(env, target);
                MIRAGE_CHECK(window.ok);
                observation = full_observation(window);
            }

            // Staggered kill offsets: fixed fractions first (pre-write,
            // mid-write, written-not-renamed), then uniform sampling across
            // and past the calibrated window.
            long delay_us = 0;
            if (iteration < 4) {
                constexpr long kFractions[] = {0, 30, 60, 90};
                delay_us = duration_us * kFractions[iteration] / 100;
            } else {
                std::uniform_int_distribution<long long> spread(
                    0, static_cast<long long>(duration_us * 13 / 10));
                delay_us = static_cast<long>(spread(rng));
            }

            const int status = kill_child_after(
                [&] {
                    run_crash_child(env, matrix.op, target, matrix.new_content, observation,
                                    matrix.old_string, matrix.new_string);
                },
                delay_us);
            if (WIFSIGNALED(status)) {
                ++killed_running;
            } else {
                ++already_done; // the child finished before the kill landed
            }

            // Invariant 1: the target is exactly the old or the committed
            // content (create: absent or exactly new). For edit the
            // committed content is the spliced marker replacement, not
            // new_content. A half-write cannot digest to either side.
            if (matrix.op == CrashOp::Create) {
                if (std::filesystem::exists(target)) {
                    MIRAGE_CHECK(digest_via_read(target) == committed_digest);
                }
            } else {
                const std::string target_digest = digest_via_read(target);
                MIRAGE_CHECK(target_digest == old_digest || target_digest == committed_digest);
            }

            // Invariant 2: every directory entry is known or kill residue.
            std::size_t residue = 0;
            MIRAGE_CHECK(residue_free_names(
                dir, {target.filename().string(), sibling.filename().string()}, residue));
            residue_total += static_cast<int>(residue);

            // Invariant 3: the sibling is byte-identical.
            MIRAGE_CHECK(slurp(sibling) == sibling_bytes);

            // Retry from the parent: lands or reports a structured error
            // (create: already_exists when the child committed; replace/edit:
            // stale_file), never hangs or crashes — and afterwards the target
            // is exactly the new content.
            const desktop::FileModifyLimits limits;
            if (matrix.op == CrashOp::Create) {
                const auto retry =
                    env.create_file(target, matrix.new_content, limits, desktop::CancelToken{});
                MIRAGE_CHECK(retry.ok || retry.error.code == std::string("already_exists"));
            } else if (matrix.op == CrashOp::Replace) {
                const auto retry = env.replace_file(target, matrix.new_content, observation, limits,
                                                    desktop::CancelToken{});
                MIRAGE_CHECK(retry.ok || retry.error.code == std::string("stale_file"));
            } else {
                const auto retry =
                    env.edit_file(target, matrix.old_string, matrix.new_string, false, observation,
                                  limits, desktop::CancelToken{});
                MIRAGE_CHECK(retry.ok || retry.error.code == std::string("stale_file"));
            }
            MIRAGE_CHECK(digest_via_read(target) == committed_digest);

            // Remove the allowed kill residue so the next iteration starts
            // from a clean directory (the contract permits it; nothing else
            // may appear under the target's name).
            for (const auto &entry : std::filesystem::directory_iterator(dir)) {
                const std::string name = entry.path().filename().string();
                if (name != target.filename().string() && name != sibling.filename().string()) {
                    MIRAGE_CHECK(name.rfind(".mirage-tmp-", 0) == 0);
                    std::error_code entry_ec;
                    std::filesystem::remove(entry.path(), entry_ec);
                }
            }
        }
        total_killed_running += killed_running;
        total_already_done += already_done;
        std::fprintf(stderr,
                     "[linux_file_actions_test] crash %s: %d killed mid-flight, %d already done, "
                     "%d residue files over %d iterations\n",
                     matrix.label, killed_running, already_done, residue_total, kIterations);
        // The next matrix scans this directory with its own target name; a
        // leftover target here would look like an unknown entry.
        std::error_code matrix_ec;
        std::filesystem::remove(target, matrix_ec);
    }
    std::fprintf(stderr,
                 "[linux_file_actions_test] crash injection totals: %d killed mid-flight, "
                 "%d already done\n",
                 total_killed_running, total_already_done);
    // Both phases must have been sampled at least once across the matrix:
    // kills that always miss the running child would prove nothing.
    MIRAGE_CHECK(total_killed_running > 0);
    MIRAGE_CHECK(total_already_done > 0);
    std::error_code cleanup_ec;
    std::filesystem::remove_all(root / "crash-calib", cleanup_ec);
}

} // namespace

int main() {
    TempWorkspace workspace;
    {
        auto env = make_env({workspace.root()}, {workspace.root()});
        scenario_capabilities_and_scope_isolation(workspace.root());
        scenario_sha256_known_vectors(workspace.root(), *env);
        scenario_window_paging_and_facts(workspace.root(), *env);
        scenario_window_text_facts(workspace.root(), *env);
        scenario_window_oversize_stream(workspace.root(), *env);
        scenario_create_matrix(workspace.root(), *env);
        scenario_replace_matrix(workspace.root(), *env);
        scenario_edit_matrix(workspace.root(), *env);
        scenario_exec_working_directory(workspace.root(), *env);
        scenario_exec_results_and_budgets(workspace.root(), *env);
        scenario_exec_timeout_and_cancel(workspace.root(), *env);
        scenario_exec_retention_sink(workspace.root(), *env);
        scenario_legacy_execute_regression(workspace.root(), *env);
        scenario_concurrent_replace_reports_busy_without_queueing(workspace.root(), *env);
        scenario_crash_injection_commit_atomicity(workspace.root(), *env);
    }
    return mirage::testing::finish("linux_file_actions_test");
}
