// M2-03 AT-SPI2 accessibility backend tests (DEC-015 topology): the
// fixture owns the org.a11y.atspi.Registry name on a private D-Bus session
// and exports the desktop tree itself with the exact org.a11y.atspi wire
// protocol, so atspi_get_desktop() in the backend under test walks a live,
// fully controlled tree. (The real at-spi2-registryd is bypassed: Ubuntu
// 24.04's build segfaults on Socket.Embed — upstream bug, see atspi_session.hpp.)

#include "../support/atspi_session.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"

#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <X11/Xlib.h>
#include <atspi/atspi.h>
#include <gio/gio.h>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::ElementTarget;
using mirage::platform::linux_backend::LinuxDesktopEnvironment;

/// One accessible in the fake desktop tree.
struct FakeNode {
    std::string path;
    unsigned role = ATSPI_ROLE_UNKNOWN;
    std::string name;
    std::string description;
    int parent = -1; // index into the node vector
    bool has_action = false;
    bool editable = false;
    std::string text;
    bool focused = false;
};

/// A minimal AT-SPI desktop: the fixture itself plays the registry (owns
/// org.a11y.atspi.Registry and serves /org/a11y/atspi/accessible/root) and
/// exports one application subtree beneath it.
class FakeAtspiDesktop {
  public:
    std::vector<FakeNode> nodes;
    // Observations are written from the dispatch thread and read from the
    // test thread; the mutex keeps that handoff clean.
    std::mutex observation_mutex;
    std::vector<std::string> activated;                     // paths of DoAction calls
    std::vector<std::pair<std::string, std::string>> texts; // path -> new contents

    bool start(const std::string &bus_address) {
        GError *error = nullptr;
        connection_ = g_dbus_connection_new_for_address_sync(
            bus_address.c_str(),
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error);
        if (connection_ == nullptr) {
            std::fprintf(stderr, "fixture connect failed: %s\n", error->message);
            g_error_free(error);
            return false;
        }
        if (!register_interfaces()) {
            return false;
        }
        // Own the registry name so atspi_get_desktop() lands on the fixture.
        name_owned_ = g_bus_own_name_on_connection(
            connection_, "org.a11y.atspi.Registry", G_BUS_NAME_OWNER_FLAGS_NONE,
            [](GDBusConnection *, const gchar *, gpointer) {},
            [](GDBusConnection *, const gchar *, gpointer) {}, this, nullptr);
        // Dispatch thread: the backend's sync libatspi calls are answered by
        // this loop. Test-side infrastructure only — production code stays
        // thread-free (RULE-03).
        loop_ = g_main_loop_new(nullptr, FALSE);
        dispatch_thread_ = std::thread([this] { g_main_loop_run(loop_); });
        return true;
    }

    ~FakeAtspiDesktop() {
        if (name_owned_ != 0) {
            g_bus_unown_name(name_owned_);
        }
        if (loop_ != nullptr) {
            g_main_loop_quit(loop_);
        }
        if (dispatch_thread_.joinable()) {
            dispatch_thread_.join();
        }
        if (loop_ != nullptr) {
            g_main_loop_unref(loop_);
        }
        if (connection_ != nullptr) {
            g_object_unref(connection_);
        }
    }

  private:
    static constexpr const char *kAccessibleXml =
        "<node><interface name='org.a11y.atspi.Accessible'>"
        "<method name='GetChildAtIndex'><arg type='i' direction='in'/>"
        "<arg type='(so)' direction='out'/></method>"
        "<method name='GetChildren'><arg type='a(so)' direction='out'/></method>"
        "<method name='GetIndexInParent'><arg type='i' direction='out'/></method>"
        "<method name='GetRelationSet'><arg type='a(ua(so))' direction='out'/></method>"
        "<method name='GetRole'><arg type='u' direction='out'/></method>"
        "<method name='GetRoleName'><arg type='s' direction='out'/></method>"
        "<method name='GetLocalizedRoleName'><arg type='s' direction='out'/></method>"
        "<method name='GetState'><arg type='au' direction='out'/></method>"
        "<method name='GetAttributes'><arg type='a{ss}' direction='out'/></method>"
        "<method name='GetApplication'><arg type='(so)' direction='out'/></method>"
        "<method name='GetInterfaces'><arg type='as' direction='out'/></method>"
        "<property name='Name' type='s' access='read'/>"
        "<property name='Description' type='s' access='read'/>"
        "<property name='Parent' type='(so)' access='read'/>"
        "<property name='ChildCount' type='i' access='read'/>"
        "<property name='Locale' type='s' access='read'/>"
        "<property name='AccessibleId' type='s' access='read'/>"
        "<property name='HelpText' type='s' access='read'/>"
        "</interface></node>";

