#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <mirador/image_view.hpp>
#include <mirador/result.hpp>
#include <mirador/visual_index.hpp>

namespace mirage::integration {

/// Budgets and geometry of one per-source template index (plan item `M3-04`,
/// DEC-016 decision 3: template identities are consumed inside the visual
/// pipeline and never enter the observation by themselves). Every limit is
/// explicit (RULE-07): exhaustion fails closed with kBudgetExceeded, nothing
/// is truncated or silently evicted without reconciliation.
struct VisualTemplateIndexConfig {
    /// Byte budget of the underlying mirador VisualIndex (thumbnail bytes plus
    /// its fixed per-entry overhead). Non-positive values fail `create`.
    std::int64_t max_bytes = std::int64_t{64} * 1024;
    /// Normalized square thumbnail side; the index and every enrollment /
    /// probe fingerprint must share it (mirador weakens every evidence tier
    /// on a mismatch). Valid range [8, 64].
    std::int32_t thumb_side = 32;
    /// Upper bound on enrolled identities (RULE-07 capacity): enrolling one
    /// more template beyond this fails with kBudgetExceeded instead of
    /// evicting. Zero fails `create`.
    std::size_t max_templates = 256;
    /// Budget for one patch fingerprint (the gray intermediate plus the
    /// thumbnail; mirador fails with kBudgetExceeded, never truncates).
    /// Non-positive values fail `create`.
    std::int64_t fingerprint_max_bytes = std::int64_t{1} * 1024 * 1024;
};

/// One accepted probe hit, mapped back to the enrolled identity. `evidence`
/// is the strongest mirador tier that matched (kExactContent / kPerceptualHash
/// / kTemplate); `similarity` is layer-dependent (exact 1.0, hash in [0, 1],
/// NCC in [-1, 1]).
struct VisualTemplateHit {
    std::string template_id;
    std::uint64_t entry_id = 0;
    double similarity = 0.0;
    mirador::VisualEvidenceKind evidence = mirador::VisualEvidenceKind::kTemplate;
};

/// Per-source template store over the pinned `mirador::VisualIndex` (plan
/// item `M3-04`): enrollment maps a template identity string to a deterministic
/// index entry, probing matches a captured patch against the enrolled
/// fingerprints through the three mirador evidence tiers (exact content hash,
/// perceptual hash, template NCC) so an enrolled icon is re-located across
/// frames without re-running a model.
///
/// The reuse policy is caller-side (mirador integration discipline): `probe`
/// accepts the top candidate only when it is a *clear winner* — its
/// similarity reaches `reuse_threshold` and no runner-up reaches it too;
/// weaker or ambiguous matches stay no-hit and belong to real recognition.
///
/// Not thread-safe: one instance belongs to one image source's serial visual
/// session context (DEC-016 decision 4), like the PerceptionSession it
/// accompanies. The identity tables always mirror the underlying index
/// membership: every successful `enroll` reconciles entries the index
/// evicted under its byte budget, so a probe hit can always be mapped back
/// to its identity. Never throws.
class VisualTemplateIndex {
  public:
    /// Fails with kInvalidArgument for a non-positive byte budget, a
    /// fingerprint budget <= 0, a zero template capacity, or a thumb side
    /// outside [8, 64] (the mirador index range).
    [[nodiscard]] static mirador::Result<VisualTemplateIndex>
    create(const VisualTemplateIndexConfig &config);

    VisualTemplateIndex(VisualTemplateIndex &&) noexcept = default;
    VisualTemplateIndex &operator=(VisualTemplateIndex &&) noexcept = default;
    ~VisualTemplateIndex() noexcept = default;

    /// Enrolls (or replaces) `template_id` with a fingerprint of `patch`.
    /// The patch is fingerprinted with the index's shared thumbnail geometry;
    /// errors: kInvalidArgument (empty identity, invalid view, thumbnail
    /// geometry mismatch), kUnsupportedFormat (unreachable gray conversion),
    /// kBudgetExceeded (identity capacity reached — fail closed, nothing is
    /// evicted; fingerprint budget; or the entry alone exceeds the index
    /// budget, leaving the index untouched).
    [[nodiscard]] mirador::Result<void> enroll(const std::string &template_id,
                                               const mirador::ImageView &patch);

    /// Fingerprints `patch` and queries the index; a clear-winner hit is
    /// reported with its enrolled identity. A nullopt value (an ok Result)
    /// means "no unambiguous enrolled match", not a failure. Non-const: the
    /// mirador query promotes touched entries between evidence tiers, one
    /// more reason the index stays in the session's serial context.
    [[nodiscard]] mirador::Result<std::optional<VisualTemplateHit>>
    probe(const mirador::ImageView &patch, const mirador::VisualQueryParams &params,
          double reuse_threshold);

    /// Removes the identity and its index entry; true when it was enrolled.
    bool erase(const std::string &template_id);

    [[nodiscard]] bool contains(const std::string &template_id) const;
    /// Identity of a live index entry (probe hits map through this).
    [[nodiscard]] std::optional<std::string> identity_for(std::uint64_t entry_id) const;

    [[nodiscard]] std::size_t entry_count() const noexcept { return index_.entry_count(); }
    [[nodiscard]] std::int64_t byte_size() const noexcept { return index_.byte_size(); }
    [[nodiscard]] std::int64_t max_bytes() const noexcept { return index_.max_bytes(); }
    [[nodiscard]] std::int32_t thumb_side() const noexcept { return index_.thumb_side(); }
    [[nodiscard]] const VisualTemplateIndexConfig &config() const noexcept { return config_; }

  private:
    VisualTemplateIndex(mirador::VisualIndex index, VisualTemplateIndexConfig config);

    /// Drops identity-table rows whose index entry the byte budget evicted,
    /// keeping the tables and the index membership in lockstep.
    void reconcile_evicted();

    mirador::VisualIndex index_;
    VisualTemplateIndexConfig config_;
    std::unordered_map<std::string, std::uint64_t> entries_by_id_;
    std::unordered_map<std::uint64_t, std::string> identities_by_entry_;
    std::uint64_t next_entry_id_ = 1;
};

} // namespace mirage::integration
