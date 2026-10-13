#include "../support/test.hpp"
#include <mirage/runtime/pet/animation.hpp>

namespace pet = mirage::runtime::pet;
using namespace std::chrono_literals;

int main() {
    const auto &clips = pet::v5_clips();
    MIRAGE_CHECK(clips.size() == 9);
    for (const auto &clip : clips) {
        MIRAGE_CHECK(clip.frame_count > 0 && clip.frame_duration_ms > 0);
        MIRAGE_CHECK(clip.intro_end > 0 && clip.intro_end <= clip.loop_end &&
                     clip.loop_end <= clip.frame_count);
        MIRAGE_CHECK(clip.once == (clip.state == "enter" || clip.state == "done" ||
                                   clip.state == "fail" || clip.state == "exit"));
        if (!clip.once)
            MIRAGE_CHECK(clip.loop_end > clip.intro_end);
    }

    const auto now = pet::AnimationPlayer::Clock::time_point{100s};
    pet::AnimationPlayer player;
    MIRAGE_CHECK(player.select("owner", "idle", {}, now));
    MIRAGE_CHECK(!player.first_cycle_complete(now + 1h)); // loading does not spend the clip
    MIRAGE_CHECK(player.frame(now + 1h).index == 0);
    player.asset_ready(now + 1h);
    const auto idle_start = now + 1h;
    MIRAGE_CHECK(player.frame(idle_start + 8 * 167ms).index == 8);
    MIRAGE_CHECK(!player.first_cycle_complete(idle_start + 44 * 167ms - 1ms));
    MIRAGE_CHECK(player.first_cycle_complete(idle_start + 44 * 167ms));
    MIRAGE_CHECK(player.frame(idle_start + (8 + 36 * 20) * 167ms).index == 8);
    MIRAGE_CHECK(!player.select("owner", "idle", {}, idle_start + 200s));

    const auto thinking_start = idle_start + 201s;
    MIRAGE_CHECK(player.select("owner", "thinking", "turn-a", thinking_start));
    player.asset_ready(thinking_start + 600ms); // decode delay never jumps into the clip
    MIRAGE_CHECK(player.frame(thinking_start + 600ms).index == 0);
    MIRAGE_CHECK(!player.select("owner", "looking", "turn-a", thinking_start + 1s));
    MIRAGE_CHECK(player.frame(thinking_start + 1s).clip->state == "think");
    MIRAGE_CHECK(!player.first_cycle_complete(thinking_start + 600ms + 33 * 125ms - 1ms));
    MIRAGE_CHECK(player.first_cycle_complete(thinking_start + 600ms + 33 * 125ms));

    // The short looking action disappeared before the first think cycle ended.
    // Only the latest live state may play; there is no backlog of old actions.
    MIRAGE_CHECK(player.select("owner", "idle", {}, thinking_start + 600ms + 33 * 125ms));
    MIRAGE_CHECK(player.frame(thinking_start + 600ms + 33 * 125ms).clip->state == "rest");
    MIRAGE_CHECK(player.select("owner", "mouse", "turn-a", thinking_start + 5s));
    MIRAGE_CHECK(player.frame(thinking_start + 5s).clip->state == "mouse");
    // This action ended while its resource was still loading. Skip it before
    // any action frame reaches the screen.
    MIRAGE_CHECK(player.select("owner", "idle", {}, thinking_start + 5500ms));
    player.asset_ready(thinking_start + 6s);
    MIRAGE_CHECK(player.first_cycle_complete(thinking_start + 6s + 44 * 167ms));

    for (const auto display : {"idle", "thinking", "looking", "mouse", "typing"}) {
        player.reset();
        const auto *clip = pet::clip_for_display(display);
        MIRAGE_CHECK(player.select("owner", display, "loop-a", now));
        player.asset_ready(now);
        const auto loop_size = clip->loop_end - clip->intro_end;
        for (int cycle = 0; cycle < 20; ++cycle) {
            const auto at = now + std::chrono::milliseconds{(clip->intro_end + loop_size * cycle) *
                                                            clip->frame_duration_ms};
            MIRAGE_CHECK(player.frame(at).index == clip->intro_end);
        }
        MIRAGE_CHECK(!player.select("owner", display, "loop-a", now + 1h));
        MIRAGE_CHECK(player.select("owner", display, "loop-b", now + 1h));
        player.asset_ready(now + 1h);
        MIRAGE_CHECK(player.frame(now + 1h).index == 0);
    }

    for (const auto display : {"task_success", "task_failure"}) {
        player.reset();
        const auto *clip = pet::clip_for_display(display);
        const auto duration =
            std::chrono::milliseconds{clip->frame_count * clip->frame_duration_ms};
        const auto start = now + 300s;
        MIRAGE_CHECK(player.select("owner", display, "result-a", start));
        player.asset_ready(start);
        MIRAGE_CHECK(!player.select("owner", "idle", {}, start + duration - 1ms));
        MIRAGE_CHECK(!player.frame(start + duration - 1ms).finished);
        MIRAGE_CHECK(player.frame(start + duration).finished);
        MIRAGE_CHECK(player.frame(start + duration).index == clip->frame_count - 1);
        if (clip->state == "fail") {
            MIRAGE_CHECK(player.select("owner", display, "result-a", start + duration));
            MIRAGE_CHECK(player.frame(start + duration).clip->state == "rest");
            player.asset_ready(start + duration);
            MIRAGE_CHECK(!player.select("owner", display, "result-a", start + 1h));
            MIRAGE_CHECK(player.frame(start + 1h).clip->state == "rest");
            player.reset(); // suspension/restoration does not replay a consumed failure
            MIRAGE_CHECK(player.select("owner", display, "result-a", start + 1h));
            MIRAGE_CHECK(player.frame(start + 1h).clip->state == "rest");
            player.asset_ready(start + 1h);
            MIRAGE_CHECK(player.select("owner", display, "result-b", start + 2h));
            player.asset_ready(start + 2h);
            MIRAGE_CHECK(player.frame(start + 2h).clip->state == "fail");
            MIRAGE_CHECK(player.frame(start + 2h).index == 0);
        } else {
            MIRAGE_CHECK(!player.select("owner", display, "result-a", start + duration + 1s));
            MIRAGE_CHECK(player.frame(start + 1h).index == clip->frame_count - 1);
            MIRAGE_CHECK(player.select("owner", "idle", {}, start + duration));
        }
    }

    player.reset();
    MIRAGE_CHECK(player.select("owner", "typing", "work-a", now));
    player.asset_ready(now);
    MIRAGE_CHECK(player.select("owner", "takeover", {}, now + 100ms));
    MIRAGE_CHECK(player.frame(now + 100ms).static_fallback); // safety interrupts immediately
    MIRAGE_CHECK(player.select("new-owner", "starting", {}, now + 200ms));
    MIRAGE_CHECK(player.frame(now + 200ms).index == 0);
    player.asset_failed(now + 300ms); // failed asset cannot hold the state gate forever
    MIRAGE_CHECK(player.select("new-owner", "idle", {}, now + 300ms));
    player.reset();
    MIRAGE_CHECK(player.select("pet-local", "starting", {}, now));
    player.asset_ready(now + 600ms);
    MIRAGE_CHECK(!player.select("owner", "idle", {}, now + 54 * 83ms));
    MIRAGE_CHECK(player.select("owner", "idle", {}, now + 600ms + 54 * 83ms));
    player.reset();
    MIRAGE_CHECK(player.select("owner", "starting", {}, now));
    player.asset_ready(now);
    MIRAGE_CHECK(player.frame(now + 54 * 83ms).finished);
    MIRAGE_CHECK(player.frame(now + 54 * 83ms).index == 53);
    MIRAGE_CHECK(player.select("owner", "exiting", {}, now + 1s)); // shutdown is urgent
    player.asset_ready(now + 1s);
    MIRAGE_CHECK(player.frame(now + 1s + 52 * 100ms).finished);
    MIRAGE_CHECK(player.frame(now + 1s + 52 * 100ms).index == 51);
    MIRAGE_CHECK(player.frame(now + 999ms).index == 0); // backwards clock clamps
    return mirage::testing::finish("pet_animation_test");
}
