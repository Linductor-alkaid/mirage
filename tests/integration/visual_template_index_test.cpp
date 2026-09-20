// M3-04 VisualTemplateIndex tests (DEC-016 decision 3, plan item `M3-04`):
// the per-source template store over the pinned mirador::VisualIndex. Covered
// here: creation validation (fail closed), deterministic fingerprinting and
// byte accounting, the clear-winner reuse policy (exact / perceptual /
// template tiers, ambiguity, thresholds), unknown patches, erase, identity
// capacity (fail closed, no eviction), replacement semantics, and the
// byte-budget eviction reconciliation between the identity tables and the
// index membership.
//
// Patch designs (deterministic bytes only):
// - horizontal ramp: every dHash bit set, so it never matches a flat patch at
//   the perceptual layer;
// - vertical ramp: no dHash bit set, orthogonal to the horizontal ramp;
// - high-frequency checkerboard: unlike every enrolled design.

#include "../support/test.hpp"

#include <mirage/integration/visual_template_index.hpp>

#include <mirador/image_view.hpp>
#include <mirador/pixel_format.hpp>
#include <mirador/result.hpp>
#include <mirador/status.hpp>
#include <mirador/visual_index.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using mirage::integration::VisualTemplateHit;
using mirage::integration::VisualTemplateIndex;
using mirage::integration::VisualTemplateIndexConfig;

constexpr std::int32_t kSide = 16;
/// Pinned mirador per-entry cost: thumb bytes + fixed overhead (thumb 32).
constexpr std::int64_t kEntryBytes32 =
    std::int64_t{32} * 32 + mirador::VisualIndex::kEntryOverheadBytes;

std::vector<std::byte> ramp_icon(bool horizontal) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(kSide) * kSide, std::byte{0});
    for (std::int32_t y = 0; y < kSide; ++y) {
        for (std::int32_t x = 0; x < kSide; ++x) {
            const std::int32_t value = 40 + 10 * (horizontal ? x : y);
            bytes[static_cast<std::size_t>(y) * kSide + static_cast<std::size_t>(x)] =
                static_cast<std::byte>(value);
        }
    }
    return bytes;
}

std::vector<std::byte> checkerboard() {
    std::vector<std::byte> bytes(static_cast<std::size_t>(kSide) * kSide, std::byte{0});
    for (std::int32_t y = 0; y < kSide; ++y) {
        for (std::int32_t x = 0; x < kSide; ++x) {
            bytes[static_cast<std::size_t>(y) * kSide + static_cast<std::size_t>(x)] =
                std::byte{((x / 2 + y / 2) % 2) != 0 ? std::uint8_t{255} : std::uint8_t{0}};
        }
    }
    return bytes;
}

/// Fixed, compression-like perturbation: every 7th pixel shifts by +/-3 by
/// position (the pinned mirador icon tour discipline). dHash-preserving.
void perturb(std::vector<std::byte> &bytes) {
    for (std::size_t i = 0; i < bytes.size(); i += 7) {
        const int value = std::to_integer<int>(bytes[i]) + (i % 14 == 0 ? 3 : -3);
        bytes[i] = static_cast<std::byte>(std::clamp(value, 0, 255));
    }
}

mirador::ImageView view_of(std::vector<std::byte> &bytes) {
    mirador::ImageView view;
    view.data = bytes.data();
    view.width = kSide;
    view.height = kSide;
    view.row_stride_bytes = kSide;
    view.format = mirador::PixelFormat::kGray8;
    return view;
}

/// Probe helper: ok()-checked, returns the optional hit.
mirador::Result<std::optional<VisualTemplateHit>>
probe(VisualTemplateIndex &index, std::vector<std::byte> &patch, double reuse_threshold) {
    return index.probe(view_of(patch), mirador::VisualQueryParams{}, reuse_threshold);
}

// ---- creation -----------------------------------------------------------------

