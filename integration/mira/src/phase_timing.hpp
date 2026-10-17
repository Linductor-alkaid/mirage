#pragma once
#include <chrono>
#include <mirage/conversation.hpp>
#include <utility>

namespace mirage::integration::detail {
// Measurements stay on the existing serialized Executor task. No scheduling
// or task-health state is created here; nested phases are deliberately inclusive.
class PhaseTimingScope {
  public:
    explicit PhaseTimingScope(conversation::PhaseTiming *phase)
        : phase_(phase), started_(std::chrono::steady_clock::now()) {}
    ~PhaseTimingScope() {
        if (phase_) {
            ++phase_->calls;
            phase_->duration_ms +=
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now() - started_)
                                               .count());
        }
    }
    PhaseTimingScope(const PhaseTimingScope &) = delete;
    PhaseTimingScope &operator=(const PhaseTimingScope &) = delete;

  private:
    conversation::PhaseTiming *phase_;
    std::chrono::steady_clock::time_point started_;
};
template <class F> decltype(auto) measure_phase(conversation::PhaseTiming *phase, F &&operation) {
    PhaseTimingScope timing(phase);
    return std::forward<F>(operation)();
}
} // namespace mirage::integration::detail
