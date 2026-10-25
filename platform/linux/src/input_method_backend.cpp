#include "input_method_backend.hpp"

#ifdef MIRAGE_LINUX_GIO
#include <gio/gio.h>
#ifdef MIRAGE_LINUX_IBUS_DISCOVERY
#include <ibus.h>
#endif
#include <algorithm>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <mirage/desktop/input_provider.hpp>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace mirage::platform::linux_backend {
namespace {
using namespace desktop;
using Clock = std::chrono::steady_clock;

template <typename T, void (*Release)(T *)> struct Delete {
    void operator()(T *value) const {
        if (value)
            Release(value);
    }
};
using Variant = std::unique_ptr<GVariant, Delete<GVariant, g_variant_unref>>;
struct ObjectDelete {
    template <typename T> void operator()(T *value) const {
        if (value)
            g_object_unref(value);
    }
};
template <typename T> using Object = std::unique_ptr<T, ObjectDelete>;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
void known(InputStateField &field, std::string value, std::string source) {
    field = {std::move(value), std::move(source), now_ms(), {}};
}
void unavailable(InputMethodState &state, const char *reason) {
    for (auto *field : {&state.engine, &state.activation, &state.conversion_mode, &state.layout,
                        &state.focus_context, &state.focused_window, &state.preedit_active,
                        &state.candidates_visible}) {
        *field = {std::nullopt, {}, now_ms(), reason};
    }
}

/// One finite platform continuation on the existing Executor caller. No GLib
/// default-loop dispatch, private task queue, thread, timer or cached state.
/// GCancellable closes connection/round-trip waits; consume completion before
/// destroying callback storage even on cancellation/timeout.
class Query {
  public:
    Query(Clock::time_point deadline, const CancelToken &cancel)
        : deadline_(deadline), cancel_(cancel), context_(g_main_context_new()),
          stop_(g_cancellable_new()) {
        g_main_context_push_thread_default(context_);
    }
    ~Query() {
        g_main_context_pop_thread_default(context_);
        g_main_context_unref(context_);
    }
    bool stopped() const { return cancel_.cancelled() || Clock::now() >= deadline_; }
    template <typename Start, typename Finish> auto run(Start start, Finish finish) {
        struct Completion {
            GAsyncResult *result = nullptr;
        } completion;
        if (stopped())
            g_cancellable_cancel(stop_.get());
        start(
            +[](GObject *, GAsyncResult *result, gpointer data) {
                static_cast<Completion *>(data)->result = G_ASYNC_RESULT(g_object_ref(result));
            },
            &completion);
        while (!completion.result) {
            if (stopped())
                g_cancellable_cancel(stop_.get());
            (void)g_main_context_iteration(context_, false);
            if (!completion.result)
                (void)::poll(nullptr, 0, 4);
        }
        Object<GAsyncResult> result(completion.result);
        GError *error = nullptr;
        auto value = finish(result.get(), &error);
        if (error)
            g_error_free(error); // Never expose bus addresses or remote messages.
        return value;
    }
    Object<GDBusConnection> connect(const std::string &address) {
        if (stopped() || address.empty() || address.size() > 4096 ||
            !address.starts_with("unix:") || address.find(';') != std::string::npos)
            return {};
        return Object<GDBusConnection>(run(
            [&](GAsyncReadyCallback cb, gpointer data) {
                g_dbus_connection_new_for_address(
                    address.c_str(),
                    static_cast<GDBusConnectionFlags>(
                        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                        G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                    nullptr, stop_.get(), cb, data);
            },
            [](GAsyncResult *result, GError **error) {
                return g_dbus_connection_new_for_address_finish(result, error);
            }));
    }
    Variant call(GDBusConnection *bus, const char *dest, const char *path, const char *iface,
                 const char *method, GVariant *args, const GVariantType *type) {
        // Sink arguments even when the total deadline has already expired.
        Variant parameters(args ? g_variant_ref_sink(args) : nullptr);
        if (stopped())
            return {};
        return Variant(run(
            [&](GAsyncReadyCallback cb, gpointer data) {
                g_dbus_connection_call(bus, dest, path, iface, method, parameters.get(), type,
                                       G_DBUS_CALL_FLAGS_NO_AUTO_START, 200, stop_.get(), cb, data);
            },
            [bus](GAsyncResult *result, GError **error) {
                return g_dbus_connection_call_finish(bus, result, error);
            }));
    }
    std::string owner(GDBusConnection *bus, const char *name) {
        auto reply =
            call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                 "GetNameOwner", g_variant_new("(s)", name), G_VARIANT_TYPE("(s)"));
        if (!reply)
            return {};
        const char *value = nullptr;
        g_variant_get(reply.get(), "(&s)", &value);
        return value;
    }