void create_rejects_invalid_configs() {
    VisualTemplateIndexConfig low_thumb;
    low_thumb.thumb_side = 7;
    MIRAGE_CHECK(!VisualTemplateIndex::create(low_thumb).ok());
    MIRAGE_CHECK(VisualTemplateIndex::create(low_thumb).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);

    VisualTemplateIndexConfig high_thumb;
    high_thumb.thumb_side = 65;
    MIRAGE_CHECK(VisualTemplateIndex::create(high_thumb).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);

    VisualTemplateIndexConfig zero_bytes;
    zero_bytes.max_bytes = 0;
    MIRAGE_CHECK(VisualTemplateIndex::create(zero_bytes).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);

    VisualTemplateIndexConfig negative_bytes;
    negative_bytes.max_bytes = -1;
    MIRAGE_CHECK(VisualTemplateIndex::create(negative_bytes).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);

    VisualTemplateIndexConfig zero_capacity;
    zero_capacity.max_templates = 0;
    MIRAGE_CHECK(VisualTemplateIndex::create(zero_capacity).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);

    VisualTemplateIndexConfig zero_fingerprint;
    zero_fingerprint.fingerprint_max_bytes = 0;
    MIRAGE_CHECK(VisualTemplateIndex::create(zero_fingerprint).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);

    VisualTemplateIndexConfig negative_fingerprint;
    negative_fingerprint.fingerprint_max_bytes = -1024;
    MIRAGE_CHECK(VisualTemplateIndex::create(negative_fingerprint).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);
}

void create_applies_a_valid_config() {
    VisualTemplateIndexConfig config;
    config.max_bytes = 4096;
    config.thumb_side = 16;
    config.max_templates = 5;
    config.fingerprint_max_bytes = 1 << 20;
    auto created = VisualTemplateIndex::create(config);
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();
    MIRAGE_CHECK(index.max_bytes() == 4096);
    MIRAGE_CHECK(index.thumb_side() == 16);
    MIRAGE_CHECK(index.config().max_templates == 5);
    MIRAGE_CHECK(index.config().fingerprint_max_bytes == (1 << 20));
    MIRAGE_CHECK(index.entry_count() == 0);
    MIRAGE_CHECK(index.byte_size() == 0);
    MIRAGE_CHECK(!index.contains("anything"));
}

// ---- enrollment ----------------------------------------------------------------

void enroll_rejects_an_empty_identity_and_an_invalid_view() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();

    std::vector<std::byte> patch = ramp_icon(/*horizontal=*/true);
    MIRAGE_CHECK(index.enroll("", view_of(patch)).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);

    const mirador::ImageView invalid; // null data, zero extents
    MIRAGE_CHECK(index.enroll("icon", invalid).status().code() ==
                 mirador::ErrorCode::kInvalidArgument);

    MIRAGE_CHECK(index.entry_count() == 0);
    MIRAGE_CHECK(index.byte_size() == 0);
    MIRAGE_CHECK(!index.contains("icon"));
}

void enroll_fingerprints_deterministically_and_accounts_bytes() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();

    std::vector<std::byte> patch = ramp_icon(true);
    MIRAGE_CHECK(index.enroll("icon", view_of(patch)).ok());
    MIRAGE_CHECK(index.contains("icon"));
    MIRAGE_CHECK(index.entry_count() == 1);
    // One entry costs thumb bytes + the pinned fixed overhead.
    MIRAGE_CHECK(index.byte_size() == kEntryBytes32);
    MIRAGE_CHECK(index.byte_size() > 0);
    MIRAGE_CHECK(index.byte_size() <= index.max_bytes());

    // Re-enrolling the same identity with the same pixels is a fingerprint
    // replace, not a second entry: equal pixels produce equal fingerprints.
    MIRAGE_CHECK(index.enroll("icon", view_of(patch)).ok());
    MIRAGE_CHECK(index.entry_count() == 1);
    MIRAGE_CHECK(index.byte_size() == kEntryBytes32);
}

// ---- probing -------------------------------------------------------------------

