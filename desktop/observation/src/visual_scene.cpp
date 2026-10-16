#include <mirage/desktop/visual_scene.hpp>

#include <mirage/desktop/input_provider.hpp>

#include <cmath>
#include <limits>
#include <new>
#include <set>
#include <utility>

namespace mirage::desktop {
namespace {
constexpr std::size_t identity_bytes = 128;

bool bounded_text(const std::string &s, std::size_t limit) {
    return s.size() <= limit && is_valid_utf8(s) && s.find('\0') == std::string::npos;
}
bool same_content(const VisualSceneObject &a, const VisualSceneObject &b) {
    return a.source == b.source && a.text == b.text && a.label == b.label;
}
bool same_object(const VisualSceneObject &a, const VisualSceneObject &b) {
    return a.id == b.id && a.incarnation == b.incarnation &&
           a.content_revision == b.content_revision && same_content(a, b) &&
           a.bounds.x == b.bounds.x && a.bounds.y == b.bounds.y &&
           a.bounds.width == b.bounds.width && a.bounds.height == b.bounds.height &&
           a.confidence == b.confidence && a.identity_confirmed == b.identity_confirmed &&
           a.pending == b.pending;
}
} // namespace

VisualScene::VisualScene(std::string source_session, VisualSceneLimits limits) : limits_(limits) {
    if (!source_session.empty() && bounded_text(source_session, identity_bytes))
        source_session_ = std::move(source_session);
}

VisualSceneDelta VisualScene::result() const {
    VisualSceneDelta delta;
    delta.ok = true;
    delta.source_session = source_session_;
    delta.context_revision = context_revision_;
    delta.base_revision = delta.revision = revision_;
    delta.frame_sequence = frame_sequence_;
    return delta;
}
VisualSceneDelta VisualScene::error(const char *code) const {
    auto delta = result();
    delta.ok = false;
    delta.error = {code, "Visual scene update rejected"};
    return delta;
}
const VisualSceneRecord *VisualScene::find(std::uint64_t id) const {
    const auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}
std::size_t VisualScene::bytes(const VisualSceneRecord &r) {
    // Logical charge includes record storage and a conservative map-node
    // allowance, not an assertion about allocator RSS or string capacity.
    auto n = sizeof(VisualSceneRecord) + 4 * sizeof(void *) + r.object.text.size() +
             r.object.label.size();
    if (r.binding)
        n += r.binding->source_session.size() + r.binding->model_request_id.size() +
             r.binding->label.size();
    return n;
}
void VisualScene::refresh_binding(VisualSceneRecord &r) {
    r.semantic_state = !r.binding ? VisualSemanticState::kNone
                       : r.object.identity_confirmed && !r.object.pending &&
                               r.binding->incarnation == r.object.incarnation &&
                               r.binding->content_revision == r.object.content_revision
                           ? VisualSemanticState::kActive
                           : VisualSemanticState::kSuspended;
}
bool VisualScene::valid_object(const VisualSceneObject &o) const {
    const bool source =
        o.source == VisualRegionSource::kOcr || o.source == VisualRegionSource::kDetector ||
        o.source == VisualRegionSource::kGeometry || o.source == VisualRegionSource::kTemplate;
    return source && o.id && o.incarnation && o.content_revision && o.bounds.width > 0 &&
           o.bounds.height > 0 && std::isfinite(o.confidence) && o.confidence >= 0 &&
           o.confidence <= 1 && bounded_text(o.text, limits_.max_field_bytes) &&
           bounded_text(o.label, limits_.max_field_bytes);
}

VisualSceneDelta VisualScene::apply(const VisualSceneUpdate &update) {
    if (source_session_.empty() || !bounded_text(source_session_, identity_bytes) ||
        !limits_.max_objects || limits_.max_objects > 4096 || !limits_.max_bytes ||
        limits_.max_bytes > (16u << 20) || !limits_.max_field_bytes ||
        limits_.max_field_bytes > 4096 || !limits_.max_label_bytes ||
        limits_.max_label_bytes > 1024)
        return error("invalid_scene_limits");
    if (update.source_session != source_session_ || !update.context_revision ||
        update.context_revision < context_revision_ || update.frame_sequence <= frame_sequence_)
        return error("stale_scene_source");
    const bool reset = context_revision_ != update.context_revision;
    if ((!update.full && (reset || update.base_revision != revision_)) ||
        (update.full && !update.removed.empty()))
        return error("scene_baseline_mismatch");
    if (update.objects.size() > limits_.max_objects || update.removed.size() > limits_.max_objects)
        return error("scene_budget_exceeded");
    try {
        // Stage only changed records, not a copy of the entire scene. Allocate
        // output and map nodes before committing, so failures are atomic.
        std::set<std::uint64_t> seen, removed;
        std::map<std::uint64_t, VisualSceneRecord> staged;
        std::size_t staged_bytes = 0;
        auto total = reset ? std::size_t{0} : bytes_;
        auto count = reset ? std::size_t{0} : records_.size();
        for (const auto id : update.removed) {
            if (!id || !removed.insert(id).second || !find(id))
                return error("invalid_scene_removal");
        }
        for (const auto &o : update.objects) {
            if (!valid_object(o) || !seen.insert(o.id).second || removed.contains(o.id))
                return error("invalid_scene_object");
            const auto *old = reset ? nullptr : find(o.id);
            if (old && (o.incarnation < old->object.incarnation ||
                        (o.incarnation == old->object.incarnation &&
                         (o.content_revision < old->object.content_revision ||
                          (!same_content(o, old->object) &&
                           o.content_revision == old->object.content_revision)))))
                return error("stale_scene_content");
            if (old && same_object(o, old->object))
                continue;
            VisualSceneRecord next;
            if (old && old->object.incarnation == o.incarnation) {
                // Preserve only semantic metadata: copying the old raw strings
                // before overwriting them doubles work on every changed ROI.
                next.binding = old->binding;
                next.introduced_sequence = old->introduced_sequence;
                next.content_sequence = old->content_sequence;
            } else
                next.introduced_sequence = update.frame_sequence;
            if (!old || old->object.incarnation != o.incarnation ||
                old->object.content_revision != o.content_revision)
                next.content_sequence = update.frame_sequence;
            next.object = o;
            refresh_binding(next);
            // A changed record survives in the final scene. Bound staging
            // before copying further input, even if the complete update is
            // eventually rejected for total scene size.
            staged_bytes += bytes(next);
            if (staged_bytes > limits_.max_bytes)
                return error("scene_budget_exceeded");
            if (old)
                total -= bytes(*old);
            else
                ++count;
            total += bytes(next);
            staged.emplace(o.id, std::move(next));
        }
        if (update.full && !reset)
            for (const auto &[id, record] : records_) {
                (void)record;
                if (!seen.contains(id))
                    removed.insert(id);
            }
        for (const auto id : removed) {
            total -= bytes(*find(id));
            --count;
        }
        if (count > limits_.max_objects || total > limits_.max_bytes)
            return error("scene_budget_exceeded");
        auto delta = result();
        delta.reset = reset;
        delta.context_revision = update.context_revision;
        delta.frame_sequence = update.frame_sequence;
        for (const auto &[id, record] : staged) {
            (void)id;
            delta.updated.push_back(record);
        }
        delta.removed.assign(removed.begin(), removed.end());
        if (reset || !staged.empty() || !removed.empty()) {
            if (revision_ == std::numeric_limits<std::uint64_t>::max())
                return error("scene_revision_exhausted");
            ++delta.revision;
        }
        // No allocations after this boundary: swaps and same-allocator node
        // insertion only. Rejected input cannot clear a confirmed annotation.
        if (reset)
            records_.swap(staged);
        else {
            for (const auto id : removed)
                records_.erase(id);
            while (!staged.empty()) {
                auto node = staged.extract(staged.begin());
                const auto old = records_.find(node.key());
                if (old == records_.end())
                    records_.insert(std::move(node));
                else
                    std::swap(old->second, node.mapped());
            }
        }
        revision_ = delta.revision;
        frame_sequence_ = update.frame_sequence;
        context_revision_ = update.context_revision;
        bytes_ = total;
        return delta;
    } catch (const std::bad_alloc &) {
        return error("scene_budget_exceeded");
    }
}

VisualSceneDelta VisualScene::annotate(const VisualSemanticBinding &binding) {
    const auto *record = find(binding.object_id);
    if (binding.source_session != source_session_ ||
        binding.context_revision != context_revision_ || !record ||
        binding.incarnation != record->object.incarnation ||
        binding.content_revision != record->object.content_revision || !binding.frame_sequence ||
        binding.frame_sequence > frame_sequence_ ||
        binding.frame_sequence < record->introduced_sequence ||
        binding.frame_sequence < record->content_sequence)
        return error("stale_semantic_binding");
    if (!record->object.identity_confirmed || record->object.pending)
        return error("semantic_identity_unconfirmed");
    if (binding.label.empty() || binding.model_request_id.empty() ||
        !bounded_text(binding.label, limits_.max_label_bytes) ||
        !bounded_text(binding.model_request_id, identity_bytes))
        return error("invalid_semantic_binding");
    if (record->binding && *record->binding == binding)
        return result();
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
        return error("scene_revision_exhausted");
    try {
        auto next = *record;
        next.binding = binding;
        refresh_binding(next);
        const auto total = bytes_ - bytes(*record) + bytes(next);
        if (total > limits_.max_bytes)
            return error("scene_budget_exceeded");
        auto delta = result();
        ++delta.revision;
        delta.updated.push_back(next);
        std::swap(records_.find(binding.object_id)->second, next);
        bytes_ = total;
        revision_ = delta.revision;
        return delta;
    } catch (const std::bad_alloc &) {
        return error("scene_budget_exceeded");
    }
}

VisualSceneDelta VisualScene::snapshot() const {
    if (!context_revision_)
        return error("scene_uninitialized");
    try {
        auto delta = result();
        delta.reset = true;
        delta.base_revision = 0;
        for (const auto &[id, record] : records_) {
            (void)id;
            delta.updated.push_back(record);
        }
        return delta;
    } catch (const std::bad_alloc &) {
        return error("scene_budget_exceeded");
    }
}

} // namespace mirage::desktop
