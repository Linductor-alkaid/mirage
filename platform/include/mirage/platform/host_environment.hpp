#pragma once

namespace mirage::platform {

/// Process-wide DPI awareness mode in effect after declare_host_dpi_awareness().
enum class DpiAwareness {
    per_monitor_v2,    ///< declared by this call (Windows 10 1703+)
    system,            ///< declared by this call (legacy fallback)
    prior_per_monitor, ///< the process was already per-monitor aware (v1 or v2 context)
    unchanged,         ///< awareness is fixed to a mode this call cannot raise
    unavailable,       ///< the platform has no process-level declaration
};

/// Declares the desktop toolchain host's process-wide DPI awareness before
/// any window, enumeration, capture or input work (M7-10, DEC-086): display
/// enumeration, window geometry, capture pixels and injected input
/// coordinates must share the desktop's physical pixel space, and the
/// DEC-086 capture gate only engages the GPU tiers while that single space
/// holds. Idempotent: a call after the awareness is fixed reports the mode
/// actually in effect instead of pretending a new declaration landed.
DpiAwareness declare_host_dpi_awareness();

} // namespace mirage::platform
