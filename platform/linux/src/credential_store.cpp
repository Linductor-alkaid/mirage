#include <algorithm>
#include <chrono>
#include <memory>
#include <mirage/platform/credential_store.hpp>
#ifdef MIRAGE_LINUX_GIO
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#endif

namespace mirage::platform {
namespace {
bool valid_reference(const std::string &ref) {
    return ref.size() == 32 && ref.find_first_not_of("0123456789abcdef") == std::string::npos;
}
#ifdef MIRAGE_LINUX_GIO
struct ObjectDelete {
    void operator()(GDBusConnection *v) const {
        if (v)
            g_object_unref(v);
    }
};
struct VariantDelete {
    void operator()(GVariant *v) const {
        if (v)
            g_variant_unref(v);
    }
};
using Variant = std::unique_ptr<GVariant, VariantDelete>;
struct SecretConnection {
    std::unique_ptr<GDBusConnection, ObjectDelete> connection;
    std::string session;
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    SecretConnection() {
        // Use the existing session bus address; no discovery/authentication
        // loop or private dispatch thread is created by this adapter.
        const char *address = g_getenv("DBUS_SESSION_BUS_ADDRESS");
        if (!address)
            return;
        GError *error = nullptr;
        const std::string bus_address(address);
        const bool abstract = bus_address.starts_with("unix:abstract=");
        if (!abstract && !bus_address.starts_with("unix:path="))
            return;
        const auto begin = abstract ? 14U : 10U;
        const auto encoded =
            bus_address.substr(begin, bus_address.find_first_of(",;", begin) - begin);
        gchar *decoded = g_uri_unescape_string(encoded.c_str(), nullptr);
        if (!decoded)
            return;
        GSocketAddress *socket_address = g_unix_socket_address_new_with_type(
            decoded, -1, abstract ? G_UNIX_SOCKET_ADDRESS_ABSTRACT : G_UNIX_SOCKET_ADDRESS_PATH);
        g_free(decoded);
        GSocketClient *client = g_socket_client_new();
        g_socket_client_set_timeout(client, 2);
        GSocketConnection *stream =
            g_socket_client_connect(client, G_SOCKET_CONNECTABLE(socket_address), nullptr, &error);
        g_object_unref(socket_address);
        g_object_unref(client);
        if (error) {
            g_error_free(error);
            error = nullptr;
        }
        if (!stream)
            return;
        g_socket_set_timeout(g_socket_connection_get_socket(stream), 2);
        connection.reset(g_dbus_connection_new_sync(
            G_IO_STREAM(stream), nullptr,
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error));
        g_object_unref(stream);
        if (error)
            g_error_free(error);
    }
    Variant call(const std::string &path, const char *interface, const char *method, GVariant *args,
                 const GVariantType *type) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (!connection || remaining <= 0) {
            if (args)
                g_variant_unref(g_variant_ref_sink(args));
            return {};
        }
        GError *error = nullptr;
        Variant result(g_dbus_connection_call_sync(
            connection.get(), "org.freedesktop.secrets", path.c_str(), interface, method, args,
            type, G_DBUS_CALL_FLAGS_NONE, static_cast<int>(remaining), nullptr, &error));
        if (error)
            g_error_free(error); // Never forward third-party diagnostics containing data.
        return result;
    }
    bool open() {
        auto result =
            call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "OpenSession",
                 g_variant_new("(sv)", "plain", g_variant_new_string("")), G_VARIANT_TYPE("(vo)"));
        if (!result)
            return false;
        const gchar *path = nullptr;
        GVariant *output = nullptr;
        g_variant_get(result.get(), "(@v&o)", &output, &path);
        Variant guard(output);
        session = path;
        return session != "/";
    }
    ~SecretConnection() {
        if (!session.empty())
            (void)call(session, "org.freedesktop.Secret.Session", "Close", nullptr,
                       G_VARIANT_TYPE("()"));
    }
};
GVariant *attributes(const std::string &ref) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_add(&b, "{ss}", "application", "org.mirage.native");
    g_variant_builder_add(&b, "{ss}", "reference", ref.c_str());
    return g_variant_builder_end(&b);
}
CredentialResult unavailable() { return {false, {}, "系统钥匙环不可用或已锁定，请解锁后重试。"}; }
// DEC-071: a missing entry is different from a locked or ambiguous entry.
enum class ItemState { Available, Absent, Locked, Ambiguous, Unavailable };
struct ItemLookup {
    ItemState state = ItemState::Unavailable;
    std::string path;
};
ItemLookup find_item(SecretConnection &c, const std::string &ref) {
    auto r = c.call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "SearchItems",
                    g_variant_new("(@a{ss})", attributes(ref)), G_VARIANT_TYPE("(aoao)"));
    if (!r)
        return {};
    Variant unlocked(g_variant_get_child_value(r.get(), 0));
    Variant locked(g_variant_get_child_value(r.get(), 1));
    const auto readable = g_variant_n_children(unlocked.get());
    const auto sealed = g_variant_n_children(locked.get());
    if (readable + sealed == 0)
        return {ItemState::Absent, {}};
    if (readable + sealed != 1)
        return {ItemState::Ambiguous, {}};
    if (sealed != 0)
        return {ItemState::Locked, {}};
    Variant item(g_variant_get_child_value(unlocked.get(), 0));
    const std::string path = g_variant_get_string(item.get(), nullptr);
    return path == "/" ? ItemLookup{} : ItemLookup{ItemState::Available, path};
}
CredentialResult lookup_failure(ItemState state) {
    if (state == ItemState::Absent)
        return {false, {}, "钥匙环中没有这项 API Key，请为该服务重新保存密钥。"};
    if (state == ItemState::Locked)
        return {false, {}, "API Key 所在钥匙环已锁定，请解锁后重试。"};
    if (state == ItemState::Ambiguous)
        return {false, {}, "钥匙环中有重复凭据，请检查 Mirage 凭据后重试。"};
    return unavailable();
}
ItemLookup persistent_collection(SecretConnection &c) {
    auto alias = c.call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "ReadAlias",
                        g_variant_new("(s)", "default"), G_VARIANT_TYPE("(o)"));
    if (!alias)
        return {};
    const gchar *path = nullptr;
    g_variant_get(alias.get(), "(&o)", &path);
    std::string selected(path);
    // GNOME Keyring exposes its volatile collection at this documented
    // platform path. Never save there, even if it is the system default.
    if (selected == "/org/freedesktop/secrets/collection/session") {
        auto properties =
            c.call("/org/freedesktop/secrets", "org.freedesktop.DBus.Properties", "Get",
                   g_variant_new("(ss)", "org.freedesktop.Secret.Service", "Collections"),
                   G_VARIANT_TYPE("(v)"));
        if (!properties)
            return {};
        Variant wrapped(g_variant_get_child_value(properties.get(), 0));
        Variant collections(g_variant_get_variant(wrapped.get()));
        if (!g_variant_is_of_type(collections.get(), G_VARIANT_TYPE("ao")) ||
            g_variant_n_children(collections.get()) > 64)
            return {};
        selected.clear();
        for (gsize i = 0; i < g_variant_n_children(collections.get()); ++i) {
            Variant entry(g_variant_get_child_value(collections.get(), i));
            if (std::string(g_variant_get_string(entry.get(), nullptr)) ==
                "/org/freedesktop/secrets/collection/login")
                selected = "/org/freedesktop/secrets/collection/login";
        }
    }
    if (selected.empty() || selected == "/")
        return {ItemState::Absent, {}};
    auto properties = c.call(selected, "org.freedesktop.DBus.Properties", "Get",
                             g_variant_new("(ss)", "org.freedesktop.Secret.Collection", "Locked"),
                             G_VARIANT_TYPE("(v)"));
    if (!properties)
        return {};
    Variant wrapped(g_variant_get_child_value(properties.get(), 0));
    Variant locked(g_variant_get_variant(wrapped.get()));
    if (!g_variant_is_of_type(locked.get(), G_VARIANT_TYPE_BOOLEAN))
        return {};
    return g_variant_get_boolean(locked.get()) ? ItemLookup{ItemState::Locked, {}}
                                               : ItemLookup{ItemState::Available, selected};
}

