// M3-01 visual contract tests (DEC-016): the VisualSnapshot render forms of
// design doc section 8 and the VisualReferenceRegistry publication contract.
// The registry is the agent-facing side of the visual pipeline: handles are
// numbered "@v1..@vN" within one snapshot, scope handles advance
// "@vs<generation>", publication replaces the active set as a whole (a
// reader observes one complete generation, never a mix), over-budget
// snapshots are refused without truncation, and clear() drops the set while
// keeping the generation counter.

#include "../support/test.hpp"

#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/desktop/visual_snapshot.hpp>

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace {

using mirage::desktop::render_visual_snapshot;
using mirage::desktop::VisualPublishOutcome;
using mirage::desktop::VisualReferenceRegistry;
using mirage::desktop::VisualRegionEntry;
using mirage::desktop::VisualRegionSource;
using mirage::desktop::VisualSnapshot;
using mirage::desktop::VisualSnapshotLimits;

/// A region in input (unpublished) form: publish() assigns refs itself, so
/// registry scenarios build entries with an empty `ref`; render scenarios
/// pass the published handle explicitly.
VisualRegionEntry entry(const std::string &ref, VisualRegionSource source,
                        mirage::desktop::WindowGeometry bounds, std::string text,
                        std::string template_id, double confidence) {
    VisualRegionEntry region;
    region.ref = ref;
    region.source = source;
    region.bounds = bounds;
    region.text = std::move(text);
    region.template_id = std::move(template_id);
    region.confidence = confidence;
    return region;
}

bool same_region(const VisualRegionEntry &lhs, const VisualRegionEntry &rhs) {
    return lhs.ref == rhs.ref && lhs.source == rhs.source && lhs.bounds.x == rhs.bounds.x &&
           lhs.bounds.y == rhs.bounds.y && lhs.bounds.width == rhs.bounds.width &&
           lhs.bounds.height == rhs.bounds.height && lhs.text == rhs.text &&
           lhs.template_id == rhs.template_id && lhs.confidence == rhs.confidence;
}

bool same_snapshot(const VisualSnapshot &lhs, const VisualSnapshot &rhs) {
    if (lhs.scope_ref != rhs.scope_ref || lhs.regions.size() != rhs.regions.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.regions.size(); ++index) {
        if (!same_region(lhs.regions[index], rhs.regions[index])) {
            return false;
        }
    }
    return true;
}

/// The form publish() must produce for `snapshot`: regions in order with
/// handles renumbered "@v1..@vN".
std::vector<VisualRegionEntry> published_form(const VisualSnapshot &snapshot) {
    std::vector<VisualRegionEntry> expected = snapshot.regions;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index].ref = "@v" + std::to_string(index + 1);
    }
    return expected;
}

/// "@vs" followed by at least one digit — the scope handle shape.
bool is_scope_handle(const std::string &scope) {
    if (scope.size() < 4 || scope.compare(0, 3, "@vs") != 0) {
        return false;
    }
    for (std::size_t index = 3; index < scope.size(); ++index) {
        if (scope[index] < '0' || scope[index] > '9') {
            return false;
        }
    }
    return true;
}

VisualSnapshot three_region_snapshot() {
    VisualSnapshot snapshot;
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kOcr, {1, 2, 300, 400}, "alpha-text", "", 0.91));
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kTemplate, {5, 6, 30, 40}, "", "alpha-tpl", 0.87));
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kGeometry, {7, 8, 31, 41}, "", "", 0.5));
    return snapshot;
}

VisualSnapshot four_region_snapshot() {
    VisualSnapshot snapshot;
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kDetector, {11, 12, 130, 140}, "beta-label", "", 0.81));
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kOcr, {21, 22, 230, 240}, "beta-text", "", 0.92));
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kTemplate, {31, 32, 330, 340}, "", "beta-tpl", 0.77));
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kGeometry, {41, 42, 430, 440}, "", "", 0.4));
    return snapshot;
}

