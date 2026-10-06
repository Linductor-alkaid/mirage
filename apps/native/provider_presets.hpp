#pragma once
#include <array>
#include <string_view>
namespace mirage::native_ui {
struct ProviderPreset {
    std::string_view id, name, base_url, dialect, model, icon;
};
// ZCode 29628c9 config/provider/zcode-builtin.json API-key templates;
// MiniMax international endpoint follows the official API guide.
// Configuration defaults only: no implicit discovery or capability guarantee.
inline constexpr std::array provider_presets{
    ProviderPreset{"openai", "OpenAI", "https://api.openai.com/v1", "openai.responses.v1",
                   "gpt-6-astra", "assets/providers/model-provider-openai.png"},
    ProviderPreset{"anthropic", "Anthropic", "https://api.anthropic.com/v1",
                   "anthropic.messages.v1", "claude-fable-5-1",
                   "assets/providers/model-provider-anthropic.png"},
    ProviderPreset{"minimax", "MiniMax", "https://api.minimaxi.com/anthropic/v1",
                   "anthropic.messages.v1", "MiniMax-M3",
                   "assets/providers/model-provider-minimax.png"},
    ProviderPreset{"minimax-intl", "MiniMax（国际）", "https://api.minimax.io/anthropic/v1",
                   "anthropic.messages.v1", "MiniMax-M3",
                   "assets/providers/model-provider-minimax.png"},
    ProviderPreset{"deepseek", "DeepSeek", "https://api.deepseek.com/anthropic/v1",
                   "anthropic.messages.v1", "deepseek-flash",
                   "assets/providers/model-provider-deepseek.png"},
    ProviderPreset{"moonshot-kimi", "Kimi", "https://api.moonshot.cn/anthropic/v1",
                   "anthropic.messages.v1", "kimi-k3",
                   "assets/providers/model-provider-moonshot-kimi.png"},
    ProviderPreset{"qwen-alibaba-model-studio-cn", "阿里云百炼（中国）",
                   "https://dashscope.aliyuncs.com/apps/anthropic/v1", "anthropic.messages.v1",
                   "qwen3.8-max", "assets/providers/model-provider-alibaba-cloud.png"},
    ProviderPreset{"qwen-alibaba-model-studio-intl", "阿里云百炼（国际）",
                   "https://dashscope-intl.aliyuncs.com/compatible-mode/v1",
                   "openai.chat-completions.v1", "qwen3.8-max",
                   "assets/providers/model-provider-alibaba-cloud.png"},
    ProviderPreset{"zai-standard-api", "Z.ai API", "https://api.z.ai/api/paas/v4",
                   "openai.chat-completions.v1", "GLM-5.3",
                   "assets/providers/model-provider-zai.png"},
    ProviderPreset{"bigmodel-standard-api", "BigModel API", "https://open.bigmodel.cn/api/paas/v4",
                   "openai.chat-completions.v1", "GLM-5.3", "assets/providers/logo-bigmodel.svg"},
    ProviderPreset{"xiaomi-mimo", "Xiaomi MiMo", "https://api.xiaomimimo.com/anthropic/v1",
                   "anthropic.messages.v1", "mimo-v2.5-pro",
                   "assets/providers/model-provider-xiaomi-mimo.png"},
};
} // namespace mirage::native_ui