#endif
} // namespace
CredentialResult read_credential(const std::string &ref) {
    if (!valid_reference(ref))
        return {false, {}, "无效的凭据引用。"};
#ifdef MIRAGE_LINUX_GIO
    SecretConnection c;
    if (!c.open())
        return unavailable();
    const auto item = find_item(c, ref);
    if (item.state == ItemState::Absent)
    {
        auto missing = lookup_failure(item.state);
        missing.absent = true;
        return missing;
    }
    if (item.state != ItemState::Available)
        return lookup_failure(item.state);
    auto r = c.call(item.path, "org.freedesktop.Secret.Item", "GetSecret",
                    g_variant_new("(o)", c.session.c_str()), G_VARIANT_TYPE("((oayays))"));
    if (!r)
        return unavailable();
    Variant secret(g_variant_get_child_value(r.get(), 0));
    Variant bytes(g_variant_get_child_value(secret.get(), 2));
    gsize size = 0;
    const auto *data = static_cast<const char *>(g_variant_get_fixed_array(bytes.get(), &size, 1));
    if (!data || size == 0 || size > 2048)
        return unavailable();
    return {true, std::string(data, size), {}};
#else
    return {false, {}, "此构建不支持系统钥匙环。"};
#endif
}
CredentialResult write_credential(const std::string &ref, const std::string &value) {
    if (!valid_reference(ref) || value.size() > 2048 ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 33 || c > 126; }))
        return {false, {}, "API Key 或凭据引用无效。"};