VisualSnapshot two_region_snapshot() {
    VisualSnapshot snapshot;
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kDetector, {51, 52, 150, 160}, "gamma-label", "", 0.66));
    snapshot.regions.push_back(
        entry("", VisualRegionSource::kOcr, {61, 62, 250, 260}, "gamma-text", "", 0.72));
    return snapshot;
}

// ---- rendering ---------------------------------------------------------------

void render_ocr_line_is_byte_exact() {
    VisualSnapshot snapshot;
    snapshot.regions.push_back(
        entry("@v1", VisualRegionSource::kOcr, {0, 0, 10, 10}, "hello", "", 0.9));
    MIRAGE_CHECK(render_visual_snapshot(snapshot) == "@v1 text \"hello\"\n");
}

void render_template_line_is_byte_exact() {
    VisualSnapshot snapshot;
    snapshot.regions.push_back(
        entry("@v1", VisualRegionSource::kTemplate, {0, 0, 10, 10}, "", "settings", 0.9));
    MIRAGE_CHECK(render_visual_snapshot(snapshot) == "@v1 icon cache:settings\n");
}

void render_detector_and_geometry_lines_are_byte_exact() {
    VisualSnapshot with_label;
    with_label.regions.push_back(
        entry("@v1", VisualRegionSource::kDetector, {0, 0, 10, 10}, "button", "", 0.8));
    MIRAGE_CHECK(render_visual_snapshot(with_label) == "@v1 geometry button\n");

    VisualSnapshot without_label;
    without_label.regions.push_back(
        entry("@v1", VisualRegionSource::kGeometry, {0, 0, 10, 10}, "", "", 0.0));
    MIRAGE_CHECK(render_visual_snapshot(without_label) == "@v1 geometry\n");
}

void render_empty_snapshot_is_empty_string() {
    VisualSnapshot snapshot; // no regions, no scope
    MIRAGE_CHECK(render_visual_snapshot(snapshot).empty());
}

void render_preserves_order_and_is_deterministic() {
    VisualSnapshot snapshot;
    snapshot.regions.push_back(
        entry("@v1", VisualRegionSource::kOcr, {1, 2, 3, 4}, "first", "", 0.9));
    snapshot.regions.push_back(
        entry("@v2", VisualRegionSource::kTemplate, {5, 6, 7, 8}, "", "cache-id", 0.8));
    snapshot.regions.push_back(
        entry("@v3", VisualRegionSource::kDetector, {9, 10, 11, 12}, "widget", "", 0.7));
    snapshot.regions.push_back(
        entry("@v4", VisualRegionSource::kGeometry, {13, 14, 15, 16}, "", "", 0.0));
    const std::string expected = "@v1 text \"first\"\n"
                                 "@v2 icon cache:cache-id\n"
                                 "@v3 geometry widget\n"
                                 "@v4 geometry\n";
    const std::string first = render_visual_snapshot(snapshot);
    MIRAGE_CHECK(first == expected);
    MIRAGE_CHECK(render_visual_snapshot(snapshot) == first);
}

// ---- registry ----------------------------------------------------------------

void first_publish_numbers_refs_and_assigns_scope_1() {
    VisualReferenceRegistry registry;
    const VisualSnapshot input = three_region_snapshot();
    const VisualPublishOutcome outcome = registry.publish(input);

    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.error.code.empty());
    MIRAGE_CHECK(outcome.snapshot.scope_ref == "@vs1");
    MIRAGE_CHECK(outcome.snapshot.regions.size() == 3);
    const std::vector<VisualRegionEntry> expected = published_form(input);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        MIRAGE_CHECK(same_region(outcome.snapshot.regions[index], expected[index]));
        MIRAGE_CHECK(outcome.snapshot.regions[index].ref == "@v" + std::to_string(index + 1));
    }
    // Publication must not mutate the caller's snapshot.
    MIRAGE_CHECK(input.regions[0].ref.empty());

    // The outcome is exactly what became active.
    MIRAGE_CHECK(registry.size() == 3);
    MIRAGE_CHECK(same_snapshot(registry.current(), outcome.snapshot));
}

