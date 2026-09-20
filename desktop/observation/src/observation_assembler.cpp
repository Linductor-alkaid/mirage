#include <mirage/desktop/observation_assembler.hpp>

namespace mirage::desktop {

ObservationAssembler::ObservationAssembler(DesktopEnvironment &environment)
    : environment_(environment) {}

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

ObservationAssemblyOutcome
ObservationAssembler::assemble(const ObservationComponents &components,
                               const ObservationAssemblyLimits &limits,
                               const CancelToken &cancel) {
    ObservationAssemblyOutcome outcome;
    outcome.active_window.requested = components.active_window;
    outcome.semantic_snapshot.requested = components.semantic_snapshot;
    outcome.pointer_state.requested = components.pointer_state;
    outcome.environment_state.requested = components.environment_state;
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = {"cancelled", "assembly cancelled before any capture"};
        return outcome;
    }

    // Focused window first: the semantic snapshot is taken against its id,
    // and its fields fill the observation header.
    WindowInfo focused;
    if (components.active_window) {
        WindowProvider *window = environment_.window();
        if (window == nullptr) {
            outcome.active_window.error = {"unsupported_platform",
                                           "environment carries no window provider"};
            outcome.error = outcome.active_window.error;
        } else {
            const WindowQueryOutcome query = window->front_window(cancel);
            if (!query.ok) {
                outcome.active_window.error = query.error;
                outcome.error = query.error;
            } else if (!query.found) {
                outcome.active_window.error = {"not_found", "no window currently has focus"};
                outcome.error = outcome.active_window.error;
            } else {
                outcome.active_window.captured = true;
                focused = query.window;
                outcome.observation.active_window = focused.title;
                outcome.observation.window_geometry = focused.geometry;
                outcome.observation.window_focused = focused.focused;
            }
        }
    }

    // Semantic snapshot of the focused window; on success it also fills
    // focused_element and active_application (the accessibility application
    // root name is the only honest window -> application mapping in M2).
    if (components.semantic_snapshot && !cancel.cancelled()) {
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