  private:
    Clock::time_point deadline_;
    const CancelToken &cancel_;
    GMainContext *context_;
    Object<GCancellable> stop_;
};

std::string string_value(GVariant *value) {
    if (!value || !(g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) ||
                    g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH)))
        return {};
    gsize size = 0;
    const char *text = g_variant_get_string(value, &size);
    if (size == 0 || size > 256 || !is_valid_utf8(text))
        return {};
    return {text, size};
}

/// Read only the SDK-selected address file, with a strict size and regular-file
/// check. Unlike ibus_get_address(), no mutable process-global address cache or
/// unbounded fgets loop is used. Never scan unrelated login/session files.
std::string ibus_address(const std::string &path) {
    if (const char *value = g_getenv("IBUS_ADDRESS"))
        return std::strlen(value) <= 4096 ? value : "";
    if (path.empty())
        return {};
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return {};
    struct stat status {};
    char bytes[8193];
    ssize_t size = -1;
    if (::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) && status.st_size <= 8192)
        size = ::read(fd, bytes, sizeof(bytes));
    ::close(fd);
    if (size <= 0 || size > 8192)
        return {};
    std::string_view remaining(bytes, static_cast<std::size_t>(size));
    std::string address;
    while (!remaining.empty()) {
        const auto end = remaining.find('\n');
        const auto line = remaining.substr(0, end);
        if (line.starts_with("IBUS_ADDRESS="))
            address = line.substr(13);
        if (end == std::string_view::npos)
            break;
        remaining.remove_prefix(end + 1);
    }
    if (address.find('\0') != std::string::npos)
        return {};
    return address;
}

InputMethodState fcitx(Query &query, GDBusConnection *bus) {
    InputMethodState state;
    state.backend = "fcitx5";
    unavailable(state, "service_unavailable");
    const auto owner = query.owner(bus, "org.fcitx.Fcitx5");
    if (owner.empty())
        return state;
    unavailable(state, "not_observable");
    const auto invoke = [&](const char *method, const GVariantType *type) {
        return query.call(bus, owner.c_str(), "/controller", "org.fcitx.Fcitx.Controller1", method,
                          nullptr, type);
    };
    state.engine.reason = state.activation.reason = "query_failed";
    auto engine = invoke("CurrentInputMethod", G_VARIANT_TYPE("(s)"));
    if (engine) {
        Variant child(g_variant_get_child_value(engine.get(), 0));
        const auto value = string_value(child.get());
        if (!value.empty())
            known(state.engine, value, "fcitx5.CurrentInputMethod");
        else
            state.engine.reason = "invalid_reply";
    }
    auto active = invoke("State", G_VARIANT_TYPE("(i)"));
    if (active) {
        gint value = -1;
        g_variant_get(active.get(), "(i)", &value);
        // State describes the most recent context, not necessarily current
        // application focus. Zero must not be interpreted as disabled.
        if (value >= 0 && value <= 2)
            known(state.activation,
                  value == 2   ? "active"
                  : value == 1 ? "inactive"
                               : "no_context",
                  "fcitx5.State.most_recent_context");
        else
            state.activation.reason = "invalid_reply";
    }
    if (query.owner(bus, "org.fcitx.Fcitx5") != owner)
        unavailable(state, "service_changed");
    return state;
}

Variant property(Query &query, GDBusConnection *bus, const std::string &owner, const char *name) {
    auto reply = query.call(
        bus, owner.c_str(), "/org/freedesktop/IBus", "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.freedesktop.IBus", name), G_VARIANT_TYPE("(v)"));
    if (!reply)
        return {};
    Variant boxed(g_variant_get_child_value(reply.get(), 0));
    return Variant(g_variant_get_variant(boxed.get()));
}

