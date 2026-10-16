// M10-03 (DEC-095) additive contract verification (independent verification
// pass). The M10 additions to FilesystemProvider and ProcessProvider must
// default to fail-closed results so existing implementors keep compiling and
// behaving exactly as before: every new virtual answers
// "unsupported_platform" (or an all-false capability set) without side
// effects, the legacy entry points stay untouched, and the trailing
// ProcessOutcome member keeps every positional initializer meaningful.

#include "../support/test.hpp"

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/process_provider.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

namespace {

namespace desktop = mirage::desktop;

/// Frozen DEC-095 budget defaults (M10-02), checked at compile time.
static_assert(desktop::TextWindowLimits{}.max_lines == 2000);
static_assert(desktop::TextWindowLimits{}.max_bytes == 50u << 10);
static_assert(desktop::TextWindowLimits{}.max_scan_bytes == 16u << 20);
static_assert(desktop::TextWindowLimits{}.max_scan_time == std::chrono::milliseconds{5000});
static_assert(desktop::FileModifyLimits{}.max_content_bytes == 1u << 20);
static_assert(desktop::FileReadLimits{}.max_bytes == 1u << 20);

/// ProcessOutcome positional-initializer compatibility (DEC-095): the nine
/// pre-M10 members keep their order, so an existing backend's aggregate
/// initializers keep their meaning with retained_truncated defaulted last.
static_assert(std::is_aggregate_v<desktop::ProcessOutcome>);
static_assert([] {
    desktop::ProcessOutcome legacy_shape{
        false, false, false, false, false, -1, {}, {}, desktop::ProviderError{"io_error", "x"}};
    return !legacy_shape.ok && !legacy_shape.retained_truncated &&
           legacy_shape.error.code == "io_error";
}());
static_assert([] {
    desktop::ProcessOutcome full{true, true, false, false, true, 0, {}, {}, {}, false};
    return full.ok && full.exited_normally && full.exit_code == 0 && !full.retained_truncated;
}());

/// FilesystemCapabilities defaults to "nothing beyond the legacy read" so a
/// provider without the M10 surface is discoverable as absent, not broken.
static_assert(!desktop::FilesystemCapabilities{}.windowed_read);
static_assert(!desktop::FilesystemCapabilities{}.write);
static_assert(!desktop::FilesystemCapabilities{}.edit);

/// The smallest possible concrete provider: only the pre-M10 pure virtuals
/// are implemented, exactly like an existing backend that has not opted into
/// the M10 surface.
class LegacyFilesystemProvider final : public desktop::FilesystemProvider {
  public:
    // The override hides the base conveniences, exactly like every existing
    // backend; the using keeps them reachable.
    using desktop::FilesystemProvider::read_text_file;

    desktop::FileReadOutcome read_text_file(const std::filesystem::path &path,
                                            const desktop::FileReadLimits &limits,
                                            const desktop::CancelToken &cancel) override {
        (void)path;
        (void)limits;
        (void)cancel;
        ++read_calls_;
        desktop::FileReadOutcome outcome;
        outcome.ok = true;
        outcome.content = "legacy";
        return outcome;
    }

    [[nodiscard]] int read_calls() const { return read_calls_; }

  private:
    int read_calls_ = 0;
};

/// The smallest possible concrete process provider, same discipline.
class LegacyProcessProvider final : public desktop::ProcessProvider {
  public:
    desktop::ProcessOutcome execute(const std::string &command,
                                    const desktop::ProcessLimits &limits,
                                    const desktop::CancelToken &cancel) override {
        (void)limits;
        (void)cancel;
        ++execute_calls_;
        desktop::ProcessOutcome outcome;
        outcome.ok = true;
        outcome.exited_normally = true;
        outcome.exit_code = 7;
        outcome.standard_output = command;
        return outcome;
    }

    [[nodiscard]] int execute_calls() const { return execute_calls_; }

