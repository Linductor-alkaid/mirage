#include "portal_screen_backend.hpp"
#include "../../common/src/presentation_context.hpp"
#include "portal_authorization.hpp"
#include "portal_input.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <poll.h>
#ifdef MIRAGE_LINUX_PORTAL
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#ifdef MIRAGE_LINUX_PIPEWIRE
#include "portal_pipewire_capture.hpp"
#include <pipewire/pipewire.h>
#endif
#endif

namespace mirage::platform::linux_backend {
namespace {
desktop::CaptureOutcome failure(std::string code, std::string message) {
    return {false, code == "cancelled", {}, {std::move(code), std::move(message)}};
}
#ifdef MIRAGE_LINUX_PORTAL
constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto root = "/org/freedesktop/portal/desktop";
constexpr auto request_interface = "org.freedesktop.portal.Request";
struct ObjectDelete {
    void operator()(GDBusConnection *v) const {
        if (v) {
            // This adapter owns a private connection. Explicitly disconnect so
            // the Portal also drops any session whose Close reply was lost.
            (void)g_dbus_connection_close_sync(v, nullptr, nullptr);
            g_object_unref(v);
        }
    }
};
struct VariantDelete {
    void operator()(GVariant *v) const {
        if (v)
            g_variant_unref(v);
    }
};
using Connection = std::unique_ptr<GDBusConnection, ObjectDelete>;
using Variant = std::unique_ptr<GVariant, VariantDelete>;

Connection connect_bus() {
    // Bounded local socket/authentication path, same platform boundary as the
    // credential adapter. No external service discovery or private worker.
    const auto *address = g_getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!address)
        return {};
    const std::string value(address);
    const bool abstract = value.starts_with("unix:abstract=");
    if (!abstract && !value.starts_with("unix:path="))
        return {};
    const auto begin = abstract ? 14U : 10U;
    const auto encoded = value.substr(begin, value.find_first_of(",;", begin) - begin);
    gchar *decoded = g_uri_unescape_string(encoded.c_str(), nullptr);
    if (!decoded)
        return {};
    auto *socket_address = g_unix_socket_address_new_with_type(
        decoded, -1, abstract ? G_UNIX_SOCKET_ADDRESS_ABSTRACT : G_UNIX_SOCKET_ADDRESS_PATH);
    g_free(decoded);
    auto *client = g_socket_client_new();
    g_socket_client_set_timeout(client, 1);
    GError *error = nullptr;
    auto *stream =
        g_socket_client_connect(client, G_SOCKET_CONNECTABLE(socket_address), nullptr, &error);
    g_object_unref(socket_address);
    g_object_unref(client);
    if (error) {
        g_error_free(error);
        error = nullptr;
    }
    if (!stream)
        return {};
    g_socket_set_timeout(g_socket_connection_get_socket(stream), 1);
    Connection connection(g_dbus_connection_new_sync(
        G_IO_STREAM(stream), nullptr,
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error));
    // Authentication is bounded; established bus reads must survive human
    // authorization. The finite request owns cancellation and its deadline.
    g_socket_set_timeout(g_socket_connection_get_socket(stream), 0);
    g_object_unref(stream);
    if (error)
        g_error_free(error);
    if (connection)
        g_dbus_connection_set_exit_on_close(connection.get(), FALSE);
    return connection;
}

struct Response {
    bool received = false;
    Variant body;
};
void response(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
              GVariant *body, gpointer state) {
    auto &result = *static_cast<Response *>(state);
    // Callback on the caller-owned GMainContext only performs bounded admission.
    if (!result.received) {
        if (g_variant_is_of_type(body, G_VARIANT_TYPE("(ua{sv})")) &&
            g_variant_get_size(body) <= 4096)
            result.body.reset(g_variant_ref(body));
        result.received = true;
    }
}
Variant call(GDBusConnection *connection, const char *path, const char *interface,
             const char *method, GVariant *args, const GVariantType *type,
             const char *destination = service, std::string *failure_code = nullptr) {
    GError *error = nullptr;
    Variant result(g_dbus_connection_call_sync(connection, destination, path, interface, method,
                                               args, type, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr,
                                               &error));
    if (error && failure_code) {
        gchar *remote = g_dbus_error_get_remote_error(error);
        *failure_code = g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED) ||
                                (remote && std::string_view(remote) ==
                                               "org.freedesktop.portal.Error.NotAllowed")
                            ? "portal_permission_denied"
                            : "portal_request_failed";
        g_free(remote);
    }
    if (error)
        g_error_free(error);
    return result;
}
void close_request(GDBusConnection *connection, const std::string &path, const char *sender) {
    (void)call(connection, path.c_str(), request_interface, "Close", nullptr, G_VARIANT_TYPE_UNIT,
               sender);
}

