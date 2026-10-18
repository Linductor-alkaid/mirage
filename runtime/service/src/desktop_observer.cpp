#include "desktop_observer.hpp"
#include <mirage/desktop/observation_assembler.hpp>

namespace mirage::runtime::detail {
namespace {
DesktopObservationRead unavailable(std::string message) {
    // IPC details contain diagnostics, never model or captured content. Keep
    // even an embedding's unusually long provider message inside the wire bound.
    if (message.size() > 768) {
        std::size_t bytes = 768;
        while (bytes && (static_cast<unsigned char>(message[bytes]) & 0xc0) == 0x80)
            --bytes;
        message.resize(bytes);
    }
    return {false, {}, {"unavailable", std::move(message)}};
}
std::string detail(const desktop::ProviderError &error) {
    return error.code + (error.message.empty() ? "" : ": " + error.message);
}
} // namespace
DesktopObservationRead read_desktop_observation(const DesktopObservationSource &source,
                                                bool semantic, bool visual,
                                                const desktop::CancelToken &cancel,
                                                std::chrono::steady_clock::time_point deadline) {
    desktop::CancelToken bounded{[cancel, deadline] {
        return cancel.cancelled() || std::chrono::steady_clock::now() >= deadline;
    }};
    desktop::PresentationExclusion exclusion;
    DesktopObservationRead result;
    std::uint64_t epoch = 0;
    try {
        if (bounded.cancelled())
            return unavailable("observation cancelled_or_expired");
        // A cached generation cannot establish that pet exclusion preceded its
        // capture. Preserve the old registry-only contract for embeddings with
        // no product pet; configured owners require an actual fresh producer.
        if (visual && source.exclude_pet && !source.pipeline)
            return unavailable("visual: pet_observation_requires_fresh_visual");
        if (visual && source.pipeline) {
            if (!source.pipeline->running())
                return unavailable("visual pipeline unavailable");
            const bool allowed = source.authorize_visual && source.authorize_visual(bounded);
            if (bounded.cancelled())
                return unavailable("observation cancelled_or_expired");
            if (!allowed)
                return {false, {}, {"permission_denied", "OCR capture denied"}};
        }
        if (bounded.cancelled())
            return unavailable("observation cancelled_or_expired");
        epoch = source.actions ? source.actions->epoch() : 0;
        if (source.exclude_pet)
            exclusion = source.exclude_pet(bounded);
        result = [&]() -> DesktopObservationRead {
            if (!exclusion.error.code.empty())
                return unavailable(detail(exclusion.error));
            if (source.exclude_pet && !exclusion.lease)
                return unavailable("pet_exclusion_failed: owner returned no exclusion lease");
            if (exclusion.lease) {
                const auto error = exclusion.lease->verify(bounded);
                if (!error.code.empty())
                    return unavailable(detail(error));
            }
            std::optional<desktop::VisualSnapshot> fresh_visual;
            if (visual && source.pipeline) {
                const auto fresh = source.pipeline->refresh(true, bounded, deadline);
                if (bounded.cancelled() || fresh.capture_cancelled || fresh.analysis_cancelled)
                    return unavailable("OCR capture or analysis failed: cancelled_or_expired");
                if (!fresh.captured || !fresh.analyzed || !fresh.publish.ok ||
                    fresh.publish.snapshot.scope_ref.empty()) {
                    const auto &error = fresh.captured ? fresh.analysis_error : fresh.capture_error;
                    return unavailable("OCR capture or analysis failed: " + detail(error));
                }
                fresh_visual = fresh.publish.snapshot;
            }
            desktop::ObservationComponents components;
            components.active_window = true;
            components.pointer_state = true;
            components.environment_state = true;
            components.semantic_snapshot = semantic;
            // Fresh refresh returns its exact generation. Reading an unrelated
            // current generation here could silently substitute another producer.
            components.visual_snapshot = visual && !fresh_visual;
            desktop::ObservationAssembler assembler(source.environment, source.registry);
            const auto assembled = assembler.assemble(components, {}, bounded);
            if (bounded.cancelled() || assembled.cancelled)
                return unavailable("observation cancelled_or_expired");
            if (!assembled.ok)
                return unavailable("observation unavailable: " + detail(assembled.error));
            if (source.actions && source.actions->epoch() != epoch)
                return unavailable("observation unavailable: desktop_epoch_changed");
            auto observation = assembled.observation;
            if (exclusion.lease && !exclusion.lease->excluded())
                observation.environment_state +=
                    "; pet_presentation=not_excluded; pet_exclusion=unsupported_compositor";
            if (fresh_visual) {
                if (source.pipeline->registry().current().scope_ref != fresh_visual->scope_ref)
                    return unavailable("visual: visual_snapshot_changed");
                observation.visual_snapshot = std::move(*fresh_visual);
                observation.visual_snapshot_ref = observation.visual_snapshot.scope_ref;
            }
            return {true, std::move(observation), {}};
        }();
    } catch (...) {
        result = unavailable("observation provider or pet exclusion failed");
    }
    // Always restore before sending any response, including provider failures,
    // exceptions and expired operations. A throwing verifier cannot skip release.
    if (exclusion.lease) {
        desktop::ProviderError verified, restored;
        try {
            verified = exclusion.lease->verify({});
        } catch (...) {
            verified = {"pet_exclusion_failed", "pet verification threw"};
        }
        try {
            restored = exclusion.lease->release();
        } catch (...) {
            restored = {"pet_restore_failed", "pet restore threw"};
        }
        if (!restored.code.empty())
            return unavailable(detail(restored));
        if (!verified.code.empty())
            return unavailable(detail(verified));
    }
    if (bounded.cancelled())
        return unavailable("observation cancelled_or_expired");
    try {
        if (result.ok && source.actions && source.actions->epoch() != epoch)
            return unavailable("observation unavailable: desktop_epoch_changed");
        if (result.ok && visual && source.pipeline &&
            source.pipeline->registry().current().scope_ref !=
                result.observation.visual_snapshot_ref)
            return unavailable("visual: visual_snapshot_changed");
    } catch (...) {
        return unavailable("observation final validation failed");
    }
    return result;
}
} // namespace mirage::runtime::detail
