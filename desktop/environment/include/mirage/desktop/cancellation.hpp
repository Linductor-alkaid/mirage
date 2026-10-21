#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace mirage::desktop {

/// Cooperative cancellation flag shared between a caller that may request
/// cancellation and a provider operation that observes it (design doc
/// section 5, M1-05 cancellation path).
///
/// The desktop core must not expose third-party types (RULE-01), so this is
/// the pinned-free counterpart of the runtime's stop tokens: the runtime
/// layer adapts its own cancellation onto it, providers only observe. Copies
/// share one state, like the executor's StopToken; request_cancel() is safe
/// from any thread and is idempotent. A default token starts uncancelled.
///
/// Requesting cancellation never interrupts anything by itself: providers
/// observe the flag at bounded intervals on their own execution context and
/// tear their work down cooperatively before reporting a cancelled outcome.
class CancelToken {
  public:
    CancelToken() = default;
    /// A bounded caller-owned stop/deadline probe; no worker or timer. Copies
    /// remain valid only while the synchronous provider operation is active.
    /// When shared with another managed context (controlled input), the probe
    /// must be thread-safe and its captured owners must outlive worker join.
    explicit CancelToken(std::function<bool()> probe) : probe_(std::move(probe)) {}

    void request_cancel() noexcept { state->cancelled.store(true, std::memory_order_release); }

    [[nodiscard]] bool cancelled() const noexcept {
        if (state->cancelled.load(std::memory_order_acquire))
            return true;
        try {
            return probe_ && probe_();
        } catch (...) {
            return true;
        }
    }

  private:
    struct State {
        std::atomic_bool cancelled{false};
    };
    std::shared_ptr<State> state = std::make_shared<State>();
    std::function<bool()> probe_;
};

} // namespace mirage::desktop