void second_publish_assigns_scope_2_and_renumbers() {
    VisualReferenceRegistry registry;
    MIRAGE_CHECK(registry.publish(three_region_snapshot()).ok);

    const VisualSnapshot second_input = four_region_snapshot();
    const VisualPublishOutcome second = registry.publish(second_input);
    MIRAGE_CHECK(second.ok);
    MIRAGE_CHECK(second.snapshot.scope_ref == "@vs2");
    MIRAGE_CHECK(second.snapshot.regions.size() == 4);
    const std::vector<VisualRegionEntry> expected = published_form(second_input);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        MIRAGE_CHECK(same_region(second.snapshot.regions[index], expected[index]));
    }
    MIRAGE_CHECK(registry.size() == 4);
    MIRAGE_CHECK(same_snapshot(registry.current(), second.snapshot));
}

void resolve_hits_and_misses() {
    VisualReferenceRegistry registry;
    const VisualPublishOutcome outcome = registry.publish(three_region_snapshot());
    MIRAGE_CHECK(outcome.ok);

    const auto hit = registry.resolve("@v2");
    MIRAGE_CHECK(hit.has_value());
    if (hit) {
        MIRAGE_CHECK(hit->source == VisualRegionSource::kTemplate);
        MIRAGE_CHECK(hit->template_id == "alpha-tpl");
        MIRAGE_CHECK(hit->text.empty());
    }

    MIRAGE_CHECK(!registry.resolve("@v4").has_value()); // beyond the set
    MIRAGE_CHECK(!registry.resolve("").has_value());
    MIRAGE_CHECK(!registry.resolve("@vs1").has_value()); // a scope is not a region ref
}

void replace_invalidates_stale_refs_and_resolves_new_ones() {
    VisualReferenceRegistry registry;
    const VisualPublishOutcome first = registry.publish(three_region_snapshot());
    MIRAGE_CHECK(first.ok);
    const auto stale = registry.resolve("@v1");
    MIRAGE_CHECK(stale.has_value());
    MIRAGE_CHECK(stale->text == "alpha-text");

    // The replacement has a different first region, so "@v1" now carries the
    // new content; "@v4" only exists in the new generation.
    const VisualPublishOutcome second = registry.publish(four_region_snapshot());
    MIRAGE_CHECK(second.ok);
    const auto replaced = registry.resolve("@v1");
    MIRAGE_CHECK(replaced.has_value());
    if (replaced) {
        MIRAGE_CHECK(replaced->text == "beta-label");
        MIRAGE_CHECK(replaced->source == VisualRegionSource::kDetector);
    }
    const auto extended = registry.resolve("@v4");
    MIRAGE_CHECK(extended.has_value());
    if (extended) {
        MIRAGE_CHECK(extended->source == VisualRegionSource::kGeometry);
    }
    MIRAGE_CHECK(registry.current().scope_ref == "@vs2");
}