InputMethodState ibus(Query &query, const std::string &address) {
    InputMethodState state;
    state.backend = "ibus";
    unavailable(state, "service_unavailable");
    auto bus = query.connect(address);
    if (!bus)
        return state;
    const auto owner = query.owner(bus.get(), "org.freedesktop.IBus");
    if (owner.empty())
        return state;
    unavailable(state, "not_observable");
    state.engine.reason = state.focus_context.reason = "query_failed";
    auto engine = property(query, bus.get(), owner, "GlobalEngine");
    // GlobalEngine is itself a variant-valued property (a second box).
    if (engine && g_variant_is_of_type(engine.get(), G_VARIANT_TYPE_VARIANT))
        engine = Variant(g_variant_get_variant(engine.get()));
    // IBusEngineDesc wire layout begins (s a{sv} s ...): type name,
    // attachments, engine name. Validate before touching individual children;
    // do not deserialize arbitrary remote object types into a GType registry.
    if (engine && g_variant_is_container(engine.get()) && g_variant_n_children(engine.get()) >= 3 &&
        g_variant_get_size(engine.get()) <= 8192) {
        Variant type(g_variant_get_child_value(engine.get(), 0));
        Variant name(g_variant_get_child_value(engine.get(), 2));
        const auto value = string_value(name.get());
        if (string_value(type.get()) == "IBusEngineDesc" && !value.empty())
            known(state.engine, value, "ibus.GlobalEngine");
        else
            state.engine.reason = "invalid_reply";
    } else if (engine)
        state.engine.reason = "invalid_reply";
    auto focus = property(query, bus.get(), owner, "CurrentInputContext");
    if (focus && g_variant_is_of_type(focus.get(), G_VARIANT_TYPE_OBJECT_PATH)) {
        const auto value = string_value(focus.get());
        if (!value.empty() && value != "/")
            known(state.focus_context, value, "ibus.CurrentInputContext");
        else
            state.focus_context.reason = "no_context";
    } else if (focus)
        state.focus_context.reason = "invalid_reply";
    if (query.owner(bus.get(), "org.freedesktop.IBus") != owner)
        unavailable(state, "service_changed");
    return state;
}
} // namespace

std::unique_ptr<InputMethodBackend> InputMethodBackend::open() {
    auto backend = std::unique_ptr<InputMethodBackend>(new InputMethodBackend());
#ifdef MIRAGE_LINUX_IBUS_DISCOVERY
    // SDK path selection only, on environment construction. It creates no bus,
    // context, watcher or daemon. Per-query addresses are never cached.
    static const std::string socket_path = [] {
        const char *path = ibus_get_socket_path();
        return path ? std::string(path) : std::string{};
    }();
    backend->ibus_address_file_ = socket_path;
#endif
    return backend;
}

desktop::InputMethodOutcome InputMethodBackend::observe(const InputMethodLimits &limits,
                                                        const CancelToken &cancel) {
    if (cancel.cancelled())
        return {false, true, {}, {"cancelled", "input state cancelled"}};
    if (limits.timeout.count() <= 0 || limits.timeout > std::chrono::seconds(2))
        return {false, false, {}, {"invalid_argument", "input state budget must be 1..2000 ms"}};
    Query query(Clock::now() + limits.timeout, cancel);
    InputMethodOutcome result;
    const char *session_address = g_getenv("DBUS_SESSION_BUS_ADDRESS");
    auto bus = query.connect(session_address ? session_address : "");
    if (bus)
        result.input_methods.push_back(fcitx(query, bus.get()));
    else {
        InputMethodState state;
        state.backend = "fcitx5";
        unavailable(state, "service_unavailable");
        result.input_methods.push_back(std::move(state));
    }
    result.input_methods.push_back(ibus(query, ibus_address(ibus_address_file_)));
    if (cancel.cancelled())
        return {false, true, {}, {"cancelled", "input state cancelled"}};
    if (query.stopped())
        return {false, false, {}, {"timeout", "input state budget expired"}};
    result.ok = true;
    return result;
}
} // namespace mirage::platform::linux_backend
#else
namespace mirage::platform::linux_backend {
std::unique_ptr<InputMethodBackend> InputMethodBackend::open() { return nullptr; }
desktop::InputMethodOutcome InputMethodBackend::observe(const desktop::InputMethodLimits &,
                                                        const desktop::CancelToken &) {
    return {false, false, {}, {"unsupported_platform", "GIO input state unavailable"}};
}
} // namespace mirage::platform::linux_backend
#endif
