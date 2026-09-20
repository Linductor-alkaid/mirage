#include <mirage/desktop/observation_assembler.hpp>

namespace mirage::desktop {

ObservationAssembler::ObservationAssembler(DesktopEnvironment &environment,
                                           VisualReferenceRegistry *visual_registry)
    : environment_(environment), visual_registry_(visual_registry) {}

namespace {

/// Formats the environment_state summary from the environment's provider
/// inventory: `<platform>:<provider>[,<provider>...]` in the fixed accessor
/// order. Deterministic, honest (a provider appears exactly when its
/// accessor is non-null) and platform-free.
std::string environment_summary(DesktopEnvironment &environment) {
    std::string summary = environment.info().platform + ":";
    const std::pair<const char *, bool> present[] = {
        {"filesystem", environment.filesystem() != nullptr},
        {"process", environment.process() != nullptr},
        {"application", environment.application() != nullptr},
        {"window", environment.window() != nullptr},
        {"accessibility", environment.accessibility() != nullptr},
        {"screen", environment.screen() != nullptr},
        {"input", environment.input() != nullptr},
        {"clipboard", environment.clipboard() != nullptr},
        {"notification", environment.notification() != nullptr},
    };
    bool first = true;
    for (const auto &entry : present) {
        if (!entry.second) {
            continue;
        }
        if (!first) {
            summary += ",";
        }
        summary += entry.first;
        first = false;
    }
    return summary;
}

} // namespace

ObservationAssemblyOutcome ObservationAssembler::assemble(const ObservationComponents &components,
                                                          const ObservationAssemblyLimits &limits,
                                                          const CancelToken &cancel) {
    ObservationAssemblyOutcome outcome;
    outcome.active_window.requested = components.active_window;
    outcome.semantic_snapshot.requested = components.semantic_snapshot;
    outcome.visual_snapshot.requested = components.visual_snapshot;
    outcome.pointer_state.requested = components.pointer_state;
    outcome.environment_state.requested = components.environment_state;
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = {"cancelled", "assembly cancelled before any capture"};
        return outcome;
    }

    // Focused window first: the semantic snapshot is taken against its id,
    // and its fields fill the observation header. The focused-window query
    // runs for every component that needs it (active_window reads its
    // fields, semantic_snapshot needs the id), so a structure-only request
    // resolves the focused window too instead of snapshotting an empty id.
    WindowInfo focused;
    bool have_focus = false;
    const bool needs_focus = components.active_window || components.semantic_snapshot;
    if (needs_focus) {
        WindowProvider *window = environment_.window();
        if (window == nullptr) {
            const ProviderError unsupported{"unsupported_platform",
                                            "environment carries no window provider"};
            if (components.active_window) {
                outcome.active_window.error = unsupported;
                if (outcome.error.code.empty()) {
                    outcome.error = unsupported;
                }
            }
            if (components.semantic_snapshot) {
                outcome.semantic_snapshot.error = unsupported;
                if (outcome.error.code.empty()) {
                    outcome.error = unsupported;
                }
            }
        } else {
            const WindowQueryOutcome query = window->front_window(cancel);
            if (!query.ok) {
                if (components.active_window) {
                    outcome.active_window.error = query.error;
                    if (outcome.error.code.empty()) {
                        outcome.error = query.error;
                    }
                }
                if (components.semantic_snapshot) {
                    outcome.semantic_snapshot.error = query.error;
                    if (outcome.error.code.empty()) {
                        outcome.error = query.error;
                    }
                }
            } else if (!query.found) {
                const ProviderError no_focus{"not_found", "no window currently has focus"};
                if (components.active_window) {
                    outcome.active_window.error = no_focus;
                    if (outcome.error.code.empty()) {
                        outcome.error = no_focus;
                    }
                }
                if (components.semantic_snapshot) {
                    outcome.semantic_snapshot.error = no_focus;
                    if (outcome.error.code.empty()) {
                        outcome.error = no_focus;
                    }
                }
            } else {
                focused = query.window;
                have_focus = true;
                if (components.active_window) {
                    outcome.active_window.captured = true;
                    outcome.observation.active_window = focused.title;
                    outcome.observation.window_geometry = focused.geometry;
                    outcome.observation.window_focused = focused.focused;
                }
            }
        }
    }