#ifdef MIRAGE_LINUX_PIPEWIRE
constexpr auto remote_desktop_interface = "org.freedesktop.portal.RemoteDesktop";
constexpr auto screencast_interface = "org.freedesktop.portal.ScreenCast";
constexpr auto session_interface = "org.freedesktop.portal.Session";
std::string random_token() {
    gchar *value = g_uuid_string_random();
    std::string token = "mirage_" + std::string(value);
    g_free(value);
    std::replace(token.begin(), token.end(), '-', '_');
    return token;
}
struct PortalResult {
    Variant values;
    std::string error;
};
PortalResult session_request(GDBusConnection *connection, const char *sender,
                             const std::string &identity, const char *method,
                             const std::string &session, GVariantBuilder &options,
                             GMainContext *context, const desktop::CancelToken &cancel,
                             const char *interface = screencast_interface) {
    if (cancel.cancelled()) {
        g_variant_builder_clear(&options);
        return {{}, "cancelled"};
    }
    const auto token = random_token();
    const auto path = std::string(root) + "/request/" + identity + "/" + token;
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
    Response received;
    const auto id = g_dbus_connection_signal_subscribe(
        connection, sender, request_interface, "Response", path.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, response, &received, nullptr);
    struct Subscription {
        GDBusConnection *connection;
        guint id;
        ~Subscription() { g_dbus_connection_signal_unsubscribe(connection, id); }
    } subscription{connection, id};
    GVariant *args = std::string_view(method) == "CreateSession"
                         ? g_variant_new("(a{sv})", &options)
                     : std::string_view(method) == "Start"
                         ? g_variant_new("(osa{sv})", session.c_str(), "", &options)
                         : g_variant_new("(oa{sv})", session.c_str(), &options);
    std::string error = "portal_request_failed";
    auto reply =
        call(connection, root, interface, method, args, G_VARIANT_TYPE("(o)"), sender, &error);
    const gchar *returned = nullptr;
    if (reply)
        g_variant_get(reply.get(), "(&o)", &returned);
    if (!reply || path != returned) {
        close_request(connection, path, sender);
        if (returned &&
            std::string_view(returned).starts_with(std::string(root) + "/request/" + identity +
                                                   "/") &&
            std::char_traits<char>::length(returned) <= 512)
            close_request(connection, returned, sender);
        return {{}, reply ? "portal_protocol_error" : error};
    }
    while (!received.received && !cancel.cancelled() && !g_dbus_connection_is_closed(connection)) {
        (void)g_main_context_iteration(context, FALSE);
        if (!received.received)
            (void)::poll(nullptr, 0, 25);
    }
    if (cancel.cancelled() || !received.received) {
        close_request(connection, path, sender);
        return {{}, cancel.cancelled() ? "cancelled" : "portal_unavailable"};
    }
    if (!received.body)
        return {{}, "portal_protocol_error"};
    guint32 code = 2;
    GVariant *values = nullptr;
    g_variant_get(received.body.get(), "(u@a{sv})", &code, &values);
    Variant data(values);
    if (code)
        return {{},
                code == 1   ? "portal_user_cancelled"
                : code == 2 ? "portal_failed"
                            : "portal_protocol_error"};
    return {std::move(data), {}};
}
void session_closed(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
                    GVariant *, gpointer data) {
    *static_cast<bool *>(data) = true;
}
// DEC-074: no background loop. The fixed state belongs to one serialized
// task and its Closed callback is driven only inside that task's capture.
struct CastSession {
    Connection connection;
    std::string sender, identity, path;
    GMainContext *context = g_main_context_new();
    bool closed = false;
    guint subscription = 0;
    guint32 node = 0, devices = 0;
    int logical_width = 0, logical_height = 0, frame_width = 0, frame_height = 0;
    bool interaction = false;
    std::unique_ptr<PortalPipeWireStream> stream;
    ~CastSession() {
        stream.reset();
        if (!path.empty())
            (void)call(connection.get(), path.c_str(), session_interface, "Close", nullptr,
                       G_VARIANT_TYPE_UNIT, sender.c_str());
        if (subscription)
            g_dbus_connection_signal_unsubscribe(connection.get(), subscription);
        g_main_context_unref(context);
    }
};
struct ContextScope {
    GMainContext *value;
    explicit ContextScope(GMainContext *v) : value(v) { g_main_context_push_thread_default(value); }
    ~ContextScope() { g_main_context_pop_thread_default(value); }
};
desktop::CaptureOutcome capture_cast_frame(CastSession &state, const desktop::CaptureLimits &limits,
                                           const desktop::CancelToken &cancel,
                                           std::chrono::steady_clock::time_point deadline,
                                           std::shared_ptr<PresentationContext> presentation = {},
                                           const desktop::CapturePresentation &acquire = {}) {
    ContextScope context(state.context);
    const desktop::CancelToken bounded([&] {
        (void)g_main_context_iteration(state.context, FALSE);
        return cancel.cancelled() || state.closed ||
               g_dbus_connection_is_closed(state.connection.get()) ||
               std::chrono::steady_clock::now() >= deadline;
    });
    const char *stage = "remote";
    auto rejected = [&](const std::string &code, const std::string &detail = "") {
        const auto effective = cancel.cancelled() ? "cancelled"
                               : state.closed     ? "portal_session_closed"
                               : g_dbus_connection_is_closed(state.connection.get())
                                   ? "portal_unavailable"
                               : std::chrono::steady_clock::now() >= deadline ? "portal_timeout"
                                                                              : code;
        return failure(effective, std::string("ScreenCast capture failed; stage=") + stage + "; " +
                                      detail + "; session closed, no reauthorization");
    };
    if (bounded.cancelled())
        return rejected("cancelled");
    if (state.stream) {
        stage = "stream";
        auto frame = desktop::capture_with_presentation(acquire, bounded, [&] {
            return state.stream->capture(limits, bounded,
                                         presentation ? presentation->not_before_ns.load() : 0);
        });
        if (!frame.ok || bounded.cancelled())
            return rejected(frame.error.code, frame.error.message);
        state.frame_width = frame.frame.width;
        state.frame_height = frame.frame.height;
        return frame;
    }
    auto *connection = state.connection.get();
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GUnixFDList *fds = nullptr;
    GError *error = nullptr;
    Variant remote(g_dbus_connection_call_with_unix_fd_list_sync(
        connection, state.sender.c_str(), root, screencast_interface, "OpenPipeWireRemote",
        g_variant_new("(oa{sv})", state.path.c_str(), &options), G_VARIANT_TYPE("(h)"),
        G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &fds, nullptr, &error));
    if (error)
        g_error_free(error);
    struct Fds {
        GUnixFDList *value;
        ~Fds() {
            if (value)
                g_object_unref(value);
        }
    } owned_fds{fds};
    gint index = -1;
    if (remote)
        g_variant_get(remote.get(), "(h)", &index);
    const int fd = fds && index >= 0 && index < g_unix_fd_list_get_length(fds)
                       ? g_unix_fd_list_get(fds, index, nullptr)
                       : -1;
    if (fd < 0)
        return rejected("portal_remote_failed");
    stage = "stream";
    state.stream = std::make_unique<PortalPipeWireStream>(fd, state.node);
    // Negotiate/prime the stream while the pet is visible. These pixels are
    // never published; the subsequent capture applies the post-hide fence.
    if (acquire) {
        const auto primed = state.stream->capture(limits, bounded);
        if (!primed.ok || bounded.cancelled())
            return rejected(primed.error.code, primed.error.message);
    }
    auto frame = desktop::capture_with_presentation(acquire, bounded, [&] {
        return state.stream->capture(limits, bounded,
                                     presentation ? presentation->not_before_ns.load() : 0);
    });
    if (!frame.ok || bounded.cancelled())
        return rejected(frame.error.code, frame.error.message);
    // A live stream changing pixel extent invalidates input rather than silently
    // applying an old image's coordinate transform.
    state.frame_width = frame.frame.width;
    state.frame_height = frame.frame.height;
    return frame;
}
desktop::CaptureOutcome
screencast(Connection connection, const char *sender, const std::string &identity,
           const desktop::CaptureLimits &limits, const desktop::CancelToken &cancel,
           std::chrono::steady_clock::time_point deadline, std::unique_ptr<CastSession> *retained,
           bool interaction = false, PortalAuthorization *authorization = nullptr,
           const std::string &restore_token = {}, bool authorization_only = false,
           std::shared_ptr<PresentationContext> presentation = {},
           const desktop::CapturePresentation &acquire = {}) {
    auto state = std::make_unique<CastSession>();
    state->connection = std::move(connection);
    state->sender = sender;
    state->identity = identity;
    state->interaction = interaction;
    ContextScope owned_context(state->context);
    const auto token = random_token();
    state->path = std::string(root) + "/session/" + identity + "/" + token;
    state->subscription = g_dbus_connection_signal_subscribe(
        state->connection.get(), state->sender.c_str(), session_interface, "Closed",
        state->path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, session_closed, &state->closed,
        nullptr);
    const desktop::CancelToken bounded([&] {
        (void)g_main_context_iteration(state->context, FALSE);
        return cancel.cancelled() || state->closed ||
               g_dbus_connection_is_closed(state->connection.get()) ||
               std::chrono::steady_clock::now() >= deadline;
    });
    const char *stage = "create";
    auto rejected = [&](const std::string &code, const std::string &detail = "") {
        const auto effective = cancel.cancelled() ? "cancelled"
                               : state->closed    ? "portal_session_closed"
                               : g_dbus_connection_is_closed(state->connection.get())
                                   ? "portal_unavailable"
                               : std::chrono::steady_clock::now() >= deadline ? "portal_timeout"
                                                                              : code;
        return failure(
            effective,
            std::string(interaction ? "RemoteDesktop" : "ScreenCast") +
                " request failed; stage=" + stage + "; " + detail +
                "; session closed; check desktop unlock and system sharing authorization");
    };
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "session_handle_token",
                          g_variant_new_string(token.c_str()));
    auto created = session_request(state->connection.get(), state->sender.c_str(), identity,
                                   "CreateSession", {}, options, state->context, bounded,
                                   interaction ? remote_desktop_interface : screencast_interface);
    if (!created.values)
        return rejected(created.error);
    const gchar *returned = nullptr;
    if (!g_variant_lookup(created.values.get(), "session_handle", "&s", &returned) ||
        state->path != returned) {
        if (returned &&
            std::string_view(returned).starts_with(std::string(root) + "/session/" + identity +
                                                   "/") &&
            std::char_traits<char>::length(returned) <= 512)
            (void)call(state->connection.get(), returned, session_interface, "Close", nullptr,
                       G_VARIANT_TYPE_UNIT, state->sender.c_str());
        return rejected("portal_protocol_error");
    }
    if (interaction) {
        stage = "devices";
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(3));
        if (authorization) {
            g_variant_builder_add(&options, "{sv}", "persist_mode", g_variant_new_uint32(2));
            if (!restore_token.empty())
                g_variant_builder_add(&options, "{sv}", "restore_token",
                                      g_variant_new_string(restore_token.c_str()));
        }
        auto devices = session_request(state->connection.get(), state->sender.c_str(), identity,
                                       "SelectDevices", state->path, options, state->context,
                                       bounded, remote_desktop_interface);
        if (!devices.values)
            return rejected(devices.error);
    }
    stage = "sources";
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(1));
    g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(FALSE));
    auto selected = session_request(state->connection.get(), state->sender.c_str(), identity,
                                    "SelectSources", state->path, options, state->context, bounded);
    if (!selected.values)
        return rejected(selected.error);
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    stage = "start";
    auto started = session_request(state->connection.get(), state->sender.c_str(), identity,
                                   "Start", state->path, options, state->context, bounded,
                                   interaction ? remote_desktop_interface : screencast_interface);
    if (!started.values)
        return rejected(started.error);
    Variant streams(
        g_variant_lookup_value(started.values.get(), "streams", G_VARIANT_TYPE("a(ua{sv})")));
    if (!streams || g_variant_n_children(streams.get()) != 1)
        return rejected("portal_protocol_error");
    GVariant *properties = nullptr;
    g_variant_get_child(streams.get(), 0, "(u@a{sv})", &state->node, &properties);
    Variant stream_properties(properties);
    if (interaction) {
        if (!g_variant_lookup(started.values.get(), "devices", "u", &state->devices) ||
            (state->devices & 3) != 3)
            return rejected("portal_input_denied",
                            "granted_device_types=" + std::to_string(state->devices) +
                                "; required_device_types=3");
        if (!g_variant_lookup(properties, "size", "(ii)", &state->logical_width,
                              &state->logical_height) ||
            state->logical_width <= 0 || state->logical_height <= 0 ||
            state->logical_width > 16384 || state->logical_height > 16384)
            return rejected("portal_input_mapping_unavailable");
    }
    if (!state->node || state->node == UINT32_MAX || bounded.cancelled())
        return rejected(state->node && state->node != UINT32_MAX ? "cancelled"
                                                                 : "portal_protocol_error");
    if (authorization && interaction) {
        const gchar *new_token = nullptr;
        if (!g_variant_lookup(started.values.get(), "restore_token", "&s", &new_token) ||
            bounded.cancelled())
            return rejected(bounded.cancelled() ? "cancelled" : "persistence_not_granted");
        const auto saved = authorization->save(new_token);
        if (!saved.code.empty())
            return rejected(saved.code); // Never copy tokens into diagnostics.
    }
    if (authorization_only) {
        desktop::CaptureOutcome out;
        out.ok = true; // Authorization needs no PipeWire frame.
        return out;
    }
    auto frame = capture_cast_frame(*state, limits, cancel,
                                    std::chrono::steady_clock::now() + std::chrono::seconds(5),
                                    presentation, acquire);
    if (frame.ok && retained)
        *retained = std::move(state);
    return frame;
}

