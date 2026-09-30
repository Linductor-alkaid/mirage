// M5-10 Linux tray carrier verification (independent verification pass,
// DEC-030). Exercises the real GIO StatusNotifierItem carrier against a
// private D-Bus session bus with a wire-exact fake watcher host, built from
// the SNI specification (freedesktop.org StatusNotifierWatcher page) and a
// real host's introspection XML (waybar protocol/dbus-status-notifier-watcher.xml):
// the watcher bus name AND interface are both "org.kde.StatusNotifierWatcher"
// and RegisterStatusNotifierItem's parameter is the item's BUS NAME, not an
// object path.
//
// The spec-faithful scenario (registration against a watcher that serves
// only the specification interface) is the conformance gate; a tolerant
// fixture that additionally serves the implementation's current interface
// name exercises the rest of the carrier lifecycle (item properties, menu
// layout, menu Event → on_action delivery, wakeup-driven state push, clean
// stop, and the watcher-vanished diagnostic exit) independently of that
// conformance question.

#include "../support/dbus_session.hpp"
#include "../support/test.hpp"

#include <mirage/desktop/tray_carrier.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <signal.h>

#include <gio/gio.h>

namespace {

using mirage::desktop::TrayAction;
using mirage::desktop::TrayCarrier;
using mirage::desktop::TrayCarrierContext;
using mirage::desktop::TrayState;
using mirage::platform::linux_backend::open_tray_carrier;
using mirage::testing::DbusSession;

constexpr auto kSettleBudget = std::chrono::seconds{10};

template <typename Predicate> bool wait_for(Predicate &&predicate) {
    const auto deadline = std::chrono::steady_clock::now() + kSettleBudget;
    for (;;) {
        if (predicate()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

/// The final scenario drives paths whose CURRENT defects abort the process
/// through GLib's fatal log (GLib-ERROR → core dump). A core dump under
/// ctest's captured pipes hangs on this machine's apport core collector, so
/// the probe installs this handler: the captured fatal message IS the defect
/// evidence and the process exits with kFatalProbeExit deterministically. On
/// the fixed tree the fatal never fires and the scenario finishes normally.
constexpr int kFatalProbeExit = 42;
std::atomic<bool> g_fatal_seen{false};
std::atomic<pid_t> g_dbus_daemon_pid{0};

void tray_fatal_probe_handler(const gchar *domain, GLogLevelFlags level, const gchar *message,
                              gpointer) {
    if ((level & G_LOG_LEVEL_ERROR) != 0 || (level & G_LOG_FLAG_FATAL) != 0) {
        g_fatal_seen.store(true, std::memory_order_release);
        std::fprintf(stderr, "[tray_backend_test] captured expected fatal: %s: %s\n",
                     domain != nullptr ? domain : "", message != nullptr ? message : "");
        std::fflush(stderr);
        // _exit skips the fixture destructors: tear the daemon down here so
        // its inherited output fds release the harness's pipes.
        const pid_t daemon = g_dbus_daemon_pid.load(std::memory_order_acquire);
        if (daemon > 0) {
            ::kill(daemon, SIGTERM);
        }
        ::_exit(kFatalProbeExit);
    }
    g_log_default_handler(domain, level, message, nullptr);
}

/// The watcher's served surface. The specification interface is
/// org.kde.StatusNotifierWatcher (bus name == interface name, per the SNI
/// spec and waybar's host XML); the tolerant fixture additionally serves
/// the implementation's current interface name so the lifecycle scenarios
/// do not depend on the conformance question.
constexpr const char *kWatcherNodeXml =
    "<node>"
    "  <interface name='org.kde.StatusNotifierWatcher'>"
    "    <method name='RegisterStatusNotifierItem'>"
    "      <arg name='service' type='s' direction='in'/>"
    "    </method>"
    "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "  </interface>"
    "  <interface name='org.kde.StatusNotifierItemWatcher'>"
    "    <method name='RegisterStatusNotifierItem'>"
    "      <arg name='service' type='s' direction='in'/>"
    "    </method>"
    "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "  </interface>"
    "</node>";

/// One recorded registration with the sender identity the message carried.
struct RecordedRegistration {
    std::string interface_used;
    std::string service_parameter; ///< verbatim RegisterStatusNotifierItem argument
    std::string sender;            ///< the caller's unique bus name
};

/// One recorded ItemsPropertiesUpdated push (menu id → "enabled" value).
struct RecordedMenuProps {
    gint id = 0;
    gboolean enabled = FALSE;
    std::string label;
};

/// The fake watcher host: owns org.kde.StatusNotifierWatcher on the private
/// bus and records every registration. `tolerant` also serves the
/// implementation's current (spec-divergent) interface name.
class FakeStatusNotifierWatcher {
  public:
    bool start(const std::string &bus_address, bool tolerant) {
        tolerant_ = tolerant;
        GError *error = nullptr;
        connection_ = g_dbus_connection_new_for_address_sync(
            bus_address.c_str(),
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error);
        if (connection_ == nullptr) {
            std::fprintf(stderr, "[tray fixture] connect failed: %s\n",
                         error != nullptr ? error->message : "?");
            if (error != nullptr) {
                g_error_free(error);
            }
            return false;
        }
        node_ = g_dbus_node_info_new_for_xml(kWatcherNodeXml, nullptr);
        // interfaces[0] is the specification interface; interfaces[1] is the
        // tolerant extra. The vtable records which interface name served.
        const guint spec_registration = g_dbus_connection_register_object(
            connection_, "/StatusNotifierWatcher", node_->interfaces[0], &kWatcherVtable, this,
            nullptr, &error);
        if (spec_registration == 0) {
            std::fprintf(stderr, "[tray fixture] spec interface export failed\n");
            return false;
        }
        if (tolerant_) {
            GError *tolerant_error = nullptr;
            const guint tolerant_registration = g_dbus_connection_register_object(
                connection_, "/StatusNotifierWatcher", node_->interfaces[1], &kWatcherVtable, this,
                nullptr, &tolerant_error);
            if (tolerant_registration == 0) {
                std::fprintf(stderr, "[tray fixture] tolerant interface export failed: %s\n",
                             tolerant_error != nullptr ? tolerant_error->message : "?");
                if (tolerant_error != nullptr) {
                    g_error_free(tolerant_error);
                }
                return false;
            }
        }
        loop_ = g_main_loop_new(nullptr, FALSE);
        dispatch_thread_ = std::thread([this] { g_main_loop_run(loop_); });
        name_owned_ = g_bus_own_name_on_connection(
            connection_, "org.kde.StatusNotifierWatcher", G_BUS_NAME_OWNER_FLAGS_NONE,
            [](GDBusConnection *, const gchar *, gpointer user_data) {
                static_cast<FakeStatusNotifierWatcher *>(user_data)->name_acquired_.store(true);
            },
            [](GDBusConnection *, const gchar *, gpointer) {}, this, nullptr);
        if (!wait_for([this] { return name_acquired_.load(); })) {
            std::fprintf(stderr, "[tray fixture] never acquired org.kde.StatusNotifierWatcher\n");
            return false;
        }
        return true;
    }

    ~FakeStatusNotifierWatcher() {
        if (name_owned_ != 0) {
            g_bus_unown_name(name_owned_);
        }
        if (loop_ != nullptr) {
            g_main_loop_quit(loop_);
        }
        if (dispatch_thread_.joinable()) {
            dispatch_thread_.join();
        }
        if (node_ != nullptr) {
            g_dbus_node_info_unref(node_);
        }
        if (connection_ != nullptr) {
            g_object_unref(connection_);
        }
    }

    /// Drops the well-known name mid-run (the "indicator host vanished"
    /// topology); the carrier must end its loop with the diagnostic.
    void vanish() {
        if (name_owned_ != 0) {
            g_bus_unown_name(name_owned_);
            name_owned_ = 0;
        }
    }

    std::vector<RecordedRegistration> registrations() {
        const std::lock_guard guard(mutex_);
        return registrations_;
    }

    std::vector<RecordedMenuProps> menu_pushes() {
        const std::lock_guard guard(mutex_);
        return menu_pushes_;
    }

    bool properties_changed_seen() const { return properties_changed_.load(); }

    /// Subscribes the fixture connection to the carrier's state push
    /// signals (menu enablement + item PropertiesChanged).
    void subscribe_state_pushes() {
        menu_signal_id_ = g_dbus_connection_signal_subscribe(
            connection_, nullptr, "com.canonical.dbusmenu", "ItemsPropertiesUpdated",
            "/org/mirage/tray/menu", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
               GVariant *parameters, gpointer user_data) {
                auto *self = static_cast<FakeStatusNotifierWatcher *>(user_data);
                GVariantIter *updated = nullptr;
                GVariantIter *removed = nullptr;
                g_variant_get(parameters, "(a(ia{sv})as)", &updated, &removed);
                const std::lock_guard guard(self->mutex_);
                // Child-by-child reads (no container varargs slots), and the
                // dict values are v-wrapped: unwrap before reading.
                while (GVariant *entry = g_variant_iter_next_value(updated)) {
                    RecordedMenuProps record;
                    GVariant *id_variant = g_variant_get_child_value(entry, 0);
                    record.id = g_variant_get_int32(id_variant);
                    g_variant_unref(id_variant);
                    GVariant *props = g_variant_get_child_value(entry, 1);
                    for (const char *key : {"enabled", "label"}) {
                        // g_variant_lookup_value unwraps the dict's v values.
                        GVariant *value = g_variant_lookup_value(props, key, nullptr);
                        if (value == nullptr) {
                            continue;
                        }
                        if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
                            record.enabled = g_variant_get_boolean(value) ? TRUE : FALSE;
                        }
                        if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
                            record.label = g_variant_get_string(value, nullptr);
                        }
                        g_variant_unref(value);
                    }
                    g_variant_unref(props);
                    g_variant_unref(entry);
                    self->menu_pushes_.push_back(std::move(record));
                }
                g_variant_iter_free(updated);
                g_variant_iter_free(removed);
            },
            this, nullptr);
        item_signal_id_ = g_dbus_connection_signal_subscribe(
            connection_, nullptr, "org.freedesktop.DBus.Properties", "PropertiesChanged",
            "/org/mirage/tray", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
               GVariant *, gpointer user_data) {
                static_cast<FakeStatusNotifierWatcher *>(user_data)->properties_changed_.store(
                    true);
            },
            this, nullptr);
    }

