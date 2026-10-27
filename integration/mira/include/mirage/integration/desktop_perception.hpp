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
    double x = 0, y = 0, width = 0, height = 0;
    std::chrono::steady_clock::time_point observed_at{};
};
// Complete temporal object projection, independent of notification filtering.
// Content revisions are assigned by the capture owner on every completed sample.
struct PerceptionObject {
    desktop::VisualSceneObject object;
    bool content_observed = false, invalidate_content = false;
};
struct PerceptionSample {
    bool ok = false, skipped = false, cancelled = false, resync = false;
    desktop::ProviderError error;
    std::uint64_t source_epoch = 0, frame_sequence = 0, dropped = 0;
    std::vector<PerceptionEvent> events;
    PerceptionMediaActivity media;
    bool objects_complete = false;
    std::vector<PerceptionObject> objects;
};
struct PerceptionPreview {
    std::string id;
    desktop::ImageFrame image;
    std::chrono::steady_clock::time_point observed_at{};
};
struct PerceptionView {
    std::string state = "starting", error;
    std::uint64_t newest = 0, oldest = 0, dropped = 0, filtered = 0, source_epoch = 0;
    bool resync = false;
    std::uint64_t frames = 0, analyzed = 0, skipped = 0, analysis_failed = 0;
    std::size_t journal_bytes = 0, preview_bytes = 0;
    std::uint64_t ack_rejected = 0, ack_depth = 0; // kairo CommStats projection
    PerceptionMediaActivity media;
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
