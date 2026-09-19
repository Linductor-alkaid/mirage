// M2-05 NotificationBackend tests (DEC-015 topology): a private D-Bus
// session bus per scenario, with a wire-exact GDBus fixture that owns
// org.freedesktop.Notifications and serves Notify itself — the real
// notification daemon is bypassed, so delivery, wire layout and every
// pre-D-Bus rejection are fully observable. The backend under test is
// reached through LinuxDesktopEnvironment with NotificationsOptions and
// DBUS_SESSION_BUS_ADDRESS. The rejection order against the frozen fake
// contract (tests/support/fake_desktop_environment.hpp) is re-verified on
// the real GDBus path.

#include "../support/dbus_session.hpp"
#include "../support/test.hpp"

#include <mirage/desktop/cancellation.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gio/gio.h>

namespace {

using mirage::platform::linux_backend::LinuxDesktopEnvironment;

/// One recorded Notify call, with every wire field the fixture received.
struct RecordedNotification {
    std::string app_name;
    guint replaces_id = 0;
    std::string app_icon;
    std::string title; // the Notify "summary"
    std::string body;
    std::vector<std::string> actions;
    gsize hints_count = 0;
    gint expire_timeout = 0;
};

/// The fixture: connects to a private bus, exports
/// /org/freedesktop/Notifications with the org.freedesktop.Notifications
/// interface and (optionally) owns the well-known name. `own_name=false`
/// models a session bus without a notification service.
class FakeNotificationsService {
  public:
    bool start(const std::string &bus_address, bool own_name) {
        GError *error = nullptr;
        connection_ = g_dbus_connection_new_for_address_sync(
            bus_address.c_str(),
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error);
        if (connection_ == nullptr) {
            std::fprintf(stderr, "fixture connect failed: %s\n",
                         error != nullptr ? error->message : "?");
            if (error != nullptr) {
                g_error_free(error);
            }
            return false;
        }
        if (!register_interface()) {
            return false;
        }
        // Dispatch thread first: the name request below is answered through
        // the main loop this thread runs.
        loop_ = g_main_loop_new(nullptr, FALSE);
        dispatch_thread_ = std::thread([this] { g_main_loop_run(loop_); });
        if (own_name) {
            name_owned_ = g_bus_own_name_on_connection(
                connection_, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_NONE,
                [](GDBusConnection *, const gchar *, gpointer user_data) {
                    auto *self = static_cast<FakeNotificationsService *>(user_data);
                    self->name_acquired_ = true;
                },
                [](GDBusConnection *, const gchar *, gpointer) {}, this, nullptr);
            // The name request is answered on the dispatch thread; wait for
            // it so the backend's first call cannot race the acquisition.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
            while (!name_acquired_.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
            if (!name_acquired_.load()) {
                std::fprintf(stderr, "fixture never acquired org.freedesktop.Notifications\n");
                return false;
            }
        }
        // Test-side infrastructure thread only — production code stays
        // thread-free (RULE-03), same as the M2-03 fixture.
        return true;
    }

    ~FakeNotificationsService() {
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

    std::size_t call_count() {
        std::lock_guard<std::mutex> guard(mutex_);
        return calls_.size();
    }

    RecordedNotification call_at(std::size_t index) {
        std::lock_guard<std::mutex> guard(mutex_);
        return calls_.at(index);
    }

  private:
    static void on_notify(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                          const gchar *method_name, GVariant *parameters,
                          GDBusMethodInvocation *invocation, gpointer user_data) {
        auto *self = static_cast<FakeNotificationsService *>(user_data);
        if (std::string(method_name) != "Notify") {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.DBus.Error.UnknownMethod", method_name);
            return;
        }
        RecordedNotification record;
        GVariantIter *actions_iter = nullptr;
        GVariantIter *hints_iter = nullptr;
        const gchar *app_name = nullptr;
        const gchar *app_icon = nullptr;
        const gchar *summary = nullptr;
        const gchar *body = nullptr;
        gchar *action = nullptr;
        // Borrowed ("&") scalars; the containers yield heap iterators that
        // must be freed after use.
        g_variant_get(parameters, "(&su&s&s&sasa{sv}i)", &app_name, &record.replaces_id, &app_icon,
                      &summary, &body, &actions_iter, &hints_iter, &record.expire_timeout);
        record.app_name = app_name != nullptr ? app_name : "";
        record.app_icon = app_icon != nullptr ? app_icon : "";
        record.title = summary != nullptr ? summary : "";
        record.body = body != nullptr ? body : "";
        while (g_variant_iter_loop(actions_iter, "s", &action)) {
            record.actions.emplace_back(action != nullptr ? action : "");
        }
        while (g_variant_iter_loop(hints_iter, "{sv}", nullptr, nullptr)) {
            ++record.hints_count;
        }
        g_variant_iter_free(actions_iter);
        g_variant_iter_free(hints_iter);

        const guint assigned = self->next_id_++;
        {
            std::lock_guard<std::mutex> guard(self->mutex_);
            self->calls_.push_back(record);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", assigned));
    }

    bool register_interface() {
        static constexpr const char *kNotificationsXml =
            "<node><interface name='org.freedesktop.Notifications'>"
            "<method name='Notify'>"
            "<arg type='s' direction='in'/>"
            "<arg type='u' direction='in'/>"
            "<arg type='s' direction='in'/>"
            "<arg type='s' direction='in'/>"
            "<arg type='s' direction='in'/>"
            "<arg type='as' direction='in'/>"
            "<arg type='a{sv}' direction='in'/>"
            "<arg type='i' direction='in'/>"
            "<arg type='u' direction='out'/>"
            "</method>"
            "</interface></node>";
        GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(kNotificationsXml, nullptr);
        GDBusInterfaceVTable vtable{};
        vtable.method_call = on_notify;
        vtable.get_property = nullptr;
        vtable.set_property = nullptr;
        GError *error = nullptr;
        const gboolean registered =
            g_dbus_connection_register_object(connection_, "/org/freedesktop/Notifications",
                                              info->interfaces[0], &vtable, this, nullptr, &error);
        g_dbus_node_info_unref(info);
        if (registered == 0) {
            std::fprintf(stderr, "fixture register failed: %s\n",
                         error != nullptr ? error->message : "?");
            if (error != nullptr) {
                g_error_free(error);
            }
            return false;
        }
        return true;
    }

    GDBusConnection *connection_ = nullptr;
    guint name_owned_ = 0;
    GMainLoop *loop_ = nullptr;
    std::thread dispatch_thread_;
    std::atomic<bool> name_acquired_{false};
    guint next_id_ = 42; // non-zero start proves the fixture's id travels back
    std::mutex mutex_;
    std::vector<RecordedNotification> calls_;
};

} // namespace

int main() {
    try {
        mirage::testing::DbusSession session;
        ::setenv("DBUS_SESSION_BUS_ADDRESS", session.bus_address().c_str(), 1);

        {
            FakeNotificationsService service;
            if (!service.start(session.bus_address(), /*own_name=*/true)) {
                return 1;
            }
            LinuxDesktopEnvironment env({}, {}, {}, {}, {.enabled = true});
            auto *notifications = env.notification();
            if (notifications == nullptr) {
                std::fprintf(stderr, "notification accessor is null despite a live bus\n");
                return 1;
            }

            std::fprintf(stderr, "[notification_backend_test] scenario: delivery_and_wire\n");
            const auto posted = notifications->notify("Mirage task done", "42 files exported");
            MIRAGE_CHECK(posted.ok);
            MIRAGE_CHECK(!posted.cancelled);
            MIRAGE_CHECK(posted.error.code.empty());
            MIRAGE_CHECK(service.call_count() == 1);
            if (service.call_count() == 1) {
                const RecordedNotification record = service.call_at(0);
                MIRAGE_CHECK(record.app_name == "Mirage");
                MIRAGE_CHECK(record.replaces_id == 0);
                MIRAGE_CHECK(record.app_icon.empty());
                MIRAGE_CHECK(record.title == "Mirage task done");
                MIRAGE_CHECK(record.body == "42 files exported");
                MIRAGE_CHECK(record.actions.empty());
                MIRAGE_CHECK(record.hints_count == 0);
                MIRAGE_CHECK(record.expire_timeout == -1);
            }
            // An empty body is legal; only the title is required.
            MIRAGE_CHECK(notifications->notify("Empty body ok", "").ok);
            MIRAGE_CHECK(service.call_count() == 2);
            if (service.call_count() == 2) {
                MIRAGE_CHECK(service.call_at(1).body.empty());
                MIRAGE_CHECK(service.call_at(1).title == "Empty body ok");
            }

            std::fprintf(stderr, "[notification_backend_test] scenario: validation_order\n");
            // Every rejection below lands before any D-Bus traffic: the
            // fixture call count must stay at 2.
            MIRAGE_CHECK(!notifications->notify("", "no title").ok);
            MIRAGE_CHECK(notifications->notify("", "no title").error.code == "invalid_argument");
            mirage::desktop::NotificationLimits tiny_title;
            tiny_title.max_title_bytes = 4;
            MIRAGE_CHECK(notifications->notify("12345", "x", tiny_title, {}).error.code ==
                         "invalid_argument");
            mirage::desktop::NotificationLimits tiny_body;
            tiny_body.max_body_bytes = 4;
            MIRAGE_CHECK(notifications->notify("title", "12345", tiny_body, {}).error.code ==
                         "invalid_argument");
            // Invalid UTF-8: lone 0xFF, truncated 2-byte lead, UTF-16
            // surrogate, and a bare continuation byte.
            MIRAGE_CHECK(notifications->notify("\xff", "").error.code == "invalid_argument");
            MIRAGE_CHECK(notifications->notify("ok", "\xc3").error.code == "invalid_argument");
            MIRAGE_CHECK(notifications->notify("\xed\xa0\x80", "").error.code ==
                         "invalid_argument");
            MIRAGE_CHECK(notifications->notify("\x80", "").error.code == "invalid_argument");
            MIRAGE_CHECK(service.call_count() == 2);

            // Cancellation precedes every validation step and reaches no bus.
            mirage::desktop::CancelToken cancelled;
            cancelled.request_cancel();
            const auto cancelled_outcome =
                notifications->notify("never posted", "body", {}, cancelled);
            MIRAGE_CHECK(cancelled_outcome.cancelled);
            MIRAGE_CHECK(!cancelled_outcome.ok);
            MIRAGE_CHECK(cancelled_outcome.error.code == "cancelled");
            // An empty title under a cancelled token reports cancelled (the
            // token is observed first, like the fake contract).
            MIRAGE_CHECK(notifications->notify("", "", {}, cancelled).cancelled);
            MIRAGE_CHECK(service.call_count() == 2);

            std::fprintf(stderr, "[notification_backend_test] scenario: budget_boundaries\n");
            const std::string exact_title(256, 't');
            const std::string exact_body(4096, 'b');
            const auto exact_post = notifications->notify(exact_title, exact_body);
            MIRAGE_CHECK(exact_post.ok);
            MIRAGE_CHECK(service.call_count() == 3);
            if (service.call_count() == 3) {
                MIRAGE_CHECK(service.call_at(2).title == exact_title);
                MIRAGE_CHECK(service.call_at(2).body == exact_body);
            }
            const std::string one_over_title(257, 't');
            const std::string one_over_body(4097, 'b');
            MIRAGE_CHECK(notifications->notify(one_over_title, "").error.code ==
                         "invalid_argument");
            MIRAGE_CHECK(notifications->notify("title", one_over_body).error.code ==
                         "invalid_argument");
            MIRAGE_CHECK(service.call_count() == 3); // boundaries only: nothing else arrived

            // Multi-byte characters count by bytes, not code points.
            const std::string multibyte = "\xe6\x97\xa5\xe6\x9c\xac"; // 2 x 3-byte chars
            mirage::desktop::NotificationLimits six_bytes;
            six_bytes.max_title_bytes = 6;
            MIRAGE_CHECK(notifications->notify(multibyte, "", six_bytes, {}).ok);
            six_bytes.max_title_bytes = 5;
            MIRAGE_CHECK(notifications->notify(multibyte, "", six_bytes, {}).error.code ==
                         "invalid_argument");
            MIRAGE_CHECK(service.call_count() == 4);
        }

        std::fprintf(stderr, "[notification_backend_test] scenario: no_notification_service\n");
        // A live session bus with NO owner for org.freedesktop.Notifications:
        // the service resolution fails per call and surfaces as io_error.
        {
            mirage::testing::DbusSession bare_session;
            ::setenv("DBUS_SESSION_BUS_ADDRESS", bare_session.bus_address().c_str(), 1);
            LinuxDesktopEnvironment bare_env({}, {}, {}, {}, {.enabled = true});
            MIRAGE_CHECK(bare_env.notification() != nullptr);
            const auto failed = bare_env.notification()->notify("nobody home", "body");
            MIRAGE_CHECK(!failed.ok);
            MIRAGE_CHECK(!failed.cancelled);
            MIRAGE_CHECK(failed.error.code == "io_error");
        }

        std::fprintf(stderr, "[notification_backend_test] scenario: no_session_bus\n");
        // Fail closed with no session bus at all: the accessor is null.
        // Every discovery fallback is removed (env var, XDG_RUNTIME_DIR bus,
        // legacy $HOME session files, X11 autolaunch), so GLib cannot find
        // or spawn any bus; this is the last scenario of the process.
        ::unsetenv("DBUS_SESSION_BUS_ADDRESS");
        ::unsetenv("XDG_RUNTIME_DIR");
        ::unsetenv("DISPLAY");
        ::setenv("HOME", "/tmp/mirage-m205-empty-home", 1);
        LinuxDesktopEnvironment busless_env({}, {}, {}, {}, {.enabled = true});
        MIRAGE_CHECK(busless_env.notification() == nullptr);
        // Without opting in, the accessor stays null too.
        LinuxDesktopEnvironment silent_env;
        MIRAGE_CHECK(silent_env.notification() == nullptr);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "notification setup failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("notification_backend_test");
}