    static constexpr const char *kActionXml =
        "<node><interface name='org.a11y.atspi.Action'>"
        "<method name='GetNActions'><arg type='i' direction='out'/></method>"
        "<method name='GetName'><arg type='i' direction='in'/>"
        "<arg type='s' direction='out'/></method>"
        "<method name='GetDescription'><arg type='i' direction='in'/>"
        "<arg type='s' direction='out'/></method>"
        "<method name='GetKeyBinding'><arg type='i' direction='in'/>"
        "<arg type='s' direction='out'/></method>"
        "<method name='DoAction'><arg type='i' direction='in'/>"
        "<arg type='b' direction='out'/></method>"
        "</interface></node>";

    static constexpr const char *kEditableTextXml =
        "<node><interface name='org.a11y.atspi.EditableText'>"
        "<method name='SetTextContents'><arg type='s' direction='in'/>"
        "<arg type='b' direction='out'/></method>"
        "</interface></node>";

    struct NodeContext {
        FakeAtspiDesktop *app;
        int index;
    };

    static std::string unique_name(FakeAtspiDesktop &self) {
        return g_dbus_connection_get_unique_name(self.connection_);
    }

    static GVariant *object_ref(FakeAtspiDesktop &self, const std::string &path) {
        return g_variant_new("(so)", unique_name(self).c_str(), path.c_str());
    }

