#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <mirage/desktop/screen_provider.hpp>

namespace kairo {
class Executor;
}

namespace mirage::integration {

/// M9-06/07, DEC-091: immutable, complete pixels, never an executable input lease.
/// source_sequence stays in image; received_at is the host's completion time.
struct SharedCaptureFrame {
    desktop::ImageFrame image;
    std::string source_id;
    /// Demand generation only; does not replace a platform authority/placement epoch.
    std::uint64_t epoch = 0;
    std::uint64_t sequence = 0;
    std::chrono::steady_clock::time_point received_at;
};

struct ContinuousCaptureConfig {
    std::size_t max_frame_bytes = 8u << 20;
    std::chrono::milliseconds sample_period{250};
    std::chrono::milliseconds capture_timeout{2000};
};

struct ContinuousCaptureStats {
    enum class State { New, Idle, Sampling, Backpressured, Failed, TimedOut, Stopped };
    State state = State::New;
    std::uint64_t epoch = 0;
    std::uint64_t capture_calls = 0, published_frames = 0, backpressure_count = 0;
    std::uint64_t cancelled_frames = 0, timed_out_frames = 0, failed_frames = 0;
    // Projections of kairo CommStats, not a second task-health monitor.
    std::uint64_t tick_overwrites = 0, frame_overwrites = 0;
    std::size_t live_generations = 0, live_pixel_bytes = 0;
    desktop::ProviderError error;
};

/// Opt-in serial capture infrastructure; no production consumer is enabled here.
/// Lifecycle calls are serialized by the external owner, outside worker callbacks.
/// latest()/stats() may be called concurrently. Executor and dependencies of the
/// capture function must outlive stop(); platform session affinity stays in Backend.
/// A registered worker releases callback-owned state on exit, before join completes.
/// The process owner configures bounded Executor max_in_flight_tasks admission.
/// The capture function must enforce the byte limit BEFORE allocating and poll
/// cancellation. It binds source and authorization; this host never grants either.
class ContinuousCaptureHost {
  public:
    using Capture = std::function<desktop::CaptureOutcome(const desktop::CaptureLimits &,
                                                          const desktop::CancelToken &)>;
    ContinuousCaptureHost(kairo::Executor &executor, std::string source_id, Capture capture,
                          ContinuousCaptureConfig config = {});
    ~ContinuousCaptureHost();
    ContinuousCaptureHost(const ContinuousCaptureHost &) = delete;
    ContinuousCaptureHost &operator=(const ContinuousCaptureHost &) = delete;

    /// One-shot registration; starts idle, without a timer or capture calls.
    [[nodiscard]] bool start(std::string &error);
    /// false revokes the current epoch, cancels capture and clears current pixels.
    /// Failure pauses sampling; a subsequent true explicitly opens a new epoch.
    [[nodiscard]] bool set_demand(bool active, std::string &error);
    /// A background latest value. May be empty during contention/revocation/failure.
    /// Retaining two generations (including Mirador views) backpressures capture.
    [[nodiscard]] std::shared_ptr<const SharedCaptureFrame> latest() const;
    [[nodiscard]] ContinuousCaptureStats stats() const;
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::integration