desktop::InputOutcome shared_input(CastSession &s, const desktop::SharedInputAction &action,
                                   const desktop::InputLimits &limits,
                                   const desktop::CancelToken &cancel) {
    ContextScope context(s.context);
    PortalInputContext input{
        s.devices, s.logical_width, s.logical_height, s.frame_width, s.frame_height, {}, {}};
    input.stopped = [&] {
        (void)g_main_context_iteration(s.context, FALSE);
        return s.closed || g_dbus_connection_is_closed(s.connection.get());
    };
    input.notify = [&](const PortalInputEvent &event) {
        const char *method = nullptr;
        GVariant *args = nullptr;
        using Kind = PortalInputEvent::Kind;
        switch (event.kind) {
        case Kind::Motion:
            method = "NotifyPointerMotionAbsolute";
            args = g_variant_new("(oa{sv}udd)", s.path.c_str(), nullptr, s.node, event.x, event.y);
            break;
        case Kind::Key:
        case Kind::Button:
            method = event.kind == Kind::Key ? "NotifyKeyboardKeysym" : "NotifyPointerButton";
            args = g_variant_new("(oa{sv}iu)", s.path.c_str(), nullptr, event.value, event.state);
            break;
        case Kind::Axis:
            method = "NotifyPointerAxisDiscrete";
            args = g_variant_new("(oa{sv}ui)", s.path.c_str(), nullptr, event.state, event.value);
            break;
        }
        const bool accepted = bool(call(s.connection.get(), root, remote_desktop_interface, method,
                                        args, G_VARIANT_TYPE_UNIT, s.sender.c_str()));
        // A Portal reply acknowledges dispatch, not delivery to the focused
        // client. Keep keys bounded and paced on this same Executor task so
        // compositor/IME processing can consume each complete transition.
        if (accepted && event.kind == Kind::Key)
            (void)::poll(nullptr, 0, 10);
        return accepted;
    };
    auto out = dispatch_portal_input(input, action, limits, cancel);
    if (!out.ok && out.side_effect_may_have_occurred)
        s.closed = true;
    return out;
}
#endif

