#include "../../apps/pet/presentation.hpp"
#include "../support/test.hpp"
#include <algorithm>

namespace ui = mirage::pet_ui;
namespace pet = mirage::runtime::pet;

ui::DecodedClip decoded(std::string_view display, std::uint8_t base) {
    ui::DecodedClip result;
    result.clip = pet::clip_for_display(display);
    for (std::uint16_t i = 0; i < result.clip->frame_count; ++i)
        result.rgba_frames.push_back(std::make_shared<const std::vector<std::uint8_t>>(
            std::size_t{4}, static_cast<std::uint8_t>(base + i)));
    return result;
}

int main() {
    const auto rest = decoded("idle", 10);
    const auto think = decoded("thinking", 70);
    const auto look = decoded("looking", 130);
    ui::FramePresentation presentation;
    auto visible = presentation.next({rest.clip, 8}, rest, false);
    MIRAGE_CHECK(visible == rest.rgba_frames[8]); // warm the neutral fallback cache
    presentation.restart();
    visible = presentation.next({think.clip, 0}, think, false);
    MIRAGE_CHECK(visible == think.rgba_frames[0]);
    visible = presentation.next({think.clip, 32}, think, false);
    MIRAGE_CHECK(visible == think.rgba_frames[32]);

    // BUG-20261014-003: observe the actual publication sequence while the
    // next action is loading, including a stale old-clip result. No rest
    // pixels or transparent upload may interrupt the last visible think frame.
    presentation.restart();
    for (int tick = 0; tick < 20; ++tick) {
        auto pixels =
            presentation.next({look.clip, 0}, tick == 0 ? think : ui::DecodedClip{}, false);
        MIRAGE_CHECK(!pixels);
        if (pixels)
            visible = std::move(pixels);
        MIRAGE_CHECK(visible == think.rgba_frames[32]);
    }
    visible = presentation.next({look.clip, 0}, look, false);
    MIRAGE_CHECK(visible == look.rgba_frames[0]); // direct first target frame
    MIRAGE_CHECK(!presentation.next({look.clip, 0}, look, false));
    MIRAGE_CHECK(presentation.next({look.clip, 1}, look, false) == look.rgba_frames[1]);

    // Ready-on-arrival target also has exactly one publication; a real idle
    // state still enters its own animation, without a prelude at neutral frame 8.
    presentation.restart();
    MIRAGE_CHECK(presentation.next({rest.clip, 0}, rest, false) == rest.rgba_frames[0]);
    presentation.restart(); // same clip, new playback identity
    MIRAGE_CHECK(presentation.next({rest.clip, 0}, rest, false) == rest.rgba_frames[0]);

    // The safety paths must not hold the stale input pose during loading.
    presentation.restart();
    MIRAGE_CHECK(presentation.next({think.clip, 3}, think, false) == think.rgba_frames[3]);
    MIRAGE_CHECK(presentation.next({rest.clip, 0, false, true}, {}, true) == rest.rgba_frames[8]);
    MIRAGE_CHECK(!presentation.next({rest.clip, 0, false, true}, {}, true));
    presentation.restart();
    MIRAGE_CHECK(presentation.next({think.clip, 3}, think, false) == think.rgba_frames[3]);
    MIRAGE_CHECK(presentation.next({look.clip, 0}, {}, true) ==
                 rest.rgba_frames[8]); // failure/epoch
    MIRAGE_CHECK(presentation.next({look.clip, 0}, look, true) == look.rgba_frames[0]);

    // Hide/restore releases the neutral reference and clears once; when the
    // target is already ready it replaces the clear directly in that tick.
    presentation.suspend();
    const auto transparent = presentation.next({look.clip, 0}, {}, false);
    MIRAGE_CHECK(transparent && transparent->size() == 512 * 512 * 4);
    if (transparent)
        MIRAGE_CHECK(std::all_of(transparent->begin(), transparent->end(),
                                 [](auto value) { return !value; }));
    MIRAGE_CHECK(!presentation.next({look.clip, 0}, {}, false));
    MIRAGE_CHECK(presentation.next({look.clip, 0}, look, false) == look.rgba_frames[0]);
    presentation.clear();
    MIRAGE_CHECK(presentation.next({look.clip, 1}, look, false) == look.rgba_frames[1]);
    MIRAGE_CHECK(!presentation.next({look.clip, 1}, look, false));
    // DEC-107: temporary capture withdrawal preserves the decoded frame.
    // Restore only invalidates upload identity; no transparent/decode gap.
    presentation.restart();
    MIRAGE_CHECK(presentation.next({look.clip, 1}, look, false) == look.rgba_frames[1]);
    MIRAGE_CHECK(!presentation.next({look.clip, 1}, look, false));
    ui::FramePresentation cold;
    MIRAGE_CHECK(cold.next({look.clip, 0}, {}, true)); // no cached neutral: clear safely
    MIRAGE_CHECK(!cold.next({look.clip, 0}, {}, true));
    return mirage::testing::finish("pet_frame_presentation_test");
}
