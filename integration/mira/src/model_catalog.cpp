#include <mirage/integration/model_catalog.hpp>

#include "model_layer_env.hpp"

#include <mira/adapters/net/socket_transport.hpp>
#include <mira/json.hpp>
#ifdef MIRAGE_HAVE_OPENSSL_TLS
#include <mira/adapters/net/openssl_tls.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <memory>
#include <string_view>

namespace mirage::integration {
namespace {
class CatalogSecrets final : public mira::ISecretResolver {
  public:
    explicit CatalogSecrets(const ModelCatalogQuery &query) : query_(query) {}
    mira::Result<std::string> resolve(const mira::SecretRef &) override {
        std::string value;
        if (query_.api_key)
            value = *query_.api_key;
        else if (!query_.credential_ref.empty() && query_.credential_lookup)
            value = query_.credential_lookup(query_.credential_ref).value_or("");
        else if (!query_.credential_env.empty())
            value = detail::read_environment_value(query_.credential_env);
        if (!value.empty())
            return value;
        mira::Error error;
        error.code = mira::ErrorCode::PermissionDenied;
        error.domain = "mirage.model_catalog";
        error.safe_message = "API Key 不可用，请填写 Key 或检查系统钥匙环。";
        return error;
    }

  private:
    const ModelCatalogQuery &query_;
};

std::vector<std::string> candidates(const ModelCatalogQuery &query) {
    std::string base = query.endpoint_origin + query.api_prefix;
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    std::vector<std::string> urls;
    if (query.api_prefix.empty()) {
        urls.push_back(base + "/v1/models");
        urls.push_back(base + "/models");
    } else {
        urls.push_back(base + "/models");
        if (!base.ends_with("/v1")) {
            constexpr std::string_view compat_suffixes[] = {"/api/anthropic", "/apps/anthropic",
                                                            "/claudecode", "/anthropic", "/claude"};
            bool compatibility_path = false;
            for (const auto suffix : compat_suffixes) {
                if (base.ends_with(suffix)) {
                    urls.push_back(base.substr(0, base.size() - suffix.size()) + "/v1/models");
                    compatibility_path = true;
                    break;
                }
            }
            if (!compatibility_path)
                urls.push_back(base + "/v1/models");
        }
    }
    return urls;
}

} // namespace

ModelCatalogResult parse_model_catalog(const std::string &body) {
    const auto parsed = mira::parse_json(body, {.max_depth = 16,
                                                .max_document_bytes = 2 * 1024 * 1024,
                                                .max_string_bytes = 64 * 1024,
                                                .max_array_items = 4096,
                                                .max_object_members = 64});
    if (!parsed.has_value() || !parsed.value().is_object())
        return {{}, "模型列表响应不是有效 JSON 对象。"};
    const mira::JsonValue *entries = parsed.value().find("data");
    const bool models_format = entries == nullptr;
    if (!entries)
        entries = parsed.value().find("models");
    if (!entries || !entries->as_array())
        return {{}, "此服务未返回可识别的模型列表。"};
    ModelCatalogResult result;
    for (const auto &entry : *entries->as_array()) {
        const auto *id = entry.find(models_format ? "slug" : "id");
        if (!id)
            id = entry.find(models_format ? "id" : "slug");
        if (!id || !id->as_string() || id->as_string()->empty() || id->as_string()->size() > 1024)
            continue;
        if (std::any_of(id->as_string()->begin(), id->as_string()->end(),
                        [](unsigned char c) { return c < 33 || c == 127; }))
            continue;
        if (std::find(result.ids.begin(), result.ids.end(), *id->as_string()) == result.ids.end())
            result.ids.push_back(*id->as_string());
        if (result.ids.size() == 256)
            break;
    }
    return result;
}
ModelCatalogResult fetch_model_catalog(executor::Executor &executor, const ModelCatalogQuery &query,
                                       const std::function<bool()> &cancelled) {
    auto secrets = std::make_shared<CatalogSecrets>(query);
    std::shared_ptr<mira::ITlsChannelFactory> tls;
#ifdef MIRAGE_HAVE_OPENSSL_TLS
    auto factory = std::make_shared<mira::adapters::net::OpenSslTlsChannelFactory>();
    if (!factory->initialize())
        return {{}, "无法初始化模型列表的 TLS 信任库。"};
    tls = std::move(factory);
#endif
    mira::adapters::net::SocketTransportConfig config;
    config.worker_name = "mirage-model-catalog-" + query.request_id;
    config.max_queued_exchanges = 2;
    mira::adapters::net::SocketHttpTransport transport(executor, secrets, tls, config);
    if (!transport.start())
        return {{}, "模型列表请求未能进入 Executor。"};
    ModelCatalogResult last{{}, "无法获取模型列表，请检查 Base URL 与服务支持情况。"};
    for (const auto &url : candidates(query)) {
        if (cancelled()) {
            last.error = "模型列表请求已取消。";
            break;
        }
        mira::HttpRequest request;
        request.method = "GET";
        request.url = url;
        request.authorization = mira::SecretRef{"model-catalog"};
        request.credential_scheme = query.dialect == "anthropic.messages.v1"
                                        ? mira::HttpCredentialScheme::ApiKey
                                        : mira::HttpCredentialScheme::Bearer;
        if (query.dialect == "anthropic.messages.v1")
            request.headers.emplace_back("anthropic-version", "2023-06-01");
        mira::TransportLimits limits;
        limits.deadlines.total = std::chrono::seconds{10};
        limits.deadlines.first_byte = std::chrono::seconds{8};
        limits.max_response_bytes = 2 * 1024 * 1024;
        limits.max_redirects = 0;
        mira::OperationContext context;
        context.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        context.cancellation_requested = cancelled;
        std::string body;
        mira::TransportTrace trace;
        const auto response = transport.execute(
            request, limits, context, [&](std::string_view chunk) { body.append(chunk); }, trace);
        if (!response.has_value()) {
            last.error = response.error().safe_message.empty() ? "模型列表网络请求失败。"
                                                               : response.error().safe_message;
            continue;
        }
        if (response.value().status == 401 || response.value().status == 403) {
            last.error = "API Key 无效或没有获取模型列表的权限。";
            break;
        }
        if (response.value().status == 404 || response.value().status == 405)
            continue;
        if (response.value().status < 200 || response.value().status >= 300) {
            last.error =
                "获取模型列表失败（HTTP " + std::to_string(response.value().status) + "）。";
            break;
        }
        last = parse_model_catalog(body);
        break;
    }
    transport.shutdown();
    return last;
}
} // namespace mirage::integration
