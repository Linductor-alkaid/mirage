#include "bridge.hpp"
#include "frames.hpp"

#include <chrono>
#include <cstdlib>
#include <eui_neo.h>
#include <iostream>
#include <mirage/platform/pet_window.hpp>
#include <mirage/runtime/pet/animation.hpp>
#include <mirage/runtime/pet/status.hpp>

namespace mirage::pet_ui {
namespace {
using Clock = runtime::pet::AnimationPlayer::Clock;
using namespace std::chrono_literals;
struct State {
    std::unique_ptr<Bridge> bridge;
    std::shared_ptr<eui::ImageStream> image = std::make_shared<eui::ImageStream>(2);
    runtime::pet::AnimationPlayer player;
    runtime::pet::Status status;
    DecodedClip decoded;
    const runtime::pet::Clip *asset_attempt = nullptr;
    std::string caption = "启动中";
    std::uint64_t frame_sequence = 0;
    int last_frame = -1;
    Clock::time_point started{}, lost_at{}, exit_at{};
    bool have_status = false;
};
State &state() {
    static State value;
    return value;
}
} // namespace

void start(const std::string &endpoint) {
    auto &model = state();
    model.started = Clock::now();
    std::string diagnostic;
    if (!platform::pet_window::prepare(diagnostic)) {
        std::cerr << "mira_pet: " << diagnostic << '\n';
        platform::pet_window::request_close();
        return;
    }
    model.bridge = std::make_unique<Bridge>(endpoint);
    if (!model.bridge->start(diagnostic)) {
        std::cerr << "mira_pet: " << diagnostic << '\n';
        platform::pet_window::request_close();
        return;
    }
    model.player.select("pet-local", "starting", {}, model.started);
}
void stop() {
    auto &model = state();
    if (model.bridge)
        model.bridge->shutdown();
    model.bridge.reset();
    model.decoded = {};
    model.image.reset();
}
void tick() {
    auto &model = state();
    if (!model.bridge)
        return;
    const auto now = Clock::now();
    if (platform::pet_window::exit_requested() && model.exit_at == Clock::time_point{})
        model.exit_at = now;
    runtime::pet::Status received;
    if (model.bridge->latest(received)) {
        if (!model.have_status || received.owner_epoch != model.status.owner_epoch ||
            received.revision >= model.status.revision) {
            model.status = std::move(received);
            model.have_status = true;
        }
        model.lost_at = {};
    } else if (!model.bridge->connected()) {
        if (model.lost_at == Clock::time_point{})
            model.lost_at = now;
    }
    const bool disconnected = model.lost_at != Clock::time_point{};
    std::string_view display = model.have_status ? model.status.display_state : "starting";
    std::string_view owner = model.have_status ? model.status.owner_epoch : "pet-local";
    std::string_view source = model.have_status ? model.status.source_id : "";
    if (model.exit_at != Clock::time_point{}) {
        display = "exiting";
        owner = "pet-local";
        source = {};
    } else if (disconnected && display != "exiting") {
        display = "disconnected";
        source = {};
    } else if (display == "idle" && now - model.started < 4482ms) {
        // Pet-local entrance is only retained while the service has no work.
        display = "starting";
        owner = "pet-local";
        source = {};
    }
    model.player.select(owner, display, source, now);
    const auto frame = model.player.frame(now);
    model.caption = display == "exiting"                            ? "正在退出"
                    : display == "starting" && owner == "pet-local" ? "启动中"
                    : display == "disconnected"                     ? "连接中断"
                    : model.have_status                             ? model.status.summary
                                        : runtime::pet::state_label(display);
    if (model.have_status && model.status.active_count > 1 && !disconnected)
        model.caption += " · " + std::to_string(model.status.active_count) + " 项";
    if (frame.clip != model.asset_attempt) {
        model.asset_attempt = frame.clip;
        model.decoded = {}; // release the old <=96 MiB state before loading the next
        model.image = std::make_shared<eui::ImageStream>(2); // never retain a stale action frame
        model.last_frame = -1;
        std::string diagnostic;
        if (!decode_clip("assets/pet", *frame.clip, model.decoded, diagnostic)) {
            model.caption = "素材不可用";
            std::cerr << "mira_pet: " << diagnostic << '\n';
        }
    }
    if (model.asset_attempt && !model.decoded.clip)
        model.caption = "素材不可用";
    if (model.decoded.clip && frame.index < model.decoded.rgba_frames.size() &&
        model.last_frame != frame.index) {
        eui::ImageFrame pixels;
        pixels.pixels = model.decoded.rgba_frames[frame.index];
        pixels.width = pixels.height = 512;
        pixels.stride = 512 * 4;
        pixels.format = eui::ImagePixelFormat::RGBA8;
        pixels.sequence = ++model.frame_sequence;
        (void)model.image->submit(std::move(pixels));
        model.last_frame = frame.index;
    }
    model.bridge->set_animating(model.decoded.clip && !frame.finished && !frame.static_fallback);
    if (model.exit_at != Clock::time_point{} && frame.finished)
        platform::pet_window::request_close();
    else if (disconnected && model.exit_at == Clock::time_point{} && now - model.lost_at >= 5s)
        platform::pet_window::request_close();
}
std::shared_ptr<eui::ImageStream> image_stream() { return state().image; }
const std::string &caption() { return state().caption; }
} // namespace mirage::pet_ui

namespace app {
const DslAppConfig &dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
                                           .title("Mira · 状态")
                                           .appId("org.mirage.pet")
                                           .pageId("mira-pet")
                                           .windowSize(240, 274)
                                           .decorated(false)
                                           .resizable(false)
                                           .transparent(true)
                                           .alwaysOnTop(true)
                                           .tray(false)
                                           .clearColor({0, 0, 0, 0})
                                           .fps(30)
                                           .textFont("assets/NotoSansSC-Regular.otf")
                                           .onStart([] {
                                               const char *endpoint =
                                                   std::getenv("MIRAGE_PET_SOCKET");
                                               mirage::pet_ui::start(endpoint ? endpoint : "");
                                           })
                                           .onShutdown(mirage::pet_ui::stop);
    return config;
}
void compose(eui::Ui &ui, const eui::Screen &screen) {
    ui.stack("pet.root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.image("pet.animation")
                .position(0, 0)
                .size(screen.width, 230)
                .stream(mirage::pet_ui::image_stream())
                .contain()
                .hitTestMode(eui::dsl::HitTestMode::None)
                .onFrame([](float) { mirage::pet_ui::tick(); })
                .build();
            ui.rect("pet.caption.surface")
                .position(6, 228)
                .size(screen.width - 12, 38)
                .color({0.09f, 0.11f, 0.18f, 0.89f})
                .radius(16)
                .hitTestMode(eui::dsl::HitTestMode::None)
                .build();
            ui.text("pet.caption")
                .position(18, 231)
                .size(screen.width - 36, 30)
                .text(mirage::pet_ui::caption())
                .fontSize(18)
                .lineHeight(26)
                .color({1, 1, 1, 1})
                .verticalAlign(eui::VerticalAlign::Center)
                .hitTestMode(eui::dsl::HitTestMode::None)
                .build();
        })
        .build();
}
} // namespace app
