#include "../common/environment.hpp"
#include "../common/typography.hpp"
#include "bridge.hpp"
#include "bubble_view.hpp"
#include "frame_texture.hpp"
#include "frames.hpp"
#include "presentation.hpp"
#include <core/render/text.h>

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
    FrameTexture texture;
    FramePresentation presentation;
    runtime::pet::AnimationPlayer player;
    runtime::pet::Status status;
    BubblePresentation bubble;
    runtime::persistence::AppPreferences preferences;
    runtime::persistence::LocalStateStore position_store{
        runtime::persistence::default_state_directory(), "pet-window-position.txt", 64};
    DecodedClip decoded;
    platform::pet_window::Layout layout;
    const runtime::pet::Clip *asset_attempt = nullptr;
    std::uint64_t asset_generation = 0;
    bool asset_failed = false;
    Clock::time_point started{}, lost_at{}, exit_at{};
    bool have_status = false;
};
State &state() {
    static State value;
    return value;
}
void clear_image(State &model) {
    // Submit the transparent replacement on the next visible UI tick.
    model.presentation.clear();
}
bool submit_pixels(State &model, FramePresentation::Pixels pixels) {
    if (model.texture.upload(*pixels)) {
        platform::pet_window::update_hit_region(*pixels, model.layout);
        return true;
    }
    model.asset_failed = true;
    model.texture.hide();
    platform::pet_window::update_hit_region({}, model.layout);
    std::cerr << "mira_pet: RGBA texture upload failed\n";
    return false;
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
    model.player.begin_startup(model.started);
    clear_image(model); // schedule the first upload in the UI backend scope
}
void stop() {
    auto &model = state();
    if (model.bridge)
        model.bridge->shutdown();
    platform::pet_window::release_visibility();
    model.bridge.reset();
    model.decoded = {};
    model.presentation.suspend();
    model.texture.shutdown();
}
void tick() {
    auto &model = state();
    if (!model.bridge)
        return;
    platform::pet_window::watch_visibility([&model](bool visible) {
        model.bridge->set_visible(visible);
        if (!visible) {
            model.bridge->set_animating(false);
            model.bridge->loader().cancel();
            // Keep the single bounded decoded clip and last GPU frame.
            // Capture withdrawal must not turn into a decode-length blank on
            // restoration. Hidden windows still stop animation and bubble wakes.
            if (!model.decoded.clip)
                model.asset_attempt = nullptr;
            model.bubble = {};
            return;
        }
        // Rebind/re-upload the retained frame in the restored UI scope. A
        // changed status can select a different clip through the ordinary path.
        model.presentation.restart();
        model.asset_attempt = model.decoded.clip;
        app::requestUpdate();
    });
    const auto now = Clock::now();
    if (!model.bridge->visible()) {
        // Drain the cancelled generation without accepting another decode.
        LoadUpdate discarded;
        (void)model.bridge->loader().poll(discarded, now);
        if (!model.bridge->connected()) {
            if (model.lost_at == Clock::time_point{})
                model.lost_at = now;
        } else {
            model.lost_at = {};
        }
        if (platform::pet_window::exit_requested() ||
            (model.lost_at != Clock::time_point{} && now - model.lost_at >= 5s))
            platform::pet_window::request_close();
        return;
    }
    runtime::persistence::AppPreferences preferences;
    if (model.bridge->preferences(preferences))
        model.preferences = std::move(preferences);
    if (platform::pet_window::exit_requested() && model.exit_at == Clock::time_point{})
        model.exit_at = now;
    runtime::pet::Status received;
    bool owner_changed = false;
    if (model.bridge->latest(received)) {
        if (!model.have_status || received.owner_epoch != model.status.owner_epoch ||
            received.revision >= model.status.revision) {
            owner_changed = model.have_status && received.owner_epoch != model.status.owner_epoch;
            model.status = std::move(received);
            const auto scale = std::max(0.01f, model.layout.image_side / 240.0f);
            model.bubble.update(
                model.status.owner_epoch, model.status.source_id, model.status.bubble, now,
                [scale](const std::string &text) {
                    core::TextStyle style;
                    style.text = text;
                    style.fontSize = native_ui::ui_font_size(13) * scale;
                    return core::TextPrimitive::measureTextSize(style).x;
                },
                scale);
            model.bridge->set_bubble_expiry(model.status.bubble && model.status.bubble->final
                                                ? model.status.bubble->remaining_ms
                                                : 0);
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
    }
    if (model.player.select(owner, display, source, now))
        model.presentation.restart();
    auto frame = model.player.frame(now);
    if (frame.clip != model.asset_attempt) {
        model.asset_attempt = frame.clip;
        model.decoded = {}; // release the old <=96 MiB state before loading the next
        model.presentation.restart();
        model.asset_failed = false;
        model.asset_generation = model.bridge->loader().request(*frame.clip, now);
    }
    LoadUpdate loaded;
    if (model.bridge->loader().poll(loaded, now) && loaded.generation == model.asset_generation) {
        model.asset_failed = loaded.state != LoadState::Ready;
        if (!model.asset_failed) {
            model.decoded = std::move(loaded.decoded);
            model.player.asset_ready(now);
        } else {
            std::cerr << "mira_pet: " << loaded.diagnostic << '\n';
            model.player.asset_failed(now);
        }
    }
    if (model.decoded.clip == frame.clip)
        model.player.asset_ready(now);
    frame = model.player.frame(now);
    const bool safe_fallback =
        frame.static_fallback || model.asset_failed || owner_changed || display == "exiting";
    if (auto pixels = model.presentation.next(frame, model.decoded, safe_fallback)) {
        if (!submit_pixels(model, std::move(pixels))) {
            model.decoded = {};
            model.player.asset_failed(now);
            clear_image(model);
        }
    }
    const bool scroll_bubble =
        !disconnected && model.exit_at == Clock::time_point{} && model.bubble.visible(now) &&
        model.bubble.value()->kind == "thinking" && !model.preferences.reduced_motion;
    model.bridge->set_animating(
        scroll_bubble || model.bridge->loader().busy() ||
            (model.decoded.clip && !frame.finished && !frame.static_fallback) ||
            model.exit_at != Clock::time_point{},
        scroll_bubble || model.bridge->loader().busy()
            ? 33ms
            : std::chrono::milliseconds{frame.clip->frame_duration_ms});
    if (model.exit_at != Clock::time_point{} && frame.finished)
        platform::pet_window::request_close();
    else if (disconnected && model.exit_at == Clock::time_point{} && now - model.lost_at >= 5s)
        platform::pet_window::request_close();
}
const FrameTexture &texture() { return state().texture; }
void set_layout(platform::pet_window::Layout layout) { state().layout = layout; }
bool presented() { return state().bridge && state().bridge->visible(); }
void compose_bubble(eui::Ui &ui) {
    auto &model = state();
    if (!model.bridge || !model.bridge->visible() || !model.bridge->connected() ||
        model.exit_at != Clock::time_point{})
        return;
    compose_bubble_view(ui, model.bubble, model.layout, model.preferences, Clock::now());
}
} // namespace mirage::pet_ui

