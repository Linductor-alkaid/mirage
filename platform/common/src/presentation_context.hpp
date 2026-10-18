#pragma once
#include <atomic>
#include <cstdint>

namespace mirage::platform {
// Explicit Host-owned resource watermark, shared with the owned pet and Portal.
// No event queue, task ownership, or independent scheduling lives here.
struct PresentationContext {
    std::atomic<std::int64_t> not_before_ns{0};
};
} // namespace mirage::platform
