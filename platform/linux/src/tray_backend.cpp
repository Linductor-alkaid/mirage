#include "tray_backend.hpp"

#include <gio/gio.h>

#include <chrono>
#include <string>
#include <utility>

namespace mirage::platform::linux_backend {
namespace {

using mirage::desktop::TrayAction;
using mirage::desktop::TrayCarrierContext;
using mirage::desktop::TrayState;

/// Bounded stop latency: the loop's timeout source polls the stop probe at
/// this cadence (DEC-030 decision 6).
constexpr guint kStopPollMs = 100;

/// Well-known names and object paths of the indicator protocol.
constexpr const char *kWatcherName = "org.kde.StatusNotifierItemWatcher";
constexpr const char *kItemPath = "/org/mirage/tray";
constexpr const char *kMenuPath = "/org/mirage/tray/menu";
constexpr const char *kItemId = "mirage-tray";

/// Menu item ids of the fixed layout (com.canonical.dbusmenu item ids).
enum : guint {
    kItemStatus = 1, ///< header entry: disabled, shows the status line
    kItemPause = 2,
    kItemResume = 3,
    kItemSeparator1 = 4,
    kItemOpenShell = 5,
    kItemSeparator2 = 6,
    kItemQuit = 99,
};

/// The org.kde.StatusNotifierItem interface (the subset the tray exposes).
constexpr const char *kItemXml = "<node>"
                                 "  <interface name='org.kde.StatusNotifierItem'>"
                                 "    <property name='Category' type='s' access='read'/>"
                                 "    <property name='Id' type='s' access='read'/>"
                                 "    <property name='Title' type='s' access='read'/>"
                                 "    <property name='Status' type='s' access='read'/>"
                                 "    <property name='IconName' type='s' access='read'/>"
                                 "    <property name='ToolTip' type='(sa(iiidd)ss)' access='read'/>"
                                 "    <property name='Menu' type='o' access='read'/>"
                                 "  </interface>"
                                 "</node>";

/// The com.canonical.dbusmenu interface (the subset the menu uses).
constexpr const char *kMenuXml = "<node>"
                                 "  <interface name='com.canonical.dbusmenu'>"
                                 "    <method name='AboutToShow'>"
                                 "      <arg name='parentId' type='u' direction='in'/>"
                                 "      <arg name='needUpdate' type='b' direction='out'/>"
                                 "    </method>"
                                 "    <method name='GetLayout'>"
                                 "      <arg name='parentId' type='i' direction='in'/>"
                                 "      <arg name='recursionDepth' type='i' direction='in'/>"
                                 "      <arg name='propertyNames' type='as' direction='in'/>"
                                 "      <arg name='revision' type='u' direction='out'/>"
                                 "      <arg name='parent' type='i' direction='out'/>"
                                 "      <arg name='layout' type='a(ia{sv}av)' direction='out'/>"
                                 "    </method>"
                                 "    <method name='GetGroupProperties'>"
                                 "      <arg name='ids' type='ai' direction='in'/>"
                                 "      <arg name='propertyNames' type='as' direction='in'/>"
                                 "      <arg name='properties' type='a(ia{sv})' direction='out'/>"
                                 "    </method>"
                                 "    <method name='GetProperty'>"
                                 "      <arg name='id' type='i' direction='in'/>"
                                 "      <arg name='property' type='s' direction='in'/>"
                                 "      <arg name='value' type='v' direction='out'/>"
                                 "    </method>"
                                 "    <method name='Event'>"
                                 "      <arg name='id' type='i' direction='in'/>"
                                 "      <arg name='eventId' type='s' direction='in'/>"
                                 "      <arg name='data' type='v' direction='in'/>"
                                 "      <arg name='timestamp' type='u' direction='in'/>"
                                 "    </method>"
                                 "    <signal name='ItemsPropertiesUpdated'>"
                                 "      <arg name='updatedProps' type='a(ia{sv})' direction='out'/>"
                                 "      <arg name='removedProps' type='as' direction='out'/>"
                                 "    </signal>"
                                 "    <signal name='LayoutUpdated'>"
                                 "      <arg name='revision' type='u' direction='out'/>"
                                 "      <arg name='parent' type='i' direction='out'/>"
                                 "    </signal>"
                                 "  </interface>"
                                 "</node>";

/// One menu entry of the fixed layout.
struct MenuEntry {
    guint id = 0;
    const char *label = nullptr; ///< nullptr renders a separator entry
};

constexpr MenuEntry kMenuLayout[] = {
    {kItemStatus, nullptr},     {kItemPause, "暂停任务"},        {kItemResume, "恢复任务"},
    {kItemSeparator1, nullptr}, {kItemOpenShell, "打开 Mirage"}, {kItemSeparator2, nullptr},
    {kItemQuit, "退出"},
};

GVariant *menu_props_for(const TrayState &state, guint id) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    if (id == kItemSeparator1 || id == kItemSeparator2) {
        g_variant_builder_add(&builder, "{sv}", "type", g_variant_new_string("separator"));
        g_variant_builder_add(&builder, "{sv}", "enabled", g_variant_new_boolean(FALSE));
        return g_variant_builder_end(&builder);
    }
    if (id == kItemStatus) {
        g_variant_builder_add(&builder, "{sv}", "label",
                              g_variant_new_string(state.status.c_str()));
        g_variant_builder_add(&builder, "{sv}", "enabled", g_variant_new_boolean(FALSE));
        return g_variant_builder_end(&builder);
    }
    const char *label = "";
    gboolean enabled = FALSE;
    switch (id) {
    case kItemPause:
        label = "暂停任务";
        enabled = state.can_pause ? TRUE : FALSE;
        break;
    case kItemResume:
        label = "恢复任务";
        enabled = state.can_resume ? TRUE : FALSE;
        break;
    case kItemOpenShell:
        label = "打开 Mirage";
        enabled = state.can_open_shell ? TRUE : FALSE;
        break;
    case kItemQuit:
        label = "退出";
        enabled = TRUE;
        break;
    default:
        break;
    }
    g_variant_builder_add(&builder, "{sv}", "label", g_variant_new_string(label));
    g_variant_builder_add(&builder, "{sv}", "enabled", g_variant_new_boolean(enabled));
    return g_variant_builder_end(&builder);
}

} // namespace

