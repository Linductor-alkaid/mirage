#include <mirage/integration/desktop_atom_toolset.hpp>

#include <mirage/desktop/accessibility_provider.hpp>
#include <mirage/desktop/application_provider.hpp>
#include <mirage/desktop/clipboard_provider.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/notification_provider.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>
#include <mirage/desktop/window_provider.hpp>

#include <mira/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

namespace mirage::integration {
namespace {

using mira::Error;
using mira::ErrorCode;
using mira::JsonValue;
using mira::Result;

/// Tool results land in the run context (step_result signals), so every
/// text-bearing atom caps its payload at this budget (RULE-07). Provider
/// budgets below refuse instead of truncating; stream-shaped output (process
/// stdout/stderr, rendered snapshots) truncates with an explicit flag.
constexpr std::size_t kAtomResultBytes = 64u << 10;
/// Semantic snapshot node budget for the atom (the provider default 4096
/// nodes is the observation-pipeline budget, not a tool-result budget).
constexpr std::size_t kAtomSnapshotNodes = 512;

Error atom_error(ErrorCode code, std::string message) {
    Error error;
    error.code = code;
    error.domain = "mirage.desktop.atom";
    error.safe_message = std::move(message);
    return error;
}

/// Provider failures keep their stable code in the pinned safe message, so a
/// failed tool record stays actionable ("clipboard_too_large: ...") without
/// leaking content.
Error provider_error(const mirage::desktop::ProviderError &error) {
    ErrorCode code = ErrorCode::PlatformError;
    if (error.code == "cancelled") {
        code = ErrorCode::Cancelled;
    } else if (error.code == "invalid_argument") {
        code = ErrorCode::InvalidArgument;
    } else if (error.code == "not_found") {
        code = ErrorCode::NotFound;
    } else if (error.code == "permission_denied") {
        code = ErrorCode::PermissionDenied;
    } else if (error.code == "result_too_large" || error.code == "file_too_large" ||
               error.code == "clipboard_too_large" || error.code == "snapshot_too_large" ||
               error.code == "capture_too_large") {
        code = ErrorCode::ResourceExhausted;
    } else if (error.code == "unsupported_content" || error.code == "unsupported_window" ||
               error.code == "unsupported_platform") {
        code = ErrorCode::UnsupportedCapability;
    }
    return atom_error(code, error.code + ": " + error.message);
}

/// True when the drive asked the step to stop. Checked at every handler
/// entry; an atom refused before any provider call settles cancelled, never
/// as a provider failure.
Error cancelled_error() {
    return atom_error(ErrorCode::Cancelled, "run cancelled before the atom dispatched");
}

/// Judges the atom's capability through the gate. A null gate denies (fail
/// closed); the probe forwards the drive's cancellation so a Confirm-rule
/// wait converges when the run is cancelled (DEC-020).
bool authorized(const AtomPermissionGate &gate, const char *capability, const std::string &resource,
                const mira::OperationContext &context) {
    if (!gate) {
        return false;
    }
    return gate(capability, resource, [&context] { return context.cancelled(); });
}

Error permission_denied(const char *capability) {
    return atom_error(ErrorCode::PermissionDenied,
                      std::string("permission denied by policy: ") + capability);
}

JsonValue json_string(std::string value) { return JsonValue{std::move(value)}; }

JsonValue json_bool(bool value) { return JsonValue{value}; }

JsonValue json_int(std::int64_t value) { return JsonValue{value}; }

/// {"type":"string"} with an optional description.
JsonValue string_schema(const char *description) {
    JsonValue::Object object;
    object.emplace_back("type", json_string("string"));
    object.emplace_back("description", json_string(description));
    return JsonValue{std::move(object)};
}

/// {"type":"integer","minimum":..,"maximum":..} with an optional description.
JsonValue integer_schema(std::int64_t minimum, std::int64_t maximum, const char *description) {
    JsonValue::Object object;
    object.emplace_back("type", json_string("integer"));
    object.emplace_back("minimum", json_int(minimum));
    object.emplace_back("maximum", json_int(maximum));
    object.emplace_back("description", json_string(description));
    return JsonValue{std::move(object)};
}

/// {"type":"object","properties":{...},"required":[...]} — the supported
/// schema subset (strict: unlisted members are rejected at dispatch).
JsonValue object_schema(JsonValue::Object properties, std::vector<std::string> required) {
    JsonValue::Object object;
    object.emplace_back("type", json_string("object"));
    object.emplace_back("properties", JsonValue{std::move(properties)});
    JsonValue::Array required_array;
    for (auto &name : required) {
        required_array.push_back(json_string(std::move(name)));
    }
    object.emplace_back("required", JsonValue{std::move(required_array)});
    return JsonValue{std::move(object)};
}

/// Reads one string member; the registry has already schema-validated the
/// input, so an absent member is a programming error, not an input error.
std::string string_member(const JsonValue &arguments, const char *name) {
    const auto *value = arguments.find(name);
    return value != nullptr && value->is_string() ? *value->as_string() : std::string();
}

std::optional<std::int64_t> int_member(const JsonValue &arguments, const char *name) {
    const auto *value = arguments.find(name);
    if (value == nullptr || !value->is_integer()) {
        return std::nullopt;
    }
    return *value->as_integer();
}

/// {id,title,focused} projection of one window (geometry stays out of tool
/// results; the observation surface carries it).
JsonValue window_json(const mirage::desktop::WindowInfo &window) {
    JsonValue::Object object;
    object.emplace_back("id", json_string(window.id));
    object.emplace_back("title", json_string(window.title));
    object.emplace_back("focused", json_bool(window.focused));
    return JsonValue{std::move(object)};
}

JsonValue application_json(const mirage::desktop::ApplicationInfo &application) {
    JsonValue::Object object;
    object.emplace_back("id", json_string(application.id));
    object.emplace_back("name", json_string(application.name));
    object.emplace_back("running", json_bool(application.running));
    return JsonValue{std::move(object)};
}

/// One registration candidate: built only when its provider is present.
struct AtomRegistration {
    mira::BuiltinToolSpec spec;
    mira::BuiltinToolHandler handler;
};

AtomRegistration filesystem_read_text(AtomPermissionGate gate,
                                      mirage::desktop::FilesystemProvider &filesystem) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.filesystem.read_text";
    registration.spec.description =
        "Reads a UTF-8 text file inside the declared read scope; over-budget files are refused, "
        "not truncated.";
    registration.spec.parameters_schema =
        mira::JsonSchema{object_schema({{"path", string_schema("File path")}}, {"path"})};
    registration.handler =
        [gate, &filesystem](const JsonValue &arguments,
                            const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto path = string_member(arguments, "path");
        if (!authorized(gate, "filesystem.read", path, context)) {
            return permission_denied("filesystem.read");
        }
        mirage::desktop::FileReadLimits limits;
        limits.max_bytes = kAtomResultBytes;
        const auto outcome =
            filesystem.read_text_file(path, limits, mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("content", json_string(outcome.content));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration process_execute(AtomPermissionGate gate,
                                 mirage::desktop::ProcessProvider &process) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.process.execute";
    registration.spec.description =
        "Executes one command line through the platform shell within a wall-clock budget and "
        "captures its output; the whole process group is torn down at the budget.";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(
        {{"command", string_schema("Command line")},
         {"timeout_ms",
          integer_schema(1000, 120000, "Wall-clock budget in milliseconds (default 30000)")}},
        {"command"})};
    registration.handler = [gate,
                            &process](const JsonValue &arguments,
                                      const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto command = string_member(arguments, "command");
        if (!authorized(gate, "process.execute", command, context)) {
            return permission_denied("process.execute");
        }
        mirage::desktop::ProcessLimits limits;
        limits.timeout = std::chrono::milliseconds(30000);
        if (const auto timeout = int_member(arguments, "timeout_ms")) {
            limits.timeout =
                std::chrono::milliseconds(std::clamp<std::int64_t>(*timeout, 1000, 120000));
        }
        limits.max_output_bytes = kAtomResultBytes;
        const auto outcome = process.execute(command, limits, mirage::desktop::CancelToken{});
        if (outcome.cancelled) {
            return provider_error(outcome.error);
        }
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("exit_code", json_int(outcome.exit_code));
        result.emplace_back("stdout", json_string(outcome.standard_output));
        result.emplace_back("stderr", json_string(outcome.standard_error));
        result.emplace_back("output_truncated", json_bool(outcome.output_truncated));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration window_list(mirage::desktop::WindowProvider &window) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.window.list";
    registration.spec.description = "Lists the current top-level windows with their focus state.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(JsonValue::Object{}, {})};
    registration.handler = [&window](const JsonValue &,
                                     const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto outcome = window.list_windows(mirage::desktop::WindowListLimits{},
                                                 mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Array windows;
        for (const auto &entry : outcome.windows) {
            windows.push_back(window_json(entry));
        }
        JsonValue::Object result;
        result.emplace_back("windows", JsonValue{std::move(windows)});
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration window_front(mirage::desktop::WindowProvider &window) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.window.front";
    registration.spec.description = "Reports the currently focused window, if any.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(JsonValue::Object{}, {})};
    registration.handler = [&window](const JsonValue &,
                                     const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto outcome = window.front_window(mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("found", json_bool(outcome.found));
        if (outcome.found) {
            result.emplace_back("id", json_string(outcome.window.id));
            result.emplace_back("title", json_string(outcome.window.title));
        }
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration window_activate(AtomPermissionGate gate, mirage::desktop::WindowProvider &window,
                                 const AtomOverlayFeed &feed) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.window.activate";
    registration.spec.description = "Focuses one window by id.";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(
        {{"window_id", string_schema("Window id from desktop.window.list")}}, {"window_id"})};
    registration.handler = [gate, &window,
                            feed](const JsonValue &arguments,
                                  const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto window_id = string_member(arguments, "window_id");
        if (!authorized(gate, "window.activate", window_id, context)) {
            return permission_denied("window.activate");
        }
        // Overlay mirror (M5-09, DEC-029): the upcoming activation with the
        // target geometry, published after the judgement and before the
        // side effect. A failed geometry lookup degrades to a highlight-less
        // hint and never blocks the action.
        if (feed.show_action) {
            AtomOverlayAction action;
            const auto windows = window.list_windows(mirage::desktop::WindowListLimits{},
                                                     mirage::desktop::CancelToken{});
            if (windows.ok) {
                const auto found = std::find_if(
                    windows.windows.begin(), windows.windows.end(),
                    [&](const mirage::desktop::WindowInfo &info) { return info.id == window_id; });
                if (found != windows.windows.end()) {
                    const std::string &title = !found->title.empty() ? found->title : found->id;
                    if (found->geometry.width > 0 && found->geometry.height > 0) {
                        action.highlights.push_back({found->geometry, title});
                    }
                    action.hint = "activating \"" + title + "\"";
                }
            }
            if (action.hint.empty()) {
                action.hint = "activating window " + window_id;
            }
            feed.show_action(action);
        }
        const auto outcome = window.activate(window_id, mirage::desktop::CancelToken{});
        if (feed.show_action) {
            feed.show_action(AtomOverlayAction{}); // the action settled
        }
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("activated", json_string(window_id));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration application_list(mirage::desktop::ApplicationProvider &application) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.application.list";
    registration.spec.description = "Lists the installed applications with their running state.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(JsonValue::Object{}, {})};
    registration.handler =
        [&application](const JsonValue &,
                       const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto outcome = application.list_applications(mirage::desktop::ApplicationListLimits{},
                                                           mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Array applications;
        for (const auto &entry : outcome.applications) {
            applications.push_back(application_json(entry));
        }
        JsonValue::Object result;
        result.emplace_back("applications", JsonValue{std::move(applications)});
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration application_launch(AtomPermissionGate gate,
                                    mirage::desktop::ApplicationProvider &application) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.application.launch";
    registration.spec.description =
        "Launches an installed application by id (bounded to the desktop entry's fixed command).";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(
        {{"application_id", string_schema("Application id from desktop.application.list")}},
        {"application_id"})};
    registration.handler =
        [gate, &application](const JsonValue &arguments,
                             const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto application_id = string_member(arguments, "application_id");
        if (!authorized(gate, "application.launch", application_id, context)) {
            return permission_denied("application.launch");
        }
        const auto outcome =
            application.launch(application_id, mirage::desktop::ApplicationLaunchLimits{},
                               mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("instance_id", json_string(outcome.instance_id));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration application_terminate(AtomPermissionGate gate,
                                       mirage::desktop::ApplicationProvider &application) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.application.terminate";
    registration.spec.description =
        "Asks a running application instance to terminate (a cooperative signal, never a kill).";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(
        {{"application_id", string_schema("Application id from desktop.application.list")}},
        {"application_id"})};
    registration.handler =
        [gate, &application](const JsonValue &arguments,
                             const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto application_id = string_member(arguments, "application_id");
        if (!authorized(gate, "application.terminate", application_id, context)) {
            return permission_denied("application.terminate");
        }
        const auto outcome =
            application.terminate(application_id, mirage::desktop::ApplicationLaunchLimits{},
                                  mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("terminated", json_string(application_id));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration clipboard_read_text(AtomPermissionGate gate,
                                     mirage::desktop::ClipboardProvider &clipboard) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.clipboard.read_text";
    registration.spec.description =
        "Reads the clipboard as UTF-8 text; non-text content is refused.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(JsonValue::Object{}, {})};
    registration.handler =
        [gate, &clipboard](const JsonValue &,
                           const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        if (!authorized(gate, "clipboard.read", "", context)) {
            return permission_denied("clipboard.read");
        }
        mirage::desktop::ClipboardReadLimits limits;
        limits.max_bytes = kAtomResultBytes;
        const auto outcome = clipboard.read_text(limits, mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("content", json_string(outcome.content));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration clipboard_write_text(AtomPermissionGate gate,
                                      mirage::desktop::ClipboardProvider &clipboard) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.clipboard.write_text";
    registration.spec.description = "Replaces the clipboard content with UTF-8 text.";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema = mira::JsonSchema{
        object_schema({{"text", string_schema("Text to place on the clipboard")}}, {"text"})};
    registration.handler =
        [gate, &clipboard](const JsonValue &arguments,
                           const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto text = string_member(arguments, "text");
        if (!authorized(gate, "clipboard.write", "", context)) {
            return permission_denied("clipboard.write");
        }
        const auto outcome = clipboard.write_text(text, mirage::desktop::ClipboardWriteLimits{},
                                                  mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("written", json_int(static_cast<std::int64_t>(text.size())));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration input_type_text(AtomPermissionGate gate, mirage::desktop::InputProvider &input,
                                 mirage::desktop::WindowProvider *window,
                                 const AtomOverlayFeed &feed) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.input.type_text";
    registration.spec.description = "Types UTF-8 text into the focused window.";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema =
        mira::JsonSchema{object_schema({{"text", string_schema("Text to type")}}, {"text"})};
    registration.handler = [gate, &input, window,
                            feed](const JsonValue &arguments,
                                  const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto text = string_member(arguments, "text");
        if (!authorized(gate, "input.inject", text, context)) {
            return permission_denied("input.inject");
        }
        // Overlay mirror (M5-09, DEC-029): the upcoming typing with the
        // focused window's geometry; a failed lookup degrades to a
        // highlight-less hint and never blocks the action.
        if (feed.show_action) {
            AtomOverlayAction action;
            action.hint = "typing " + std::to_string(text.size()) + " character(s)";
            if (window != nullptr) {
                const auto front = window->front_window(mirage::desktop::CancelToken{});
                if (front.ok && front.found && front.window.geometry.width > 0 &&
                    front.window.geometry.height > 0) {
                    const std::string &title =
                        !front.window.title.empty() ? front.window.title : front.window.id;
                    action.highlights.push_back({front.window.geometry, title});
                    action.hint = "typing into \"" + title + "\"";
                }
            }
            feed.show_action(action);
        }
        const auto outcome =
            input.type_text(text, mirage::desktop::InputLimits{}, mirage::desktop::CancelToken{});
        if (feed.show_action) {
            feed.show_action(AtomOverlayAction{}); // the action settled
        }
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("typed", json_int(static_cast<std::int64_t>(text.size())));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration notification_post(AtomPermissionGate gate,
                                   mirage::desktop::NotificationProvider &notification) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.notification.post";
    registration.spec.description = "Posts one user-facing desktop notification.";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema =
        mira::JsonSchema{object_schema({{"title", string_schema("Notification title")},
                                        {"body", string_schema("Notification body")}},
                                       {"title", "body"})};
    registration.handler =
        [gate, &notification](const JsonValue &arguments,
                              const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        const auto title = string_member(arguments, "title");
        if (!authorized(gate, "notification.post", title, context)) {
            return permission_denied("notification.post");
        }
        const auto outcome = notification.notify(title, string_member(arguments, "body"),
                                                 mirage::desktop::NotificationLimits{},
                                                 mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        result.emplace_back("posted", json_bool(true));
        return JsonValue{std::move(result)};
    };
    return registration;
}

AtomRegistration
accessibility_semantic_snapshot(mirage::desktop::AccessibilityProvider &accessibility,
                                const AtomOverlayFeed &feed) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.accessibility.semantic_snapshot";
    registration.spec.description =
        "Takes the semantic snapshot (interactable element tree) of one window and returns it in "
        "the deterministic rendered form.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(
        {{"window_id", string_schema("Window id from desktop.window.list")}}, {"window_id"})};
    registration.handler = [&accessibility,
                            feed](const JsonValue &arguments,
                                  const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled()) {
            return cancelled_error();
        }
        mirage::desktop::SemanticSnapshotLimits limits;
        limits.max_nodes = kAtomSnapshotNodes;
        const auto outcome = accessibility.semantic_snapshot(
            string_member(arguments, "window_id"), limits, mirage::desktop::CancelToken{});
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        // Overlay mirror (M5-09, DEC-029): the Observation debug face —
        // the presenter projects the node geometry when the service
        // enabled the face; the feed member is dark otherwise.
        if (feed.show_observation) {
            feed.show_observation(outcome.snapshot);
        }
        auto rendered = mirage::desktop::render_semantic_snapshot(outcome.snapshot);
        bool truncated = false;
        if (rendered.size() > kAtomResultBytes) {
            rendered.resize(kAtomResultBytes);
            truncated = true;
        }
        JsonValue::Object result;
        result.emplace_back("application", json_string(outcome.snapshot.application));
        result.emplace_back("window_title", json_string(outcome.snapshot.window_title));
        result.emplace_back("snapshot", json_string(std::move(rendered)));
        result.emplace_back("node_count",
                            json_int(static_cast<std::int64_t>(outcome.snapshot.nodes.size())));
        result.emplace_back("snapshot_truncated", json_bool(truncated));
        return JsonValue{std::move(result)};
    };
    return registration;
}

} // namespace

struct DesktopAtomToolset::Impl final {
    std::shared_ptr<mira::BuiltinToolRegistry> registry =
        std::make_shared<mira::BuiltinToolRegistry>();
};

std::shared_ptr<DesktopAtomToolset>
DesktopAtomToolset::build(mirage::desktop::DesktopEnvironment *environment, AtomPermissionGate gate,
                          const AtomOverlayFeed &feed) {
    auto toolset = std::shared_ptr<DesktopAtomToolset>(new DesktopAtomToolset());
    toolset->impl_ = std::make_unique<Impl>();
    if (environment == nullptr) {
        return toolset; // No environment, no atoms: the empty registry is honest.
    }

    // Candidates are materialized per available provider; registration is
    // fail closed on the pinned side (schema subset, wire-name collisions),
    // so a rejected atom aborts the build instead of silently shrinking the
    // catalog.
    const auto register_atom = [&registry =
                                    *toolset->impl_->registry](AtomRegistration &&candidate) {
        const auto registered =
            registry.register_tool(std::move(candidate.spec), std::move(candidate.handler));
        if (!registered) {
            // Internal invariant: the static table registers clean schemas
            // and unique wire names; a failure here is a build-order bug.
            throw std::logic_error("desktop atom registration rejected: " +
                                   registered.error().safe_message);
        }
    };

    if (auto *provider = environment->filesystem()) {
        register_atom(filesystem_read_text(gate, *provider));
    }
    if (auto *provider = environment->process()) {
        register_atom(process_execute(gate, *provider));
    }
    // The input atoms' overlay hint resolves the focused window through the
    // window provider when one is bound (DEC-029 decision 5); captured as a
    // plain pointer — the environment outlives the registry's attachment.
    mirage::desktop::WindowProvider *window_provider = nullptr;
    if (auto *provider = environment->window()) {
        register_atom(window_list(*provider));
        register_atom(window_front(*provider));
        register_atom(window_activate(gate, *provider, feed));
        window_provider = provider;
    }
    if (auto *provider = environment->application()) {
        register_atom(application_list(*provider));
        register_atom(application_launch(gate, *provider));
        register_atom(application_terminate(gate, *provider));
    }
    if (auto *provider = environment->clipboard()) {
        register_atom(clipboard_read_text(gate, *provider));
        register_atom(clipboard_write_text(gate, *provider));
    }
    if (auto *provider = environment->input()) {
        register_atom(input_type_text(gate, *provider, window_provider, feed));
    }
    if (auto *provider = environment->notification()) {
        register_atom(notification_post(gate, *provider));
    }
    if (auto *provider = environment->accessibility()) {
        register_atom(accessibility_semantic_snapshot(*provider, feed));
    }
    return toolset;
}

DesktopAtomToolset::~DesktopAtomToolset() = default;

std::shared_ptr<mira::BuiltinToolRegistry> DesktopAtomToolset::registry() const {
    return impl_->registry;
}

std::vector<DesktopAtomView> DesktopAtomToolset::exposed_atoms() const {
    std::vector<DesktopAtomView> atoms;
    for (const auto &tool : impl_->registry->exposed_tools()) {
        DesktopAtomView view;
        view.wire_name = tool.wire_name;
        view.version = std::to_string(tool.version.major) + "." +
                       std::to_string(tool.version.minor) + "." +
                       std::to_string(tool.version.patch);
        view.description = tool.description;
        view.has_side_effects = tool.has_side_effects;
        view.parameters_schema_json = mira::to_json_string(tool.parameters_schema.root);
        atoms.push_back(std::move(view));
    }
    return atoms;
}

} // namespace mirage::integration