  private:
    int execute_calls_ = 0;
};

// --- version / observation primitives ----------------------------------------

void scenario_file_version_facts() {
    const desktop::FileVersion empty;
    MIRAGE_CHECK(empty.device == 0);
    MIRAGE_CHECK(empty.file_index == 0);
    MIRAGE_CHECK(empty.size == 0);
    MIRAGE_CHECK(empty.content_digest.empty());
    MIRAGE_CHECK(!empty.digest_available());

    desktop::FileVersion a;
    a.device = 1;
    a.file_index = 2;
    a.size = 3;
    a.content_digest = "aa";
    MIRAGE_CHECK(a.digest_available());

    const desktop::FileVersion same = a;
    MIRAGE_CHECK(a == same);
    MIRAGE_CHECK(a.same_identity_as(same));

    desktop::FileVersion other_index;
    other_index.device = 1;
    other_index.file_index = 9;
    other_index.size = 3;
    other_index.content_digest = "aa";
    MIRAGE_CHECK(!(a == other_index));
    MIRAGE_CHECK(!a.same_identity_as(other_index));

    // Identity ignores size and digest: a replaced file at a new inode is a
    // different identity even with the same content facts.
    desktop::FileVersion other_device;
    other_device.device = 5;
    other_device.file_index = 2;
    MIRAGE_CHECK(!a.same_identity_as(other_device));
}

void scenario_file_observation_covers() {
    const desktop::FileObservation full;
    MIRAGE_CHECK(!full.full);
    MIRAGE_CHECK(full.ranges.empty());
    // A non-full observation with no ranges covers nothing.
    MIRAGE_CHECK(!full.covers(0, 1));

    desktop::FileObservation whole;
    whole.full = true;
    MIRAGE_CHECK(whole.covers(0, 0));
    MIRAGE_CHECK(whole.covers(0, 4096));
    MIRAGE_CHECK(whole.covers(123456, 789));

    desktop::FileObservation partial;
    partial.version.device = 1;
    partial.ranges.push_back({10, 5});
    partial.ranges.push_back({100, 20});
    MIRAGE_CHECK(partial.covers(10, 5));   // exact
    MIRAGE_CHECK(partial.covers(12, 2));   // inside
    MIRAGE_CHECK(partial.covers(100, 20)); // exact second range
    MIRAGE_CHECK(partial.covers(110, 10)); // tail of second range
    MIRAGE_CHECK(!partial.covers(9, 5));   // starts before
    MIRAGE_CHECK(!partial.covers(10, 6));  // ends after
    MIRAGE_CHECK(!partial.covers(15, 1));  // just past the first range
    MIRAGE_CHECK(!partial.covers(50, 10)); // gap between ranges
    MIRAGE_CHECK(!partial.covers(100, 21));
    MIRAGE_CHECK(!partial.covers(0, 10));
}

// --- filesystem provider defaults --------------------------------------------

void scenario_filesystem_defaults_fail_closed() {
    LegacyFilesystemProvider provider;

    // The M10 capability query reports nothing by default.
    const desktop::FilesystemCapabilities caps = provider.capabilities();
    MIRAGE_CHECK(!caps.windowed_read);
    MIRAGE_CHECK(!caps.write);
    MIRAGE_CHECK(!caps.edit);

    desktop::CancelToken cancel;

    const desktop::TextWindowOutcome window =
        provider.read_text_window("/tmp/whatever.txt", 0, desktop::TextWindowLimits{}, cancel);
    MIRAGE_CHECK(!window.ok);
    MIRAGE_CHECK(window.error.code == "unsupported_platform");
    MIRAGE_CHECK(window.lines.empty());
    MIRAGE_CHECK(!window.eof);
    MIRAGE_CHECK(window.first_line == 0);
    MIRAGE_CHECK(window.next_offset == 0);
    MIRAGE_CHECK(!window.total_lines.has_value());
    MIRAGE_CHECK(!window.has_bom);
    MIRAGE_CHECK(!window.has_crlf);

    const desktop::FileWriteOutcome created =
        provider.create_file("/tmp/whatever.txt", "content", desktop::FileModifyLimits{}, cancel);
    MIRAGE_CHECK(!created.ok);
    MIRAGE_CHECK(created.error.code == "unsupported_platform");
    MIRAGE_CHECK(!created.created);
    MIRAGE_CHECK(!created.replaced);
    MIRAGE_CHECK(!created.applied);
    MIRAGE_CHECK(created.bytes_written == 0);

    desktop::FileObservation observation;
    observation.full = true;
    const desktop::FileWriteOutcome replaced = provider.replace_file(
        "/tmp/whatever.txt", "content", observation, desktop::FileModifyLimits{}, cancel);
    MIRAGE_CHECK(!replaced.ok);
    MIRAGE_CHECK(replaced.error.code == "unsupported_platform");
    MIRAGE_CHECK(!replaced.applied);

    const desktop::FileEditOutcome edited = provider.edit_file(
        "/tmp/whatever.txt", "old", "new", false, observation, desktop::FileModifyLimits{}, cancel);
    MIRAGE_CHECK(!edited.ok);
    MIRAGE_CHECK(edited.error.code == "unsupported_platform");
    MIRAGE_CHECK(!edited.applied);
    MIRAGE_CHECK(edited.replacements == 0);

    // Zero side effects: the legacy entry point was not touched by any of the
    // four failed calls above.
    MIRAGE_CHECK(provider.read_calls() == 0);
}

void scenario_legacy_filesystem_entry_untouched() {
    LegacyFilesystemProvider provider;
    const desktop::FileReadOutcome read = provider.read_text_file("/tmp/whatever.txt");
    MIRAGE_CHECK(read.ok);
    MIRAGE_CHECK(read.content == "legacy");
    MIRAGE_CHECK(provider.read_calls() == 1);
}

// --- process provider defaults -----------------------------------------------

void scenario_process_defaults_fail_closed() {
    LegacyProcessProvider provider;

    MIRAGE_CHECK(!provider.supports_working_directory());
    MIRAGE_CHECK(!provider.supports_output_retention());

    desktop::CancelToken cancel;
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{1000};

    // A request needing cwd or retention fails closed.
    desktop::ProcessRequest with_cwd;
    with_cwd.command = "true";
    with_cwd.working_directory = std::filesystem::path{"/tmp"};
    const desktop::ProcessOutcome cwd_outcome = provider.execute_request(with_cwd, limits, cancel);
    MIRAGE_CHECK(!cwd_outcome.ok);
    MIRAGE_CHECK(!cwd_outcome.timed_out);
    MIRAGE_CHECK(!cwd_outcome.cancelled);
    MIRAGE_CHECK(cwd_outcome.error.code == "unsupported_platform");
    MIRAGE_CHECK(!cwd_outcome.retained_truncated);

    class NullSink final : public desktop::ProcessOutputSink {
      public:
        bool retain(bool, std::string_view) override { return true; }
    } sink;
    desktop::ProcessRequest with_sink;
    with_sink.command = "true";
    with_sink.output_sink = &sink;
    const desktop::ProcessOutcome sink_outcome =
        provider.execute_request(with_sink, limits, cancel);
    MIRAGE_CHECK(!sink_outcome.ok);
    MIRAGE_CHECK(sink_outcome.error.code == "unsupported_platform");

    // The legacy entry point was never reached by the failed requests.
    MIRAGE_CHECK(provider.execute_calls() == 0);
}

void scenario_process_default_request_delegates_to_legacy() {
    LegacyProcessProvider provider;

    desktop::ProcessRequest plain;
    plain.command = "echo delegated";
    const desktop::ProcessOutcome outcome =
        provider.execute_request(plain, desktop::ProcessLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.exited_normally);
    MIRAGE_CHECK(outcome.exit_code == 7);
    MIRAGE_CHECK(outcome.standard_output == "echo delegated");
    MIRAGE_CHECK(provider.execute_calls() == 1);

    // Same delegation shape with both optional fields defaulted: an
    // explicitly empty working directory is the legacy behavior contract.
    desktop::ProcessRequest explicit_empty;
    explicit_empty.command = "echo again";
    explicit_empty.working_directory = std::filesystem::path{};
    explicit_empty.output_sink = nullptr;
    const desktop::ProcessOutcome again =
        provider.execute_request(explicit_empty, desktop::ProcessLimits{}, desktop::CancelToken{});
    MIRAGE_CHECK(again.ok);
    MIRAGE_CHECK(again.standard_output == "echo again");
    MIRAGE_CHECK(provider.execute_calls() == 2);
}

} // namespace

int main() {
    scenario_file_version_facts();
    scenario_file_observation_covers();
    scenario_filesystem_defaults_fail_closed();
    scenario_legacy_filesystem_entry_untouched();
    scenario_process_defaults_fail_closed();
    scenario_process_default_request_delegates_to_legacy();
    return mirage::testing::finish("basic_contract_default_test");
}