    static GVariant *state_words(const FakeNode &node) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
        // States are packed one bit per state, 32 states per word.
        const unsigned states[] = {ATSPI_STATE_ACTIVE,
                                   ATSPI_STATE_ENABLED,
                                   ATSPI_STATE_SENSITIVE,
                                   ATSPI_STATE_SHOWING,
                                   ATSPI_STATE_VISIBLE,
                                   ATSPI_STATE_FOCUSABLE,
                                   node.focused ? ATSPI_STATE_FOCUSED : ATSPI_STATE_LAST_DEFINED};
        std::vector<guint32> words(ATSPI_STATE_LAST_DEFINED / 32 + 1, 0);
        for (const unsigned state : states) {
            if (state < ATSPI_STATE_LAST_DEFINED) {
                words[state / 32] |= 1u << (state % 32);
            }
        }
        for (const guint32 word : words) {
            g_variant_builder_add_value(&builder, g_variant_new_uint32(word));
        }
        return g_variant_builder_end(&builder);
    }

    static void on_method_call(GDBusConnection * /*connection*/, const gchar * /*sender*/,
                               const gchar * /*object_path*/, const gchar * /*interface_name*/,
                               const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data) {
        auto *context = static_cast<NodeContext *>(user_data);
        FakeAtspiDesktop &self = *context->app;
        FakeNode &node = self.nodes[static_cast<std::size_t>(context->index)];
        const std::string name = method_name;

        if (name == "GetChildAtIndex") {
            gint index = 0;
            g_variant_get(parameters, "(i)", &index);
            const std::vector<int> kids = self.children_of(node);
            GVariant *reply =
                index >= 0 && static_cast<std::size_t>(index) < kids.size()
                    ? object_ref(self, self.nodes[static_cast<std::size_t>(
                                                      kids[static_cast<std::size_t>(index)])]
                                           .path)
                    : g_variant_new("(so)", "org.a11y.atspi.Registry", "/org/a11y/atspi/null");
            g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&reply, 1));
        } else if (name == "GetChildren") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a(so)"));
            for (const int child : self.children_of(node)) {
                g_variant_builder_add_value(
                    &builder, object_ref(self, self.nodes[static_cast<std::size_t>(child)].path));
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(so))", &builder));
        } else if (name == "GetIndexInParent") {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(i)", node.parent));
        } else if (name == "GetRelationSet") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ua(so))"));
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(a(ua(so)))", &builder));
        } else if (name == "GetAttributes") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{ss})", &builder));
        } else if (name == "GetRole") {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", node.role));
        } else if (name == "GetRoleName" || name == "GetLocalizedRoleName") {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", "role"));
        } else if (name == "GetState") {
            GVariant *words = state_words(node);
            g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&words, 1));
        } else if (name == "GetApplication") {
            GVariant *reference = object_ref(self, self.nodes[self.application_index(node)].path);
            g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&reference, 1));
        } else if (name == "GetInterfaces") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.Accessible");
            if (node.has_action) {
                g_variant_builder_add(&builder, "s", "org.a11y.atspi.Action");
            }
            if (node.editable) {
                g_variant_builder_add(&builder, "s", "org.a11y.atspi.EditableText");
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &builder));
        } else if (name == "GetNActions") {
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(i)", node.has_action ? 1 : 0));
        } else if (name == "GetName" || name == "GetDescription" || name == "GetKeyBinding") {
            if (name == "GetName" && node.has_action) {
                g_dbus_method_invocation_return_value(invocation,
                                                      g_variant_new("(s)", node.name.c_str()));
            } else {
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", ""));
            }
        } else if (name == "DoAction") {
            gint index = 0;
            g_variant_get(parameters, "(i)", &index);
            const gboolean ok = node.has_action && index == 0 ? TRUE : FALSE;
            if (ok == TRUE) {
                std::lock_guard<std::mutex> guard(self.observation_mutex);
                self.activated.push_back(node.path);
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", ok));
        } else if (name == "SetTextContents" || name == "SetContents") {
            const gchar *contents = nullptr;
            g_variant_get(parameters, "(&s)", &contents);
            if (node.editable) {
                node.text = contents;
                {
                    std::lock_guard<std::mutex> guard(self.observation_mutex);
                    self.texts.emplace_back(node.path, contents);
                }
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
            } else {
                g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            }
        } else {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.DBus.Error.UnknownMethod", name.c_str());
        }
    }

    static GVariant *on_get_property(GDBusConnection * /*connection*/, const gchar * /*sender*/,
                                     const gchar * /*object_path*/,
                                     const gchar * /*interface_name*/, const gchar *property_name,
                                     GError ** /*error*/, gpointer user_data) {
        auto *context = static_cast<NodeContext *>(user_data);
        FakeAtspiDesktop &self = *context->app;
        FakeNode &node = self.nodes[static_cast<std::size_t>(context->index)];
        const std::string name = property_name;
        if (name == "Name") {
            return g_variant_new_string(node.name.c_str());
        }
        if (name == "Description" || name == "HelpText" || name == "Locale") {
            return g_variant_new_string("");
        }
        if (name == "AccessibleId") {
            return g_variant_new_string(node.path.c_str());
        }
        if (name == "ChildCount") {
            return g_variant_new_int32(static_cast<gint32>(self.children_of(node).size()));
        }
        if (name == "Parent") {
            if (node.parent < 0) {
                // Protocol "no parent" reference: the path must still be a
                // valid object path, hence the null-object path.
                return g_variant_new("(so)", "", "/org/a11y/atspi/null");
            }
            return object_ref(self, self.nodes[static_cast<std::size_t>(node.parent)].path);
        }
        return nullptr;
    }

    std::vector<int> children_of(const FakeNode &node) const {
        std::vector<int> kids;
        const int self_index = index_of(node);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].parent == self_index) {
                kids.push_back(static_cast<int>(i));
            }
        }
        return kids;
    }

    int index_of(const FakeNode &node) const { return static_cast<int>(&node - nodes.data()); }

    /// Nearest APPLICATION ancestor (the node itself when it is one; the
    /// desktop root has none and answers with itself).
    int application_index(const FakeNode &node) const {
        int index = index_of(node);
        while (index > 0 && nodes[static_cast<std::size_t>(index)].role != ATSPI_ROLE_APPLICATION) {
            index = nodes[static_cast<std::size_t>(index)].parent;
        }
        return index;
    }

    bool register_interfaces() {
        GDBusNodeInfo *accessible_info = g_dbus_node_info_new_for_xml(kAccessibleXml, nullptr);
        GDBusNodeInfo *action_info = g_dbus_node_info_new_for_xml(kActionXml, nullptr);
        GDBusNodeInfo *editable_info = g_dbus_node_info_new_for_xml(kEditableTextXml, nullptr);
        GDBusInterfaceVTable vtable{};
        vtable.method_call = on_method_call;
        vtable.get_property = on_get_property;
        vtable.set_property = nullptr;

        bool ok = true;
        contexts_.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            contexts_.push_back(NodeContext{this, static_cast<int>(i)});
            auto *context = &contexts_.back();
            GError *error = nullptr;
            if (g_dbus_connection_register_object(connection_, nodes[i].path.c_str(),
                                                  accessible_info->interfaces[0], &vtable, context,
                                                  nullptr, &error) == 0) {
                std::fprintf(stderr, "register Accessible failed: %s\n", error->message);
                g_error_free(error);
                ok = false;
            }
            if (nodes[i].has_action) {
                if (g_dbus_connection_register_object(connection_, nodes[i].path.c_str(),
                                                      action_info->interfaces[0], &vtable, context,
                                                      nullptr, &error) == 0) {
                    std::fprintf(stderr, "register Action failed: %s\n", error->message);
                    g_error_free(error);
                    ok = false;
                }
            }
            if (nodes[i].editable) {
                if (g_dbus_connection_register_object(connection_, nodes[i].path.c_str(),
                                                      editable_info->interfaces[0], &vtable,
                                                      context, nullptr, &error) == 0) {
                    std::fprintf(stderr, "register EditableText failed: %s\n", error->message);
                    g_error_free(error);
                    ok = false;
                }
            }
        }
        g_dbus_node_info_unref(accessible_info);
        g_dbus_node_info_unref(action_info);
        g_dbus_node_info_unref(editable_info);
        return ok;
    }

    GDBusConnection *connection_ = nullptr;
    guint name_owned_ = 0;
    GMainLoop *loop_ = nullptr;
    std::thread dispatch_thread_;
    std::vector<NodeContext> contexts_;
};

} // namespace

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