#endif
} // namespace

PortalScreenBackend::PortalScreenBackend(std::shared_ptr<PortalAuthorization> authorization,
                                         std::shared_ptr<PresentationContext> presentation)
    : authorization_(authorization ? std::move(authorization)
                                   : std::make_shared<PortalAuthorization>()),
      presentation_(std::move(presentation)) {
#ifdef MIRAGE_LINUX_PIPEWIRE
    pw_init(nullptr, nullptr);
#endif
}
PortalScreenBackend::~PortalScreenBackend() {
#ifdef MIRAGE_LINUX_PIPEWIRE
    pw_deinit();
#endif
}

std::unique_ptr<PortalScreenBackend>
PortalScreenBackend::open(std::shared_ptr<PresentationContext> presentation) {
#ifdef MIRAGE_LINUX_PORTAL
    if (std::getenv("DBUS_SESSION_BUS_ADDRESS"))
        return std::make_unique<PortalScreenBackend>(nullptr, std::move(presentation));
#endif
    (void)presentation;
    return {};
}

namespace {
struct NativeObservation {
    bool attempted = false;
    bool interaction = false;
    bool authorization_only = false;
    std::shared_ptr<PortalAuthorization> authorization;
    std::shared_ptr<PresentationContext> presentation;
    desktop::CapturePresentation acquire;
#ifdef MIRAGE_LINUX_PIPEWIRE
    std::unique_ptr<CastSession> cast;
#endif
};

desktop::CaptureOutcome capture_portal(const desktop::CaptureLimits &limits,
                                       const desktop::CancelToken &cancel,
                                       [[maybe_unused]] NativeObservation *retained) {
    if (cancel.cancelled())
        return failure("cancelled", "Portal capture cancelled before request");
    if (!limits.max_bytes)
        return failure("invalid_argument", "Portal capture requires a positive pixel budget");
#ifdef MIRAGE_LINUX_PORTAL
#ifdef MIRAGE_LINUX_PIPEWIRE
    if (retained && retained->cast) {
        auto frame = capture_cast_frame(*retained->cast, limits, cancel,
                                        std::chrono::steady_clock::now() + std::chrono::seconds(5),
                                        retained->presentation, retained->acquire);
        if (!frame.ok)
            retained->cast.reset();
        return frame;
    }
#endif
    if (retained && retained->attempted)
        return failure("portal_session_closed",
                       "No reusable shared session; no reauthorization in this task");
#ifdef MIRAGE_LINUX_PIPEWIRE
    if (retained)
        retained->attempted = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto connection = connect_bus();
    if (!connection)
        return failure("portal_unavailable", "Wayland capture needs a local session Portal bus");
    if (cancel.cancelled())
        return failure("cancelled", "Portal capture cancelled before authorization");
    // Resolve and pin the unique service owner, rejecting cross-owner replies.
    GError *bus_error = nullptr;
    Variant owner(g_dbus_connection_call_sync(
        connection.get(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "GetNameOwner", g_variant_new("(s)", service), G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &bus_error));
    if (bus_error)
        g_error_free(bus_error);
    if (!owner && !cancel.cancelled()) {
        Variant activated(g_dbus_connection_call_sync(
            connection.get(), "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "StartServiceByName", g_variant_new("(su)", service, 0u),
            G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr));
        if (activated)
            owner.reset(g_dbus_connection_call_sync(
                connection.get(), "org.freedesktop.DBus", "/org/freedesktop/DBus",
                "org.freedesktop.DBus", "GetNameOwner", g_variant_new("(s)", service),
                G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr));
    }
    if (!owner)
        return failure("portal_unavailable", "Desktop portal is not running in this session");
    const gchar *sender = nullptr;
    g_variant_get(owner.get(), "(&s)", &sender);
    const gchar *unique = g_dbus_connection_get_unique_name(connection.get());
    std::string identity = unique ? unique + 1 : "";
    std::replace(identity.begin(), identity.end(), '.', '_');
    auto *authorization = retained && retained->authorization && retained->authorization->enabled()
                              ? retained->authorization.get()
                              : nullptr;
    struct Rotation {
        PortalAuthorization *state = nullptr;
        ~Rotation() {
            if (state)
                state->finish();
        }
    } rotation;
    std::string restore_token;
    if (authorization) {
        if (!authorization->begin())
            return failure("desktop_authorization_busy",
                           "Another desktop authorization is in progress");
        rotation.state = authorization;
        const auto consumed = authorization->consume(restore_token);
        if (!consumed.code.empty())
            return failure(consumed.code, consumed.message);
    }
    const bool remote = retained && (retained->interaction || !restore_token.empty());
    if (authorization && remote) {
        const auto property = call(connection.get(), root, "org.freedesktop.DBus.Properties", "Get",
                                   g_variant_new("(ss)", remote_desktop_interface, "version"),
                                   G_VARIANT_TYPE("(v)"), sender);
        Variant boxed(property ? g_variant_get_child_value(property.get(), 0) : nullptr);
        Variant version(boxed ? g_variant_get_variant(boxed.get()) : nullptr);
        if (!version || !g_variant_is_of_type(version.get(), G_VARIANT_TYPE_UINT32) ||
            g_variant_get_uint32(version.get()) < 2)
            return failure("persistence_unavailable",
                           "System RemoteDesktop v2 is required to remember authorization");
    }
    // Choose the transport before any authorization UI. Once ScreenCast is
    // available, failures never trigger a second Screenshot prompt.
    std::string capability_error = "portal_request_failed";
    auto capability = call(connection.get(), root, "org.freedesktop.DBus.Properties", "Get",
                           g_variant_new("(ss)", screencast_interface, "version"),
                           G_VARIANT_TYPE("(v)"), sender, &capability_error);
    if (capability) {
        Variant boxed(g_variant_get_child_value(capability.get(), 0));
        Variant version(g_variant_get_variant(boxed.get()));
        if (g_variant_is_of_type(version.get(), G_VARIANT_TYPE_UINT32) &&
            g_variant_get_uint32(version.get()) >= 1)
            return screencast(std::move(connection), sender, identity, limits, cancel, deadline,
                              retained ? &retained->cast : nullptr, remote,
                              authorization && remote ? authorization : nullptr, restore_token,
                              retained && retained->authorization_only,
                              retained ? retained->presentation : nullptr,
                              retained ? retained->acquire : desktop::CapturePresentation{});
        return failure("portal_protocol_error",
                       "ScreenCast capability returned an invalid version");
    }
    // A failed property lookup alone is not proof of absent capability. Some
    // GDBus implementations report missing interfaces as localized InvalidArgs.
    // Verify absence through bounded typed introspection before selecting fallback.
    auto introspection = call(connection.get(), root, "org.freedesktop.DBus.Introspectable",
                              "Introspect", nullptr, G_VARIANT_TYPE("(s)"), sender);
    const gchar *xml = nullptr;
    if (introspection && g_variant_get_size(introspection.get()) <= 65536)
        g_variant_get(introspection.get(), "(&s)", &xml);
    GDBusNodeInfo *node = xml ? g_dbus_node_info_new_for_xml(xml, nullptr) : nullptr;
    const bool absent = node && !g_dbus_node_info_lookup_interface(node, screencast_interface);
    if (node)
        g_dbus_node_info_unref(node);
    if (!absent || capability_error == "portal_permission_denied")
        return failure(
            capability_error,
            "ScreenCast capability could not be checked; no second authorization requested");
    // The session portal verifiably lacks ScreenCast: no reusable authorized
    // observation exists in this environment.
    return failure("unsupported_capability",
                   "This desktop session's portal does not provide ScreenCast; no reusable "
                   "authorized observation is available");
#endif
    // Compiled without the PipeWire path: there is no reusable authorized
    // observation, and the interactive Screenshot UI is not an autonomous
    // observation path — it covers the user's desktop and returns an image
    // with no coordinate mapping. Fail closed instead (DEC-015); see
    // docs/compatibility/wayland-capture-failclosed-20261014.md.
    return failure("unsupported_capability",
                   "Wayland ScreenCast/PipeWire capture is not compiled into this build; "
                   "install the libpipewire-0.3 development files or set "
                   "MIRAGE_PIPEWIRE_SDK_ROOT and rebuild");
#else
    return failure("unsupported_platform", "Wayland Portal support was not compiled");
#endif
}
class PortalObservation final : public desktop::DesktopCaptureSession {
  public:
    explicit PortalObservation(bool interaction = false,
                               std::shared_ptr<PortalAuthorization> authorization = {},
                               std::shared_ptr<PresentationContext> presentation = {}) {
        state_.interaction = interaction;
        state_.authorization = std::move(authorization);
        state_.presentation = std::move(presentation);
        generation_ = state_.authorization ? state_.authorization->generation() : 0;
    }
    bool revoked() const {
        return state_.authorization && generation_ != state_.authorization->generation();
    }
    bool input_supported() const override {
#ifdef MIRAGE_LINUX_PIPEWIRE
        return state_.interaction && reusable() && state_.cast->interaction &&
               (state_.cast->devices & 3) == 3;
#else
        return false;
#endif
    }
    desktop::InputOutcome input(const desktop::SharedInputAction &action,
                                const desktop::InputLimits &limits,
                                const desktop::CancelToken &cancel) override {
#ifdef MIRAGE_LINUX_PIPEWIRE
        if (revoked()) {
            state_.cast.reset();
            return {false,
                    false,
                    {"desktop_authorization_changed", "Desktop authorization changed"},
                    false,
                    true};
        }
        if (input_supported()) {
            const desktop::CancelToken bounded([&] { return cancel.cancelled() || revoked(); });
            auto out = shared_input(*state_.cast, action, limits, bounded);
            if (out.side_effect_may_have_occurred && !out.ok) {
                failed_ = true;
                state_.cast.reset();
            }
            return out;
        }
#endif
        (void)action;
        (void)limits;
        (void)cancel;
        return {
            false, false, {"unsupported_capability", "Shared input not authorized"}, false, true};
    }
    desktop::CaptureOutcome capture(const desktop::CaptureLimits &limits,
                                    const desktop::CancelToken &cancel) override {
        if (failed_)
            return failure("portal_session_closed", "Sharing session ended; no reauthorization");
        const desktop::CancelToken bounded([&] { return cancel.cancelled() || revoked(); });
        auto out = capture_portal(limits, bounded, &state_);
        if (revoked())
            out = failure("desktop_authorization_changed",
                          "Desktop authorization changed; start a new task");
        failed_ = !out.ok;
#ifdef MIRAGE_LINUX_PIPEWIRE
        if (!out.ok)
            state_.cast.reset();
#endif
        return out;
    }
    desktop::CaptureOutcome
    capture_presented(const desktop::CaptureLimits &limits, const desktop::CancelToken &cancel,
                      const desktop::CapturePresentation &acquire) override {
        // Serialized task owner; no callback survives this finite invocation.
        state_.acquire = acquire;
        struct Reset {
            desktop::CapturePresentation &value;
            ~Reset() { value = {}; }
        } reset{state_.acquire};
        return capture(limits, cancel);
    }
    bool reusable() const override {
#ifdef MIRAGE_LINUX_PIPEWIRE
        return !revoked() && state_.cast && !state_.cast->closed;
#else
        return false;
#endif
    }

  private:
    NativeObservation state_;
    std::uint64_t generation_ = 0;
    bool failed_ = false;
};
} // namespace
std::unique_ptr<desktop::DesktopCaptureSession>
PortalScreenBackend::create_desktop_capture_session() {
    return std::make_unique<PortalObservation>(false, authorization_, presentation_);
}
bool PortalScreenBackend::desktop_interaction_supported() const {
#ifdef MIRAGE_LINUX_PIPEWIRE
    return true;
#else
    return false;
#endif
}
std::unique_ptr<desktop::DesktopCaptureSession>
PortalScreenBackend::create_desktop_interaction_session() {
    return desktop_interaction_supported()
               ? std::make_unique<PortalObservation>(true, authorization_, presentation_)
               : nullptr;
}
void PortalScreenBackend::configure_desktop_authorization(bool remember) {
    authorization_->configure(remember);
}
desktop::DesktopAuthorizationView
PortalScreenBackend::desktop_authorization(const desktop::CancelToken &cancel) {
    if (!desktop_interaction_supported())
        return {};
    if (cancel.cancelled())
        return {true,
                authorization_->enabled(),
                false,
                "cancelled",
                {"cancelled", "Authorization status cancelled"}};
#ifdef MIRAGE_LINUX_PIPEWIRE
    auto connection = connect_bus();
    auto property = connection
                        ? call(connection.get(), root, "org.freedesktop.DBus.Properties", "Get",
                               g_variant_new("(ss)", remote_desktop_interface, "version"),
                               G_VARIANT_TYPE("(v)"))
                        : Variant{};
    Variant boxed(property ? g_variant_get_child_value(property.get(), 0) : nullptr);
    Variant version(boxed ? g_variant_get_variant(boxed.get()) : nullptr);
    if (!version || !g_variant_is_of_type(version.get(), G_VARIANT_TYPE_UINT32) ||
        g_variant_get_uint32(version.get()) < 2)
        return {false,
                authorization_->enabled(),
                false,
                "unsupported",
                {"persistence_unavailable", "系统不支持保留桌面授权。"}};
    return authorization_->view();
#else
    return {};
#endif
}
desktop::ProviderError
PortalScreenBackend::manage_desktop_authorization(bool remember,
                                                  const desktop::CancelToken &cancel) {
    if (cancel.cancelled())
        return {"cancelled", "Desktop authorization cancelled"};
    if (!remember)
        return authorization_->forget();
    if (!desktop_interaction_supported())
        return {"unsupported_capability", "Native desktop authorization unavailable"};
    authorization_->configure(true);
    const auto generation = authorization_->generation();
    NativeObservation state;
    state.interaction = true;
    state.authorization_only = true;
    state.authorization = authorization_;
    const desktop::CancelToken bounded(
        [&] { return cancel.cancelled() || generation != authorization_->generation(); });
    const auto out = capture_portal({32u << 20}, bounded, &state);
    return out.ok ? desktop::ProviderError{} : out.error;
}
desktop::CaptureOutcome PortalScreenBackend::capture_desktop(const desktop::CaptureLimits &limits,
                                                             const desktop::CancelToken &cancel) {
    NativeObservation state;
    state.presentation = presentation_;
    return capture_portal(limits, cancel, &state);
}

desktop::DisplayListOutcome PortalScreenBackend::list_displays(const desktop::DisplayListLimits &,
                                                               const desktop::CancelToken &) {
    return {false,
            {},
            {"unsupported_capability", "Use capture source=desktop; Portal observation "
                                       "has no executable display coordinates"}};
}
desktop::CaptureOutcome PortalScreenBackend::capture_display(const std::string &,
                                                             const desktop::CaptureLimits &,
                                                             const desktop::CancelToken &) {
    return failure("unsupported_capability", "Use capture source=desktop on Wayland");
}
desktop::CaptureOutcome PortalScreenBackend::capture_window(const std::string &,
                                                            const desktop::CaptureLimits &,
                                                            const desktop::CancelToken &) {
    return failure("unsupported_capability", "Wayland window capture is not available");
}
desktop::CaptureOutcome PortalScreenBackend::capture_roi(const desktop::WindowGeometry &,
                                                         const desktop::CaptureLimits &,
                                                         const desktop::CancelToken &) {
    return failure("unsupported_capability", "Wayland ROI needs an authorized mapped stream");
}
} // namespace mirage::platform::linux_backend
