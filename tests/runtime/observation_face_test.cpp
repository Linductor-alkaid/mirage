/// M5-06 second round (DEC-026): the observation face end to end over Local
/// IPC. A real RuntimeService serves desktop.observe against the same live
/// Xvfb + AT-SPI fixture topology observation_e2e_test uses, so the wire
/// projection is asserted against a real accessibility tree — the semantic
/// snapshot, the frame context and the visual component's fail-closed
/// behaviour (unavailable without a wired registry, the published generation
/// projected when one is wired). This is the M3 non-goal's "visual
/// references enter the UI observation face" carrier on the wire.

#include "../support/atspi_session.hpp"
#include "../support/fake_atspi_desktop.hpp"
#include "../support/ipc_io.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"

#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <X11/Xlib.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace ipc = mirage::runtime::ipc;
namespace linux_backend = mirage::platform::linux_backend;
using linux_backend::LinuxDesktopEnvironment;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::testing::FakeAtspiDesktop;

constexpr auto kCallBudget = std::chrono::seconds{30};
constexpr const char *kWindowTitle = "FakeWindow — main";
constexpr const char *kApplicationName = "FakeEditor";

ServiceConfig make_config(const mirage::testing::TempDir &dir,
                          mirage::desktop::VisualReferenceRegistry *visual_registry) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.4.0-test";
    config.executor_threads = 2;
    config.persist_recovery_state = false;
    config.visual_registry = visual_registry;
    return config;
}

} // namespace

