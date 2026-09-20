// M2-06 observation end-to-end test (design doc section 6, M2 exit
// condition): the full observation loop runs against real platform
// frontends — a private D-Bus session hosting the wire-exact AT-SPI2
// fixture desktop, a real Xvfb window for the window-id -> title ->
// accessibility-tree mapping — bound through MiraEnvironmentBinding.
// observe() must deliver a pinned-validator-clean UiTreeSnapshot plus the
// foreground context, snapshot refs must resolve into real semantic
// actions on the fixture, and a fresh observation must be a fresh capture.

#include "../support/atspi_session.hpp"
#include "../support/fake_atspi_desktop.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"

#include <mirage/desktop/observation_assembler.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <X11/Xlib.h>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::ElementTarget;
using mirage::desktop::ObservationAssembler;
using mirage::desktop::ObservationComponents;
namespace linux_backend = mirage::platform::linux_backend;
using linux_backend::LinuxDesktopEnvironment;
using mirage::testing::FakeAtspiDesktop;

constexpr const char *kWindowTitle = "FakeWindow — main";
constexpr const char *kApplicationName = "FakeEditor";

} // namespace

int main() {
    try {
        mirage::testing::AtspiSession session;

        // One application subtree: frame -> panel -> {button, entry}; the
        // entry carries the Focused state so focused-element delivery is
        // observable, the button exposes a semantic action.
        FakeAtspiDesktop desktop;
        desktop.nodes.push_back({"/org/a11y/atspi/accessible/root", ATSPI_ROLE_DESKTOP_FRAME,
                                 "test-desktop", "", -1, false, false, "", false});
        desktop.nodes.push_back({"/org/fake/root", ATSPI_ROLE_APPLICATION, kApplicationName, "", 0,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/root/window", ATSPI_ROLE_FRAME, kWindowTitle, "", 1,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/root/window/pane", ATSPI_ROLE_PANEL, "main", "", 2,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/root/window/pane/run", ATSPI_ROLE_PUSH_BUTTON, "Run",
                                 "Run button", 3, true, false, "", false});
        desktop.nodes.push_back({"/org/fake/root/window/pane/name", ATSPI_ROLE_ENTRY, "Name",
                                 "Name field", 3, false, true, "hello", /*focused=*/true});
        if (!desktop.start(session.bus_address())) {
            return 1;
        }

        // A real X window whose title matches the accessible window drives
        // the window-id -> title -> accessibility-tree mapping.
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

        auto environment = std::make_shared<LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{},
            linux_backend::X11Options{true, server.display_name()},
            linux_backend::AtspiOptions{true});
        if (environment->accessibility() == nullptr || environment->window() == nullptr) {
            std::fprintf(stderr, "backend failed to initialize\n");
            return 1;
        }
        mirage::integration::MiraEnvironmentBinding binding(environment);

        // Focus the fixture window through the environment's own activation
        // path (no WM runs on Xvfb: activation takes the input-focus
        // fallback), so the observation has a focused window to capture.
        std::string window_id;
        for (const auto &window : environment->window()->list_windows().windows) {
            if (window.title == kWindowTitle) {
                window_id = window.id;
            }
        }
        MIRAGE_CHECK(!window_id.empty());
        MIRAGE_CHECK(environment->window()->activate(window_id).ok);
        MIRAGE_CHECK(environment->window()->front_window().found);

        // ---- capabilities over the live backends ---------------------------
        const auto capabilities = binding.capabilities();
        MIRAGE_CHECK(capabilities.ui_tree);
        MIRAGE_CHECK(capabilities.foreground_app);
        MIRAGE_CHECK(!capabilities.screen_capture);

        // ---- observe structure + foreground end to end ---------------------
        mira::ObservationRequest request;
        request.required.structure = true;
        request.required.foreground = true;
        const auto first = binding.observe(request, mira::make_control_context());
        MIRAGE_CHECK(first.has_value());
        if (!first.has_value()) {
            std::fprintf(stderr, "observe failed: %s\n",
                         first.error().safe_message.c_str()); // loud, then stop
            XCloseDisplay(xdisplay);
            return 1;
        }
        const mira::Observation &observation = first.value();

        MIRAGE_CHECK(!observation.id.is_nil());
        MIRAGE_CHECK(observation.atomicity == mira::ObservationAtomicity::NonAtomic);
        MIRAGE_CHECK(observation.aggregate_span.normalized_begin.monotonic <=
                     observation.aggregate_span.normalized_end.monotonic);
        MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Good);
        MIRAGE_CHECK(observation.quality.degradations.empty());

        // The X11 frontend is live, so the best-effort topology is present.
        MIRAGE_CHECK(observation.topology.displays.size() == 1);

        // Structure: validator-clean, matching the fixture tree exactly
        // (frame -> panel -> {button, entry}).
        MIRAGE_CHECK(observation.structure.has_value());
        const mira::UiTreeSnapshot &structure = observation.structure->value;
        MIRAGE_CHECK(mira::validate_ui_tree_snapshot(structure).has_value());
        MIRAGE_CHECK(structure.nodes.size() == 4);
        MIRAGE_CHECK(structure.complete);
        MIRAGE_CHECK(!structure.truncated);
        MIRAGE_CHECK(!structure.space.is_nil());
        MIRAGE_CHECK(observation.structure->capture.normalized_begin.monotonic <=
                     observation.structure->capture.normalized_end.monotonic);

        std::set<std::string> hints;
        for (std::size_t i = 0; i < structure.nodes.size(); ++i) {
            MIRAGE_CHECK(structure.nodes[i].stable_hint.has_value());
            const std::string &hint = structure.nodes[i].stable_hint->hint;
            MIRAGE_CHECK(hint.size() >= 3 && hint[0] == '@' && hint[1] == 'e');
            MIRAGE_CHECK(hints.insert(hint).second); // refs are unique
            MIRAGE_CHECK(structure.nodes[i].space == structure.space);
        }
        MIRAGE_CHECK(structure.nodes[0].stable_hint->hint == "@e1");
        MIRAGE_CHECK(structure.nodes[0].role == mira::UiRole::Window); // frame
        MIRAGE_CHECK(structure.nodes[0].text == kWindowTitle);
        MIRAGE_CHECK(!structure.nodes[0].parent.has_value());
        MIRAGE_CHECK(structure.nodes[1].stable_hint->hint == "@e2");
        MIRAGE_CHECK(structure.nodes[1].role == mira::UiRole::Pane); // panel
        MIRAGE_CHECK(structure.nodes[1].parent == structure.nodes[0].id);
        MIRAGE_CHECK(structure.nodes[2].stable_hint->hint == "@e3");
        MIRAGE_CHECK(structure.nodes[2].role == mira::UiRole::Button);
        MIRAGE_CHECK(structure.nodes[2].text == "Run");
        MIRAGE_CHECK(structure.nodes[2].parent == structure.nodes[1].id);
        MIRAGE_CHECK(structure.nodes[3].stable_hint->hint == "@e4");
        MIRAGE_CHECK(structure.nodes[3].role == mira::UiRole::Text); // AT-SPI entry
        MIRAGE_CHECK(structure.nodes[3].text == "Name");
        MIRAGE_CHECK(structure.nodes[3].parent == structure.nodes[1].id);
        for (std::size_t i = 0; i < structure.nodes.size(); ++i) {
            // The fixture enables every node; only the entry is focused.
            MIRAGE_CHECK(mira::has_state(structure.nodes[i].state, mira::UiNodeState::Enabled));
            MIRAGE_CHECK(mira::has_state(structure.nodes[i].state, mira::UiNodeState::Focused) ==
                         (i == 3));
        }

        // Foreground: package from the accessibility application root,
        // activity from the X window title.
        MIRAGE_CHECK(observation.foreground.has_value());
        MIRAGE_CHECK(observation.foreground->value.package_name == kApplicationName);
        MIRAGE_CHECK(observation.foreground->value.activity_name == kWindowTitle);

        // The desktop-layer assembler over the same live environment reports
        // the focused element from the fixture's Focused state.
        ObservationAssembler assembler(*environment);
        ObservationComponents focus_components;
        focus_components.active_window = true;
        focus_components.semantic_snapshot = true;
        const auto desktop_view = assembler.assemble(focus_components);
        MIRAGE_CHECK(desktop_view.ok);
        MIRAGE_CHECK(desktop_view.observation.focused_element == "@e4");
        MIRAGE_CHECK(desktop_view.observation.active_application == kApplicationName);
        MIRAGE_CHECK(desktop_view.observation.active_window == kWindowTitle);

        // ---- snapshot refs drive real semantic actions ---------------------
        ElementTarget button;
        button.reference.id = structure.nodes[2].stable_hint->hint; // "@e3"
        const auto activated = environment->accessibility()->activate_element(button);
        MIRAGE_CHECK(activated.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 1);
            MIRAGE_CHECK(desktop.activated.back() == "/org/fake/root/window/pane/run");
        }

        // ---- re-observe: a fresh observation is a fresh capture -----------
        const auto second = binding.observe(request, mira::make_control_context());
        MIRAGE_CHECK(second.has_value());
        if (!second.has_value()) {
            std::fprintf(stderr, "re-observe failed: %s\n", second.error().safe_message.c_str());
            XCloseDisplay(xdisplay);
            return 1;
        }
        MIRAGE_CHECK(second.value().id != observation.id);
        MIRAGE_CHECK(second.value().structure.has_value());
        const mira::UiTreeSnapshot &refreshed = second.value().structure->value;
        MIRAGE_CHECK(mira::validate_ui_tree_snapshot(refreshed).has_value());
        MIRAGE_CHECK(refreshed.nodes.size() == 4);
        // The pinned node identities are freshly generated per capture...
        for (std::size_t i = 0; i < refreshed.nodes.size(); ++i) {
            MIRAGE_CHECK(refreshed.nodes[i].id != structure.nodes[i].id);
        }
        // ...while the same tree walks to the same @eN refs, and the ref
        // from the NEW observation resolves against the live registry.
        MIRAGE_CHECK(refreshed.nodes[2].stable_hint->hint == "@e3");
        ElementTarget refreshed_button;
        refreshed_button.reference.id = refreshed.nodes[2].stable_hint->hint;
        MIRAGE_CHECK(environment->accessibility()->activate_element(refreshed_button).ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 2);
            MIRAGE_CHECK(desktop.activated.back() == "/org/fake/root/window/pane/run");
        }
        // Fixture semantics: activation does not move focus, and a fresh
        // capture reports exactly the live focus state.
        MIRAGE_CHECK(second.value().foreground.has_value());
        MIRAGE_CHECK(second.value().foreground->value.package_name == kApplicationName);
        const auto reassembled = assembler.assemble(focus_components);
        MIRAGE_CHECK(reassembled.ok);
        MIRAGE_CHECK(reassembled.observation.focused_element == "@e4");
        for (std::size_t i = 0; i < refreshed.nodes.size(); ++i) {
            MIRAGE_CHECK(mira::has_state(refreshed.nodes[i].state, mira::UiNodeState::Focused) ==
                         (i == 3));
        }

        // Close the fixture's X connection only now: closing it destroys the
        // windows it created, and the ASAN build fails the process on the
        // otherwise harmless leak.
        XCloseDisplay(xdisplay);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "observation e2e setup failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("observation_e2e");
}