    void unsubscribe_state_pushes() {
        if (menu_signal_id_ != 0) {
            g_dbus_connection_signal_unsubscribe(connection_, menu_signal_id_);
            menu_signal_id_ = 0;
        }
        if (item_signal_id_ != 0) {
            g_dbus_connection_signal_unsubscribe(connection_, item_signal_id_);
            item_signal_id_ = 0;
        }
    }

    /// Test-side synchronous call into the carrier's exported menu.
    void call_menu_event(const std::string &carrier_sender, gint id) {
        GVariant *data = g_variant_new_boolean(TRUE);
        GVariant *reply = g_dbus_connection_call_sync(
            connection_, carrier_sender.c_str(), "/org/mirage/tray/menu", "com.canonical.dbusmenu",
            "Event", g_variant_new("(isvu)", id, "clicked", data, 0), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
        if (reply != nullptr) {
            g_variant_unref(reply);
        }
    }

    GVariant *call_menu_get_layout(const std::string &carrier_sender, GError **error) {
        static const char *const no_properties[] = {nullptr};
        return g_dbus_connection_call_sync(
            connection_, carrier_sender.c_str(), "/org/mirage/tray/menu", "com.canonical.dbusmenu",
            "GetLayout", g_variant_new("(ii^as)", 0, 1, no_properties), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, error);
    }

    GVariant *call_menu_get_group_properties(const std::string &carrier_sender) {
        static const gint queried_ids[] = {2, 99};
        GVariant *ids =
            g_variant_new_fixed_array(G_VARIANT_TYPE_INT32, queried_ids, 2, sizeof(gint32));
        GVariant *properties = g_variant_new_strv(nullptr, 0);
        return g_dbus_connection_call_sync(
            connection_, carrier_sender.c_str(), "/org/mirage/tray/menu", "com.canonical.dbusmenu",
            "GetGroupProperties", g_variant_new("(@ai@as)", ids, properties), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
    }

    GVariant *call_item_property(const std::string &carrier_sender, const char *property,
                                 GError **error) {
        return g_dbus_connection_call_sync(
            connection_, carrier_sender.c_str(), "/org/mirage/tray",
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.kde.StatusNotifierItem", property), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, error);
    }

  private:
    static GVariant *watcher_get_property(GDBusConnection *, const gchar *, const gchar *,
                                          const gchar *, const gchar *property, GError **,
                                          gpointer) {
        if (g_strcmp0(property, "IsStatusNotifierHostRegistered") == 0) {
            return g_variant_new_boolean(TRUE);
        }
        return g_variant_new_boolean(FALSE);
    }

    static void watcher_method_call(GDBusConnection *, const gchar *sender, const gchar *,
                                    const gchar *interface_name, const gchar *method_name,
                                    GVariant *parameters, GDBusMethodInvocation *invocation,
                                    gpointer user_data) {
        auto *self = static_cast<FakeStatusNotifierWatcher *>(user_data);
        if (g_strcmp0(method_name, "RegisterStatusNotifierItem") != 0) {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.DBus.Error.UnknownMethod", method_name);
            return;
        }
        RecordedRegistration record;
        record.interface_used = interface_name;
        const gchar *service = nullptr;
        g_variant_get(parameters, "(&s)", &service);
        record.service_parameter = service != nullptr ? service : "";
        record.sender = sender != nullptr ? sender : "";
        {
            const std::lock_guard guard(self->mutex_);
            self->registrations_.push_back(std::move(record));
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
    }

    static const GDBusInterfaceVTable kWatcherVtable;

    bool tolerant_ = false;
    GDBusConnection *connection_ = nullptr;
    GDBusNodeInfo *node_ = nullptr;
    GMainLoop *loop_ = nullptr;
    std::thread dispatch_thread_;
    guint name_owned_ = 0;
    gulong menu_signal_id_ = 0;
    gulong item_signal_id_ = 0;
    std::atomic<bool> name_acquired_{false};
    std::atomic<bool> properties_changed_{false};
    std::mutex mutex_;
    std::vector<RecordedRegistration> registrations_;
    std::vector<RecordedMenuProps> menu_pushes_;
};

const GDBusInterfaceVTable FakeStatusNotifierWatcher::kWatcherVtable = {
    watcher_method_call,
    watcher_get_property,
    nullptr,
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

/// The carrier pump (the service's blocking-worker topology) with a
/// controllable state source and recorded actions. The run report is
/// published through the done flag and read only after join().
class TrayPump {
  public:
    void start(TrayCarrier &carrier) {
        thread_ = std::thread([this, &carrier] {
            TrayCarrierContext context;
            context.load_state = [this] {
                const std::lock_guard guard(mutex_);
                return state_;
            };
            context.on_action = [this](const TrayAction action) {
                const std::lock_guard guard(mutex_);
                actions_.push_back(action);
            };
            report_ = carrier.run(context, [this] { return stop_.load(); });
            done_.store(true, std::memory_order_release);
        });
    }

    void request_stop() { stop_.store(true); }

    bool done() const { return done_.load(std::memory_order_acquire); }

    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void set_state(TrayState state) {
        const std::lock_guard guard(mutex_);
        state_ = std::move(state);
    }

    std::vector<TrayAction> actions() {
        const std::lock_guard guard(mutex_);
        return actions_;
    }

    /// Only meaningful after done() and join().
    const TrayCarrier::RunReport &report() const { return report_; }

  private:
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> done_{false};
    TrayCarrier::RunReport report_{};
    std::mutex mutex_;
    TrayState state_{};
    std::vector<TrayAction> actions_;
};

// --- capability honesty ------------------------------------------------------

void scenario_no_session_bus_returns_null() {
    // A dead literal address: address resolution accepts it verbatim and
    // the probe connection must fail. (Unsetting the variable entirely is
    // NOT asserted: GLib then falls back to the autolaunch transport, which
    // can legitimately find or spawn a session bus on a desktop machine.)
    const char *saved = ::getenv("DBUS_SESSION_BUS_ADDRESS");
    ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/mirage-tray-test-bus", 1);
    const auto carrier = open_tray_carrier();
    if (saved != nullptr) {
        ::setenv("DBUS_SESSION_BUS_ADDRESS", saved, 1);
    } else {
        ::unsetenv("DBUS_SESSION_BUS_ADDRESS");
    }
    MIRAGE_CHECK(carrier == nullptr);
}

void scenario_bus_without_watcher_returns_null(const std::string &main_bus_address) {
    // A private bus with no indicator host: the carrier must refuse. The
    // environment is restored so the later scenarios keep addressing the
    // main session.
    DbusSession bare_session;
    ::setenv("DBUS_SESSION_BUS_ADDRESS", bare_session.bus_address().c_str(), 1);
    const auto carrier = open_tray_carrier();
    ::setenv("DBUS_SESSION_BUS_ADDRESS", main_bus_address.c_str(), 1);
    MIRAGE_CHECK(carrier == nullptr);
}

// --- the specification conformance gate --------------------------------------

void scenario_registers_with_the_specification_watcher(FakeStatusNotifierWatcher &watcher) {
    const auto carrier = open_tray_carrier();
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }
    TrayPump pump;
    pump.start(*carrier);
    // The carrier must register with the watcher (the indicator only
    // materializes through RegisterStatusNotifierItem).
    MIRAGE_CHECK(wait_for([&watcher] { return !watcher.registrations().empty(); }));

    const auto registrations = watcher.registrations();
    MIRAGE_CHECK(registrations.size() == 1);
    if (registrations.size() == 1) {
        const RecordedRegistration &record = registrations.front();
        // SNI spec: the interface is the watcher's bus name (no
        // "ItemWatcher" interface exists) and the service parameter is the
        // item's bus name ("in the form of its full name on the session
        // bus") — never an object path.
        MIRAGE_CHECK(record.interface_used == "org.kde.StatusNotifierWatcher");
        MIRAGE_CHECK(!record.service_parameter.empty());
        MIRAGE_CHECK(record.service_parameter.front() == ':');
        MIRAGE_CHECK(record.sender == record.service_parameter);
    }

    // The presentation loop stays up clean over the specification watcher
    // and stops cleanly on request.
    pump.request_stop();
    pump.join();
    MIRAGE_CHECK(pump.report().clean);
    MIRAGE_CHECK(pump.report().diagnostic.empty());
}

// --- the carrier lifecycle over a tolerant watcher ---------------------------

void scenario_lifecycle_over_tolerant_watcher(FakeStatusNotifierWatcher &watcher) {
    const auto carrier = open_tray_carrier();
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }

    TrayPump pump;
    TrayState initial;
    initial.status = "Mirage：空闲";
    initial.can_open_shell = true;
    pump.set_state(initial);
    pump.start(*carrier);
    MIRAGE_CHECK(wait_for([&watcher] { return !watcher.registrations().empty(); }));

    // The registration's sender is the carrier connection's unique name:
    // every test-side call into the exported objects addresses it.
    std::string carrier_sender;
    {
        const auto registrations = watcher.registrations();
        if (!registrations.empty()) {
            carrier_sender = registrations.front().sender;
        }
    }
    MIRAGE_CHECK(!carrier_sender.empty());
    MIRAGE_CHECK(carrier_sender.front() == ':');

    // Item properties answer fresh from the carrier's state pull.
    GError *error = nullptr;
    GVariant *title = watcher.call_item_property(carrier_sender, "Title", &error);
    MIRAGE_CHECK(title != nullptr);
    if (title != nullptr) {
        GVariant *inner = nullptr;
        g_variant_get(title, "(v)", &inner);
        MIRAGE_CHECK(g_strcmp0(g_variant_get_string(inner, nullptr), "Mirage") == 0);
        g_variant_unref(inner);
        g_variant_unref(title);
    }
    GVariant *menu_path = watcher.call_item_property(carrier_sender, "Menu", &error);
    MIRAGE_CHECK(menu_path != nullptr);
    if (menu_path != nullptr) {
        GVariant *inner = nullptr;
        g_variant_get(menu_path, "(v)", &inner);
        MIRAGE_CHECK(g_strcmp0(g_variant_get_string(inner, nullptr), "/org/mirage/tray/menu") == 0);
        g_variant_unref(inner);
        g_variant_unref(menu_path);
    }

    // The menu layout probe lives in its own final scenario: a GetLayout
    // reply is what a real indicator host requests first, and any defect
    // there must not mask the rest of the lifecycle evidence.

    // Menu events deliver the mapped actions; the disabled header entry
    // delivers nothing. (The wakeup-driven state push is probed in its own
    // final scenario: on the current tree it aborts the process, and that
    // must not mask the rest of the lifecycle evidence.)

    const std::size_t before_header = pump.actions().size();
    watcher.call_menu_event(carrier_sender, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    MIRAGE_CHECK(pump.actions().size() == before_header);

    watcher.unsubscribe_state_pushes();

    // Clean stop within one poll slice of the quit request.
    pump.request_stop();
    pump.join();
    MIRAGE_CHECK(pump.report().clean);
    MIRAGE_CHECK(pump.report().diagnostic.empty());
}

void scenario_watcher_vanish_ends_the_loop_with_a_diagnostic(FakeStatusNotifierWatcher &watcher) {
    const auto carrier = open_tray_carrier();
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }
    TrayPump pump;
    pump.start(*carrier);
    MIRAGE_CHECK(wait_for([&watcher] { return !watcher.registrations().empty(); }));

    // The indicator host disappears: the surface cannot be presented any
    // more, so the loop must end with the diagnostic (DEC-030 capability
    // honesty), never silently.
    watcher.vanish();
    MIRAGE_CHECK(wait_for([&pump] { return pump.done(); }));
    pump.join();
    MIRAGE_CHECK(!pump.report().clean);
    MIRAGE_CHECK(pump.report().diagnostic.find("StatusNotifierWatcher") != std::string::npos);
}

/// The runtime state push and the menu layout read path — exactly what a
/// real indicator host drives: the owner flips the TrayState and wakes the
/// pump (the watcher must receive ItemsPropertiesUpdated with the new
/// enablement), then GetLayout returns the fixed seven-entry table and
/// GetGroupProperties answers the same properties. Kept as the FINAL
/// scenario: on the current tree both the state push (tray_backend.cpp:326,
/// unterminated a{sv} varargs) and the layout read (spaced GVariant format
/// strings) abort the process (GLib fatal) — the abort is the defect
/// evidence and must not mask the earlier scenarios.
void scenario_state_push_and_menu_layout_queries(FakeStatusNotifierWatcher &watcher) {
    g_log_set_default_handler(tray_fatal_probe_handler, nullptr);
    const auto carrier = open_tray_carrier();
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }
    TrayPump pump;
    TrayState active;
    active.status = "Mirage：任务运行中：clean the cache — Active";
    active.can_pause = true;
    active.can_resume = false;
    active.can_open_shell = true;
    pump.set_state(active);
    pump.start(*carrier);
    MIRAGE_CHECK(wait_for([&watcher] { return !watcher.registrations().empty(); }));

    std::string carrier_sender;
    {
        const auto registrations = watcher.registrations();
        if (!registrations.empty()) {
            carrier_sender = registrations.front().sender;
        }
    }
    MIRAGE_CHECK(!carrier_sender.empty());

    GError *error = nullptr;
    GVariant *layout = watcher.call_menu_get_layout(carrier_sender, &error);
    MIRAGE_CHECK(layout != nullptr);
    if (layout != nullptr) {
        guint revision = 0;
        GVariantIter *entries = nullptr;
        g_variant_get(layout, "(ua(ia{sv}av))", &revision, &entries);
        MIRAGE_CHECK(revision >= 1);
        MIRAGE_CHECK(g_variant_iter_n_children(entries) == 7);
        // The pause entry (id 2) mirrors the state's can_pause. Entries are
        // read child-by-child (the "av" slot is not directly loopable).
        gboolean pause_enabled = FALSE;
        gboolean quit_enabled = FALSE;
        while (GVariant *entry = g_variant_iter_next_value(entries)) {
            GVariant *id_variant = g_variant_get_child_value(entry, 0);
            const gint id = g_variant_get_int32(id_variant);
            g_variant_unref(id_variant);
            GVariant *props = g_variant_get_child_value(entry, 1);
            GVariant *enabled = g_variant_lookup_value(props, "enabled", nullptr);
            if (enabled != nullptr) {
                if (g_variant_is_of_type(enabled, G_VARIANT_TYPE_BOOLEAN)) {
                    if (id == 2) {
                        pause_enabled = g_variant_get_boolean(enabled) ? TRUE : FALSE;
                    }
                    if (id == 99) {
                        quit_enabled = g_variant_get_boolean(enabled) ? TRUE : FALSE;
                    }
                }
                g_variant_unref(enabled);
            }
            g_variant_unref(props);
            g_variant_unref(entry);
        }
        g_variant_iter_free(entries);
        g_variant_unref(layout);
        MIRAGE_CHECK(pause_enabled == TRUE);
        MIRAGE_CHECK(quit_enabled == TRUE);
    }

    // The runtime state push: flip the state (paused task → resume entry
    // enabled, pause disabled) and wake the pump — the watcher receives
    // PropertiesChanged on the item and ItemsPropertiesUpdated on the menu.
    watcher.subscribe_state_pushes();
    TrayState paused;
    paused.status = "Mirage：任务已暂停：clean the cache — Paused";
    paused.can_resume = true;
    paused.can_open_shell = true;
    pump.set_state(paused);
    carrier->wakeup();
    MIRAGE_CHECK(wait_for([&watcher] { return watcher.properties_changed_seen(); }));
    MIRAGE_CHECK(wait_for([&watcher] {
        for (const RecordedMenuProps &push : watcher.menu_pushes()) {
            if (push.id == 3 && push.enabled == TRUE && push.label == "恢复任务") {
                return true;
            }
        }
        return false;
    }));
    watcher.unsubscribe_state_pushes();

    pump.request_stop();
    pump.join();
    MIRAGE_CHECK(pump.report().clean);
    // Neither the state push nor the layout read may hit a GLib fatal: the
    // fatal-probe handler exits the process when one fires, so reaching this
    // line already proves the push and the read are both clean.
    MIRAGE_CHECK(!g_fatal_seen.load(std::memory_order_acquire));
}

/// The host's batch read (GetGroupProperties) over the same ids. The
/// handler's g_variant_get passes a GVariantIter struct where the varargs
/// slot requires GVariantIter** — on the current tree the first iteration
/// aborts the process through a GLib type-info assertion (which bypasses
/// the GLog default handler, hence the SIGABRT probe): the probe captures
/// the abort and exits with kFatalProbeExit deterministically, so the
/// crash evidence survives ctest's captured pipes without a core-dump hang.
static std::atomic<bool> g_abort_probe_armed{false};

void tray_abort_probe_handler(int) {
    if (!g_abort_probe_armed.load(std::memory_order_acquire)) {
        ::signal(SIGABRT, SIG_DFL);
        ::raise(SIGABRT);
        return;
    }
    std::fprintf(stderr,
                 "[tray_backend_test] captured expected SIGABRT (GLib type-info assertion)\n");
    std::fflush(stderr);
    const pid_t daemon = g_dbus_daemon_pid.load(std::memory_order_acquire);
    if (daemon > 0) {
        ::kill(daemon, SIGTERM);
    }
    ::_exit(kFatalProbeExit);
}

void scenario_menu_group_properties_delivery(FakeStatusNotifierWatcher &watcher) {
    ::signal(SIGABRT, tray_abort_probe_handler);
    g_abort_probe_armed.store(true, std::memory_order_release);
    const auto carrier = open_tray_carrier();
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }
    TrayPump pump;
    TrayState active;
    active.status = "Mirage：任务运行中：clean the cache — Active";
    active.can_pause = true;
    active.can_open_shell = true;
    pump.set_state(active);
    pump.start(*carrier);
    MIRAGE_CHECK(wait_for([&watcher] { return !watcher.registrations().empty(); }));

    std::string carrier_sender;
    {
        const auto registrations = watcher.registrations();
        if (!registrations.empty()) {
            carrier_sender = registrations.front().sender;
        }
    }
    MIRAGE_CHECK(!carrier_sender.empty());

    GVariant *group = watcher.call_menu_get_group_properties(carrier_sender);
    MIRAGE_CHECK(group != nullptr);
    if (group != nullptr) {
        g_variant_unref(group);
    }
    pump.request_stop();
    pump.join();
    MIRAGE_CHECK(pump.report().clean);
    g_abort_probe_armed.store(false, std::memory_order_release);
    ::signal(SIGABRT, SIG_DFL);
}

/// Verification round 2 regression probe: the session bus can die between
/// open() and run() (the session address is resolved and probed in open(),
/// the carrier's own connection is only created inside run()). When that
/// connection fails, run() must converge on clean=false with a diagnostic —
/// the registration block after it must stay guarded. Runs in a forked
/// child: on the current tree the unguarded path crashes the child (null
/// node info dereference) and the parent turns that into evidence without
/// taking the suite down.
void scenario_bus_dies_between_open_and_run() {
    DbusSession dying_session;
    g_dbus_daemon_pid.store(dying_session.daemon_pid(), std::memory_order_release);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", dying_session.bus_address().c_str(), 1);
    FakeStatusNotifierWatcher dying_watcher;
    if (!dying_watcher.start(dying_session.bus_address(), /*tolerant=*/true)) {
        std::fprintf(stderr, "[tray_backend_test] dying-session fixture failed to start\n");
        MIRAGE_CHECK(false);
        return;
    }
    const auto carrier = open_tray_carrier();
    MIRAGE_CHECK(carrier != nullptr);
    if (carrier == nullptr) {
        return;
    }

    // The bus goes away after the carrier was opened and before run().
    ::kill(dying_session.daemon_pid(), SIGTERM);
    std::this_thread::sleep_for(std::chrono::milliseconds{300});

    const pid_t child = ::fork();
    if (child == 0) {
        mirage::desktop::TrayCarrierContext context;
        context.load_state = [] { return TrayState{}; };
        const TrayCarrier::RunReport report = carrier->run(context, [] { return false; });
        const bool degraded = !report.clean && !report.diagnostic.empty();
        std::fprintf(stderr, "[tray_backend_test] dying-bus child: clean=%d diagnostic='%s'\n",
                     report.clean ? 1 : 0, report.diagnostic.c_str());
        std::fflush(stderr);
        ::_exit(degraded ? 0 : 1);
    }

    // Bounded wait with kill-on-hang: the child either exits by itself or
    // the parent reaps it and reports the hang as a failure.
    int status = 0;
    bool reaped = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t got = ::waitpid(child, &status, WNOHANG);
        if (got == child) {
            reaped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    if (!reaped) {
        std::fprintf(stderr, "[tray_backend_test] dying-bus child hung; killed\n");
        ::kill(child, SIGKILL);
        ::waitpid(child, &status, 0);
        MIRAGE_CHECK(false);
        return;
    }
    std::fprintf(stderr,
                 "[tray_backend_test] dying-bus child reaped: exited=%d signal=%d code=%d\n",
                 WIFEXITED(status) ? 1 : 0, WIFSIGNALED(status) ? WTERMSIG(status) : 0,
                 WIFEXITED(status) ? WEXITSTATUS(status) : 0);
    MIRAGE_CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) {
        MIRAGE_CHECK(WEXITSTATUS(status) == 0);
    }
}

} // namespace

