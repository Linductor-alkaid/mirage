#include "../support/test.hpp"
#include <frames.hpp>

namespace pet = mirage::runtime::pet;

int main() {
    for (const auto &clip : pet::v5_clips()) {
        mirage::pet_ui::DecodedClip frames;
        std::string diagnostic;
        MIRAGE_CHECK(mirage::pet_ui::decode_clip(MIRAGE_PET_TEST_ASSETS, clip, frames, diagnostic));
        MIRAGE_CHECK(diagnostic.empty() && frames.clip == &clip &&
                     frames.rgba_frames.size() == clip.frame_count);
        if (!frames.rgba_frames.empty()) {
            MIRAGE_CHECK(frames.rgba_frames.front()->size() == 512u * 512u * 4u);
            const auto &last = *frames.rgba_frames.back();
            MIRAGE_CHECK(last[3] == 0); // transparent canvas corner is preserved
            bool visible_pixel = false;
            for (std::size_t pixel = 3; pixel < last.size(); pixel += 4)
                visible_pixel = visible_pixel || last[pixel] != 0;
            MIRAGE_CHECK(visible_pixel);
        }
    }
    mirage::pet_ui::DecodedClip frames;
    std::string diagnostic;
    MIRAGE_CHECK(
        !mirage::pet_ui::decode_clip("/missing/mira_pet", pet::v5_clips()[0], frames, diagnostic));
    MIRAGE_CHECK(!diagnostic.empty() && frames.rgba_frames.empty());
    const pet::Clip invalid{"invalid", "../escape.webp", 100, 0, 0, 0, false};
    MIRAGE_CHECK(!mirage::pet_ui::decode_clip(MIRAGE_PET_TEST_ASSETS, invalid, frames, diagnostic));
    MIRAGE_CHECK(diagnostic == "invalid v5 clip bounds");
    return mirage::testing::finish("pet_frames_test");
}
