#include "../../apps/pet/bridge.hpp"
#include "../support/ipc_io.hpp"
#include <atomic>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/frontend_process.hpp>
#include <mirage/runtime/runtime_service.hpp>
#include <poll.h>

using namespace std::chrono_literals;
namespace {
std::atomic_int wakeups{0};
std::atomic_bool decoding{false}, cancelled{false}, saw_owner_loss{false};
class Environment final : public mirage::desktop::DesktopEnvironment {
  public:
    mirage::desktop::EnvironmentInfo info() const override { return {"pet-bridge", "private"}; }
};
class Tray final : public mirage::desktop::TrayCarrier {
  public:
    RunReport run(const mirage::desktop::TrayCarrierContext &context,
                  const std::function<bool()> &stop) override {
        if (context.on_ready)
            context.on_ready();
        while (!stop())
            (void)::poll(nullptr, 0, 2);
        return {true, {}};
    }
    void wakeup() override {}
};
class Pet final : public mirage::desktop::FrontendProcess {
  public:
    bool open(const std::string &, std::string &) override {
        pid_ = mirage::platform::current_process_id();
        return true;
    }
    std::int64_t pid() override { return pid_.load(); }
    bool stop(std::string &) override {
        pid_ = 0;
        return true;
    }

  private:
    std::atomic_int64_t pid_{0};
};
template <typename Predicate> bool await(Predicate ready, std::chrono::milliseconds budget = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (ready())
            return true;
        (void)::poll(nullptr, 0, 2);
    }
    return ready();
}
} // namespace
namespace app {
void requestUpdate() { ++wakeups; }
} // namespace app
namespace mirage::platform::pet_window {
// Backend/window boundary only; the real Bridge, IPC, timers, mailbox, loader
// and shutdown remain under test. Native Xvfb tests cover the actual backend.
bool relay_hidden_exit(bool lost, std::string &) {
    if (lost)
        saw_owner_loss = true;
    return true;
}
bool exit_requested() { return false; }
} // namespace mirage::platform::pet_window
namespace mirage::pet_ui {
bool decode_clip(const std::filesystem::path &, const runtime::pet::Clip &, DecodedClip &,
                 std::string &, const std::function<bool()> &stop) {
    decoding = true;
    while (!stop())
        (void)::poll(nullptr, 0, 1);
    cancelled = true;
    return false;
}
} // namespace mirage::pet_ui
int main() {
    namespace ui = mirage::pet_ui;
    namespace runtime = mirage::runtime;
    mirage::testing::TempDir directory;
    runtime::ServiceConfig config;
    config.socket_path = directory.root() / "bridge.sock";
    config.persist_settings = config.persist_session_state = config.persist_recovery_state = false;
    config.open_frontend = false;
    config.tray_carrier = std::make_shared<Tray>();
    config.pet = std::make_shared<Pet>();
    runtime::RuntimeService service(config);
    const auto started =
        service.start(std::make_shared<mirage::integration::MiraEnvironmentBinding>(
            std::make_shared<Environment>()));
    MIRAGE_CHECK(started.ok);
    if (!started.ok)
        return mirage::testing::finish("pet_bridge_lifecycle_test");
    std::string diagnostic;
    ui::Bridge bridge(config.socket_path);
    MIRAGE_CHECK(bridge.start(diagnostic));
    if (!diagnostic.empty())
        std::fprintf(stderr, "bridge: %s\n", diagnostic.c_str());
    MIRAGE_CHECK(bridge.connected() && bridge.visible());
    runtime::pet::Status status;
    MIRAGE_CHECK(await([&] { return bridge.latest(status); }));
    const auto revision = status.revision;
    bridge.set_animating(true);
    const auto before = wakeups.load();
    MIRAGE_CHECK(await([&] { return wakeups >= before + 4; }));
    // Source cadence replaces the fast timer; repeated identical UI updates
    // must neither restart/starve it nor retain the old 33 ms wake stream.
    bridge.set_animating(true, 167ms);
    const auto slow = wakeups.load();
    for (int i = 0; i < 6; ++i) {
        bridge.set_animating(true, 167ms);
        (void)::poll(nullptr, 0, 100);
    }
    MIRAGE_CHECK(wakeups - slow >= 2 && wakeups - slow <= 8);
    bridge.set_animating(false, 167ms);
    (void)::poll(nullptr, 0, 50); // drain an already admitted callback
    const auto static_frame = wakeups.load();
    (void)::poll(nullptr, 0, 350);
    MIRAGE_CHECK(wakeups - static_frame <= 2); // no visible animation wake stream
    bridge.set_animating(true);
    auto &loader = bridge.loader();
    (void)loader.request(*runtime::pet::clip_for_display("thinking"),
                         ui::ResourceLoader::Clock::now());
    ui::LoadUpdate update;
    (void)loader.poll(update, ui::ResourceLoader::Clock::now());
    MIRAGE_CHECK(await([] { return decoding.load(); }));
    // The same UI-owned sequence as the app's visibility callback.
    bridge.set_visible(false);
    bridge.set_animating(false);
    loader.cancel();
    MIRAGE_CHECK(!bridge.visible());
    MIRAGE_CHECK(await([&] {
        (void)loader.poll(update, ui::ResourceLoader::Clock::now());
        return cancelled && !loader.busy();
    }));
    const auto paused = wakeups.load();
    (void)::poll(nullptr, 0, 650);
    MIRAGE_CHECK(wakeups > paused && wakeups - paused <= 6); // control wake, not 33 ms animation
    bridge.set_visible(true);
    MIRAGE_CHECK(bridge.visible());
    MIRAGE_CHECK(await([&] { return bridge.latest(status); })); // no newer event required
    MIRAGE_CHECK(status.revision >= revision);
    bridge.set_animating(true);
    const auto restored = wakeups.load();
    MIRAGE_CHECK(await([&] { return wakeups >= restored + 4; }));
    bridge.set_visible(false);
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
    MIRAGE_CHECK(await([&] { return !bridge.connected(); }));
    MIRAGE_CHECK(await([] { return saw_owner_loss.load(); }, 7s));
    bridge.shutdown();
    bridge.shutdown(); // terminal is idempotent; no timer survives the owner
    const auto stopped = wakeups.load();
    (void)::poll(nullptr, 0, 300);
    MIRAGE_CHECK(wakeups == stopped);
    ui::Bridge absent((directory.root() / "absent.sock").string());
    diagnostic.clear();
    MIRAGE_CHECK(!absent.start(diagnostic) && !diagnostic.empty());
    absent.shutdown();
    return mirage::testing::finish("pet_bridge_lifecycle_test");
}
