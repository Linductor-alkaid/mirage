#include "tool_alias_provider.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mirage::integration::detail {
namespace {
struct ToolAlias {
    std::string canonical;
    std::string provider;
    mira::ToolId id;
};

mira::Error alias_error() {
    mira::Error error;
    error.code = mira::ErrorCode::InvalidArgument;
    error.domain = "mirage.tool-alias";
    error.safe_message = "模型服务工具名称无法安全映射。";
    return error;
}

bool provider_name(const std::string &canonical, std::string &result) {
    if (canonical.empty() || canonical.size() > 64)
        return false;
    result.clear();
    result.reserve(canonical.size());
    for (const unsigned char c : canonical) {
        if (c == '.' || c == '-')
            result.push_back('_');
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '_')
            result.push_back(static_cast<char>(c));
        else
            return false;
    }
    if (!((result.front() >= 'a' && result.front() <= 'z') ||
          (result.front() >= 'A' && result.front() <= 'Z')))
        result.insert(0, "tool_");
    return result.size() <= 64;
}

class ToolAliasProvider final : public mira::IModelProvider {
  public:
    explicit ToolAliasProvider(std::shared_ptr<mira::IModelProvider> provider)
        : provider_(std::move(provider)) {}

    const mira::ModelProfile &profile() const override { return provider_->profile(); }

    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &context,
                                            const mira::ProviderInferOptions &options) override {
        mira::ModelRequest translated = request;
        std::vector<ToolAlias> aliases;
        aliases.reserve(request.tools.size());
        for (auto &tool : translated.tools) {
            std::string alias;
            if (!provider_name(tool.wire_name, alias) ||
                std::any_of(aliases.begin(), aliases.end(),
                            [&](const auto &entry) { return entry.provider == alias; }))
                return alias_error();
            aliases.push_back({tool.wire_name, alias, tool.tool_id});
            tool.wire_name = std::move(alias);
            alias_host_tool_names(tool.description, request.tools);
        }
        for (auto &item : translated.input)
            for (auto &part : item.content)
                if (auto *call = std::get_if<mira::ToolCallPart>(&part)) {
                    std::string alias;
                    if (!provider_name(call->wire_name, alias) ||
                        std::any_of(aliases.begin(), aliases.end(), [&](const auto &entry) {
                            return entry.provider == alias && entry.canonical != call->wire_name;
                        }))
                        return alias_error();
                    call->wire_name = std::move(alias);
                }
        auto result = provider_->infer(translated, context, options);
        if (!result)
            return result;
        for (auto &item : result.value().output)
            if (auto *call = std::get_if<mira::ToolCallOutput>(&item)) {
                const auto found =
                    std::find_if(aliases.begin(), aliases.end(), [&](const auto &entry) {
                        return entry.provider == call->provider_name && entry.id == call->tool_id;
                    });
                if (found == aliases.end())
                    return mira::make_model_error(mira::ModelDomainCode::ProtocolViolation,
                                                  "模型返回了未声明的工具名称或 ID。", false);
                call->provider_name = found->canonical;
            }
        return result;
    }

    const mira::TransportTrace &last_trace() const override { return provider_->last_trace(); }
    const mira::SseStreamStats &last_sse_stats() const override {
        return provider_->last_sse_stats();
    }
    std::optional<std::chrono::milliseconds> last_retry_after_hint() const override {
        return provider_->last_retry_after_hint();
    }

  private:
    std::shared_ptr<mira::IModelProvider> provider_;
};
} // namespace

std::string model_tool_name(const std::string &canonical) {
    std::string result;
    return provider_name(canonical, result) ? result : std::string{};
}

void alias_host_tool_names(std::string &guidance, std::span<const mira::ExposedToolSpec> tools) {
    const auto name_character = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.' || c == '-';
    };
    for (const auto &tool : tools) {
        const auto alias = model_tool_name(tool.wire_name);
        if (alias.empty())
            continue; // infer rejects the invalid declaration before network dispatch
        const std::string names[] = {tool.wire_name, tool.wire_name.starts_with("desktop.")
                                                         ? tool.wire_name.substr(8)
                                                         : tool.wire_name};
        for (const auto &name : names) {
            std::size_t position = 0;
            while ((position = guidance.find(name, position)) != std::string::npos) {
                const auto end = position + name.size();
                if ((position == 0 || !name_character(guidance[position - 1])) &&
                    (end == guidance.size() || !name_character(guidance[end]))) {
                    guidance.replace(position, name.size(), alias);
                    position += alias.size();
                } else
                    position = end;
            }
        }
    }
}

std::shared_ptr<mira::IModelProvider>
make_tool_alias_provider(std::shared_ptr<mira::IModelProvider> provider) {
    return std::make_shared<ToolAliasProvider>(std::move(provider));
}
} // namespace mirage::integration::detail
