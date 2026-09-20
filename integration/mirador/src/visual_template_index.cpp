#include <mirage/integration/visual_template_index.hpp>

#include <mirador/patch_fingerprint.hpp>

#include <utility>
#include <vector>

namespace mirage::integration {
namespace {

/// Fingerprints one patch under the index's shared thumbnail geometry (the
/// enrollment and every probe must use identical params, mirador discipline).
mirador::Result<mirador::VisualPatchFingerprint>
fingerprint(const mirador::ImageView &patch, const VisualTemplateIndexConfig &config) {
    mirador::PatchFingerprintParams params;
    params.thumb_side = config.thumb_side;
    return mirador::make_visual_patch_fingerprint(patch, params, config.fingerprint_max_bytes);
}

} // namespace

mirador::Result<VisualTemplateIndex>
VisualTemplateIndex::create(const VisualTemplateIndexConfig &config) {
    if (config.max_bytes <= 0) {
        return mirador::Status(mirador::ErrorCode::kInvalidArgument,
                               "template index byte budget must be positive");
    }
    if (config.max_templates == 0) {
        return mirador::Status(mirador::ErrorCode::kInvalidArgument,
                               "template index identity capacity must be positive");
    }
    if (config.fingerprint_max_bytes <= 0) {
        return mirador::Status(mirador::ErrorCode::kInvalidArgument,
                               "template fingerprint budget must be positive");
    }
    if (config.thumb_side < 8 || config.thumb_side > 64) {
        return mirador::Status(mirador::ErrorCode::kInvalidArgument,
                               "template thumbnail side must be in [8, 64]");
    }
    mirador::Result<mirador::VisualIndex> index =
        mirador::VisualIndex::create(config.max_bytes, config.thumb_side);
    if (!index.ok()) {
        return index.status();
    }
    return VisualTemplateIndex(index.take_value(), config);
}

VisualTemplateIndex::VisualTemplateIndex(mirador::VisualIndex index,
                                         VisualTemplateIndexConfig config)
    : index_(std::move(index)), config_(config) {}

mirador::Result<void> VisualTemplateIndex::enroll(const std::string &template_id,
                                                  const mirador::ImageView &patch) {
    if (template_id.empty()) {
        return mirador::Status(mirador::ErrorCode::kInvalidArgument,
                               "template identity must not be empty");
    }
    const auto known = entries_by_id_.find(template_id);
    if (known == entries_by_id_.end() && entries_by_id_.size() >= config_.max_templates) {
        // Fail closed on the identity capacity (RULE-07): the caller decides
        // what to drop, nothing is evicted behind its back.
        return mirador::Status(mirador::ErrorCode::kBudgetExceeded,
                               "template identity capacity (" +
                                   std::to_string(config_.max_templates) + ") reached");
    }
    const mirador::Result<mirador::VisualPatchFingerprint> built = fingerprint(patch, config_);
    if (!built.ok()) {
        return built.status();
    }
    std::uint64_t entry_id = 0;
    if (known != entries_by_id_.end()) {
        entry_id = known->second; // mirador insert replaces the entry in place
    } else {
        entry_id = next_entry_id_++;
    }
    if (mirador::Result<void> inserted = index_.insert(entry_id, built.value()); !inserted.ok()) {
        if (known == entries_by_id_.end()) {
            --next_entry_id_; // the id stays unused; the index is untouched
        }
        return inserted.status();
    }
    entries_by_id_.insert_or_assign(template_id, entry_id);
    identities_by_entry_.insert_or_assign(entry_id, template_id);
    reconcile_evicted();
    return mirador::Result<void>{};
}

mirador::Result<std::optional<VisualTemplateHit>>
VisualTemplateIndex::probe(const mirador::ImageView &patch,
                           const mirador::VisualQueryParams &params, double reuse_threshold) {
    const mirador::Result<mirador::VisualPatchFingerprint> built = fingerprint(patch, config_);
    if (!built.ok()) {
        return built.status();
    }
    mirador::Result<std::vector<mirador::VisualCandidate>> candidates =
        index_.query(built.value(), params);
    if (!candidates.ok()) {
        return candidates.status();
    }
    const std::vector<mirador::VisualCandidate> &hits = candidates.value();
    if (hits.empty()) {
        return std::optional<VisualTemplateHit>{};
    }
    // Clear-winner reuse policy (caller-side mirador discipline): the top
    // candidate must reach the threshold and no runner-up may reach it too;
    // ambiguous patches stay unclaimed for real recognition.
    if (!(hits[0].similarity >= reuse_threshold) ||
        (hits.size() > 1 && hits[1].similarity >= reuse_threshold)) {
        return std::optional<VisualTemplateHit>{};
    }
    const auto identity = identities_by_entry_.find(hits[0].entry_id);
    if (identity == identities_by_entry_.end()) {
        return std::optional<VisualTemplateHit>{}; // unreachable under the reconciliation invariant
    }
    return std::optional<VisualTemplateHit>{VisualTemplateHit{
        identity->second, hits[0].entry_id, hits[0].similarity, hits[0].evidence}};
}

bool VisualTemplateIndex::erase(const std::string &template_id) {
    const auto known = entries_by_id_.find(template_id);
    if (known == entries_by_id_.end()) {
        return false;
    }
    const std::uint64_t entry_id = known->second;
    entries_by_id_.erase(known);
    identities_by_entry_.erase(entry_id);
    static_cast<void>(index_.erase(entry_id));
    return true;
}

bool VisualTemplateIndex::contains(const std::string &template_id) const {
    return entries_by_id_.find(template_id) != entries_by_id_.end();
}

std::optional<std::string> VisualTemplateIndex::identity_for(std::uint64_t entry_id) const {
    const auto known = identities_by_entry_.find(entry_id);
    if (known == identities_by_entry_.end()) {
        return std::nullopt;
    }
    return known->second;
}

void VisualTemplateIndex::reconcile_evicted() {
    for (auto entry = identities_by_entry_.begin(); entry != identities_by_entry_.end();) {
        if (!index_.contains(entry->first)) {
            entries_by_id_.erase(entry->second);
            entry = identities_by_entry_.erase(entry);
        } else {
            ++entry;
        }
    }
}

} // namespace mirage::integration