void probe_reports_an_exact_hit_for_the_enrolled_patch() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();
    std::vector<std::byte> patch = ramp_icon(true);
    MIRAGE_CHECK(index.enroll("icon", view_of(patch)).ok());

    const auto found = probe(index, patch, 0.95);
    MIRAGE_CHECK(found.ok());
    if (!found.ok()) {
        return;
    }
    MIRAGE_CHECK(found.value().has_value());
    if (!found.value().has_value()) {
        return;
    }
    const VisualTemplateHit &hit = *found.value();
    MIRAGE_CHECK(hit.template_id == "icon");
    MIRAGE_CHECK(hit.evidence == mirador::VisualEvidenceKind::kExactContent);
    MIRAGE_CHECK(hit.similarity == 1.0);
    MIRAGE_CHECK(hit.entry_id != 0);
    const std::optional<std::string> identity = index.identity_for(hit.entry_id);
    MIRAGE_CHECK(identity.has_value());
    MIRAGE_CHECK(identity == "icon");
}

void probe_recovers_a_noisy_patch_through_a_weaker_tier() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();
    std::vector<std::byte> patch = ramp_icon(true);
    MIRAGE_CHECK(index.enroll("icon", view_of(patch)).ok());

    std::vector<std::byte> noisy = patch;
    perturb(noisy);
    const auto found = probe(index, noisy, 0.95);
    MIRAGE_CHECK(found.ok());
    if (!found.ok()) {
        return;
    }
    MIRAGE_CHECK(found.value().has_value());
    if (!found.value().has_value()) {
        return;
    }
    const VisualTemplateHit &hit = *found.value();
    MIRAGE_CHECK(hit.template_id == "icon");
    MIRAGE_CHECK(hit.evidence == mirador::VisualEvidenceKind::kPerceptualHash ||
                 hit.evidence == mirador::VisualEvidenceKind::kTemplate);
    MIRAGE_CHECK(hit.similarity <= 1.0);
}

void probe_stays_silent_for_unknown_patches() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();

    std::vector<std::byte> board = checkerboard();
    const auto empty_index = probe(index, board, 0.95); // nothing enrolled yet
    MIRAGE_CHECK(empty_index.ok());
    MIRAGE_CHECK(!empty_index.value().has_value());

    std::vector<std::byte> patch = ramp_icon(true);
    MIRAGE_CHECK(index.enroll("icon", view_of(patch)).ok());
    const auto unlike = probe(index, board, 0.95);
    MIRAGE_CHECK(unlike.ok());
    if (unlike.ok()) {
        MIRAGE_CHECK(!unlike.value().has_value());
    }
}

void probe_keeps_twin_fingerprints_ambiguous() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();
    std::vector<std::byte> patch = ramp_icon(true);
    MIRAGE_CHECK(index.enroll("first", view_of(patch)).ok());
    MIRAGE_CHECK(index.enroll("second", view_of(patch)).ok()); // identical fingerprint
    MIRAGE_CHECK(index.entry_count() == 2);

    // Both candidates carry the exact layer at similarity 1.0, so they reach
    // every legal threshold together: the clear-winner policy answers
    // "ambiguous" (an ok Result without a hit), never an arbitrary winner.
    for (const double threshold : {1.0, 0.95, 0.5, 0.0}) {
        const auto found = probe(index, patch, threshold);
        MIRAGE_CHECK(found.ok());
        if (found.ok()) {
            MIRAGE_CHECK(!found.value().has_value());
        }
    }
}

void probe_threshold_gate_admits_only_the_exact_layer_at_one() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();
    std::vector<std::byte> horizontal = ramp_icon(true);
    std::vector<std::byte> vertical = ramp_icon(false);
    MIRAGE_CHECK(index.enroll("horizontal", view_of(horizontal)).ok());
    MIRAGE_CHECK(index.enroll("vertical", view_of(vertical)).ok());

    // At threshold 1.0 only the exact layer passes: the queried patch matches
    // its own enrollment and nothing else.
    const auto exact_only = probe(index, horizontal, 1.0);
    MIRAGE_CHECK(exact_only.ok());
    if (!exact_only.ok()) {
        return;
    }
    MIRAGE_CHECK(exact_only.value().has_value());
    if (!exact_only.value().has_value()) {
        return;
    }
    MIRAGE_CHECK(exact_only.value()->template_id == "horizontal");
    MIRAGE_CHECK(exact_only.value()->evidence == mirador::VisualEvidenceKind::kExactContent);
    MIRAGE_CHECK(exact_only.value()->similarity == 1.0);

    // The orthogonal design stays below every reuse threshold for the other
    // patch: a clear winner at the default discipline.
    const auto clear_winner = probe(index, vertical, 0.95);
    MIRAGE_CHECK(clear_winner.ok());
    if (!clear_winner.ok()) {
        return;
    }
    MIRAGE_CHECK(clear_winner.value().has_value());
    if (!clear_winner.value().has_value()) {
        return;
    }
    MIRAGE_CHECK(clear_winner.value()->template_id == "vertical");
}

