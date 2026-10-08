#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kairo {
class Executor;
}

namespace mirage::integration {
struct ModelCatalogQuery {
    std::string request_id;
    std::string endpoint_origin;
    std::string api_prefix;
    std::string dialect;
    std::optional<std::string> api_key;
    std::string credential_ref;
    std::string credential_env;
    std::function<std::optional<std::string>(const std::string &)> credential_lookup;
};
struct ModelCatalogResult {
    std::vector<std::string> ids;
    std::string error;
    [[nodiscard]] bool ok() const { return error.empty(); }
};

/// Normalize the bounded OpenAI/Anthropic or models[] catalog response.
ModelCatalogResult parse_model_catalog(const std::string &body);

/// Bounded, explicit discovery over Mira's Executor-backed HTTP transport.
/// The caller runs this on an Executor task and owns its cancellation probe.
ModelCatalogResult fetch_model_catalog(kairo::Executor &executor, const ModelCatalogQuery &query,
                                       const std::function<bool()> &cancelled);
} // namespace mirage::integration
