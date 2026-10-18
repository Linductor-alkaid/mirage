#pragma once
#include <algorithm>
#include <mirage/integration/desktop_tool_context.hpp>

namespace mirage::integration::detail {
using PetExclusion = decltype(DesktopToolContext::exclude_pet);
inline bool needs_pet_exclusion(const std::string &name) {
    return name.starts_with("desktop.window.") || name.starts_with("desktop.input.") ||
           name.starts_with("desktop.accessibility.") || name.starts_with("desktop.screen.") ||
           name.starts_with("desktop.vision.") || name.starts_with("desktop.session.") ||
           name.starts_with("desktop.knowledge.") || name.starts_with("desktop.action.") ||
           name.starts_with("desktop.application.");
}
inline mira::BuiltinToolHandler exclude_pet_during(const std::string &name,
                                                   mira::BuiltinToolHandler handler,
                                                   const PetExclusion &acquire,
                                                   desktop::CancelToken cancel = {}) {
    if (!acquire || !needs_pet_exclusion(name))
        return handler;
    return [handler = std::move(handler), acquire,
            cancel](const auto &args, const auto &ctx) -> mira::Result<mira::JsonValue> {
        const auto failure = [](const desktop::ProviderError &error, bool after = false) {
            mira::Error result;
            result.code = error.code == "cancelled" ? mira::ErrorCode::Cancelled
                          : after                   ? mira::ErrorCode::ExecutionUncertain
                                                    : mira::ErrorCode::PlatformError;
            result.domain = "mirage.pet.exclusion";
            result.safe_message = error.code + ": " + error.message;
            result.retryable = false;
            return result;
        };
        desktop::PresentationExclusion exclusion;
        bool dispatched = false;
        try {
            desktop::CancelToken active{[&] {
                return cancel.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now());
            }};
            if (active.cancelled())
                return failure({"cancelled", "pet exclusion cancelled"});
            exclusion = acquire(active);
            if (!exclusion.error.code.empty())
                return failure(exclusion.error);
            const bool excluded = !exclusion.lease || exclusion.lease->excluded();
            if (exclusion.lease) {
                const auto error = exclusion.lease->verify(active);
                if (!error.code.empty()) {
                    const auto restored = exclusion.lease->release();
                    return failure(restored.code.empty() ? error : restored);
                }
            }
            dispatched = true;
            auto result = handler(args, ctx);
            if (exclusion.lease) {
                // Verification encompasses post-action captures too. Cleanup
                // always runs even after cancellation or an expired deadline.
                const auto verified = exclusion.lease->verify({});
                const auto restored = exclusion.lease->release();
                if (!restored.code.empty())
                    return failure(restored, true);
                if (!verified.code.empty())
                    return failure(verified, true);
            }
            if (result && !excluded) {
                if (const auto *object = result.value().as_object()) {
                    auto fields = *object;
                    std::erase_if(fields, [](const auto &field) { return field.first == "pet_presentation"; });
                    fields.emplace_back("pet_presentation", mira::JsonValue{mira::JsonValue::Object{
                        {"excluded", false}, {"reason", "unsupported_compositor"},
                        {"pixels_may_include_pet", true}}});
                    result = mira::JsonValue{std::move(fields)};
                }
            }
            return result;
        } catch (...) {
            if (exclusion.lease) {
                const auto restored = exclusion.lease->release();
                if (!restored.code.empty())
                    return failure(restored, true);
            }
            return failure({"pet_exclusion_failed", "desktop tool or exclusion admission failed"},
                           dispatched);
        }
    };
}
} // namespace mirage::integration::detail