// ---- erase ---------------------------------------------------------------------

void erase_removes_the_identity_and_its_entry() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();
    std::vector<std::byte> patch = ramp_icon(true);
    MIRAGE_CHECK(index.enroll("icon", view_of(patch)).ok());
    const auto before = probe(index, patch, 0.95);
    MIRAGE_CHECK(before.ok() && before.value().has_value());
    if (!(before.ok() && before.value().has_value())) {
        return;
    }
    const std::uint64_t entry_id = before.value()->entry_id;

    MIRAGE_CHECK(index.erase("icon"));
    MIRAGE_CHECK(!index.contains("icon"));
    MIRAGE_CHECK(index.entry_count() == 0);
    MIRAGE_CHECK(index.byte_size() == 0);
    MIRAGE_CHECK(!index.erase("icon")); // second erase is a no-op

    const auto after = probe(index, patch, 0.95);
    MIRAGE_CHECK(after.ok());
    if (after.ok()) {
        MIRAGE_CHECK(!after.value().has_value());
    }
    MIRAGE_CHECK(!index.identity_for(entry_id).has_value());
    MIRAGE_CHECK(!index.identity_for(999999).has_value()); // never-enrolled entry
}

// ---- capacity ------------------------------------------------------------------

void capacity_fails_closed_without_evicting() {
    VisualTemplateIndexConfig config;
    config.max_templates = 2;
    auto created = VisualTemplateIndex::create(config);
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();
    std::vector<std::byte> horizontal = ramp_icon(true);
    std::vector<std::byte> vertical = ramp_icon(false);
    std::vector<std::byte> board = checkerboard();
    MIRAGE_CHECK(index.enroll("a", view_of(horizontal)).ok());
    MIRAGE_CHECK(index.enroll("b", view_of(vertical)).ok());

    const auto refused = index.enroll("c", view_of(board));
    MIRAGE_CHECK(!refused.ok());
    MIRAGE_CHECK(refused.status().code() == mirador::ErrorCode::kBudgetExceeded);
    MIRAGE_CHECK(index.entry_count() == 2);
    MIRAGE_CHECK(!index.contains("c"));

    // The refused enrollment changed nothing: both live identities still probe.
    const auto still_a = probe(index, horizontal, 0.95);
    MIRAGE_CHECK(still_a.ok() && still_a.value().has_value());
    if (still_a.ok() && still_a.value().has_value()) {
        MIRAGE_CHECK(still_a.value()->template_id == "a");
    }
    const auto still_b = probe(index, vertical, 0.95);
    MIRAGE_CHECK(still_b.ok() && still_b.value().has_value());
    if (still_b.ok() && still_b.value().has_value()) {
        MIRAGE_CHECK(still_b.value()->template_id == "b");
    }

    // Replacement of a known identity stays legal at capacity.
    std::vector<std::byte> perturbed = horizontal;
    perturb(perturbed);
    MIRAGE_CHECK(index.enroll("a", view_of(perturbed)).ok());
    MIRAGE_CHECK(index.entry_count() == 2);
}

// ---- replacement ----------------------------------------------------------------