    // Semantic snapshot of the focused window; on success it also fills
    // focused_element and active_application (the accessibility application
    // root name is the only honest window -> application mapping in M2).
    // Skipped when the focused window could not be resolved above: the
    // focus-stage error (unsupported_platform / not_found / query failure)
    // is the component's answer, and snapshotting an empty id would only
    // overwrite it with a misleading invalid_argument.
    if (components.semantic_snapshot && have_focus && !cancel.cancelled()) {
        AccessibilityProvider *accessibility = environment_.accessibility();
        if (accessibility == nullptr) {
            outcome.semantic_snapshot.error = {"unsupported_platform",
                                               "environment carries no accessibility provider"};
            if (outcome.error.code.empty()) {
                outcome.error = outcome.semantic_snapshot.error;
            }
        } else {
            const SnapshotOutcome snapshot =
                accessibility->semantic_snapshot(focused.id, limits.snapshot_limits, cancel);
            if (!snapshot.ok) {
                outcome.semantic_snapshot.error = snapshot.error;
                outcome.cancelled = snapshot.cancelled;
                if (outcome.error.code.empty()) {
                    outcome.error = snapshot.error;
                }
            } else {
                outcome.semantic_snapshot.captured = true;
                outcome.observation.semantic_snapshot = snapshot.snapshot;
                for (const auto &node : snapshot.snapshot.nodes) {
                    if (node.focused) {
                        outcome.observation.focused_element = node.ref;
                        break;
                    }
                }
                outcome.observation.active_application = snapshot.snapshot.application;
            }
        }
    }

    // The active visual generation: a read of what the visual pipeline has
    // published, never an analysis trigger (DEC-016 decisions 2 and 4).
    // Requested without a bound registry the component fails closed with
    // "unsupported_platform"; with a registry that has no published
    // generation yet, with "not_found" — an empty visual component would be
    // a silently incomplete observation.
    if (components.visual_snapshot && !cancel.cancelled()) {
        if (visual_registry_ == nullptr) {
            outcome.visual_snapshot.error = {"unsupported_platform",
                                             "no visual observation source is bound"};
            if (outcome.error.code.empty()) {
                outcome.error = outcome.visual_snapshot.error;
            }
        } else {
            const VisualSnapshot snapshot = visual_registry_->current();
            if (snapshot.scope_ref.empty()) {
                outcome.visual_snapshot.error = {"not_found",
                                                 "no visual snapshot has been published"};
                if (outcome.error.code.empty()) {
                    outcome.error = outcome.visual_snapshot.error;
                }
            } else {
                outcome.visual_snapshot.captured = true;
                outcome.observation.visual_snapshot = std::move(snapshot);
                outcome.observation.visual_snapshot_ref =
                    outcome.observation.visual_snapshot.scope_ref;
            }
        }
    }

    // Pointer position; a read-only query through the input provider.
    if (components.pointer_state && !cancel.cancelled()) {
        InputProvider *input = environment_.input();
        if (input == nullptr) {
            outcome.pointer_state.error = {"unsupported_platform",
                                           "environment carries no input provider"};
            if (outcome.error.code.empty()) {
                outcome.error = outcome.pointer_state.error;
            }
        } else {
            const PointerQueryOutcome query = input->pointer_position(cancel);
            if (!query.ok) {
                outcome.pointer_state.error = query.error;
                outcome.cancelled = query.cancelled;
                if (outcome.error.code.empty()) {
                    outcome.error = query.error;
                }
            } else {
                outcome.pointer_state.captured = true;
                outcome.observation.pointer_state = query.position;
            }
        }
    }

    // Environment summary: derived from the accessor inventory, cannot fail.
    if (components.environment_state && !cancel.cancelled()) {
        outcome.environment_state.captured = true;
        outcome.observation.environment_state = environment_summary(environment_);
    }

    outcome.ok = !outcome.cancelled && outcome.error.code.empty();
    return outcome;
}

} // namespace mirage::desktop