int main() {
    mirage::testing::TempDir dir;
    mirage::testing::AtspiSession session;

    // The same fixture tree observation_e2e_test captures: frame -> panel ->
    // {button, entry}; the entry carries the Focused state.
    FakeAtspiDesktop desktop;
    desktop.nodes.push_back({"/org/a11y/atspi/accessible/root", ATSPI_ROLE_DESKTOP_FRAME,
                             "test-desktop", "", -1, false, false, "", false});
    desktop.nodes.push_back({"/org/fake/root", ATSPI_ROLE_APPLICATION, kApplicationName, "", 0,
                             false, false, "", false});
    desktop.nodes.push_back(
        {"/org/fake/root/window", ATSPI_ROLE_FRAME, kWindowTitle, "", 1, false, false, "", false});
    desktop.nodes.push_back(
        {"/org/fake/root/window/pane", ATSPI_ROLE_PANEL, "main", "", 2, false, false, "", false});
    desktop.nodes.push_back({"/org/fake/root/window/pane/run", ATSPI_ROLE_PUSH_BUTTON, "Run",
                             "Run button", 3, true, false, "", false});
    desktop.nodes.push_back({"/org/fake/root/window/pane/name", ATSPI_ROLE_ENTRY, "Name",
                             "Name field", 3, false, true, "hello", /*focused=*/true});
    if (!desktop.start(session.bus_address())) {
        return 1;
    }

    const std::string xvfb = mirage::testing::find_xvfb();
    if (xvfb.empty()) {
        std::fprintf(stderr, "Xvfb not found: install xvfb or set MIRAGE_XVFB\n");
        return 1;
    }
    mirage::testing::XvfbDisplay server(xvfb);
    Display *xdisplay = XOpenDisplay(server.display_name().c_str());
    if (xdisplay == nullptr) {
        std::fprintf(stderr, "cannot open Xvfb display %s\n", server.display_name().c_str());
        return 1;
    }
    const Window xwindow =
        XCreateSimpleWindow(xdisplay, DefaultRootWindow(xdisplay), 10, 10, 200, 150, 0, 0, 0);
    XStoreName(xdisplay, xwindow, kWindowTitle);
    XMapWindow(xdisplay, xwindow);
    XSync(xdisplay, False);

    // ---- first service: no visual registry wired (product topology today) --
    auto environment = std::make_shared<LinuxDesktopEnvironment>(
        std::vector<std::filesystem::path>{},
        linux_backend::X11Options{true, server.display_name()}, linux_backend::AtspiOptions{true});
    if (environment->accessibility() == nullptr || environment->window() == nullptr) {
        std::fprintf(stderr, "backend failed to initialize\n");
        return 1;
    }

    // Focus the fixture window through the environment's own activation path
    // (no WM runs on Xvfb: activation takes the input-focus fallback), so the
    // observation has a focused window to capture.
    std::string window_id;
    for (const auto &window : environment->window()->list_windows().windows) {
        if (window.title == kWindowTitle) {
            window_id = window.id;
        }
    }
    MIRAGE_CHECK(!window_id.empty());
    MIRAGE_CHECK(environment->window()->activate(window_id).ok);
    MIRAGE_CHECK(environment->window()->front_window().found);

    mirage::desktop::VisualReferenceRegistry *no_registry = nullptr;
    ServiceConfig config = make_config(dir, no_registry);
    RuntimeService service(config);
    MIRAGE_CHECK(
        service.start(std::make_shared<mirage::integration::MiraEnvironmentBinding>(environment))
            .ok);
    ipc::IpcClient client(config.socket_path);

    // hello advertises the observation face (DEC-026).
    const ipc::Response hello = client.call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(hello.ok);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->observation.has_value());
        MIRAGE_CHECK(identity->observation.value_or(false));
    }

    // Default observe: frame + semantic projection from the real tree; the
    // visual pair stays dark (not requested).
    const ipc::Response observe = client.call(ipc::DesktopObserveRequest{}, kCallBudget);
    MIRAGE_CHECK(observe.ok);
    const auto *view = std::get_if<ipc::ObservationView>(&observe.payload);
    MIRAGE_CHECK(view != nullptr);
    if (view != nullptr) {
        MIRAGE_CHECK(view->active_application == kApplicationName);
        MIRAGE_CHECK(view->active_window == kWindowTitle);
        MIRAGE_CHECK(view->window_focused);
        MIRAGE_CHECK(!view->environment_state.empty());
        MIRAGE_CHECK(view->semantic.has_value());
        if (view->semantic.has_value()) {
            MIRAGE_CHECK(view->semantic->application == kApplicationName);
            MIRAGE_CHECK(view->semantic->window_title == kWindowTitle);
            // frame -> panel -> button + entry, refs renumbered from @e1.
            MIRAGE_CHECK(view->semantic->nodes.size() == 4);
            MIRAGE_CHECK(!view->semantic->truncated);
            if (view->semantic->nodes.size() == 4) {
                MIRAGE_CHECK(view->semantic->nodes[0].ref == "@e1");
                MIRAGE_CHECK(view->semantic->nodes[0].parent == -1);
                MIRAGE_CHECK(view->semantic->nodes[2].ref == "@e3");
                MIRAGE_CHECK(view->semantic->nodes[2].name == "Run");
                MIRAGE_CHECK(view->semantic->nodes[3].focused);
            }
        }
        MIRAGE_CHECK(!view->visual_snapshot_ref.has_value());
        MIRAGE_CHECK(!view->visual_regions.has_value());
    }

    // semantic=false skips the projection entirely.
    const ipc::Response frame_only =
        client.call(ipc::DesktopObserveRequest{false, false}, kCallBudget);
    MIRAGE_CHECK(frame_only.ok);
    const auto *frame_view = std::get_if<ipc::ObservationView>(&frame_only.payload);
    MIRAGE_CHECK(frame_view != nullptr);
    if (frame_view != nullptr) {
        MIRAGE_CHECK(!frame_view->semantic.has_value());
        MIRAGE_CHECK(frame_view->active_window == kWindowTitle);
    }

    // visual=true without a wired registry fails closed with the stable
    // unavailable error naming the visual component (DEC-016 discipline).
    const ipc::Response dark_visual =
        client.call(ipc::DesktopObserveRequest{true, true}, kCallBudget);
    MIRAGE_CHECK(!dark_visual.ok);
    MIRAGE_CHECK(dark_visual.error.code == "unavailable");
    MIRAGE_CHECK(dark_visual.error.message.find("visual") != std::string::npos);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);

    // ---- second service: a wired registry with a published generation -----
    mirage::desktop::VisualReferenceRegistry registry;
    mirage::desktop::VisualSnapshot snapshot;
    snapshot.scope_ref = "unassigned"; // re-assigned at publication ("@vs1")
    mirage::desktop::VisualRegionEntry region;
    region.source = mirage::desktop::VisualRegionSource::kOcr;
    region.bounds = {100, 200, 120, 24};
    region.text = "Run";
    snapshot.regions.push_back(region);
    const mirage::desktop::VisualPublishOutcome published = registry.publish(snapshot);
    MIRAGE_CHECK(published.ok);

    ServiceConfig wired_config = make_config(dir, &registry);
    RuntimeService wired_service(wired_config);
    MIRAGE_CHECK(
        wired_service
            .start(std::make_shared<mirage::integration::MiraEnvironmentBinding>(environment))
            .ok);
    ipc::IpcClient wired_client(wired_config.socket_path);

    const ipc::Response wired_observe =
        wired_client.call(ipc::DesktopObserveRequest{true, true}, kCallBudget);
    MIRAGE_CHECK(wired_observe.ok);
    const auto *wired_view = std::get_if<ipc::ObservationView>(&wired_observe.payload);
    MIRAGE_CHECK(wired_view != nullptr);
    if (wired_view != nullptr) {
        MIRAGE_CHECK(wired_view->visual_snapshot_ref.has_value());
        MIRAGE_CHECK(wired_view->visual_snapshot_ref.value_or("") == "@vs1");
        MIRAGE_CHECK(wired_view->visual_regions.has_value());
        if (wired_view->visual_regions.has_value()) {
            MIRAGE_CHECK(wired_view->visual_regions->size() == 1);
            const ipc::ObservationRegion &wire_region = wired_view->visual_regions->front();
            MIRAGE_CHECK(wire_region.ref == "@v1");
            MIRAGE_CHECK(wire_region.source == "ocr");
            MIRAGE_CHECK(wire_region.text == "Run");
            MIRAGE_CHECK(wire_region.geometry.x == 100);
        }
    }

    wired_service.request_shutdown();
    MIRAGE_CHECK(wired_service.run().clean);
    XCloseDisplay(xdisplay);
    return mirage::testing::finish("observation_face_test");
}
