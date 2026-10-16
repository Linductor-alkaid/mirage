// M10-06 (DEC-095) retained command output store verification (independent
// verification pass). Covers the retain/settle/read round trip with
// continuation, the CRLF-normalized view, line_too_long, empty streams, the
// per-stream cap with a continuing drain, session/global/open-execution
// admission refusals before any payload exists, expiry with quota refund,
// cross-session and forged reference opacity, constructor cleanup, the
// un-settled handle destructor, key validation, owner-only file modes and
// the scan budget.

#include "../support/test.hpp"

#include <mirage/runtime/basic_output_store.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include <unistd.h>

namespace {

namespace desktop = mirage::desktop;
using mirage::runtime::BasicOutputStore;
using mirage::runtime::BasicOutputStoreConfig;

class TempDir {
  public:
    explicit TempDir(std::string name)
        : root_(std::filesystem::temp_directory_path() /
                ("mirage-m10-retention-" + name + "-" + std::to_string(::getpid()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {}
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

BasicOutputStoreConfig functional_config(const std::filesystem::path &dir) {
    BasicOutputStoreConfig config;
    config.directory = dir;
    config.max_stream_bytes = 64u << 10;  // deterministic cap reach
    config.retention_after_terminal = std::chrono::hours{1};
    return config;
}

desktop::TextWindowLimits read_limits(std::size_t max_lines = 2000,
                                      std::size_t max_bytes = 50u << 10,
                                      std::size_t max_scan_bytes = 16u << 20) {
    desktop::TextWindowLimits limits;
    limits.max_lines = max_lines;
    limits.max_bytes = max_bytes;
    limits.max_scan_bytes = max_scan_bytes;
    return limits;
}

// --- retain / settle / read round trip ---------------------------------------------

void scenario_retain_settle_read_roundtrip(const std::filesystem::path &dir) {
    auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
    auto streams = store->open("cafe01");
    MIRAGE_CHECK(streams != nullptr);

    MIRAGE_CHECK(streams->retain(true, "alpha\nbeta\n"));
    MIRAGE_CHECK(streams->retain(false, "err-one\n"));
    const auto facts = streams->settle();
    MIRAGE_CHECK(!facts.stdout_ref.empty());
    MIRAGE_CHECK(!facts.stderr_ref.empty());
    MIRAGE_CHECK(facts.stdout_ref != facts.stderr_ref);
    MIRAGE_CHECK(facts.stdout_bytes == 11); // "alpha\nbeta\n"
    MIRAGE_CHECK(facts.stderr_bytes == 8);  // "err-one\n"
    MIRAGE_CHECK(!facts.stdout_truncated);
    MIRAGE_CHECK(!facts.stderr_truncated);
    MIRAGE_CHECK(!facts.retention_failed);

    // Idempotent settle: the facts stay fixed.
    const auto again = streams->settle();
    MIRAGE_CHECK(again.stdout_ref == facts.stdout_ref);
    MIRAGE_CHECK(again.stderr_bytes == facts.stderr_bytes);

    // Full window.
    const auto first = store->read("cafe01", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(first.ok);
    MIRAGE_CHECK(first.lines.size() == 2);
    MIRAGE_CHECK(first.lines[0] == "alpha");
    MIRAGE_CHECK(first.lines[1] == "beta");
    MIRAGE_CHECK(first.eof);
    MIRAGE_CHECK(first.next_offset == 2);
    MIRAGE_CHECK(first.total_lines.value_or(0) == 2);
    MIRAGE_CHECK(first.first_line == 1);
    // Informational byte count, no file-version digest.
    MIRAGE_CHECK(first.version.size == 11);
    MIRAGE_CHECK(!first.version.digest_available());

    // A window at eof of a newline-terminated stream is a stale-offset bug,
    // the same rule the platform provider enforces on every path.
    const auto at_eof = store->read("cafe01", facts.stdout_ref, 2, read_limits());
    MIRAGE_CHECK(!at_eof.ok);
    MIRAGE_CHECK(at_eof.error.code == std::string("invalid_argument"));
    const auto beyond = store->read("cafe01", facts.stdout_ref, 3, read_limits());
    MIRAGE_CHECK(!beyond.ok);
    MIRAGE_CHECK(beyond.error.code == std::string("invalid_argument"));

    // The second stream is independent.
    const auto err_window = store->read("cafe01", facts.stderr_ref, 0, read_limits());
    MIRAGE_CHECK(err_window.ok);
    MIRAGE_CHECK(err_window.lines.size() == 1);
    MIRAGE_CHECK(err_window.lines[0] == "err-one");

    // Owner-only permissions on the payloads (best-effort 0600).
    std::size_t payloads = 0;
    for (const auto &entry : std::filesystem::directory_iterator(dir / "cafe01")) {
        const auto perms = std::filesystem::status(entry.path()).permissions();
        MIRAGE_CHECK((perms & std::filesystem::perms::group_all) == std::filesystem::perms::none);
        MIRAGE_CHECK((perms & std::filesystem::perms::others_all) == std::filesystem::perms::none);
        ++payloads;
    }
    MIRAGE_CHECK(payloads == 2);
}

void scenario_empty_stream_reads_as_empty_eof_window(const std::filesystem::path &dir) {
    auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
    auto streams = store->open("cafe02");
    MIRAGE_CHECK(streams != nullptr);
    const auto facts = streams->settle();
    MIRAGE_CHECK(facts.stdout_bytes == 0);
    MIRAGE_CHECK(facts.stderr_bytes == 0);
    const auto window = store->read("cafe02", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(window.ok);
    MIRAGE_CHECK(window.eof);
    MIRAGE_CHECK(window.lines.empty());
    MIRAGE_CHECK(window.total_lines.value_or(-1) == 0);
}

void scenario_crlf_view_and_byte_facts(const std::filesystem::path &dir) {
    auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
    auto streams = store->open("cafe03");
    MIRAGE_CHECK(streams != nullptr);
    MIRAGE_CHECK(streams->retain(true, "a\r\nb\r\n"));
    const auto facts = streams->settle();
    const auto window = store->read("cafe03", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(window.ok);
    MIRAGE_CHECK(window.lines.size() == 2);
    MIRAGE_CHECK(window.lines[0] == "a");
    MIRAGE_CHECK(window.lines[1] == "b");
    MIRAGE_CHECK(window.has_crlf);
    MIRAGE_CHECK(window.version.size == 6);
}

void scenario_line_too_long(const std::filesystem::path &dir) {
    auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
    auto streams = store->open("cafe04");
    MIRAGE_CHECK(streams != nullptr);
    MIRAGE_CHECK(streams->retain(true, "ok\n0123456789\n"));
    const auto facts = streams->settle();
    const auto window =
        store->read("cafe04", facts.stdout_ref, 0, read_limits(2000, /*max_bytes=*/4));
    MIRAGE_CHECK(!window.ok);
    MIRAGE_CHECK(window.error.code == std::string("line_too_long"));
    MIRAGE_CHECK(window.error.message.find("10") != std::string::npos);
}

void scenario_scan_budget_exceeded(const std::filesystem::path &dir) {
    auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
    auto streams = store->open("cafe05");
    MIRAGE_CHECK(streams != nullptr);
    MIRAGE_CHECK(streams->retain(true, "abc\n"));
    const auto facts = streams->settle();
    // max_scan_bytes=3: the first buffered read pushes scanned past the
    // budget before the window completes.
    const auto window =
        store->read("cafe05", facts.stdout_ref, 0, read_limits(1000000, 1u << 20, 3));
    MIRAGE_CHECK(!window.ok);
    MIRAGE_CHECK(window.error.code == std::string("scan_budget_exceeded"));
}

// --- per-stream cap ----------------------------------------------------------------

void scenario_per_stream_cap_truncates_and_drain_continues(const std::filesystem::path &dir) {
    auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
    auto streams = store->open("cafe06");
    MIRAGE_CHECK(streams != nullptr);
    const std::string chunk(16u << 10, 'x');
    int accepted = 0;
    for (int i = 0; i < 8; ++i) {
        if (streams->retain(true, chunk)) {
            ++accepted;
        }
    }
    MIRAGE_CHECK(accepted == 4); // exactly 64 KiB retained
    MIRAGE_CHECK(streams->retain(false, "stderr-ok\n"));
    const auto facts = streams->settle();
    MIRAGE_CHECK(facts.stdout_bytes == 64u << 10);
    MIRAGE_CHECK(facts.stdout_truncated);
    MIRAGE_CHECK(facts.stderr_bytes == 10);
    MIRAGE_CHECK(!facts.stderr_truncated);
    MIRAGE_CHECK(!facts.retention_failed); // the cap is a fact, not a failure

    // The retained bytes are the first 64 KiB, readable end to end.
    const auto window = store->read("cafe06", facts.stdout_ref, 0, read_limits(200000, 1u << 20));
    MIRAGE_CHECK(window.ok);
    MIRAGE_CHECK(window.eof);
    MIRAGE_CHECK(window.version.size == 64u << 10);
}

// --- admission refusals ------------------------------------------------------------

void scenario_session_and_global_admission(const std::filesystem::path &dir_session,
                                           const std::filesystem::path &dir_global) {
    // Session budget: once the session's retained bytes reach the cap, new
    // executions are refused before any payload is written.
    {
        BasicOutputStoreConfig config = functional_config(dir_session);
        config.max_session_bytes = 8u << 10;
        auto store = std::make_unique<BasicOutputStore>(config);
        auto streams = store->open("cafe07");
        MIRAGE_CHECK(streams != nullptr);
        const std::string chunk(4u << 10, 'y');
        MIRAGE_CHECK(streams->retain(true, chunk));
        MIRAGE_CHECK(streams->retain(true, chunk));
        const auto facts = streams->settle();
        MIRAGE_CHECK(facts.stdout_bytes == 8u << 10);
        std::string refusal;
        const auto refused = store->open("cafe07", &refusal);
        MIRAGE_CHECK(refused == nullptr);
        MIRAGE_CHECK(refusal == "capacity_exceeded");
        // A different session still fits the global budget.
        auto other = store->open("beef08");
        MIRAGE_CHECK(other != nullptr);
    }
    // Global budget across sessions.
    {
        BasicOutputStoreConfig config = functional_config(dir_global);
        config.max_global_bytes = 8u << 10;
        auto store = std::make_unique<BasicOutputStore>(config);
        auto first = store->open("cafe09");
        MIRAGE_CHECK(first != nullptr);
        const std::string chunk(4u << 10, 'z');
        first->retain(true, chunk);
        first->retain(true, chunk);
        first->settle();
        std::string refusal;
        const auto refused = store->open("beef0a", &refusal);
        MIRAGE_CHECK(refused == nullptr);
        MIRAGE_CHECK(refusal == "capacity_exceeded");
    }
}

void scenario_open_execution_admission_and_handle_release(const std::filesystem::path &dir) {
    BasicOutputStoreConfig config = functional_config(dir);
    config.max_open_executions = 2;
    auto store = std::make_unique<BasicOutputStore>(config);
    auto first = store->open("cafe0b");
    auto second = store->open("cafe0b");
    MIRAGE_CHECK(first != nullptr);
    MIRAGE_CHECK(second != nullptr);
    std::string refusal;
    auto third = store->open("cafe0b", &refusal);
    MIRAGE_CHECK(third == nullptr);
    MIRAGE_CHECK(refusal == "capacity_exceeded");

    // Settling one handle frees its admission slot.
    first->settle();
    auto fourth = store->open("cafe0b");
    MIRAGE_CHECK(fourth != nullptr);

    // An un-settled handle's destructor releases its slot too.
    fourth.reset();
    auto fifth = store->open("cafe0b");
    MIRAGE_CHECK(fifth != nullptr);
}

// --- expiry, opacity and quota refund ------------------------------------------------

void scenario_expiry_and_quota_refund(const std::filesystem::path &dir) {
    BasicOutputStoreConfig config = functional_config(dir);
    config.max_session_bytes = 4u << 10;
    config.max_stream_bytes = 4u << 10;
    config.retention_after_terminal = std::chrono::milliseconds{100};
    auto store = std::make_unique<BasicOutputStore>(config);

    auto streams = store->open("beef0c");
    MIRAGE_CHECK(streams != nullptr);
    const std::string chunk(4u << 10, 'q');
    MIRAGE_CHECK(streams->retain(true, chunk));
    const auto facts = streams->settle();
    MIRAGE_CHECK(facts.stdout_bytes == 4u << 10);

    // Still inside the window: readable.
    const auto fresh = store->read("beef0c", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(fresh.ok);

    // Past the window: output_expired, and the sweep frees the session
    // quota so a new full-size retention fits again.
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    const auto expired = store->read("beef0c", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(!expired.ok);
    MIRAGE_CHECK(expired.error.code == std::string("output_expired"));
    auto again = store->open("beef0c");
    MIRAGE_CHECK(again != nullptr);
    MIRAGE_CHECK(again->retain(true, chunk));
    const auto facts_two = again->settle();
    MIRAGE_CHECK(facts_two.stdout_bytes == 4u << 10);
    MIRAGE_CHECK(facts_two.stdout_ref != facts.stdout_ref);
    const auto reread = store->read("beef0c", facts_two.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(reread.ok);
}

void scenario_foreign_and_forged_refs_are_output_expired(const std::filesystem::path &dir) {
    auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
    auto streams = store->open("cafe0d");
    MIRAGE_CHECK(streams != nullptr);
    MIRAGE_CHECK(streams->retain(true, "secret\n"));
    const auto facts = streams->settle();

    // Another session's ref is indistinguishable from an unknown one.
    const auto foreign = store->read("beef0e", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(!foreign.ok);
    MIRAGE_CHECK(foreign.error.code == std::string("output_expired"));
    // Plain unknown ref.
    const auto unknown =
        store->read("cafe0d", "0123456789abcdef0123456789abcdef", 0, read_limits());
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == std::string("output_expired"));

    // Malformed keys are a caller bug: invalid_argument, before any lookup.
    const auto empty_session = store->read("", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(empty_session.error.code == std::string("invalid_argument"));
    const auto bad_session = store->read("sessions/../etc", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(bad_session.error.code == std::string("invalid_argument"));
    const auto non_hex_session = store->read("session-x", facts.stdout_ref, 0, read_limits());
    MIRAGE_CHECK(non_hex_session.error.code == std::string("invalid_argument"));
    const auto path_ref = store->read("cafe0d", "../escape", 0, read_limits());
    MIRAGE_CHECK(path_ref.error.code == std::string("invalid_argument"));
    const auto slashed_ref = store->read("cafe0d", "a/b", 0, read_limits());
    MIRAGE_CHECK(slashed_ref.error.code == std::string("invalid_argument"));
    const auto long_ref = store->read("cafe0d", std::string(65, 'a'), 0, read_limits());
    MIRAGE_CHECK(long_ref.error.code == std::string("invalid_argument"));
    // open() validates the session key too.
    std::string refusal;
    MIRAGE_CHECK(store->open("bad session!", &refusal) == nullptr);
    MIRAGE_CHECK(refusal == "invalid_argument");

    // Non-positive read budgets are invalid input.
    const auto zero_lines = store->read("cafe0d", facts.stdout_ref, 0, read_limits(0));
    MIRAGE_CHECK(zero_lines.error.code == std::string("invalid_argument"));
}

// --- lifecycle: constructor cleanup and un-settled handles ----------------------------

void scenario_constructor_clears_previous_refs(const std::filesystem::path &dir) {
    std::string surviving_ref;
    {
        auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
        auto streams = store->open("cafe0f");
        MIRAGE_CHECK(streams != nullptr);
        MIRAGE_CHECK(streams->retain(true, "before-restart\n"));
        surviving_ref = streams->settle().stdout_ref;
        MIRAGE_CHECK(!surviving_ref.empty());
        const auto readable = store->read("cafe0f", surviving_ref, 0, read_limits());
        MIRAGE_CHECK(readable.ok);
    }
    {
        // A fresh store instance in the same directory is a new runtime: the
        // old ref must be gone, and the directory cleaned.
        auto store = std::make_unique<BasicOutputStore>(functional_config(dir));
        const auto stale = store->read("cafe0f", surviving_ref, 0, read_limits());
        MIRAGE_CHECK(!stale.ok);
        MIRAGE_CHECK(stale.error.code == std::string("output_expired"));
        std::error_code ec;
        MIRAGE_CHECK(!std::filesystem::exists(dir / "cafe0f", ec));
        // And the store still works.
        auto streams = store->open("cafe0f");
        MIRAGE_CHECK(streams != nullptr);
    }
}

void scenario_unsettled_handle_destructor_releases_everything(const std::filesystem::path &dir) {
    BasicOutputStoreConfig config = functional_config(dir);
    config.max_session_bytes = 4u << 10;
    config.max_stream_bytes = 4u << 10;
    config.max_open_executions = 1;
    auto store = std::make_unique<BasicOutputStore>(config);

    {
        auto streams = store->open("cafe10");
        MIRAGE_CHECK(streams != nullptr);
        MIRAGE_CHECK(streams->retain(true, std::string(2u << 10, 'm')));
        MIRAGE_CHECK(streams->retain(false, std::string(2u << 10, 'n')));
        // Dropped without settle: admission and entries must go now.
    }
    // Admission slot released despite max_open_executions == 1.
    std::size_t orphans = 0;
    for (const auto &orphan_entry : std::filesystem::directory_iterator(dir / "cafe10")) {
        (void)orphan_entry;
        ++orphans;
    }
    std::fprintf(stderr, "[basic_output_store_test] orphan payload files on disk: %zu\n", orphans);
    std::string other_refusal;
    auto other = store->open("beef11", &other_refusal);
    std::fprintf(stderr, "[basic_output_store_test] fresh session open: %s (refusal: %s)\n",
                 other ? "ok" : "refused", other_refusal.c_str());
    std::string reopen_refusal;
    auto again = store->open("cafe10", &reopen_refusal);
    if (again == nullptr) {
        std::fprintf(stderr, "[basic_output_store_test] reopen refused: %s\n",
                     reopen_refusal.c_str());
    }
    MIRAGE_CHECK(again != nullptr);
    if (other != nullptr) {
        other->settle();
    }
    if (again != nullptr) {
        // Session quota released: the full budget fits again.
        MIRAGE_CHECK(again->retain(true, std::string(4u << 10, 'o')));
        const auto facts = again->settle();
        MIRAGE_CHECK(facts.stdout_bytes == 4u << 10);
        MIRAGE_CHECK(!facts.retention_failed);
        // The orphaned handle's payload files are gone; only the new pair
        // exists.
        std::size_t payloads = 0;
        for (const auto &entry : std::filesystem::directory_iterator(dir / "cafe10")) {
            MIRAGE_CHECK(entry.is_regular_file());
            ++payloads;
        }
        MIRAGE_CHECK(payloads == 2);
    }
}

void scenario_minimal_open_drop_reopen(const std::filesystem::path &dir) {
    BasicOutputStoreConfig config = functional_config(dir);
    config.max_open_executions = 1;
    auto store = std::make_unique<BasicOutputStore>(config);
    {
        auto handle = store->open("cafe99");
        MIRAGE_CHECK(handle != nullptr);
    }
    std::string refusal;
    auto reopened = store->open("cafe99", &refusal);
    std::fprintf(stderr, "[basic_output_store_test] minimal reopen: %s refusal='%s'\n",
                 reopened ? "ok" : "refused", refusal.c_str());
    MIRAGE_CHECK(reopened != nullptr);
}

} // namespace

int main() {
    TempDir roundtrip("roundtrip");
    TempDir empty("empty");
    TempDir crlf("crlf");
    TempDir too_long("too-long");
    TempDir scan("scan");
    TempDir capped("capped");
    TempDir session_budget("session-budget");
    TempDir global_budget("global-budget");
    TempDir open_budget("open-budget");
    TempDir expiry("expiry");
    TempDir opacity("opacity");
    TempDir restart("restart");
    TempDir orphan("orphan");
    scenario_retain_settle_read_roundtrip(roundtrip.root());
    scenario_empty_stream_reads_as_empty_eof_window(empty.root());
    scenario_crlf_view_and_byte_facts(crlf.root());
    scenario_line_too_long(too_long.root());
    scenario_scan_budget_exceeded(scan.root());
    scenario_per_stream_cap_truncates_and_drain_continues(capped.root());
    scenario_session_and_global_admission(session_budget.root(), global_budget.root());
    scenario_open_execution_admission_and_handle_release(open_budget.root());
    scenario_expiry_and_quota_refund(expiry.root());
    scenario_foreign_and_forged_refs_are_output_expired(opacity.root());
    scenario_constructor_clears_previous_refs(restart.root());
    scenario_unsettled_handle_destructor_releases_everything(orphan.root());
    TempDir minimal("minimal");
    scenario_minimal_open_drop_reopen(minimal.root());
    return mirage::testing::finish("basic_output_store_test");
}
