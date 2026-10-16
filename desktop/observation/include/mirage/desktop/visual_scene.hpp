#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <mirage/desktop/visual_snapshot.hpp>

namespace mirage::desktop {

// DEC-091: IDs and content revisions are supplied by the perception owner,
// never inferred here from coordinates. No pixels or execution permissions.
struct VisualSceneObject {
    std::uint64_t id = 0, incarnation = 0, content_revision = 0;
    VisualRegionSource source = VisualRegionSource::kGeometry;
    WindowGeometry bounds;
    std::string text, label;
    double confidence = 0;
    bool identity_confirmed = false, pending = false;
};

struct VisualSemanticBinding {
    std::string source_session, model_request_id, label;
    std::uint64_t context_revision = 0, object_id = 0, incarnation = 0;
    std::uint64_t content_revision = 0, frame_sequence = 0;
    bool operator==(const VisualSemanticBinding &) const = default;
};

enum class VisualSemanticState { kNone, kActive, kSuspended };

struct VisualSceneRecord {
    VisualSceneObject object;
    std::optional<VisualSemanticBinding> binding;
    VisualSemanticState semantic_state = VisualSemanticState::kNone;
    // Recognition/annotation age is not refreshed by an unrelated frame.
    std::uint64_t introduced_sequence = 0, content_sequence = 0;
};

struct VisualSceneLimits {
    std::size_t max_objects = 256, max_bytes = 512u << 10;
    std::size_t max_field_bytes = 4096, max_label_bytes = 1024;
};

struct VisualSceneUpdate {
    std::string source_session;
    std::uint64_t context_revision = 0, frame_sequence = 0, base_revision = 0;
    bool full = false;
    std::span<const VisualSceneObject> objects;
    std::span<const std::uint64_t> removed;
};

struct VisualSceneDelta {
    bool ok = false, reset = false;
    ProviderError error;
    std::string source_session;
    std::uint64_t context_revision = 0, base_revision = 0, revision = 0, frame_sequence = 0;
    std::vector<VisualSceneRecord> updated;
    std::vector<std::uint64_t> removed;
};

// Single-owner synchronous state, called on the existing serialized visual
// context. No scheduling or synchronization facility. Outputs are bounded
// values; callers must not accumulate deltas. A complete snapshot is copied
// only on an explicit snapshot() call. Rejected updates leave state intact.
class VisualScene {
  public:
    explicit VisualScene(std::string source_session, VisualSceneLimits limits = {});
    [[nodiscard]] VisualSceneDelta apply(const VisualSceneUpdate &update);
    // Trusted owner must validate request/image provenance first; a supplied
    // request ID is attribution, not authentication. This checks scene versions.
    [[nodiscard]] VisualSceneDelta annotate(const VisualSemanticBinding &binding);
    [[nodiscard]] VisualSceneDelta snapshot() const;
    // Borrowed until the next mutation; never retain across execution contexts.
    [[nodiscard]] const VisualSceneRecord *find(std::uint64_t id) const;
    [[nodiscard]] std::size_t size() const { return records_.size(); }
    [[nodiscard]] std::size_t retained_bytes() const { return bytes_; }
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

  private:
    VisualSceneDelta result() const;
    VisualSceneDelta error(const char *code) const;
    bool valid_object(const VisualSceneObject &) const;
    static std::size_t bytes(const VisualSceneRecord &);
    static void refresh_binding(VisualSceneRecord &);

    std::string source_session_;
    VisualSceneLimits limits_;
    std::map<std::uint64_t, VisualSceneRecord> records_;
    std::size_t bytes_ = 0;
    std::uint64_t revision_ = 0, context_revision_ = 0, frame_sequence_ = 0;
};

} // namespace mirage::desktop