struct GioTrayCarrier::Surface {
    GDBusConnection *connection = nullptr; ///< owned reference (session bus)
    GMainLoop *loop = nullptr;
    GMainContext *context = nullptr; ///< thread-default context of run()
    guint item_registration = 0;
    guint menu_registration = 0;
    guint stop_source = 0;
    guint watcher_watch = 0;
    guint revision = 1;
    TrayCarrierContext carrier;

    /// True once the run loop should converge on a stop (set by the stop
    /// probe or by the watcher vanishing).
    bool stopping = false;
    std::string stop_reason;
};

namespace {

/// The item's read-only properties, answered fresh from the carrier's
/// context (pump-thread pull — the loop thread owns the surface).
TrayState current_state(GioTrayCarrier::Surface &surface) {
    if (surface.carrier.load_state) {
        return surface.carrier.load_state();
    }
    return TrayState{};
}

GVariant *get_property(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                       const gchar *property, GError **error, gpointer user_data) {
    auto *surface = static_cast<GioTrayCarrier::Surface *>(user_data);
    const TrayState state = current_state(*surface);
    if (g_strcmp0(property, "Category") == 0) {
        return g_variant_new_string("ApplicationStatus");
    }
    if (g_strcmp0(property, "Id") == 0) {
        return g_variant_new_string(kItemId);
    }
    if (g_strcmp0(property, "Title") == 0) {
        return g_variant_new_string("Mirage");
    }
    if (g_strcmp0(property, "Status") == 0) {
        return g_variant_new_string("Active");
    }
    if (g_strcmp0(property, "IconName") == 0) {
        // The icon asset ships with packaging (M5-11); development sessions
        // show the indicator host's placeholder (declared in DEC-030).
        return g_variant_new_string(kItemId);
    }
    if (g_strcmp0(property, "ToolTip") == 0) {
        return g_variant_new_parsed("('%s', @a(iiidd) [], 'Mirage', %s)", kItemId,
                                    state.status.c_str());
    }
    if (g_strcmp0(property, "Menu") == 0) {
        return g_variant_new_object_path(kMenuPath);
    }
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "unknown property '%s'",
                property);
    return nullptr;
}

