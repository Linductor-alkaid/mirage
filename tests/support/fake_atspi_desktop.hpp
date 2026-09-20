#pragma once

// Wire-exact AT-SPI2 test fixture (M2-03, DEC-015), shared by the AT-SPI2
// backend tests and the M2-06 observation end-to-end test. The fixture owns
// the org.a11y.atspi.Registry name on a private D-Bus session and exports
// the desktop tree itself with the exact org.a11y.atspi wire protocol, so
// atspi_get_desktop() in the backend under test walks a live, fully
// controlled tree. (The real at-spi2-registryd is bypassed: Ubuntu 24.04's
// build segfaults on Socket.Embed — upstream bug.)
//
// The GDBus dispatch thread is test-side infrastructure only; production
// code stays thread-free (RULE-03 exemption boundary, same shape as the
// original atspi_backend_test.cpp topology).

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <atspi/atspi.h>
#include <gio/gio.h>

namespace mirage::testing {

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
/// exports one or more application subtrees beneath it.
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

} // namespace mirage::testing
