#pragma once

#include <algorithm>
#include <chrono>
#include <mirage/runtime/pet/status.hpp>

namespace mirage::pet_ui {
// Local receipt time belongs to this process only; no wire clock comparison.
// A hidden UI may keep only the newest status for longer than its final bubble.
struct StatusDelivery {
    using Clock = std::chrono::steady_clock;
    runtime::pet::Status status;
    Clock::time_point received_at{};

    runtime::pet::Status at(Clock::time_point now) const {
        auto value = status;
        if (value.bubble && value.bubble->final) {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::max(now - received_at, Clock::duration::zero()));
            if (static_cast<std::uint64_t>(age.count()) >= value.bubble->remaining_ms)
                value.bubble.reset();
            else
                value.bubble->remaining_ms -= static_cast<std::uint64_t>(age.count());
        }
        return value;
    }
};
} // namespace mirage::pet_ui