/// Menu item properties by id, answered fresh from the carrier's context.
GVariant *menu_properties_variant(GioTrayCarrier::Surface &surface, guint id) {
    return menu_props_for(current_state(surface), id);
}

void menu_method_call(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                      const gchar *method, GVariant *parameters, GDBusMethodInvocation *invocation,
                      gpointer user_data) {
    auto *surface = static_cast<GioTrayCarrier::Surface *>(user_data);
    if (g_strcmp0(method, "GetLayout") == 0) {
        guint depth = 0;
        g_variant_get(parameters, "(ii as)", nullptr, &depth, nullptr);
        GVariantBuilder layout;
        g_variant_builder_init(&layout, G_VARIANT_TYPE("a(ia{sv}av)"));
        for (const MenuEntry &entry : kMenuLayout) {
            GVariantBuilder props;
            g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
            GVariantBuilder children;
            g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
            if (depth != 0) {
                GVariant *properties = menu_properties_variant(*surface, entry.id);
                GVariantIter iterator;
                const gchar *key = nullptr;
                GVariant *value = nullptr;
                g_variant_iter_init(&iterator, properties);
                while (g_variant_iter_loop(&iterator, "{sv}", &key, &value)) {
                    g_variant_builder_add(&props, "{sv}", key, value);
                }
                g_variant_unref(properties);
            }
            g_variant_builder_add(&layout, "(ia{sv}av)", static_cast<gint>(entry.id),
                                  g_variant_builder_end(&props), g_variant_builder_end(&children));
        }
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u i a(ia{sv}av))", surface->revision,
                                                            0, g_variant_builder_end(&layout)));
        return;
    }
    if (g_strcmp0(method, "GetGroupProperties") == 0) {
        GVariantBuilder properties;
        g_variant_builder_init(&properties, G_VARIANT_TYPE("a(ia{sv})"));
        GVariantIter iterator;
        gint id = 0;
        g_variant_get(parameters, "(ai as)", &iterator, nullptr);
        while (g_variant_iter_loop(&iterator, "i", &id)) {
            GVariant *props = menu_properties_variant(*surface, static_cast<guint>(id));
            g_variant_builder_add(&properties, "(ia{sv})", id, props);
            g_variant_unref(props);
        }
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(a(ia{sv}))", g_variant_builder_end(&properties)));
        return;
    }
    if (g_strcmp0(method, "GetProperty") == 0) {
        const gchar *name = nullptr;
        gint id = 0;
        g_variant_get(parameters, "(is)", &id, &name);
        GVariant *props = menu_properties_variant(*surface, static_cast<guint>(id));
        GVariant *value = g_variant_lookup_value(props, name, nullptr);
        g_variant_unref(props);
        if (value == nullptr) {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_UNKNOWN_PROPERTY,
                                                  "unknown menu property '%s'", name);
            return;
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
        return;
    }
    if (g_strcmp0(method, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        return;
    }
    if (g_strcmp0(method, "Event") == 0) {
        gint id = 0;
        const gchar *event_id = nullptr;
        g_variant_get(parameters, "(isvu)", &id, &event_id, nullptr, nullptr);
        if (g_strcmp0(event_id, "clicked") == 0) {
            const TrayAction action = id == static_cast<gint>(kItemPause)    ? TrayAction::Pause
                                      : id == static_cast<gint>(kItemResume) ? TrayAction::Resume
                                      : id == static_cast<gint>(kItemOpenShell)
                                          ? TrayAction::OpenShell
                                      : id == static_cast<gint>(kItemQuit) ? TrayAction::Quit
                                                                           : TrayAction::Quit;
            if (id == static_cast<gint>(kItemPause) || id == static_cast<gint>(kItemResume) ||
                id == static_cast<gint>(kItemOpenShell) || id == static_cast<gint>(kItemQuit)) {
                if (surface->carrier.on_action) {
                    surface->carrier.on_action(action);
                }
            }
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown dbusmenu method '%s'", method);
}

/// Notifies the indicator host that item properties changed: the host
/// re-pulls through the vtable (PropertiesChanged for the item's tooltip,
/// ItemsPropertiesUpdated for the menu's labels and enabled flags).
void notify_state_changed(GioTrayCarrier::Surface &surface) {
    GError *error = nullptr;
    g_dbus_connection_emit_signal(
        surface.connection, nullptr, kItemPath, "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new_parsed("('org.kde.StatusNotifierItem', @a{sv} {}, @as [])"), &error);
    if (error != nullptr) {
        g_error_free(error);
    }
    GVariantBuilder updated;
    g_variant_builder_init(&updated, G_VARIANT_TYPE("a(ia{sv})"));
    const guint ids[] = {kItemStatus, kItemPause, kItemResume, kItemOpenShell};
    for (const guint id : ids) {
        GVariant *props = menu_properties_variant(surface, id);
        g_variant_builder_add(&updated, "(ia{sv})", static_cast<gint>(id), props);
        g_variant_unref(props);
    }
    GVariantBuilder removed;
    g_variant_builder_init(&removed, G_VARIANT_TYPE("as"));
    error = nullptr;
    g_dbus_connection_emit_signal(surface.connection, nullptr, kMenuPath, "com.canonical.dbusmenu",
                                  "ItemsPropertiesUpdated",
                                  g_variant_new("(a(ia{sv})as)", g_variant_builder_end(&updated),
                                                g_variant_builder_end(&removed)),
                                  &error);
    if (error != nullptr) {
        g_error_free(error);
    }
}

gboolean refresh_on_loop(gpointer user_data) {
    auto *surface = static_cast<GioTrayCarrier::Surface *>(user_data);
    notify_state_changed(*surface);
    return G_SOURCE_REMOVE;
}

void watcher_vanished(GDBusConnection *, const gchar *, gpointer user_data) {
    auto *surface = static_cast<GioTrayCarrier::Surface *>(user_data);
    // The indicator host is gone: the surface cannot be presented any more
    // (capability honesty — end the loop with the diagnostic, DEC-030).
    surface->stopping = true;
    surface->stop_reason = "StatusNotifierWatcher left the session bus";
    if (surface->loop != nullptr) {
        g_main_loop_quit(surface->loop);
    }
}

} // namespace

GioTrayCarrier::~GioTrayCarrier() = default;

std::unique_ptr<GioTrayCarrier> GioTrayCarrier::open() {
    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (connection == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return nullptr; // no session bus: no indicator carrier
    }
    // A StatusNotifierWatcher host is what turns an exported item into a
    // visible indicator (capability honesty, DEC-030 decision 4).
    GVariant *has_owner = g_dbus_connection_call_sync(
        connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "NameHasOwner", g_variant_new("(s)", kWatcherName), G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
    if (has_owner == nullptr) {
        g_object_unref(connection);
        if (error != nullptr) {
            g_error_free(error);
        }
        return nullptr;
    }
    gboolean owned = FALSE;
    g_variant_get(has_owner, "(b)", &owned);
    g_variant_unref(has_owner);
    if (!owned) {
        g_object_unref(connection);
        return nullptr;
    }
    auto carrier = std::unique_ptr<GioTrayCarrier>(new GioTrayCarrier());
    carrier->surface_ = std::make_unique<Surface>();
    carrier->surface_->connection = connection;
    return carrier;
}

namespace {

// --- vtable wiring --------------------------------------------------------

const GDBusInterfaceVTable kItemVtable = {
    nullptr,
    get_property,
    nullptr,
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};
const GDBusInterfaceVTable kMenuVtable = {
    menu_method_call,
    nullptr,
    nullptr,
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

} // namespace

mirage::desktop::TrayCarrier::RunReport
GioTrayCarrier::run(const TrayCarrierContext &context,
                    const std::function<bool()> &stop_requested) {
    RunReport report;
    Surface &surface = *surface_;
    surface.carrier = context;

    surface.context = g_main_context_new();
    g_main_context_push_thread_default(surface.context);
    surface.loop = g_main_loop_new(surface.context, FALSE);

    GDBusNodeInfo *item_info = g_dbus_node_info_new_for_xml(kItemXml, nullptr);
    GDBusNodeInfo *menu_info = g_dbus_node_info_new_for_xml(kMenuXml, nullptr);
    surface.item_registration =
        g_dbus_connection_register_object(surface.connection, kItemPath, item_info->interfaces[0],
                                          &kItemVtable, &surface, nullptr, nullptr);
    surface.menu_registration =
        g_dbus_connection_register_object(surface.connection, kMenuPath, menu_info->interfaces[0],
                                          &kMenuVtable, &surface, nullptr, nullptr);
    if (surface.item_registration == 0 || surface.menu_registration == 0) {
        report.diagnostic = "tray indicator object export failed";
    }

    // Register with the watcher so the indicator materializes.
    if (report.diagnostic.empty()) {
        GError *error = nullptr;
        GVariant *registered = g_dbus_connection_call_sync(
            surface.connection, kWatcherName, "/StatusNotifierWatcher", kWatcherName,
            "RegisterStatusNotifierItem", g_variant_new("(s)", kItemPath), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
        if (registered == nullptr) {
            report.diagnostic = std::string("indicator registration refused: ") +
                                (error != nullptr ? error->message : "unknown error");
            if (error != nullptr) {
                g_error_free(error);
            }
        } else {
            g_variant_unref(registered);
        }
    }

    struct StopContext {
        GioTrayCarrier::Surface *surface;
        const std::function<bool()> *stop_requested;
    };
    StopContext stop_context{&surface, &stop_requested};
    if (report.diagnostic.empty()) {
        // Bounded stop: the timeout source polls the owner's stop probe on
        // the loop (DEC-030 decision 6); wakeup() invokes a refresh so the
        // newest state is pushed within one slice too.
        surface.stop_source = g_timeout_add(
            kStopPollMs,
            [](gpointer user_data) -> gint {
                auto *stop = static_cast<StopContext *>(user_data);
                if ((*stop->stop_requested)()) {
                    stop->surface->stopping = true;
                    g_main_loop_quit(stop->surface->loop);
                    return G_SOURCE_REMOVE;
                }
                return G_SOURCE_CONTINUE;
            },
            &stop_context);
        surface.watcher_watch = g_bus_watch_name_on_connection(
            surface.connection, kWatcherName, G_BUS_NAME_WATCHER_FLAGS_NONE, nullptr,
            watcher_vanished, &surface, nullptr);

        wakeup_target_.store(&surface, std::memory_order_release);
        report.clean = true;
        g_main_loop_run(surface.loop);
        wakeup_target_.store(nullptr, std::memory_order_release);

        if (surface.stopping && !surface.stop_reason.empty()) {
            report.clean = false;
            report.diagnostic = surface.stop_reason;
        }
    }

    if (surface.stop_source != 0) {
        g_source_remove(surface.stop_source);
    }
    if (surface.watcher_watch != 0) {
        g_bus_unwatch_name(surface.watcher_watch);
    }
    if (surface.menu_registration != 0) {
        g_dbus_connection_unregister_object(surface.connection, surface.menu_registration);
    }
    if (surface.item_registration != 0) {
        g_dbus_connection_unregister_object(surface.connection, surface.item_registration);
    }
    g_dbus_node_info_unref(item_info);
    g_dbus_node_info_unref(menu_info);
    g_main_loop_unref(surface.loop);
    g_main_context_pop_thread_default(surface.context);
    g_main_context_unref(surface.context);
    surface.loop = nullptr;
    surface.context = nullptr;
    return report;
}

void GioTrayCarrier::wakeup() {
    auto *surface = static_cast<Surface *>(wakeup_target_.load(std::memory_order_acquire));
    if (surface == nullptr || surface->context == nullptr) {
        return;
    }
    g_main_context_invoke_full(surface->context, G_PRIORITY_DEFAULT, refresh_on_loop, surface,
                               nullptr);
}

std::unique_ptr<mirage::desktop::TrayCarrier> open_tray_carrier() { return GioTrayCarrier::open(); }

} // namespace mirage::platform::linux_backend
