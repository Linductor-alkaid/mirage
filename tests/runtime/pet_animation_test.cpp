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
        MIRAGE_CHECK(clip.once == (clip.state == "enter" || clip.state == "exit"));
        if (!clip.once)
            MIRAGE_CHECK(clip.loop_end > clip.intro_end);
    }
    const auto now = pet::AnimationPlayer::Clock::time_point{100s};
    pet::AnimationPlayer player;
    player.select("owner", "idle", {}, now);
    MIRAGE_CHECK(player.frame(now).index == 0);
    MIRAGE_CHECK(player.frame(now + 8 * 167ms).index == 8);
    const auto loop = player.frame(now + (8 + 36 * 20) * 167ms);
    MIRAGE_CHECK(loop.clip->state == "rest" && loop.index == 8);
    player.select("owner", "idle", {}, now + 200s); // a newer revision must not replay intro
    MIRAGE_CHECK(player.frame(now + (8 + 36 * 20) * 167ms).index == 8);
    player.select("owner", "thinking", "turn-a", now + 201s);
    MIRAGE_CHECK(player.frame(now + 201s).index == 0);
    MIRAGE_CHECK(player.frame(now + 201s + 22 * 125ms).index == 22);
    MIRAGE_CHECK(player.frame(now + 201s + (22 + 11 * 20) * 125ms).index == 22);
    player.select("owner", "thinking", "turn-b", now + 203s); // new work restarts intro
    MIRAGE_CHECK(player.frame(now + 203s).index == 0);
    // Verify 20-cycle loop seams for all seven looping states (M8-02)
    player.select("owner", "looking", {}, now + 210s);
    MIRAGE_CHECK(player.frame(now + 210s + (22 + 67 * 20) * 100ms).index == 22); // look: [22,89)
    player.select("owner", "mouse", {}, now + 220s);
    MIRAGE_CHECK(player.frame(now + 220s + (22 + 11 * 20) * 91ms).index == 22); // mouse: [22,33)
    player.select("owner", "typing", {}, now + 230s);
    MIRAGE_CHECK(player.frame(now + 230s + (27 + 24 * 20) * 71ms).index == 27); // typing: [27,51)
    player.select("owner", "task_success", {}, now + 240s);
    MIRAGE_CHECK(player.frame(now + 240s + (46 + 8 * 20) * 83ms).index == 46); // done: [46,54)
    player.select("owner", "task_failure", {}, now + 250s);
    MIRAGE_CHECK(player.frame(now + 250s + (12 + 13 * 20) * 125ms).index == 12); // fail: [12,25)
    player.select("owner", "takeover", {}, now + 260s);
    const auto safe = player.frame(now + 261s);
    MIRAGE_CHECK(safe.static_fallback && safe.clip->state == "rest" && safe.index == 0);
    player.select("owner", "unknown_state", {}, now + 261s);
    const auto fallback = player.frame(now + 262s);
    MIRAGE_CHECK(fallback.static_fallback && fallback.clip->state == "rest" && fallback.index == 0);
    // Fast state switch: thinking -> looking must reset intro immediately (M8-03)
    player.select("owner", "thinking", "work-1", now + 270s);
    MIRAGE_CHECK(player.frame(now + 270s).index == 0); // intro starts
    player.select("owner", "looking", {}, now + 270s + 100ms); // rapid switch mid-intro
    MIRAGE_CHECK(player.frame(now + 270s + 100ms).index == 0); // new intro from frame 0
    player.select("owner", "starting", {}, now + 280s);
    MIRAGE_CHECK(player.frame(now + 280s + 54 * 83ms).finished);
    MIRAGE_CHECK(player.frame(now + 280s + 54 * 83ms).index == 53);
    MIRAGE_CHECK(player.frame(now + 280s + 100s).index == 53); // never loops enter
    player.select("owner", "exiting", {}, now + 381s);
    MIRAGE_CHECK(player.frame(now + 381s + 52 * 100ms).finished);
    MIRAGE_CHECK(player.frame(now + 381s + 52 * 100ms).index == 51);
    player.select("new-owner", "idle", {}, now + 387s);
    MIRAGE_CHECK(player.frame(now + 387s).index == 0);
    MIRAGE_CHECK(player.frame(now + 386s).index == 0); // backwards clock clamps
    return mirage::testing::finish("pet_animation_test");
}
