#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <mirage/desktop/geometry.hpp>

namespace mirage::desktop {

/// One highlighted on-screen region of the Desktop Overlay (design doc
/// section 14, DEC-029). `rect` is in global desktop coordinates; `label`
/// is a short bounded caption rendered next to the box (element ref, window
/// title fragment, ...).
struct OverlayHighlight {
    WindowGeometry rect;
    std::string label;
};

/// One permission confirmation surfaced on the overlay (M5-09, DEC-029):
/// the same pending request the IPC permission face broadcasts (DEC-020),
/// projected for the at-work-position confirm entry. `timeout_ms` is the
/// remaining wait budget at publish time; the carrier recomputes the
/// countdown from its own tick.
struct OverlayConfirmation {
    std::string request_id;
    std::string capability;
    std::string resource;
    std::int64_t timeout_ms = 0;
};

/// One complete overlay frame: a full snapshot of what the overlay shows,
/// published latest-state-wins (DEC-029). `visible=false` hides the
/// surface. The vectors are bounded by their budgets below — publishers
/// truncate (with the budget visible in no way) or drop rather than grow
/// without bound (RULE-07).
struct OverlaySurfaceFrame {
    /// Overall presence: false when nothing has to be on screen.
    bool visible = false;
    /// Status line ("Mirage: activating \"Save\""), also the task banner.
    std::string hint;
    /// Target highlights (the boxes the agent is working on).
    std::vector<OverlayHighlight> highlights;
    /// Pending confirmation, when the overlay should show the confirm
    /// entry; nullopt hides it.
    std::optional<OverlayConfirmation> confirmation;
    /// Observation debug face (DEC-026 semantic snapshot nodes projected
    /// as boxes; M5-09 debug mode only).
    std::vector<OverlayHighlight> debug_boxes;

    /// Upper bound for target highlights per frame (RULE-07).
    static constexpr std::size_t kMaxHighlights = 16;
    /// Upper bound for observation debug boxes per frame (RULE-07).
    static constexpr std::size_t kMaxDebugBoxes = 64;
    /// Upper bound for one label / hint string (bytes, RULE-07).
    static constexpr std::size_t kMaxTextBytes = 256;
};

/// One user click made on the overlay surface (the confirm entry, DEC-029).
/// `approved` mirrors permission.respond's verdict; the request may already
/// be decided by an IPC client (first-response-wins, DEC-020), so delivery
/// is best effort by contract.
struct OverlayClick {
    std::string request_id;
    bool approved = false;
};

/// Clamps one overlay text to the frame budget (RULE-07): over-budget text
/// is cut at a UTF-8 boundary instead of growing the frame without bound.
void clamp_overlay_text(std::string &text);

} // namespace mirage::desktop
