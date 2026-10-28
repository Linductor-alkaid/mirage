#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mirage/desktop/screen_provider.hpp>
#include <mirage/desktop/visual_scene.hpp>
#include <optional>
#include <string>
#include <vector>

namespace mirage::integration {
// DEC-118: media-activity projection of the continuous feed (video player,
// canvas). Normalized to the analyzed frame; an active region gates per-sample
// noise events and is surfaced so the model can watch via throttled keyframes.
struct PerceptionMediaActivity {
    bool active = false;
    double x = 0, y = 0, width = 0, height = 0;
};
// DEC-115: historical observations, never executable desktop references.
struct PerceptionEvent {
    std::uint64_t sequence = 0, source_epoch = 0, frame_sequence = 0, entity = 0,
                  instance_generation = 0;
    std::string kind, text, frame_id;
    bool confirmed = false;
    // DEC-136: emitted by product media-keyframe branches, not inferred from text.
    bool temporal_keyframe = false;
    double x = 0, y = 0, width = 0, height = 0;
    std::chrono::steady_clock::time_point observed_at{};
};
// Complete temporal object projection, independent of notification filtering.
// Content revisions are assigned by the capture owner on every completed sample.
struct PerceptionObject {
    desktop::VisualSceneObject object;
    bool content_observed = false, invalidate_content = false;
};
// DEC-123: advisory image-space tracks, never text freshness or input refs.
struct PerceptionTrack {
    std::uint64_t entity = 0, instance_generation = 0, verified_sequence = 0;
    desktop::WindowGeometry bounds;
    std::string state, evidence;
    bool reacquired = false, discontinuous = false;
};
struct PerceptionTracking {
    std::uint64_t source_epoch = 0, frame_sequence = 0;
    std::uint32_t layout_generation = 0;
    std::size_t verified = 0, reused = 0, excluded = 0, evicted = 0;
    std::int64_t pool_bytes = 0;
    bool requires_recognition = false;
    std::string error;
    std::vector<PerceptionTrack> tracks;
    std::vector<desktop::WindowGeometry> candidates;
};
// DEC-126: scheduling hints in analysis-image coordinates. A regional result
// is historical evidence only; it establishes neither identity nor coverage.
struct PerceptionRecognitionWork {
    bool ocr = true, detector = true;
    std::optional<desktop::WindowGeometry> roi;
};
struct PerceptionSample {
    bool ok = false, skipped = false, cancelled = false, resync = false;
    desktop::ProviderError error;
    std::uint64_t source_epoch = 0, frame_sequence = 0, dropped = 0;
    std::vector<PerceptionEvent> events;
    PerceptionMediaActivity media;
    bool objects_complete = false;
    std::vector<PerceptionObject> objects;
    bool recognition_deferred = false;
    std::string recognition_reason;
    std::optional<PerceptionTracking> tracking;
    std::optional<PerceptionRecognitionWork> recognition;
};
struct PerceptionPreview {
    std::string id;
    desktop::ImageFrame image;
    std::chrono::steady_clock::time_point observed_at{};
};
struct PerceptionDispatch {
    std::uint64_t sampled = 0, admitted = 0, superseded = 0, rejected = 0;
    std::uint64_t cancelled = 0, timed_out = 0, discarded = 0;
    std::size_t retained_pixel_bytes = 0;
    bool pending = false;
    std::uint64_t regional = 0, ocr_completed = 0, detector_completed = 0;
};
// DEC-131: authenticated by the caller; the feed atomically revalidates all targets.
struct PerceptionFocusTarget {
    desktop::VisualSemanticBinding object;
    bool wake = true;
};
struct PerceptionAttentionTarget {
    desktop::WindowGeometry region;
    std::uint64_t entity = 0, instance_generation = 0, content_revision = 0;
    bool wake = true;
};
// DEC-129: consumer-local intent only; never content freshness or input authority.
struct PerceptionAttention {
    std::string mode = "global", invalidation_reason;
    bool active = false;
    desktop::WindowGeometry region;
    std::uint64_t entity = 0, instance_generation = 0, content_revision = 0;
    std::vector<PerceptionAttentionTarget> targets; // DEC-131: at most four, objects mode only.
};
struct PerceptionView {
    std::string state = "starting", error;
    std::uint64_t newest = 0, oldest = 0, dropped = 0, filtered = 0, source_epoch = 0;
    bool resync = false;
    std::uint64_t frames = 0, analyzed = 0, skipped = 0, analysis_failed = 0;
    std::uint64_t deferred_frames = 0;
    std::optional<PerceptionDispatch> dispatch;
    bool recognition_deferred = false;
    std::string recognition_reason;
    std::optional<PerceptionTracking> tracking;
    std::size_t journal_bytes = 0, preview_bytes = 0;
    std::uint64_t ack_rejected = 0, ack_depth = 0; // kairo CommStats projection
    PerceptionMediaActivity media;
    std::optional<PerceptionAttention> attention;
    std::vector<PerceptionEvent> events;
    // Full bounded observation state; contains no model annotations or input refs.
    std::optional<desktop::VisualSceneDelta> scene;
    std::string scene_error;
    std::chrono::steady_clock::time_point scene_observed_at{};
    // Weak handles prevent old mailbox generations from pinning retired pixels.
    std::vector<std::pair<std::string, std::weak_ptr<const PerceptionPreview>>> previews;
};
class PerceptionPause {
  public:
    virtual ~PerceptionPause() = default;
};
class DesktopPerceptionFeed {
  public:
    virtual ~DesktopPerceptionFeed() = default;
    virtual PerceptionView view() const = 0;
    virtual bool acknowledge(std::uint64_t cursor) = 0;
    virtual bool acknowledge_frame(const std::string &) = 0;
    virtual void focus(const desktop::WindowGeometry &) = 0;
    // The caller authenticates model provenance; the feed rechecks observation versions.
    virtual bool focus_object(const desktop::VisualSemanticBinding &) { return false; }
    virtual bool focus_objects(const std::vector<PerceptionFocusTarget> &) { return false; }
    // Brackets the WHOLE foreground step, including capture/pointer/input.
    virtual std::unique_ptr<PerceptionPause> pause() = 0;
    /// Bounded local subscription; no capture, recognition or model polling.
    virtual std::string wait(std::uint64_t, std::chrono::milliseconds,
                             const desktop::CancelToken &) {
        return "unavailable";
    }
};
struct PerceptionConnection {
    std::unique_ptr<desktop::DesktopCaptureSession> session;
    std::shared_ptr<DesktopPerceptionFeed> feed;
};
struct PerceptionSessionOptions {
    std::string source_id;
    desktop::CancelToken cancelled;
    desktop::CapturePresentation presentation;
    // Same already-authorized task/scope; must not create another sharing grant.
    std::function<bool()> authorized;
    /// DEC-118: continuous source sampling period. 0 keeps the 250ms default;
    /// the session clamps accepted values to 100–1000ms (5–10Hz stays a
    /// configurable choice whose resource cost is separately accepted).
    std::chrono::milliseconds sample_period{0};
};
using PerceptionSessionFactory = std::function<PerceptionConnection(
    std::unique_ptr<desktop::DesktopCaptureSession>, PerceptionSessionOptions)>;
} // namespace mirage::integration
