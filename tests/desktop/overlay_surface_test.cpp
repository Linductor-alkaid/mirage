// M5-09 Desktop Overlay surface contract verification (independent
// verification pass, DEC-029). Covers the frame vocabulary's budgets
// (RULE-07: bounded highlights, debug boxes and text) and the
// clamp_overlay_text budget cut: over-budget text is truncated at a UTF-8
// boundary instead of growing the frame or splitting a code point, the cut
// is idempotent, and in-budget text passes through untouched.

#include "../support/test.hpp"

#include <mirage/desktop/overlay_surface.hpp>

#include <string>

namespace {

using mirage::desktop::clamp_overlay_text;
using mirage::desktop::OverlayConfirmation;
using mirage::desktop::OverlayHighlight;
using mirage::desktop::OverlaySurfaceFrame;

void scenario_frame_budgets_are_frozen() {
    MIRAGE_CHECK(OverlaySurfaceFrame::kMaxHighlights == 16);
    MIRAGE_CHECK(OverlaySurfaceFrame::kMaxDebugBoxes == 64);
    MIRAGE_CHECK(OverlaySurfaceFrame::kMaxTextBytes == 256);
    // Default presence is dark: an overlay must never draw without a frame
    // that asks for it (DEC-029: 默认 off，不经同意不在桌面绘制).
    OverlaySurfaceFrame frame;
    MIRAGE_CHECK(!frame.visible);
    MIRAGE_CHECK(frame.hint.empty());
    MIRAGE_CHECK(frame.highlights.empty());
    MIRAGE_CHECK(frame.debug_boxes.empty());
    MIRAGE_CHECK(!frame.confirmation.has_value());
}

void scenario_in_budget_text_passes_through() {
    std::string empty;
    clamp_overlay_text(empty);
    MIRAGE_CHECK(empty.empty());

    std::string short_ascii = "activating \"Save\"";
    const std::string short_ascii_copy = short_ascii;
    clamp_overlay_text(short_ascii);
    MIRAGE_CHECK(short_ascii == short_ascii_copy);

    // Exactly at the budget: the early-out keeps every byte.
    std::string exact(static_cast<std::size_t>(OverlaySurfaceFrame::kMaxTextBytes), 'a');
    clamp_overlay_text(exact);
    MIRAGE_CHECK(exact.size() == OverlaySurfaceFrame::kMaxTextBytes);
}

void scenario_ascii_over_budget_cut_at_budget() {
    std::string text(static_cast<std::size_t>(400), 'a');
    clamp_overlay_text(text);
    MIRAGE_CHECK(text.size() == OverlaySurfaceFrame::kMaxTextBytes);
    MIRAGE_CHECK(text == std::string(OverlaySurfaceFrame::kMaxTextBytes, 'a'));
}

void scenario_utf8_cut_lands_on_code_point_boundary() {
    // 250 ASCII bytes followed by three 3-byte characters (U+65E5): the
    // budget byte sits on a lead byte (0xE6), so the cut keeps the two
    // complete characters before it.
    std::string aligned(250, 'a');
    aligned += "\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5";
    MIRAGE_CHECK(aligned.size() == 259);
    clamp_overlay_text(aligned);
    MIRAGE_CHECK(aligned.size() == 256);
    MIRAGE_CHECK(aligned.substr(250) == "\xE6\x97\xA5\xE6\x97\xA5");

    // The budget byte sits mid-sequence (continuation byte 0x97): the cut
    // steps back so the result never ends inside a code point.
    std::string mid_sequence(255, 'a');
    mid_sequence += "\xE6\x97\xA5";
    MIRAGE_CHECK(mid_sequence.size() == 258);
    clamp_overlay_text(mid_sequence);
    MIRAGE_CHECK(mid_sequence.size() == 255);
    MIRAGE_CHECK(mid_sequence == std::string(255, 'a'));

    // A lead byte exactly at the budget: not a continuation byte, the cut
    // keeps the 256 ASCII bytes in front of it.
    std::string lead_at_budget(256, 'a');
    lead_at_budget += "\xE6\x97\xA5";
    clamp_overlay_text(lead_at_budget);
    MIRAGE_CHECK(lead_at_budget.size() == 256);

    // Multiple continuation bytes in a row (malformed input): the loop steps
    // back over all of them until a non-continuation byte or the start.
    std::string run_of_continuations(254, 'a');
    run_of_continuations += "\x80\x80\x80\x80";
    clamp_overlay_text(run_of_continuations);
    MIRAGE_CHECK(run_of_continuations.size() == 253);
}

void scenario_clamp_is_idempotent() {
    std::string text = std::string(300, 'b') + "\xE6\x97\xA5";
    clamp_overlay_text(text);
    const std::string once = text;
    clamp_overlay_text(text);
    MIRAGE_CHECK(text == once);
    MIRAGE_CHECK(text.size() <= OverlaySurfaceFrame::kMaxTextBytes);
}

} // namespace

int main() {
    scenario_frame_budgets_are_frozen();
    scenario_in_budget_text_passes_through();
    scenario_ascii_over_budget_cut_at_budget();
    scenario_utf8_cut_lands_on_code_point_boundary();
    scenario_clamp_is_idempotent();
    return mirage::testing::finish("overlay_surface_test");
}
