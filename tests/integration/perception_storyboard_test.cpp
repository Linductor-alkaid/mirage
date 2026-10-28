#include "../../integration/mira/src/perception_storyboard.hpp"
#include <algorithm>
#include <iostream>
#include <source_location>
#include <stdexcept>

namespace {
namespace integration = mirage::integration;
namespace detail = integration::detail;
using Clock = std::chrono::steady_clock;
int checks = 0;
void check(bool value, const std::source_location loc = std::source_location::current()) {
    ++checks;
    if (!value)
        throw std::runtime_error("storyboard check failed at " + std::to_string(loc.line()));
}
struct Fixture {
    Clock::time_point now = Clock::now();
    integration::PerceptionView view;
    std::vector<std::shared_ptr<integration::PerceptionPreview>> frames;
    Fixture() {
        view.source_epoch = 2;
        view.dropped = 3;
        view.filtered = 4;
        view.resync = true;
        for (int i = 0; i < 4; ++i) {
            auto p = std::make_shared<integration::PerceptionPreview>();
            p->id = "frame-" + std::to_string(i);
            p->observed_at = now - std::chrono::seconds{4 - i};
            auto &image = p->image;
            image.width = 640;
            image.height = 320;
            image.stride = 640 * 4;
            image.pixels.resize(image.stride * 320);
            for (std::size_t j = 0; j < image.pixels.size(); j += 4) {
                image.pixels[j] = static_cast<std::uint8_t>(20 + i * 40);
                image.pixels[j + 3] = 255;
            }
            integration::PerceptionEvent event;
            event.source_epoch = 2;
            event.frame_sequence = std::uint64_t(i + 1);
            event.frame_id = p->id;
            event.observed_at = p->observed_at;
            view.events.push_back(event);
            view.previews.emplace_back(p->id, p);
            frames.push_back(std::move(p));
        }
    }
    auto make(std::vector<std::string> ids = {"frame-3", "frame-1", "frame-0", "frame-2"}) {
        return detail::make_perception_storyboard(view, ids, now, {});
    }
};
void pixels_and_time() {
    Fixture f;
    auto board = f.make();
    check(static_cast<bool>(board));
    const auto &image = board.value().image;
    check(image.width == 640 && image.height == 640 && image.pixels.size() == 640 * 640 * 4);
    const mira::JsonValue meta{board.value().metadata};
    const auto &tiles = *meta.find("tiles")->as_array();
    check(tiles.size() == 4 && *meta.find("sparse")->as_boolean() &&
          !*meta.find("audio")->as_boolean() && !*meta.find("complete_video")->as_boolean());
    check(*meta.find("dropped")->as_integer() == 3 && *meta.find("filtered")->as_integer() == 4 &&
          *meta.find("resync")->as_boolean());
    for (int i = 0; i < 4; ++i) {
        const auto &tile = tiles[std::size_t(i)];
        check(*tile.find("frame_id")->as_string() == "frame-" + std::to_string(i));
        check(*tile.find("offset_ms")->as_integer() == i * 1000 &&
              *tile.find("age_ms")->as_integer() == (4 - i) * 1000);
        const int x = (i % 2) * 320, y = (i / 2) * 320 + 80;
        check(*tile.find("x")->as_integer() == x && *tile.find("y")->as_integer() == y &&
              *tile.find("width")->as_integer() == 320 &&
              *tile.find("height")->as_integer() == 160);
        for (int row = 0; row < 160; ++row)
            for (int col = 0; col < 320; ++col)
                check(
                    image.pixels[std::size_t(y + row) * image.stride + std::size_t(x + col) * 4] ==
                    20 + i * 40);
    }
    check(image.pixels[0] == 0 && image.pixels[3] == 255); // letterbox
    auto two = f.make({"frame-0", "frame-3"});
    check(two && two.value().image.height == 320);
    mira::JsonValue two_meta{two.value().metadata};
    check(*two_meta.find("tiles")->as_array()->back().find("since_previous_ms")->as_integer() ==
          3000);
    auto three = f.make({"frame-0", "frame-1", "frame-2"});
    check(three && three.value().image.pixels.back() == 255 &&
          three.value().image.pixels[639 * three.value().image.stride + 639 * 4] == 0);
}
void boundaries() {
    Fixture f;
    check(!f.make({}) && !f.make({"frame-0"}));
    check(!f.make({"frame-0", "frame-0"}));
    check(!f.make({"frame-0", "frame-1", "frame-2", "frame-3", "frame-4"}));
    auto unavailable = [&](const char *reason) {
        auto board = f.make({"frame-0", "frame-1"});
        check(board && board.value().image.pixels.empty());
        mira::JsonValue meta{board.value().metadata};
        check(!*meta.find("available")->as_boolean() &&
              *meta.find("reason")->as_string() == reason);
    };
    f.frames[0]->observed_at = f.now - std::chrono::seconds{120};
    unavailable("expired_consumed_or_evicted");
    f.frames[0]->observed_at = f.now + std::chrono::seconds{1};
    unavailable("expired_consumed_or_evicted");
    f.frames[0]->observed_at = f.now - std::chrono::seconds{4};
    f.view.source_epoch = 3;
    unavailable("source_epoch_or_event_unavailable");
    f.view.source_epoch = 2;
    f.frames[0]->image.width = 320;
    unavailable("source_geometry_changed");
    f.frames[0]->image.width = 640;
    f.frames[0]->image.stride = 1;
    check(!f.make());
    f.frames[0]->image.stride = 640 * 4;
    f.view.events[1].frame_sequence = 1;
    unavailable("ambiguous_temporal_order");
    f.view.events[1].frame_sequence = 2;
    f.frames[1]->observed_at = f.frames[0]->observed_at;
    f.view.events[1].observed_at = f.frames[0]->observed_at;
    unavailable("ambiguous_temporal_order");
    f.frames[0].reset();
    unavailable("expired_consumed_or_evicted");
    auto cancelled = detail::make_perception_storyboard(
        f.view, {"frame-0", "frame-1"}, f.now, mirage::desktop::CancelToken{[] { return true; }});
    check(!cancelled && cancelled.error().code == mira::ErrorCode::Cancelled);
    Fixture small;
    for (auto &frame : small.frames) {
        frame->image.width = 8;
        frame->image.height = 4;
        frame->image.stride = 32;
        frame->image.pixels.resize(128);
    }
    auto board = small.make();
    check(static_cast<bool>(board));
    mira::JsonValue meta{board.value().metadata};
    check(*meta.find("tiles")->as_array()->front().find("width")->as_integer() == 8);
}
} // namespace
int main() {
    try {
        pixels_and_time();
        boundaries();
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