void capacity_rejection_keeps_active_set_and_generation() {
    VisualReferenceRegistry registry;
    const VisualSnapshot oversized = three_region_snapshot(); // 3 regions

    const VisualSnapshotLimits tight{/*max_regions=*/2};
    const VisualPublishOutcome refused = registry.publish(oversized, tight);
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "snapshot_too_large");
    MIRAGE_CHECK(!refused.error.message.empty());
    MIRAGE_CHECK(refused.snapshot.regions.empty());
    // Nothing was published: the active set is still the empty initial one
    // and the generation did not move.
    MIRAGE_CHECK(registry.size() == 0);
    MIRAGE_CHECK(!registry.resolve("@v1").has_value());

    // The exact boundary (2 regions under a budget of 2) is accepted and
    // gets the FIRST scope handle: the refusal consumed no generation.
    const VisualPublishOutcome accepted = registry.publish(two_region_snapshot(), tight);
    MIRAGE_CHECK(accepted.ok);
    MIRAGE_CHECK(accepted.snapshot.scope_ref == "@vs1");
    MIRAGE_CHECK(registry.size() == 2);

    // A later refusal leaves the accepted generation active, and the next
    // accepted publication continues the scope sequence.
    const VisualSnapshotLimits tighter{/*max_regions=*/1};
    const VisualPublishOutcome refused_again = registry.publish(oversized, tighter);
    MIRAGE_CHECK(!refused_again.ok);
    MIRAGE_CHECK(refused_again.error.code == "snapshot_too_large");
    MIRAGE_CHECK(registry.size() == 2);
    MIRAGE_CHECK(registry.current().scope_ref == "@vs1");

    VisualSnapshot one_region;
    one_region.regions.push_back(
        entry("", VisualRegionSource::kOcr, {0, 0, 1, 1}, "single", "", 0.5));
    const VisualPublishOutcome next = registry.publish(one_region, tighter);
    MIRAGE_CHECK(next.ok);
    MIRAGE_CHECK(next.snapshot.scope_ref == "@vs2");
    MIRAGE_CHECK(registry.size() == 1);
}

void clear_drops_active_set_but_keeps_generation() {
    VisualReferenceRegistry registry;
    MIRAGE_CHECK(registry.publish(three_region_snapshot()).ok);
    MIRAGE_CHECK(registry.size() == 3);

    registry.clear();
    MIRAGE_CHECK(registry.size() == 0);
    MIRAGE_CHECK(registry.current().regions.empty());
    MIRAGE_CHECK(!registry.resolve("@v1").has_value());

    // The next publication continues the generation sequence ("@vs2").
    const VisualPublishOutcome outcome = registry.publish(four_region_snapshot());
    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.snapshot.scope_ref == "@vs2");
    MIRAGE_CHECK(registry.size() == 4);
}

void empty_publish_is_accepted_and_advances_the_generation() {
    VisualReferenceRegistry registry;
    const VisualSnapshot empty;
    const VisualPublishOutcome outcome = registry.publish(empty);
    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.snapshot.regions.empty());
    MIRAGE_CHECK(outcome.snapshot.scope_ref == "@vs1");
    MIRAGE_CHECK(registry.size() == 0);
    MIRAGE_CHECK(registry.current().scope_ref == "@vs1");

    const VisualPublishOutcome next = registry.publish(three_region_snapshot());
    MIRAGE_CHECK(next.ok);
    MIRAGE_CHECK(next.snapshot.scope_ref == "@vs2");
}

// ---- publication atomicity (concurrency) -------------------------------------

