#pragma once

#include <chrono>
#include <functional>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/presentation_exclusion.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/integration/environment_visual_pipeline.hpp>

namespace mirage::runtime::detail {
// Serial-owner, synchronous request boundary. Providers/pipeline retain their
// existing Executor lifecycle; this adapter creates no task or scheduler.
struct DesktopObservationSource {
    desktop::DesktopEnvironment &environment;
    desktop::VisualReferenceRegistry *registry = nullptr;
    integration::EnvironmentVisualPipeline *pipeline = nullptr;
    const desktop::ActionCoordinator *actions = nullptr;
    std::function<bool(const desktop::CancelToken &)> authorize_visual = {};
    // Presence means a product pet owner exists, even when its child is stopped.
    std::function<desktop::PresentationExclusion(const desktop::CancelToken &)> exclude_pet = {};
};
struct DesktopObservationRead {
    bool ok = false;
    desktop::DesktopObservation observation;
    desktop::ProviderError error; // IPC error vocabulary; bounded safe detail
};
DesktopObservationRead read_desktop_observation(const DesktopObservationSource &, bool semantic,
                                                bool visual, const desktop::CancelToken &,
                                                std::chrono::steady_clock::time_point deadline);
} // namespace mirage::runtime::detail
