// DEC-118 scoped semantic snapshots (independent verification): drives
// AtspiBackend::semantic_snapshot over the wire-exact fake AT-SPI desktop on a
// private D-Bus (same topology as atspi_backend_test.cpp; no X11 needed — the
// window-id -> title mapping comes from a scripted WindowProvider, which also
// makes stale-id rename scenarios deterministic). Pins:
//   a. max_depth=1 collects the window root only and reports its direct child
//      count as collapsed_children, rendered "(+N children)";
//   b. max_depth=2 collects two levels with frontier counts, and a subtree_ref
//      from that snapshot expands the child subtree with fresh, activatable refs;
//   c. subtree refs that are absent from the current registry fail not_found
//      and malformed refs fail invalid_argument;
//   d. a stale window id (the AT-SPI window renamed underneath) still resolves
//      through the application-segment fallback when it identifies exactly one
//      titled window, and fails not_found with a re-run window.list hint when
//      the application has several windows.

#include "../../platform/linux/src/atspi_backend.hpp"
#include "../support/atspi_session.hpp"
#include "../support/fake_atspi_desktop.hpp"
#include "../support/test.hpp"

#include <mirage/desktop/semantic_snapshot.hpp>

#include <algorithm>
#include <string>
#include <vector>

using mirage::desktop::CancelToken;
using mirage::desktop::SemanticSnapshotLimits;
using mirage::desktop::WindowInfo;
using mirage::desktop::WindowListLimits;
using mirage::desktop::WindowListOutcome;
using mirage::desktop::WindowProvider;
using mirage::desktop::WindowQueryOutcome;
using mirage::desktop::WindowActionOutcome;
using mirage::testing::FakeAtspiDesktop;

namespace {

/// Scripted window surface: the ids/titles are fully controlled by the test,
/// so id staleness (title rename) is reproducible without a display server.
class ScriptedWindows final : public WindowProvider {
  public:
    std::vector<WindowInfo> windows;
    WindowListOutcome list_windows(const WindowListLimits &, const CancelToken &) override {
        WindowListOutcome outcome;
        outcome.ok = true;
        outcome.windows = windows;
        return outcome;
    }
    WindowQueryOutcome front_window(const CancelToken &) override { return {}; }
    WindowActionOutcome activate(const std::string &, const CancelToken &) override {
        return {};
    }
};

WindowInfo window_info(const std::string &app, const std::string &title) {
    WindowInfo info;
    info.id = "atspi:" + app + "/" + title;
    info.title = title;
    info.geometry = {10, 20, 200, 150};
    return info;
}

} // namespace