void replace_semantics_keep_the_entry_and_swap_the_fingerprint() {
    auto created = VisualTemplateIndex::create(VisualTemplateIndexConfig{});
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();
    std::vector<std::byte> horizontal = ramp_icon(true);
    std::vector<std::byte> vertical = ramp_icon(false);
    MIRAGE_CHECK(index.enroll("icon", view_of(horizontal)).ok());

    const auto initial = probe(index, horizontal, 0.95);
    MIRAGE_CHECK(initial.ok() && initial.value().has_value());
    if (!(initial.ok() && initial.value().has_value())) {
        return;
    }
    const std::uint64_t entry_id = initial.value()->entry_id;

    // Re-enroll the same identity with different pixels: the entry id is
    // stable, the fingerprint is the new one.
    MIRAGE_CHECK(index.enroll("icon", view_of(vertical)).ok());
    MIRAGE_CHECK(index.entry_count() == 1);

    const auto old_pixels = probe(index, horizontal, 0.95);
    MIRAGE_CHECK(old_pixels.ok());
    if (old_pixels.ok()) {
        MIRAGE_CHECK(!old_pixels.value().has_value());
    }
    const auto new_pixels = probe(index, vertical, 0.95);
    MIRAGE_CHECK(new_pixels.ok() && new_pixels.value().has_value());
    if (new_pixels.ok() && new_pixels.value().has_value()) {
        MIRAGE_CHECK(new_pixels.value()->template_id == "icon");
        MIRAGE_CHECK(new_pixels.value()->entry_id == entry_id);
    }
}

// ---- byte-budget eviction --------------------------------------------------------

void byte_budget_eviction_reconciles_the_identity_tables() {
    VisualTemplateIndexConfig config;
    config.max_bytes = kEntryBytes32; // exactly one entry fits
    auto created = VisualTemplateIndex::create(config);
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();

    std::vector<std::byte> horizontal = ramp_icon(true);
    std::vector<std::byte> vertical = ramp_icon(false);
    std::vector<std::byte> board = checkerboard();

    MIRAGE_CHECK(index.enroll("first", view_of(horizontal)).ok());
    const auto first_hit = probe(index, horizontal, 0.95);
    MIRAGE_CHECK(first_hit.ok() && first_hit.value().has_value());
    if (!(first_hit.ok() && first_hit.value().has_value())) {
        return;
    }
    const std::uint64_t first_entry = first_hit.value()->entry_id;

    // The second identity fits only by evicting the first (index policy:
    // least-recently-inserted); the reconcile must drop the stale identity.
    MIRAGE_CHECK(index.enroll("second", view_of(vertical)).ok());
    MIRAGE_CHECK(index.entry_count() == 1);
    MIRAGE_CHECK(index.byte_size() == kEntryBytes32);
    MIRAGE_CHECK(!index.contains("first"));
    MIRAGE_CHECK(index.contains("second"));
    MIRAGE_CHECK(!index.identity_for(first_entry).has_value());

    const auto evicted = probe(index, horizontal, 0.95);
    MIRAGE_CHECK(evicted.ok());
    if (evicted.ok()) {
        MIRAGE_CHECK(!evicted.value().has_value());
    }
    const auto survivor = probe(index, vertical, 0.95);
    MIRAGE_CHECK(survivor.ok() && survivor.value().has_value());
    if (survivor.ok() && survivor.value().has_value()) {
        MIRAGE_CHECK(survivor.value()->template_id == "second");
        MIRAGE_CHECK(index.identity_for(survivor.value()->entry_id).has_value());
        MIRAGE_CHECK(*index.identity_for(survivor.value()->entry_id) == "second");
    }

    // Enrollment keeps working after evictions (entry ids stay unique, no
    // dangling rows): a third identity evicts the second.
    MIRAGE_CHECK(index.enroll("third", view_of(board)).ok());
    MIRAGE_CHECK(index.entry_count() == 1);
    MIRAGE_CHECK(!index.contains("second"));
    MIRAGE_CHECK(index.contains("third"));
    const auto third_hit = probe(index, board, 0.95);
    MIRAGE_CHECK(third_hit.ok() && third_hit.value().has_value());
    if (third_hit.ok() && third_hit.value().has_value()) {
        MIRAGE_CHECK(third_hit.value()->template_id == "third");
        MIRAGE_CHECK(third_hit.value()->entry_id != first_entry);
    }
}

