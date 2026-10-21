#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <poll.h>
#include <utility>

namespace mirage::platform::linux_backend {
/// Bounded synchronous platform continuation on the caller's Executor task.
/// No thread, queue, timer owner, callbacks after return, or private scheduler.
/// The caller supplies cancellation/authority/deadline checks; cleanup never waits.
template <class Ready>
bool pace_input_until(std::chrono::steady_clock::time_point target, Ready &&ready) {
    for (;;) {
        if (!ready())
            return false;
        const auto now = std::chrono::steady_clock::now();
        if (now >= target)
            return now <= target + std::chrono::milliseconds{250};
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(target - now);
        (void)::poll(nullptr, 0,
                     static_cast<int>(std::clamp<std::int64_t>(remaining.count(), 1, 4)));
    }
}
template <class Ready> bool pace_input_for(std::chrono::milliseconds delay, Ready &&ready) {
    return pace_input_until(std::chrono::steady_clock::now() + delay, std::forward<Ready>(ready));
}
} // namespace mirage::platform::linux_backend