#ifdef MIRAGE_LINUX_GIO
    SecretConnection c;
    if (!c.open())
        return unavailable();
    if (value.empty()) {
        const auto item = find_item(c, ref);
        if (item.state == ItemState::Absent)
            return {true, {}, {}}; // Repeated cleanup is successful, without a Delete call.
        if (item.state != ItemState::Available)
            return lookup_failure(item.state);
        auto r = c.call(item.path, "org.freedesktop.Secret.Item", "Delete", nullptr,
                        G_VARIANT_TYPE("(o)"));
        if (!r)
            return unavailable();
        const gchar *prompt = nullptr;
        g_variant_get(r.get(), "(&o)", &prompt);
        return std::string(prompt) == "/" ? CredentialResult{true, {}, {}} : unavailable();
    }
    const auto collection = persistent_collection(c);
    if (collection.state == ItemState::Absent)
        return {false, {}, "未找到持久钥匙环，请在系统钥匙环中创建或选择持久集合。"};
    if (collection.state != ItemState::Available)
        return lookup_failure(collection.state);
    GVariantBuilder properties;
    g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&properties, "{sv}", "org.freedesktop.Secret.Item.Label",
                          g_variant_new_string("Mirage model API Key"));
    g_variant_builder_add(&properties, "{sv}", "org.freedesktop.Secret.Item.Attributes",
                          attributes(ref));
    auto r = c.call(
        collection.path, "org.freedesktop.Secret.Collection", "CreateItem",
        g_variant_new("(@a{sv}(o@ay@ays)b)", g_variant_builder_end(&properties), c.session.c_str(),
                      g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, nullptr, 0, 1),
                      g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, value.data(), value.size(), 1),
                      "text/plain", TRUE),
        G_VARIANT_TYPE("(oo)"));
    if (!r)
        return unavailable();
    const gchar *item = nullptr, *prompt = nullptr;
    g_variant_get(r.get(), "(&o&o)", &item, &prompt);
    return std::string(item) != "/" && std::string(prompt) == "/" ? CredentialResult{true, {}, {}}
                                                                  : unavailable();
#else
    return {false, {}, "此构建不支持系统钥匙环。"};
#endif
}
} // namespace mirage::platform