int main() {
    try {
        mirage::testing::AtspiSession session;

        FakeAtspiDesktop desktop;
        desktop.nodes.push_back({"/org/a11y/atspi/accessible/root", ATSPI_ROLE_DESKTOP_FRAME,
                                 "test-desktop", "", -1, false, false, "", false});
        desktop.nodes.push_back({"/org/fake/app", ATSPI_ROLE_APPLICATION, "FakeApp", "", 0,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/app/win", ATSPI_ROLE_FRAME, "Doc — Viewer", "", 1,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/app/win/sidebar", ATSPI_ROLE_PANEL, "sidebar", "", 2,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/app/win/sidebar/nav1", ATSPI_ROLE_PUSH_BUTTON, "Home",
                                 "", 3, true, false, "", false});
        desktop.nodes.push_back({"/org/fake/app/win/sidebar/nav2", ATSPI_ROLE_PUSH_BUTTON,
                                 "Settings", "", 3, true, false, "", false});
        desktop.nodes.push_back({"/org/fake/app/win/main", ATSPI_ROLE_PANEL, "main", "", 2,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/app/win/main/title", ATSPI_ROLE_LABEL, "Title", "", 6,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/app/win/main/body", ATSPI_ROLE_PANEL, "body", "", 6,
                                 false, false, "", false});
        desktop.nodes.push_back({"/org/fake/app/win/main/body/text", ATSPI_ROLE_ENTRY, "Body", "",
                                 8, false, true, "hello", false});
        if (!desktop.start(session.bus_address())) {
            return 1;
        }

        ScriptedWindows windows;
        windows.windows = {window_info("FakeApp", "Doc — Viewer")};
        auto backend = mirage::platform::linux_backend::AtspiBackend::open(&windows);
        MIRAGE_CHECK(backend != nullptr);
        MIRAGE_CHECK(backend->scoped_snapshots_supported());

        const std::string window_id = "atspi:FakeApp/Doc — Viewer";

        // Sanity: the unscoped snapshot collects the whole window (8 nodes).
        const auto full = backend->semantic_snapshot(window_id, {}, {});
        MIRAGE_CHECK(full.ok);
        MIRAGE_CHECK(full.snapshot.nodes.size() == 8);
        MIRAGE_CHECK(full.snapshot.window_title == "Doc — Viewer");
        MIRAGE_CHECK(full.snapshot.application == "FakeApp");
        for (const auto &node : full.snapshot.nodes)
            MIRAGE_CHECK(node.collapsed_children == 0);

        // a. max_depth=1: the window root alone, with its direct child count.
        SemanticSnapshotLimits depth_one;
        depth_one.max_depth = 1;
        const auto root_only = backend->semantic_snapshot(window_id, depth_one, {});
        MIRAGE_CHECK(root_only.ok);
        MIRAGE_CHECK(root_only.snapshot.nodes.size() == 1);
        MIRAGE_CHECK(root_only.snapshot.nodes[0].ref == "@e1");
        MIRAGE_CHECK(root_only.snapshot.nodes[0].role == "frame");
        MIRAGE_CHECK(root_only.snapshot.nodes[0].collapsed_children == 2);
        // The registry only carries the frontier ref now.
        mirage::desktop::ElementTarget beyond;
        beyond.reference.id = "@e2";
        MIRAGE_CHECK(backend->activate_element(beyond, {}).error.code == "not_found");

        // b. max_depth=2: two levels, frontier nodes carry child counts.
        SemanticSnapshotLimits depth_two;
        depth_two.max_depth = 2;
        const auto two_levels = backend->semantic_snapshot(window_id, depth_two, {});
        MIRAGE_CHECK(two_levels.ok);
        MIRAGE_CHECK(two_levels.snapshot.nodes.size() == 3);
        MIRAGE_CHECK(two_levels.snapshot.nodes[0].name == "Doc — Viewer" &&
                     two_levels.snapshot.nodes[0].collapsed_children == 0);
        MIRAGE_CHECK(two_levels.snapshot.nodes[1].name == "sidebar" &&
                     two_levels.snapshot.nodes[1].collapsed_children == 2);
        MIRAGE_CHECK(two_levels.snapshot.nodes[2].name == "main" &&
                     two_levels.snapshot.nodes[2].collapsed_children == 2);

        // Expand the sidebar subtree from the scoped snapshot's registry.
        SemanticSnapshotLimits subtree;
        subtree.subtree_ref = two_levels.snapshot.nodes[1].ref; // "@e2" (sidebar)
        const auto expanded = backend->semantic_snapshot(window_id, subtree, {});
        MIRAGE_CHECK(expanded.ok);
        MIRAGE_CHECK(expanded.snapshot.nodes.size() == 3);
        MIRAGE_CHECK(expanded.snapshot.nodes[0].name == "sidebar" &&
                     expanded.snapshot.nodes[0].parent == mirage::desktop::kNoParent);
        MIRAGE_CHECK(expanded.snapshot.nodes[1].name == "Home");
        MIRAGE_CHECK(expanded.snapshot.nodes[2].name == "Settings");
        MIRAGE_CHECK(expanded.scope != two_levels.scope); // new generation
        // The fresh refs resolve and act (scope generation updated, no staleness).
        mirage::desktop::ElementTarget scoped_ref;
        scoped_ref.reference.id = "@e2"; // Home in the subtree registry
        scoped_ref.snapshot_scope = expanded.scope;
        const auto activated = backend->activate_element(scoped_ref, {});
        MIRAGE_CHECK(activated.ok);
        {
            std::lock_guard<std::mutex> guard(desktop.observation_mutex);
            MIRAGE_CHECK(desktop.activated.size() == 1);
            MIRAGE_CHECK(desktop.activated.back() == "/org/fake/app/win/sidebar/nav1");
        }
        // A pre-expansion scope on the same registry is stale.
        mirage::desktop::ElementTarget stale_scope;
        stale_scope.reference.id = "@e2";
        stale_scope.snapshot_scope = two_levels.scope;
        MIRAGE_CHECK(backend->activate_element(stale_scope, {}).error.code == "stale_observation");

        // c. Absent and malformed subtree refs.
        SemanticSnapshotLimits missing_ref;
        missing_ref.subtree_ref = "@e9"; // outside the current 3-node registry
        const auto missing = backend->semantic_snapshot(window_id, missing_ref, {});
        MIRAGE_CHECK(!missing.ok && missing.error.code == "not_found");
        SemanticSnapshotLimits malformed;
        malformed.subtree_ref = "window";
        MIRAGE_CHECK(backend->semantic_snapshot(window_id, malformed, {}).error.code ==
                     "invalid_argument");
        // A ref from the replaced whole-window registry no longer addresses a
        // live subtree root once the registry was replaced by the subtree one.
        SemanticSnapshotLimits whole_ref;
        whole_ref.subtree_ref = full.snapshot.nodes.back().ref; // "@e8": gone
        MIRAGE_CHECK(backend->semantic_snapshot(window_id, whole_ref, {}).error.code ==
                     "not_found");

        // d. Window rename: the AT-SPI window renames (page navigation) and the
        // provider reports the new id; the stale id still resolves through the
        // application-segment fallback.
        desktop.nodes[2].name = "Article — Viewer"; // the window frame node
        windows.windows = {window_info("FakeApp", "Article — Viewer")};
        const auto renamed = backend->semantic_snapshot(window_id, {}, {});
        MIRAGE_CHECK(renamed.ok);
        MIRAGE_CHECK(renamed.snapshot.window_title == "Article — Viewer");
        MIRAGE_CHECK(renamed.snapshot.nodes.size() == 8); // same tree, fresh refs

        // Same application with two titled windows: the stale id is ambiguous
        // by application and must fail with the re-run hint.
        windows.windows = {window_info("FakeApp", "Article — Viewer"),
                           window_info("FakeApp", "Other")};
        const auto ambiguous_app = backend->semantic_snapshot(window_id, {}, {});
        MIRAGE_CHECK(!ambiguous_app.ok && ambiguous_app.error.code == "not_found");
        MIRAGE_CHECK(ambiguous_app.error.message.find("re-run desktop.window.list") !=
                     std::string::npos);

        // One titled window plus one untitled window of the same application:
        // the titled one identifies the application uniquely.
        windows.windows = {window_info("FakeApp", "Article — Viewer"), window_info("FakeApp", "")};
        const auto untitled_peer = backend->semantic_snapshot(window_id, {}, {});
        MIRAGE_CHECK(untitled_peer.ok &&
                     untitled_peer.snapshot.window_title == "Article — Viewer");

        // A different application with the same title does not block the
        // fallback, but two windows sharing one title keep the ambiguity guard.
        windows.windows = {window_info("FakeApp", "Article — Viewer"),
                           window_info("SecondApp", "Article — Viewer")};
        const auto cross_app = backend->semantic_snapshot(window_id, {}, {});
        MIRAGE_CHECK(!cross_app.ok && cross_app.error.code == "unsupported_window");
        const auto new_id = backend->semantic_snapshot("atspi:FakeApp/Article — Viewer", {}, {});
        MIRAGE_CHECK(!new_id.ok && new_id.error.code == "unsupported_window");

        // A foreign id with no application segment and no exact match stays
        // not_found, with the re-run hint.
        windows.windows = {window_info("FakeApp", "Article — Viewer")};
        const auto foreign = backend->semantic_snapshot("missing-window-id", {}, {});
        MIRAGE_CHECK(!foreign.ok && foreign.error.code == "not_found");
        MIRAGE_CHECK(foreign.error.message.find("re-run desktop.window.list") != std::string::npos);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "AT-SPI setup failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("atspi_scoped_snapshot");
}