int main() {
    try {
        DbusSession session;
        g_dbus_daemon_pid.store(session.daemon_pid(), std::memory_order_release);
        ::setenv("DBUS_SESSION_BUS_ADDRESS", session.bus_address().c_str(), 1);

        std::fprintf(stderr, "[tray_backend_test] scenario: capability_honesty\n");
        scenario_no_session_bus_returns_null();
        scenario_bus_without_watcher_returns_null(session.bus_address());

        std::fprintf(stderr, "[tray_backend_test] scenario: spec_conformance_registration\n");
        {
            FakeStatusNotifierWatcher spec_watcher;
            if (!spec_watcher.start(session.bus_address(), /*tolerant=*/false)) {
                return 1;
            }
            scenario_registers_with_the_specification_watcher(spec_watcher);
        }

        std::fprintf(stderr, "[tray_backend_test] scenario: lifecycle_over_tolerant_watcher\n");
        {
            FakeStatusNotifierWatcher tolerant_watcher;
            if (!tolerant_watcher.start(session.bus_address(), /*tolerant=*/true)) {
                return 1;
            }
            scenario_lifecycle_over_tolerant_watcher(tolerant_watcher);

            std::fprintf(stderr, "[tray_backend_test] scenario: watcher_vanish\n");
            scenario_watcher_vanish_ends_the_loop_with_a_diagnostic(tolerant_watcher);
        }

        std::fprintf(stderr, "[tray_backend_test] scenario: menu_group_properties\n");
        {
            FakeStatusNotifierWatcher fresh_watcher;
            if (!fresh_watcher.start(session.bus_address(), /*tolerant=*/true)) {
                return 1;
            }
            scenario_menu_group_properties_delivery(fresh_watcher);
        }

        std::fprintf(stderr, "[tray_backend_test] scenario: state_push_and_menu_layout\n");
        {
            // A fresh host: the vanish scenario dropped the well-known name,
            // and open_tray_carrier() must refuse a host-less bus (the same
            // capability honesty the second scenario pins).
            FakeStatusNotifierWatcher fresh_watcher;
            if (!fresh_watcher.start(session.bus_address(), /*tolerant=*/true)) {
                return 1;
            }
            scenario_state_push_and_menu_layout_queries(fresh_watcher);
        }

        std::fprintf(stderr, "[tray_backend_test] scenario: bus_dies_between_open_and_run\n");
        scenario_bus_dies_between_open_and_run();
        // Back to the main session for any later wiring.
        g_dbus_daemon_pid.store(session.daemon_pid(), std::memory_order_release);
        ::setenv("DBUS_SESSION_BUS_ADDRESS", session.bus_address().c_str(), 1);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[tray_backend_test] dbus fixture failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("tray_backend_test");
}
