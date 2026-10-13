#include "../support/test.hpp"
#include <frames.hpp>
#include <fstream>
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>
#include <loader.hpp>

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
    for (const auto file :
         {"../mira_rest.webp", "sub/mira_rest.webp", "C:\\mira_rest.webp", ".", ".."}) {
        auto clip = pet::v5_clips()[1];
        clip.file = file;
        MIRAGE_CHECK(
            !mirage::pet_ui::decode_clip(MIRAGE_PET_TEST_ASSETS, clip, frames, diagnostic));
        MIRAGE_CHECK(!diagnostic.empty() && !frames.clip);
    }
    for (int variant = 0; variant < 7; ++variant) {
        auto clip = pet::v5_clips()[1];
        if (variant == 0)
            clip.frame_count = 97;
        if (variant == 1)
            clip.frame_count = 0;
        if (variant == 2)
            clip.frame_duration_ms = 0;
        if (variant == 3)
            clip.intro_end = clip.loop_end;
        if (variant == 4)
            clip.loop_end = clip.frame_count + 1;
        if (variant == 5)
            clip.intro_end = clip.loop_end + 1;
        if (variant == 6)
            clip.once = true;
        MIRAGE_CHECK(
            !mirage::pet_ui::decode_clip(MIRAGE_PET_TEST_ASSETS, clip, frames, diagnostic));
        MIRAGE_CHECK(diagnostic == "invalid v5 clip bounds" && !frames.clip);
    }
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("mirage-pet-assets-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(temporary);
    const auto &clip = pet::v5_clips()[1];
    const auto file = temporary / clip.file;
    std::filesystem::create_symlink(std::filesystem::path(MIRAGE_PET_TEST_ASSETS) / clip.file,
                                    file);
    MIRAGE_CHECK(!mirage::pet_ui::decode_clip(temporary, clip, frames, diagnostic));
    MIRAGE_CHECK(diagnostic == "v5 asset is missing or not a regular file");
    std::filesystem::remove(file);
    {
        std::ofstream output(file, std::ios::binary);
        output << "corrupt webp";
    }
    MIRAGE_CHECK(!mirage::pet_ui::decode_clip(temporary, clip, frames, diagnostic));
    MIRAGE_CHECK(diagnostic == "v5 asset digest mismatch" && !frames.clip);
    std::filesystem::resize_file(file, 8u * 1024u * 1024u + 1);
    MIRAGE_CHECK(!mirage::pet_ui::decode_clip(temporary, clip, frames, diagnostic));
    MIRAGE_CHECK(diagnostic == "v5 asset exceeds size budget");
    std::filesystem::copy_file(std::filesystem::path(MIRAGE_PET_TEST_ASSETS) / clip.file, file,
                               std::filesystem::copy_options::overwrite_existing);
    MIRAGE_CHECK(
        !mirage::pet_ui::decode_clip(temporary, clip, frames, diagnostic, [] { return true; }));
    MIRAGE_CHECK(diagnostic == "v5 asset load cancelled" && !frames.clip);
    int checkpoints = 0;
    // Cancel after file validation and several decoded frames; no partial
    // RGBA cache may escape the failed operation.
    MIRAGE_CHECK(!mirage::pet_ui::decode_clip(temporary, clip, frames, diagnostic,
                                              [&] { return ++checkpoints > 30; }));
    MIRAGE_CHECK(diagnostic == "v5 asset load cancelled" && !frames.clip &&
                 frames.rgba_frames.empty());
    auto mismatched = clip;
    --mismatched.frame_count;
    MIRAGE_CHECK(!mirage::pet_ui::decode_clip(temporary, mismatched, frames, diagnostic));
    MIRAGE_CHECK(diagnostic == "v5 asset canvas, frame count or loop flag mismatch");
    mismatched = clip;
    ++mismatched.frame_duration_ms;
    MIRAGE_CHECK(!mirage::pet_ui::decode_clip(temporary, mismatched, frames, diagnostic));
    MIRAGE_CHECK(diagnostic == "v5 asset frame duration or decode mismatch");
    std::filesystem::remove_all(temporary);

    // Exercise the real decoder on the same managed path used by the UI.
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    config.queue_capacity = config.max_in_flight_tasks = 4;
    MIRAGE_CHECK(executor.initialize(config));
    // Instrumentation changes decoder cost. Timeout semantics use an injected
    // clock in pet_resource_loader_test; this case verifies the real transfer.
    mirage::pet_ui::ResourceLoader loader(
        executor,
        [](const auto &requested, auto &out, auto &why, const auto &stop) {
            return mirage::pet_ui::decode_clip(MIRAGE_PET_TEST_ASSETS, requested, out, why, stop);
        },
        std::chrono::seconds{15});
    loader.request(pet::v5_clips()[3], std::chrono::steady_clock::now());
    mirage::pet_ui::LoadUpdate update;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    bool ready = false;
    kairo::comm::PhaseGate pace;
    while (!ready && std::chrono::steady_clock::now() < deadline) {
        ready = loader.poll(update, std::chrono::steady_clock::now());
        if (!ready)
            (void)pace.wait_for(1, std::chrono::milliseconds{1});
    }
    MIRAGE_CHECK(ready && update.state == mirage::pet_ui::LoadState::Ready);
    MIRAGE_CHECK(update.decoded.rgba_frames.size() == 96);
    std::size_t bytes = 0;
    for (const auto &frame : update.decoded.rgba_frames)
        bytes += frame->size();
    MIRAGE_CHECK(bytes == 96u * 1024u * 1024u);
    loader.stop();
    executor.shutdown(true);
    return mirage::testing::finish("pet_frames_test");
}
