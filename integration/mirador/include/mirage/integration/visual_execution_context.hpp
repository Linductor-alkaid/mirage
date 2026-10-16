#pragma once

#include <chrono>
#include <optional>

#include <mirador/execution_context.hpp>

#include <mirage/desktop/cancellation.hpp>

namespace mirage::integration {

/// Adapts the pinned-free desktop cancellation primitive onto the mirador
/// execution context (DEC-016 decision 4): `is_cancelled` polls
/// `cancel.cancelled()` — a non-blocking atomic load the mirador pipeline
/// checks at stage boundaries — and `deadline` maps straight through on the
/// steady clock. A cancelled or expired context surfaces as kCancelled /
/// kTimeout from the mirador call, never as a hang or a generic failure.
/// The token is captured by value; copies share one state.
[[nodiscard]] mirador::ExecutionContext
to_execution_context(const desktop::CancelToken &cancel,
                     std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);

} // namespace mirage::integration