namespace app {
const DslAppConfig &dslAppConfig() {
    static const DslAppConfig config =
        DslAppConfig{}
            .title("Mira")
            .appId("org.mirage.pet")
            .pageId("mira-pet")
            .windowSize(264, 322)
            .fonts("assets/NotoSansSC-Regular.otf")
            .decorated(false)
            .resizable(false)
            .transparent(true)
            .alwaysOnTop(true)
            .tray(false)
            .clearColor({0, 0, 0, 0})
            .fps(30)
            .onStart([] {
                const auto endpoint = mirage::apps::environment_variable("MIRAGE_PET_SOCKET");
                mirage::pet_ui::start(endpoint ? endpoint->c_str() : "");
            })
            .onShutdown(mirage::pet_ui::stop);
    return config;
}
void compose(eui::Ui &ui, const eui::Screen &screen) {
    const auto layout = mirage::platform::pet_window::layout(screen.width, screen.height);
    mirage::pet_ui::set_layout(layout);
    // Bridge timers/events request UI updates. An unconditional onFrame
    // callback would keep EUI rendering even after the animation timer stops.
    mirage::pet_ui::tick();
    ui.stack("pet.root")
        .size(screen.width, screen.height)
        .content([&] {
            // Presentation is driven by the process Executor, only when needed.
            if (!mirage::pet_ui::presented())
                return;
            ui.image("pet.animation")
                .position(layout.image_x, layout.image_y)
                .size(layout.image_side, layout.image_side)
                .texture(mirage::pet_ui::texture().image(), mirage::pet_ui::texture().revision())
                .contain()
                .hitTestMode(eui::dsl::HitTestMode::None)
                .build();
            mirage::pet_ui::compose_bubble(ui);
        })
        .build();
}
} // namespace app
