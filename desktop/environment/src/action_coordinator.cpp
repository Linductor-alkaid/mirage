#include <mirage/desktop/action_coordinator.hpp>
#include <mutex>
#include <optional>
#include <utility>

namespace mirage::desktop {
struct ActionCoordinator::State {
    mutable std::mutex mutex;
    std::uint64_t epoch = 1, authority_epoch = 1;
    bool busy = false;
    bool stopped = false;
    bool dirty = false;
    std::optional<CancelToken> active;
};
ActionCoordinator::ActionCoordinator() : state_(std::make_shared<State>()) {}
ActionCoordinator::Lease::~Lease() { finish(); }
ActionCoordinator::Lease::Lease(Lease &&other) noexcept
    : state_(std::move(other.state_)), error_(std::move(other.error_)) {}
ActionCoordinator::Lease &ActionCoordinator::Lease::operator=(Lease &&other) noexcept {
    if (this != &other) {
        finish();
        state_ = std::move(other.state_);
        error_ = std::move(other.error_);
    }
    return *this;
}
void ActionCoordinator::Lease::finish() noexcept {
    if (state_) {
        const std::lock_guard guard(state_->mutex);
        state_->busy = false;
        state_->active.reset();
        ++state_->epoch;
    }
    state_.reset();
}
void ActionCoordinator::Lease::release_failed() {
    if (state_) {
        const std::lock_guard guard(state_->mutex);
        state_->dirty = state_->stopped = true;
        ++state_->authority_epoch;
    }
}
ActionCoordinator::Lease ActionCoordinator::try_begin(const CancelToken &cancel,
                                                      std::uint64_t expected_epoch) {
    Lease lease;
    // Cancellation probes are caller code and may inspect this coordinator.
    // Never invoke them while holding its mutex; admission still checks the
    // authority/epoch under lock and the provider polls cancellation before input.
    const bool cancelled = cancel.cancelled();
    const std::lock_guard guard(state_->mutex);
    if (cancelled)
        lease.error_ = {"cancelled", "desktop action cancelled before admission"};
    else if (state_->stopped)
        lease.error_ = {state_->dirty ? "input_release_failed" : "human_takeover",
                        "desktop mutations are fenced"};
    else if (state_->busy)
        lease.error_ = {"desktop_busy", "another mutation owns the input lease"};
    else if (expected_epoch != 0 && expected_epoch != state_->epoch)
        lease.error_ = {"stale_observation", "desktop epoch changed; re-observe"};
    else {
        state_->busy = true;
        state_->active = cancel;
        ++state_->epoch;
        lease.state_ = state_;
    }
    return lease;
}
std::uint64_t ActionCoordinator::epoch() const {
    const std::lock_guard guard(state_->mutex);
    return state_->epoch;
}
std::uint64_t ActionCoordinator::authority_epoch() const {
    const std::lock_guard guard(state_->mutex);
    return state_->stopped || state_->dirty ? 0 : state_->authority_epoch;
}
void ActionCoordinator::takeover() {
    const std::lock_guard guard(state_->mutex);
    state_->stopped = true;
    ++state_->authority_epoch;
    ++state_->epoch;
    if (state_->busy)
        state_->active->request_cancel();
}
void ActionCoordinator::invalidate_authority() {
    const std::lock_guard guard(state_->mutex);
    ++state_->authority_epoch;
    ++state_->epoch;
    if (state_->busy)
        state_->active->request_cancel();
}
bool ActionCoordinator::resume() {
    const std::lock_guard guard(state_->mutex);
    if (state_->busy || state_->dirty)
        return false;
    state_->stopped = false;
    ++state_->authority_epoch;
    ++state_->epoch;
    return true;
}
bool ActionCoordinator::input_released() const {
    const std::lock_guard guard(state_->mutex);
    return !state_->busy && !state_->dirty;
}
} // namespace mirage::desktop
