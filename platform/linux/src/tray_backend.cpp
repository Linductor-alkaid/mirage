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
constexpr const char *kWatcherName = "org.kde.StatusNotifierWatcher";
/// Per the SNI spec the watcher's interface name IS its bus name (no
/// "ItemWatcher" interface exists); c90e71c briefly had this backwards by
/// promoting the smoke fake's own mistake into the constant.
constexpr const char *kWatcherInterface = "org.kde.StatusNotifierWatcher";
/// The conventional item path the SNI hosts introspect under the
/// registered bus name (the registration parameter is the bus name, not
/// the path — verification round 1, defect 2).
constexpr const char *kItemPath = "/org/mirage/tray";
constexpr const char *kItemAltPath = "/StatusNotifierItem";
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
    /// Dedicated message-bus connection for this carrier (owned). Created
    /// inside run() AFTER the loop's thread-default context is pushed, so
    /// the connection's dispatch source lands on the loop that serves the
    /// exported objects — a connection created earlier (or the shared
    /// g_bus_get_sync singleton) would dispatch elsewhere and the exports
    /// would never answer (verification round 1 smoke finding).
    GDBusConnection *connection = nullptr;
    std::string session_address; ///< resolved by open()
    GMainLoop *loop = nullptr;
    GMainContext *context = nullptr; ///< thread-default context of run()
    guint item_registration = 0;
    guint alt_item_registration = 0;
    guint menu_registration = 0;
    GSource *stop_source = nullptr;
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
        g_variant_get(parameters, "(iias)", nullptr, &depth, nullptr);
        GVariantBuilder layout;
        g_variant_builder_init(&layout, G_VARIANT_TYPE("a(ia{sv}av)"));
        for (const MenuEntry &entry : kMenuLayout) {
            // Tuple construction (never "{sv}"/"a..." varargs slots): the
            // format-string varargs language would read the pre-built
            // GVariant* as a key pointer / varargs array and overrun
            // (verification round 1, defects 3 and 4).
            GVariant *props = depth != 0 ? menu_properties_variant(*surface, entry.id)
                                         : g_variant_new("a{sv}", nullptr);
            GVariant *children = g_variant_new_array(G_VARIANT_TYPE("v"), nullptr, 0);
            GVariant *values[3] = {g_variant_new_int32(entry.id), props, children};
            g_variant_builder_add_value(&layout, g_variant_new_tuple(values, 3));
        }
        // Canonical libdbusmenu signature: (u revision, a(ia{sv}av) layout)
        // — no parent member (libdbusmenu's exported introspection XML).
        GVariantBuilder reply;
        g_variant_builder_init(&reply, G_VARIANT_TYPE("(ua(ia{sv}av))"));
        g_variant_builder_add(&reply, "u", surface->revision);
        g_variant_builder_add_value(&reply, g_variant_builder_end(&layout));
        g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(&reply));
        return;
    }
    if (g_strcmp0(method, "GetGroupProperties") == 0) {
        GVariantBuilder properties;
        g_variant_builder_init(&properties, G_VARIANT_TYPE("a(ia{sv})"));
        GVariantIter iterator;
        gint id = 0;
        g_variant_get(parameters, "(aias)", &iterator, nullptr);
        while (g_variant_iter_loop(&iterator, "i", &id)) {
            GVariant *props = menu_properties_variant(*surface, static_cast<guint>(id));
            GVariant *values[2] = {g_variant_new_int32(id), props};
            g_variant_builder_add_value(&properties, g_variant_new_tuple(values, 2));
        }
        GVariantBuilder reply;
        g_variant_builder_init(&reply, G_VARIANT_TYPE("(a(ia{sv}))"));
        g_variant_builder_add_value(&reply, g_variant_builder_end(&properties));
        g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(&reply));
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
        GVariant *values[2] = {g_variant_new_int32(id), props};
        g_variant_builder_add_value(&updated, g_variant_new_tuple(values, 2));
    }
    GVariantBuilder removed;
    g_variant_builder_init(&removed, G_VARIANT_TYPE("as"));
    error = nullptr;
    GVariantBuilder signal_parameters;
    g_variant_builder_init(&signal_parameters, G_VARIANT_TYPE("(a(ia{sv})as)"));
    g_variant_builder_add_value(&signal_parameters, g_variant_builder_end(&updated));
    g_variant_builder_add_value(&signal_parameters, g_variant_builder_end(&removed));
    g_dbus_connection_emit_signal(surface.connection, nullptr, kMenuPath, "com.canonical.dbusmenu",
                                  "ItemsPropertiesUpdated",
                                  g_variant_builder_end(&signal_parameters), &error);
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
    gchar *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (address == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return nullptr; // no session bus address: no indicator carrier
    }
    // The probe uses a DEDICATED connection, never the g_bus_get_sync
    // singleton: the singleton caches the first session bus for the whole
    // process, which poisons later capability probes when the bus address
    // changes between scenarios (verification round 1 follow-up).
    GDBusConnection *probe = g_dbus_connection_new_for_address_sync(
        address,
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    if (probe == nullptr) {
        g_free(address);
        if (error != nullptr) {
            g_error_free(error);
        }
        return nullptr; // no session bus: no indicator carrier
    }
    // A StatusNotifierWatcher host is what turns an exported item into a
    // visible indicator (capability honesty, DEC-030 decision 4).
    GVariant *has_owner = g_dbus_connection_call_sync(
        probe, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "NameHasOwner", g_variant_new("(s)", kWatcherName), G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
    if (has_owner == nullptr) {
        g_object_unref(probe);
        g_free(address);
        if (error != nullptr) {
            g_error_free(error);
        }
        return nullptr;
    }
    gboolean owned = FALSE;
    g_variant_get(has_owner, "(b)", &owned);
    g_variant_unref(has_owner);
    g_object_unref(probe);
    if (!owned) {
        g_free(address);
        return nullptr;
    }
    auto carrier = std::unique_ptr<GioTrayCarrier>(new GioTrayCarrier());
    carrier->surface_ = std::make_unique<Surface>();
    carrier->surface_->session_address = address;
    g_free(address);
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

    // The carrier's OWN message-bus connection, created with this loop's
    // thread-default context current: its dispatch source then serves the
    // exported objects from this loop. (A shared g_bus_get_sync singleton
    // would dispatch on whatever context it was created under — the
    // exports would never answer.)
    GError *connection_error = nullptr;
    surface.connection = g_dbus_connection_new_for_address_sync(
        surface.session_address.c_str(),
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &connection_error);
    if (surface.connection == nullptr) {
        report.diagnostic = std::string("tray session connection failed: ") +
                            (connection_error != nullptr ? connection_error->message : "?");
        if (connection_error != nullptr) {
            g_error_free(connection_error);
        }
    }

    GDBusNodeInfo *item_info = nullptr;
    GDBusNodeInfo *menu_info = nullptr;
    if (report.diagnostic.empty()) {
        item_info = g_dbus_node_info_new_for_xml(kItemXml, nullptr);
        menu_info = g_dbus_node_info_new_for_xml(kMenuXml, nullptr);
    }
    surface.item_registration =
        g_dbus_connection_register_object(surface.connection, kItemPath, item_info->interfaces[0],
                                          &kItemVtable, &surface, nullptr, nullptr);
    surface.menu_registration =
        g_dbus_connection_register_object(surface.connection, kMenuPath, menu_info->interfaces[0],
                                          &kMenuVtable, &surface, nullptr, nullptr);
    // The conventional path hosts may probe first (same vtable, same item).
    surface.alt_item_registration = g_dbus_connection_register_object(
        surface.connection, kItemAltPath, item_info->interfaces[0], &kItemVtable, &surface, nullptr,
        nullptr);
    if (surface.item_registration == 0 || surface.menu_registration == 0 ||
        surface.alt_item_registration == 0) {
        report.diagnostic = "tray indicator object export failed";
    }

    // Register with the watcher so the indicator materializes.
    if (report.diagnostic.empty()) {
        GError *error = nullptr;
        GVariant *registered = g_dbus_connection_call_sync(
            surface.connection, kWatcherName, "/StatusNotifierWatcher", kWatcherInterface,
            "RegisterStatusNotifierItem",
            // The spec: the parameter is the item's bus name (unique name of
            // this connection) — the host introspects the conventional item
            // path under it. Passing the object path here made every real
            // host fail to resolve the entry (verification round 1).
            g_variant_new("(s)", g_dbus_connection_get_unique_name(surface.connection)), nullptr,
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
        // newest state is pushed within one slice too. The source is
        // attached to the loop's OWN thread-default context explicitly —
        // g_timeout_add would land on the global default context and the
        // stop probe would never fire inside this loop.
        GSource *stop_timer = g_timeout_source_new(kStopPollMs);
        g_source_set_callback(
            stop_timer,
            [](gpointer user_data) -> gint {
                auto *stop = static_cast<StopContext *>(user_data);
                if ((*stop->stop_requested)()) {
                    stop->surface->stopping = true;
                    g_main_loop_quit(stop->surface->loop);
                    return G_SOURCE_REMOVE;
                }
                return G_SOURCE_CONTINUE;
            },
            &stop_context, nullptr);
        surface.stop_source = stop_timer; // kept: destroyed in teardown
        g_source_attach(stop_timer, surface.context);
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

    if (surface.stop_source != nullptr) {
        // g_source_destroy (not g_source_remove): the source lives on the
        // run loop's own context, not the global default one.
        g_source_destroy(surface.stop_source);
        g_source_unref(surface.stop_source);
        surface.stop_source = nullptr;
    }
    if (surface.watcher_watch != 0) {
        g_bus_unwatch_name(surface.watcher_watch);
    }
    if (surface.menu_registration != 0) {
        g_dbus_connection_unregister_object(surface.connection, surface.menu_registration);
    }
    if (surface.alt_item_registration != 0) {
        g_dbus_connection_unregister_object(surface.connection, surface.alt_item_registration);
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
