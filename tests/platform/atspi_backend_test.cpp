// M2-03 AT-SPI2 accessibility backend tests (DEC-015 topology): the
// fixture owns the org.a11y.atspi.Registry name on a private D-Bus session
// and exports the desktop tree itself with the exact org.a11y.atspi wire
// protocol, so atspi_get_desktop() in the backend under test walks a live,
// fully controlled tree. (The real at-spi2-registryd is bypassed: Ubuntu
// 24.04's build segfaults on Socket.Embed — upstream bug, see atspi_session.hpp.)

#include "../support/atspi_session.hpp"
#include "../support/fake_atspi_desktop.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"

#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <string>
#include <vector>

#include <X11/Xlib.h>

using mirage::desktop::CancelToken;
using mirage::desktop::ElementTarget;
using mirage::platform::linux_backend::LinuxDesktopEnvironment;
using mirage::testing::FakeAtspiDesktop;

int main() {
    try {
        mirage::testing::AtspiSession session;

        FakeAtspiDesktop desktop;
        desktop.nodes.push_back({"/org/a11y/atspi/accessible/root", ATSPI_ROLE_DESKTOP_FRAME,
                                 "test-desktop", "", -1, false, false, "", false});
        desktop.nodes.push_back({"/org/fake/root", ATSPI_ROLE_APPLICATION, "FakeEditor", "", 0,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/root/window", ATSPI_ROLE_FRAME, "FakeWindow — main", "",
                                 1, false, false, "", false});
        desktop.nodes.push_back({"/org/fake/root/window/pane", ATSPI_ROLE_PANEL, "main", "", 2,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/root/window/pane/run", ATSPI_ROLE_PUSH_BUTTON, "Run",
                                 "Run button", 3, true, false, "", false});
        desktop.nodes.push_back({"/org/fake/root/window/pane/name", ATSPI_ROLE_ENTRY, "Name",
                                 "Name field", 3, false, true, "hello", false});
        // A second application subtree gives the registry-replacement and
        // desktop-enumeration-order scenarios a real second window.
        desktop.nodes.push_back({"/org/second/root", ATSPI_ROLE_APPLICATION, "SecondApp", "", 0,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/second/root/window", ATSPI_ROLE_FRAME, "SecondWindow", "", 6,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/second/root/window/button", ATSPI_ROLE_PUSH_BUTTON,
                                 "SecondButton", "", 7, true, false, "", false});
        desktop.nodes.push_back({"/org/second/root/window/note", ATSPI_ROLE_ENTRY, "Note",
                                 "Note field", 7, false, true, "", false});
        if (!desktop.start(session.bus_address())) {
            return 1;
        }

        // A real X window whose title matches the accessible window makes
        // the window-id -> title -> accessibility-tree mapping observable.
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
        XStoreName(xdisplay, xwindow, "FakeWindow — main");
        XMapWindow(xdisplay, xwindow);
        const Window second_xwindow =
            XCreateSimpleWindow(xdisplay, DefaultRootWindow(xdisplay), 10, 10, 200, 150, 0, 0, 0);
        XStoreName(xdisplay, second_xwindow, "SecondWindow");
        XMapWindow(xdisplay, second_xwindow);
        XSync(xdisplay, False);

        LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()},
                                    {.enabled = true});
        if (env.accessibility() == nullptr || env.window() == nullptr) {
            std::fprintf(stderr, "backend failed to initialize\n");
            return 1;
        }
        auto &a11y = *env.accessibility();

        // Unknown window ids fail closed.
        const auto missing = a11y.semantic_snapshot("missing-window-id");
        MIRAGE_CHECK(!missing.ok);
        MIRAGE_CHECK(missing.error.code == "not_found");

        // Full snapshot path: X window id -> title -> accessible subtree.
        const auto listed = env.window()->list_windows();
        MIRAGE_CHECK(listed.ok);
        std::string matched_id;
        for (const auto &window : listed.windows) {
            if (window.title == "FakeWindow — main") {
                matched_id = window.id;
            }
        }
        MIRAGE_CHECK(!matched_id.empty());

        const auto snapshot = a11y.semantic_snapshot(matched_id);
        MIRAGE_CHECK(snapshot.ok);
        MIRAGE_CHECK(snapshot.snapshot.application == "FakeEditor");
        MIRAGE_CHECK(snapshot.snapshot.window_title == "FakeWindow — main");
        MIRAGE_CHECK(snapshot.snapshot.nodes.size() == 4); // frame, pane, button, entry
        MIRAGE_CHECK(snapshot.snapshot.nodes[0].ref == "@e1");
        MIRAGE_CHECK(snapshot.snapshot.nodes[0].role == "frame");
        MIRAGE_CHECK(snapshot.snapshot.nodes[1].role == "panel");
        MIRAGE_CHECK(snapshot.snapshot.nodes[1].parent == 0);
        MIRAGE_CHECK(snapshot.snapshot.nodes[2].ref == "@e3");
        MIRAGE_CHECK(snapshot.snapshot.nodes[2].role == "button");
        MIRAGE_CHECK(snapshot.snapshot.nodes[2].name == "Run");
        MIRAGE_CHECK(snapshot.snapshot.nodes[3].role == "text");
        MIRAGE_CHECK(snapshot.snapshot.nodes[3].name == "Name");

        // Node budget refuses instead of truncating (the tree has 4 nodes).
        mirage::desktop::SemanticSnapshotLimits tight;
        tight.max_nodes = 2;
        MIRAGE_CHECK(a11y.semantic_snapshot(matched_id, tight, {}).error.code ==
                     "snapshot_too_large");

        // Semantic hint resolution over the live tree.
        ElementTarget semantic;
        semantic.semantic.role = "button";
        semantic.semantic.name = "Run";
        const auto activated = a11y.activate_element(semantic);
        MIRAGE_CHECK(activated.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 1);
            MIRAGE_CHECK(desktop.activated.back() == "/org/fake/root/window/pane/run");
        }

        // Reference resolution against the snapshot registry (DEC-005
        // order: reference first).
        ElementTarget reference;
        reference.reference.id = "@e3";
        const auto ref_activated = a11y.activate_element(reference);
        MIRAGE_CHECK(ref_activated.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 2);
            MIRAGE_CHECK(desktop.activated.back() == "/org/fake/root/window/pane/run");
        }

        ElementTarget stale;
        stale.reference.id = "@e99";
        MIRAGE_CHECK(a11y.activate_element(stale).error.code == "not_found");

        // Structural resolution: role/name pairs from the application root
        // down to the button.
        ElementTarget structural;
        structural.structural.path =
            "/application/FakeEditor/frame/FakeWindow — main/panel/main/button/Run";
        const auto structural_hit = a11y.activate_element(structural);
        MIRAGE_CHECK(structural_hit.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 3);
            MIRAGE_CHECK(desktop.activated.back() == "/org/fake/root/window/pane/run");
        }

        // set_text via semantic hint onto the editable entry.
        ElementTarget entry;
        entry.semantic.role = "text";
        entry.semantic.name = "Name";
        const auto written = a11y.set_text(entry, "mirage");
        MIRAGE_CHECK(written.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.texts.size() == 1);
            if (desktop.texts.size() == 1) {
                MIRAGE_CHECK(desktop.texts.back().second == "mirage");
            }
        }

        // Contract rejections before any side effect.
        ElementTarget visual;
        visual.visual.template_id = "cache:settings";
        MIRAGE_CHECK(a11y.activate_element(visual).error.code == "unsupported_hint");
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 3); // unchanged

            ElementTarget unknown;
            unknown.semantic.role = "button";
            unknown.semantic.name = "Nonexistent";
            MIRAGE_CHECK(a11y.activate_element(unknown).error.code == "not_found");

            ElementTarget no_hint;
            MIRAGE_CHECK(a11y.activate_element(no_hint).error.code == "invalid_argument");

            ElementTarget not_editable;
            not_editable.semantic.role = "button";
            not_editable.semantic.name = "Run";
            MIRAGE_CHECK(a11y.set_text(not_editable, "x").error.code == "unsupported_element");

            mirage::desktop::InputLimits tiny;
            tiny.max_text_bytes = 2;
            MIRAGE_CHECK(a11y.set_text(entry, "toolong", tiny).error.code == "invalid_argument");
            MIRAGE_CHECK(!a11y.set_text(entry, "\xff").ok);

            CancelToken cancel;
            cancel.request_cancel();
            MIRAGE_CHECK(a11y.activate_element(semantic, cancel).cancelled);
            MIRAGE_CHECK(a11y.set_text(entry, "x", {}, cancel).cancelled);
            MIRAGE_CHECK(desktop.activated.size() == 3);
        }

        // ---- supplementary verification (independent verification pass) ----

        // An empty window id is an invalid argument before any lookup.
        MIRAGE_CHECK(a11y.semantic_snapshot("").error.code == "invalid_argument");

        // Node budget boundary: exactly the tree size succeeds, one below
        // refuses, zero refuses.
        mirage::desktop::SemanticSnapshotLimits exact_nodes;
        exact_nodes.max_nodes = 4;
        const auto exact_snap = a11y.semantic_snapshot(matched_id, exact_nodes, {});
        MIRAGE_CHECK(exact_snap.ok);
        MIRAGE_CHECK(exact_snap.snapshot.nodes.size() == 4);
        mirage::desktop::SemanticSnapshotLimits over_nodes;
        over_nodes.max_nodes = 3;
        MIRAGE_CHECK(a11y.semantic_snapshot(matched_id, over_nodes, {}).error.code ==
                     "snapshot_too_large");
        mirage::desktop::SemanticSnapshotLimits zero_nodes;
        zero_nodes.max_nodes = 0;
        MIRAGE_CHECK(a11y.semantic_snapshot(matched_id, zero_nodes, {}).error.code ==
                     "snapshot_too_large");
        // A refused snapshot leaves the previous registry intact.
        const auto after_refusal = a11y.activate_element(reference);
        MIRAGE_CHECK(after_refusal.ok);
        std::size_t activated_count;
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            activated_count = desktop.activated.size();
            MIRAGE_CHECK(desktop.activated.back() == "/org/fake/root/window/pane/run");
        }
        MIRAGE_CHECK(activated_count == 4);

        // Second window snapshot succeeds and replaces the registry with its
        // own refs (@e1 frame, @e2 button, @e3 entry).
        std::string second_id;
        for (const auto &window : env.window()->list_windows().windows) {
            if (window.title == "SecondWindow") {
                second_id = window.id;
            }
        }
        MIRAGE_CHECK(!second_id.empty());
        const auto second_snap = a11y.semantic_snapshot(second_id);
        MIRAGE_CHECK(second_snap.ok);
        MIRAGE_CHECK(second_snap.snapshot.application == "SecondApp");
        MIRAGE_CHECK(second_snap.snapshot.window_title == "SecondWindow");
        MIRAGE_CHECK(second_snap.snapshot.nodes.size() == 3);
        MIRAGE_CHECK(second_snap.snapshot.nodes[0].role == "frame");
        MIRAGE_CHECK(second_snap.snapshot.nodes[1].ref == "@e2");
        MIRAGE_CHECK(second_snap.snapshot.nodes[1].name == "SecondButton");
        MIRAGE_CHECK(second_snap.snapshot.nodes[2].role == "text");

        // Refs beyond the replaced registry stop resolving (@e4 existed only
        // in the first window's four-node snapshot); a reused ref id resolves
        // to the NEW element, never the old one (@e3 now addresses the note
        // entry, which exposes no action), and no side effect lands.
        ElementTarget fourth_ref;
        fourth_ref.reference.id = "@e4"; // only in the first window's 4-node registry
        MIRAGE_CHECK(a11y.activate_element(fourth_ref).error.code == "not_found");
        MIRAGE_CHECK(a11y.activate_element(reference).error.code == "unsupported_element");
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 4); // unchanged by the stale misses
        }
        const auto live_semantic = a11y.activate_element(semantic);
        MIRAGE_CHECK(live_semantic.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 5);
            MIRAGE_CHECK(desktop.activated.back() == "/org/fake/root/window/pane/run");
        }
        ElementTarget second_ref;
        second_ref.reference.id = "@e2";
        const auto second_ref_hit = a11y.activate_element(second_ref);
        MIRAGE_CHECK(second_ref_hit.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 6);
            MIRAGE_CHECK(desktop.activated.back() == "/org/second/root/window/button");
        }

        // Structural negative paths: odd segment count and unmatched steps.
        ElementTarget odd;
        odd.structural.path = "/application/FakeEditor/frame";
        MIRAGE_CHECK(a11y.activate_element(odd).error.code == "not_found");
        ElementTarget missing_step;
        missing_step.structural.path = "/application/FakeEditor/frame/Nowhere";
        MIRAGE_CHECK(a11y.activate_element(missing_step).error.code == "not_found");

        // Role-only semantic hint: breadth-first over the desktop puts the
        // second window's button (visited at the shallower level) first.
        ElementTarget role_only;
        role_only.semantic.role = "button";
        const auto role_hit = a11y.activate_element(role_only);
        MIRAGE_CHECK(role_hit.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 7);
            MIRAGE_CHECK(desktop.activated.back() == "/org/second/root/window/button");
        }

        // set_text budget boundary: size == limit succeeds, one above is
        // rejected; empty text is valid; truncated multi-byte UTF-8 is
        // invalid. All rejections land nothing.
        mirage::desktop::InputLimits six;
        six.max_text_bytes = 6;
        MIRAGE_CHECK(a11y.set_text(entry, "123456", six, {}).ok);
        MIRAGE_CHECK(a11y.set_text(entry, "1234567", six, {}).error.code == "invalid_argument");
        MIRAGE_CHECK(a11y.set_text(entry, "", six, {}).ok);
        MIRAGE_CHECK(a11y.set_text(entry, "\xc3", {}).error.code == "invalid_argument");
        MIRAGE_CHECK(a11y.set_text(entry, "ok", six, {}).ok);
        ElementTarget note_ref;
        note_ref.reference.id = "@e3";
        MIRAGE_CHECK(a11y.set_text(note_ref, "second", {}, {}).ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.texts.size() == 5); // 1 prior + 3 valid + note
            MIRAGE_CHECK(desktop.texts.at(1).second == "123456");
            MIRAGE_CHECK(desktop.texts.at(2).second.empty());
            MIRAGE_CHECK(desktop.texts.at(3).second == "ok");
            MIRAGE_CHECK(desktop.texts.back().first == "/org/second/root/window/note");
            MIRAGE_CHECK(desktop.texts.back().second == "second");
        }

        // Cancellation precedes even the unsupported-hint rejection.
        CancelToken cancelled_early;
        cancelled_early.request_cancel();
        ElementTarget visual_early;
        visual_early.visual.template_id = "cache:x";
        const auto cancelled_visual = a11y.activate_element(visual_early, cancelled_early);
        MIRAGE_CHECK(cancelled_visual.cancelled);
        MIRAGE_CHECK(!cancelled_visual.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 7); // unchanged
            MIRAGE_CHECK(desktop.texts.size() == 5);     // unchanged
        }

        // Non-opt-in environments keep the accessor null (fail closed).
        LinuxDesktopEnvironment silent;
        MIRAGE_CHECK(silent.accessibility() == nullptr);

        // Close the fixture's X connection only now: closing it destroys the
        // windows it created, and the ASAN build fails the process on the
        // otherwise harmless leak.
        XCloseDisplay(xdisplay);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "AT-SPI setup failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("atspi_backend");
}
