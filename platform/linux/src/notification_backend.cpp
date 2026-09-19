#include "notification_backend.hpp"

#include <utility>

#include <mirage/desktop/input_provider.hpp> // is_valid_utf8 (shared contract helper)

#include <gio/gio.h>

namespace mirage::platform::linux_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::NotificationLimits;
using mirage::desktop::NotificationOutcome;
using mirage::desktop::ProviderError;

ProviderError error(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

/// Upper bound for one synchronous Notify round trip: the call must stay
/// bounded even when a misbehaving service ignores requests (the clipboard
/// read deadline precedent; the contract has no timeout field, so this is
/// an internal constant).
constexpr gint kNotifyTimeoutMs = 5000;

constexpr const char *kServiceName = "org.freedesktop.Notifications";
constexpr const char *kServicePath = "/org/freedesktop/Notifications";
constexpr const char *kServiceInterface = "org.freedesktop.Notifications";

} // namespace

struct NotificationBackend::Impl {
    GDBusConnection *connection = nullptr; // owned reference
};

NotificationBackend::~NotificationBackend() {
    if (impl_ != nullptr && impl_->connection != nullptr) {
        g_object_unref(impl_->connection);
    }
}

std::unique_ptr<NotificationBackend> NotificationBackend::open() {
    GError *bus_error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &bus_error);
    if (connection == nullptr) {
        if (bus_error != nullptr) {
            g_error_free(bus_error);
        }
        return nullptr; // no session bus: no notification capability
    }
    auto backend = std::unique_ptr<NotificationBackend>(new NotificationBackend());
    backend->impl_ = std::make_unique<Impl>();
    backend->impl_->connection = connection;
    return backend;
}

NotificationOutcome NotificationBackend::notify(const std::string &title, const std::string &body,
                                                const NotificationLimits &limits,
                                                const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    NotificationOutcome outcome;
    // Validation happens before any platform call: a refused notification
    // must never have reached D-Bus. D-Bus strings are UTF-8 by protocol,
    // so malformed payloads are refused exactly like the clipboard's.
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "notification cancelled");
        return outcome;
    }
    if (title.empty()) {
        outcome.error = error("invalid_argument", "title must not be empty");
        return outcome;
    }
    if (title.size() > limits.max_title_bytes) {
        outcome.error = error("invalid_argument", "title exceeds the byte budget");
        return outcome;
    }
    if (body.size() > limits.max_body_bytes) {
        outcome.error = error("invalid_argument", "body exceeds the byte budget");
        return outcome;
    }
    if (!mirage::desktop::is_valid_utf8(title) || !mirage::desktop::is_valid_utf8(body)) {
        outcome.error = error("invalid_argument", "title and body must be valid UTF-8");
        return outcome;
    }

    GVariantBuilder actions;
    GVariantBuilder hints;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    // (app_name, replaces_id, app_icon, summary, body, actions, hints,
    // expire_timeout): the contract surface is title + body only, so the
    // notification carries no actions, no hints, and the platform's default
    // expiry (-1). Array positions in the format string consume the
    // GVariantBuilder pointers themselves (g_variant_new finalizes them);
    // handing it finalized GVariants instead aborts inside GLib.
    GVariant *parameters = g_variant_new("(susssasa{sv}i)", "Mirage", guint{0}, "", title.c_str(),
                                         body.c_str(), &actions, &hints, gint{-1});
    GError *gio_error = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(
        impl_->connection, kServiceName, kServicePath, kServiceInterface, "Notify", parameters,
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, kNotifyTimeoutMs, nullptr, &gio_error);
    if (reply == nullptr) {
        const std::string reason =
            gio_error != nullptr ? gio_error->message : "notification call failed";
        if (gio_error != nullptr) {
            g_error_free(gio_error);
        }
        outcome.error = error("io_error", reason);
        return outcome;
    }
    g_variant_unref(reply);
    outcome.ok = true;
    return outcome;
}

} // namespace mirage::platform::linux_backend
