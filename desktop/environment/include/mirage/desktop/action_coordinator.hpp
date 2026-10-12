#pragma once
#include <cstdint>
#include <memory>
#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {
/// Service-owned, queue-free mutation admission. Shared by all adapters.
class ActionCoordinator {
    struct State;

  public:
    class Lease {
      public:
        Lease() = default;
        ~Lease();
        Lease(Lease &&) noexcept;
        Lease &operator=(Lease &&) noexcept;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        explicit operator bool() const { return state_ != nullptr; }
        const ProviderError &error() const { return error_; }
        void release_failed();

      private:
        friend class ActionCoordinator;
        std::shared_ptr<State> state_;
        ProviderError error_;
        void finish() noexcept;
    };
    ActionCoordinator();
    /// expected_epoch=0 is reserved for legacy non-reference actions.
    Lease try_begin(const CancelToken &cancel, std::uint64_t expected_epoch = 0);
    std::uint64_t epoch() const;
    /// DEC-074: zero while fenced; changes on takeover/resume or failed release,
    /// but stays stable across ordinary actions so a task can observe their result.
    std::uint64_t authority_epoch() const;
    /// Supervisor-only: cancels current action and fences new mutations.
    void takeover();
    bool resume();
    bool input_released() const;

  private:
    std::shared_ptr<State> state_;
};
} // namespace mirage::desktop
