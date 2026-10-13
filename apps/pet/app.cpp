#include "bridge.hpp"
#include "frames.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <eui_neo.h>
#include <iostream>
#include <mirage/platform/pet_window.hpp>
#include <mirage/runtime/persistence/paths.hpp>
#include <mirage/runtime/persistence/store.hpp>
#include <mirage/runtime/pet/animation.hpp>
#include <mirage/runtime/pet/status.hpp>
#include <optional>
#include <sstream>

namespace mirage::pet_ui {
namespace {
using Clock = runtime::pet::AnimationPlayer::Clock;
using namespace std::chrono_literals;
struct State {
    std::unique_ptr<Bridge> bridge;
    std::shared_ptr<eui::ImageStream> images = std::make_shared<eui::ImageStream>(1);
    bool clear_needed = true;
    runtime::pet::AnimationPlayer player;
    runtime::pet::Status status;
    runtime::persistence::LocalStateStore position_store{
        runtime::persistence::default_state_directory(), "pet-window-position.txt", 64};
    DecodedClip decoded;
    std::shared_ptr<const std::vector<std::uint8_t>> neutral_frame;
    platform::pet_window::Layout layout;
    const runtime::pet::Clip *asset_attempt = nullptr;
    std::uint64_t asset_generation = 0;
    bool asset_failed = false;
    std::uint64_t frame_sequence = 0;
    int last_frame = -1;
    Clock::time_point started{}, lost_at{}, exit_at{};
    bool have_status = false;
};
State &state() {
    static State value;
    return value;
}
void clear_image(State &model) {
    // Submit the transparent replacement on the next visible UI tick.
    model.clear_needed = true;
}
} // namespace

void start(const std::string &endpoint) {
    auto &model = state();
    model.started = Clock::now();
    std::string diagnostic;
    std::optional<platform::pet_window::Position> saved_position;
    const auto stored = model.position_store.load();
    if (stored.status == runtime::persistence::LoadStatus::Loaded) {
        int x = 0, y = 0;
        std::istringstream input(stored.body);
        std::string trailing;
        if (input >> x >> y && !(input >> trailing))
            saved_position = platform::pet_window::Position{x, y};
        else
            std::cerr << "mira_pet: stored window position is invalid\n";
    } else if (stored.status != runtime::persistence::LoadStatus::Absent) {
        std::cerr << "mira_pet: cannot load window position: " << stored.error << '\n';
    }
    if (!platform::pet_window::prepare(
            saved_position,
            [&model](platform::pet_window::Position position) {
                const auto body =
                    std::to_string(position.x) + " " + std::to_string(position.y) + "\n";
                const auto saved = model.position_store.save(body);
                if (!saved.ok)
                    std::cerr << "mira_pet: cannot save window position: " << saved.error << '\n';
            },
            diagnostic)) {
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
    clear_image(model); // schedule the first upload in the UI backend scope
}
void stop() {
    auto &model = state();
    if (model.bridge)
        model.bridge->shutdown();
    platform::pet_window::release_visibility();
    model.bridge.reset();
    model.decoded = {};
    model.images.reset();
}
void tick() {
    auto &model = state();
    if (!model.bridge)
        return;
    platform::pet_window::watch_visibility([&model](bool visible) {
        model.bridge->set_visible(visible);
        model.bridge->set_animating(false);
        model.bridge->loader().cancel();
        model.decoded = {};
        model.neutral_frame.reset();
        model.asset_attempt = nullptr;
        model.last_frame = -1;
        model.player.reset();
        clear_image(model);
        if (visible)
            app::requestUpdate();
    });
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
    // BUG-20261013-002: both conditional operands are views. Mixing std::string and a literal
    // creates a temporary string, leaving the selected view dangling.
    std::string_view display =
        model.have_status ? std::string_view{model.status.display_state} : "starting";
    std::string_view owner =
        model.have_status ? std::string_view{model.status.owner_epoch} : "pet-local";
    std::string_view source = model.have_status ? std::string_view{model.status.source_id} : "";
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
    if (model.player.select(owner, display, source, now))
        model.last_frame = -1;
    auto frame = model.player.frame(now);
    bool clip_changed = false;
    if (frame.clip != model.asset_attempt) {
        clip_changed = true;
        model.asset_attempt = frame.clip;
        model.decoded = {}; // release the old <=96 MiB state before loading the next
        model.last_frame = -1;
        model.asset_failed = false;
        model.asset_generation = model.bridge->loader().request(*frame.clip, now);
    }
    LoadUpdate loaded;
    if (model.bridge->loader().poll(loaded, now) && loaded.generation == model.asset_generation) {
        model.asset_failed = loaded.state != LoadState::Ready;
        if (!model.asset_failed) {
            model.decoded = std::move(loaded.decoded);
            model.player.asset_ready(now);
            if (model.decoded.clip && model.decoded.clip->state == "rest" &&
                model.decoded.rgba_frames.size() > 8)
                model.neutral_frame = model.decoded.rgba_frames[8];
        } else {
            std::cerr << "mira_pet: " << loaded.diagnostic << '\n';
            model.player.asset_failed(now);
            if (!model.neutral_frame)
                clear_image(model);
        }
    }
    if (model.decoded.clip == frame.clip)
        model.player.asset_ready(now);
    frame = model.player.frame(now);
    const auto submit = [&](std::shared_ptr<const std::vector<std::uint8_t>> pixels) {
        const auto region_pixels = pixels;
        eui::ImageFrame image_frame{};
        image_frame.pixels = std::move(pixels);
        image_frame.width = image_frame.height = 512;
        image_frame.stride = 512 * 4;
        image_frame.format = eui::ImagePixelFormat::RGBA8;
        image_frame.sequence = ++model.frame_sequence;
        if (model.images->submit(std::move(image_frame))) {
            platform::pet_window::update_hit_region(*region_pixels, model.layout);
            return true;
        }
        model.asset_failed = true;
        std::cerr << "mira_pet: RGBA ImageStream admission failed\n";
        return false;
    };
    if (clip_changed && !model.clear_needed) {
        if (model.neutral_frame)
            (void)submit(model.neutral_frame);
        else if (frame.static_fallback)
            clear_image(model);
    }
    if (model.clear_needed) {
        static const auto transparent =
            std::make_shared<const std::vector<std::uint8_t>>(512 * 512 * 4, 0);
        (void)submit(transparent);
        model.clear_needed = false;
    }
    if (model.decoded.clip && frame.index < model.decoded.rgba_frames.size() &&
        model.last_frame != frame.index) {
        if (submit(model.decoded.rgba_frames[frame.index]))
            model.last_frame = frame.index;
        else {
            model.decoded = {};
            model.player.asset_failed(now);
            clear_image(model);
        }
    }
    model.bridge->set_animating(model.bridge->loader().busy() ||
                                (model.decoded.clip && !frame.finished && !frame.static_fallback) ||
                                model.exit_at != Clock::time_point{});
    if (model.exit_at != Clock::time_point{} && frame.finished)
        platform::pet_window::request_close();
    else if (disconnected && model.exit_at == Clock::time_point{} && now - model.lost_at >= 5s)
        platform::pet_window::request_close();
}
std::shared_ptr<eui::ImageStream> images() { return state().images; }
void set_layout(platform::pet_window::Layout layout) { state().layout = layout; }
} // namespace mirage::pet_ui

namespace app {
const DslAppConfig &dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
                                           .title("Mira")
                                           .appId("org.mirage.pet")
                                           .pageId("mira-pet")
                                           .windowSize(240, 240)
                                           .decorated(false)
                                           .resizable(false)
                                           .transparent(true)
                                           .alwaysOnTop(true)
                                           .tray(false)
                                           .clearColor({0, 0, 0, 0})
                                           .fps(30)
                                           .onStart([] {
                                               const char *endpoint =
                                                   std::getenv("MIRAGE_PET_SOCKET");
                                               mirage::pet_ui::start(endpoint ? endpoint : "");
                                           })
                                           .onShutdown(mirage::pet_ui::stop);
    return config;
}
void compose(eui::Ui &ui, const eui::Screen &screen) {
    const float image_side = std::min(screen.width, screen.height);
    const float image_x = (screen.width - image_side) * 0.5f;
    const float image_y = (screen.height - image_side) * 0.5f;
    mirage::platform::pet_window::Layout layout{screen.width, screen.height, image_x, image_y,
                                                image_side};
    mirage::pet_ui::set_layout(layout);
    ui.stack("pet.root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.image("pet.animation")
                .position(layout.image_x, layout.image_y)
                .size(layout.image_side, layout.image_side)
                .stream(mirage::pet_ui::images())
                .contain()
                .hitTestMode(eui::dsl::HitTestMode::None)
                .onFrame([](float) { mirage::pet_ui::tick(); })
                .build();
        })
        .build();
}
} // namespace app