void concurrent_publish_readers_observe_one_complete_generation() {
    VisualReferenceRegistry registry;
    const VisualSnapshot generation_a = three_region_snapshot(); // 3 regions
    const VisualSnapshot generation_b = four_region_snapshot();  // 4 regions
    const std::vector<VisualRegionEntry> expected_a = published_form(generation_a);
    const std::vector<VisualRegionEntry> expected_b = published_form(generation_b);

    // Publish once so readers never observe the empty initial state.
    const VisualPublishOutcome first = registry.publish(generation_a);
    MIRAGE_CHECK(first.ok);

    constexpr int kWriterIterations = 2000;
    constexpr int kReaderIterations = 2000;
    constexpr int kReaderThreads = 2;

    // MIRAGE_CHECK is not thread-safe; readers count violations locally and
    // the joined main thread asserts on the total.
    std::atomic<int> violations{0};
    auto note = [&violations](bool good) {
        if (!good) {
            violations.fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto writer = std::thread([&] {
        for (int index = 0; index < kWriterIterations; ++index) {
            const VisualSnapshot &next = index % 2 == 0 ? generation_b : generation_a;
            note(registry.publish(next).ok);
        }
    });

    std::vector<std::thread> readers;
    readers.reserve(kReaderThreads);
    for (int reader = 0; reader < kReaderThreads; ++reader) {
        readers.emplace_back([&] {
            for (int index = 0; index < kReaderIterations; ++index) {
                const VisualSnapshot observed = registry.current();
                const std::size_t count = observed.regions.size();
                if (count != expected_a.size() && count != expected_b.size()) {
                    note(false);
                    continue;
                }
                // The scope handle is always well-formed and the set is one
                // complete generation: refs are "@v1..@vN" in order and every
                // region matches that generation's published content.
                note(is_scope_handle(observed.scope_ref));
                bool consistent = true;
                for (std::size_t region = 0; region < count; ++region) {
                    const VisualRegionEntry &observed_region = observed.regions[region];
                    if (observed_region.ref != "@v" + std::to_string(region + 1)) {
                        consistent = false;
                        break;
                    }
                    const bool matches_a = region < expected_a.size() &&
                                           same_region(observed_region, expected_a[region]);
                    const bool matches_b = region < expected_b.size() &&
                                           same_region(observed_region, expected_b[region]);
                    if (!matches_a && !matches_b) {
                        consistent = false;
                        break;
                    }
                }
                note(consistent);

                // Every region the reader can see must still resolve — right
                // now or after a concurrent replacement — to an entry of one
                // of the two published generations, never a mixture.
                for (std::size_t region = 0; region < count; ++region) {
                    const auto resolved = registry.resolve(observed.regions[region].ref);
                    if (!resolved) {
                        continue; // replaced between current() and resolve()
                    }
                    const bool matches_a =
                        region < expected_a.size() && same_region(*resolved, expected_a[region]);
                    const bool matches_b =
                        region < expected_b.size() && same_region(*resolved, expected_b[region]);
                    note(matches_a || matches_b);
                }
                // No generation ever has more than four regions: a handle
                // beyond both must always report not-found.
                note(!registry.resolve("@v5").has_value());
                note(!registry.resolve("@v").has_value());
            }
        });
    }

    writer.join();
    for (std::thread &reader : readers) {
        reader.join();
    }
    MIRAGE_CHECK(violations.load() == 0);
    // The registry is still fully functional after the storm.
    MIRAGE_CHECK(registry.size() == 3 || registry.size() == 4);
    MIRAGE_CHECK(is_scope_handle(registry.current().scope_ref));
}

} // namespace

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[visual_contract_test] scenario: %s\n", name);
    scenario();
}

int main() {
    run_scenario("render_ocr_line_is_byte_exact", render_ocr_line_is_byte_exact);
    run_scenario("render_template_line_is_byte_exact", render_template_line_is_byte_exact);
    run_scenario("render_detector_and_geometry_lines_are_byte_exact",
                 render_detector_and_geometry_lines_are_byte_exact);
    run_scenario("render_empty_snapshot_is_empty_string", render_empty_snapshot_is_empty_string);
    run_scenario("render_preserves_order_and_is_deterministic",
                 render_preserves_order_and_is_deterministic);
    run_scenario("first_publish_numbers_refs_and_assigns_scope_1",
                 first_publish_numbers_refs_and_assigns_scope_1);
    run_scenario("second_publish_assigns_scope_2_and_renumbers",
                 second_publish_assigns_scope_2_and_renumbers);
    run_scenario("resolve_hits_and_misses", resolve_hits_and_misses);
    run_scenario("replace_invalidates_stale_refs_and_resolves_new_ones",
                 replace_invalidates_stale_refs_and_resolves_new_ones);
    run_scenario("capacity_rejection_keeps_active_set_and_generation",
                 capacity_rejection_keeps_active_set_and_generation);
    run_scenario("clear_drops_active_set_but_keeps_generation",
                 clear_drops_active_set_but_keeps_generation);
    run_scenario("empty_publish_is_accepted_and_advances_the_generation",
                 empty_publish_is_accepted_and_advances_the_generation);
    run_scenario("concurrent_publish_readers_observe_one_complete_generation",
                 concurrent_publish_readers_observe_one_complete_generation);
    return mirage::testing::finish("visual_contract_test");
}