// ---- per-entry index budget -------------------------------------------------------

void enroll_fails_when_one_entry_exceeds_the_index_budget() {
    VisualTemplateIndexConfig config;
    config.max_bytes = 512; // positive, but below thumb 32^2 + overhead
    auto created = VisualTemplateIndex::create(config);
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();

    std::vector<std::byte> patch = ramp_icon(true);
    const auto refused = index.enroll("icon", view_of(patch));
    MIRAGE_CHECK(!refused.ok());
    MIRAGE_CHECK(refused.status().code() == mirador::ErrorCode::kBudgetExceeded);
    MIRAGE_CHECK(index.entry_count() == 0);
    MIRAGE_CHECK(index.byte_size() == 0);
    MIRAGE_CHECK(!index.contains("icon"));
}

void enroll_fails_when_the_fingerprint_budget_is_too_small() {
    VisualTemplateIndexConfig config;
    config.fingerprint_max_bytes = 512; // below gray intermediate + thumbnail
    auto created = VisualTemplateIndex::create(config);
    MIRAGE_CHECK(created.ok());
    if (!created.ok()) {
        return;
    }
    VisualTemplateIndex index = created.take_value();

    std::vector<std::byte> patch = ramp_icon(true);
    const auto refused = index.enroll("icon", view_of(patch));
    MIRAGE_CHECK(!refused.ok());
    MIRAGE_CHECK(refused.status().code() == mirador::ErrorCode::kBudgetExceeded);
    MIRAGE_CHECK(index.entry_count() == 0);
    MIRAGE_CHECK(!index.contains("icon"));
}

} // namespace

template <typename Scenario> void run_scenario(const char *name, Scenario scenario) {
    std::fprintf(stderr, "[visual_template_index_test] scenario: %s\n", name);
    scenario();
}

int main() {
    run_scenario("create_rejects_invalid_configs", create_rejects_invalid_configs);
    run_scenario("create_applies_a_valid_config", create_applies_a_valid_config);
    run_scenario("enroll_rejects_an_empty_identity_and_an_invalid_view",
                 enroll_rejects_an_empty_identity_and_an_invalid_view);
    run_scenario("enroll_fingerprints_deterministically_and_accounts_bytes",
                 enroll_fingerprints_deterministically_and_accounts_bytes);
    run_scenario("probe_reports_an_exact_hit_for_the_enrolled_patch",
                 probe_reports_an_exact_hit_for_the_enrolled_patch);
    run_scenario("probe_recovers_a_noisy_patch_through_a_weaker_tier",
                 probe_recovers_a_noisy_patch_through_a_weaker_tier);
    run_scenario("probe_stays_silent_for_unknown_patches", probe_stays_silent_for_unknown_patches);
    run_scenario("probe_keeps_twin_fingerprints_ambiguous",
                 probe_keeps_twin_fingerprints_ambiguous);
    run_scenario("probe_threshold_gate_admits_only_the_exact_layer_at_one",
                 probe_threshold_gate_admits_only_the_exact_layer_at_one);
    run_scenario("erase_removes_the_identity_and_its_entry",
                 erase_removes_the_identity_and_its_entry);
    run_scenario("capacity_fails_closed_without_evicting", capacity_fails_closed_without_evicting);
    run_scenario("replace_semantics_keep_the_entry_and_swap_the_fingerprint",
                 replace_semantics_keep_the_entry_and_swap_the_fingerprint);
    run_scenario("byte_budget_eviction_reconciles_the_identity_tables",
                 byte_budget_eviction_reconciles_the_identity_tables);
    run_scenario("enroll_fails_when_one_entry_exceeds_the_index_budget",
                 enroll_fails_when_one_entry_exceeds_the_index_budget);
    run_scenario("enroll_fails_when_the_fingerprint_budget_is_too_small",
                 enroll_fails_when_the_fingerprint_budget_is_too_small);
    return mirage::testing::finish("visual_template_index_test");
}
