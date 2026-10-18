#include "pet_exclusion.hpp"
#include <mirage/integration/desktop_atom_toolset.hpp>

#include "desktop_action_verification.hpp"
#include "desktop_visual_knowledge.hpp"
#include "tool_geometry.hpp"

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
#include <cstddef>
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
constexpr std::size_t kAtomSnapshotNodes = 4096;

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
                const mira::OperationContext &context,
                const decltype(DesktopToolContext::authorize) &authorize = {},
                const mirage::desktop::CancelToken &cancel = {}) {
    if (authorize) {
        return authorize(
            capability, resource,
            [&context, cancel] {
                return cancel.cancelled() || context.cancelled_or_expired(mira::Timestamp::now());
            },
            context.task.to_string(), context.operation.to_string());
    }
    if (!gate) {
        return false;
    }
    return gate(capability, resource, [&context, cancel] {
        return cancel.cancelled() || context.cancelled_or_expired(mira::Timestamp::now());
    });
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

AtomRegistration window_list(mirage::desktop::WindowProvider &window,
                             const mirage::desktop::CancelToken &cancel = {}) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.window.list";
    registration.spec.description = "Lists the current top-level windows with their focus state.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(JsonValue::Object{}, {})};
    registration.handler = [&window,
                            cancel](const JsonValue &,
                                    const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            return cancelled_error();
        }
        const auto outcome = window.list_windows(mirage::desktop::WindowListLimits{}, cancel);
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

AtomRegistration window_front(mirage::desktop::WindowProvider &window,
                              const mirage::desktop::CancelToken &cancel = {}) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.window.front";
    registration.spec.description = "Reports the currently focused window, if any.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(JsonValue::Object{}, {})};
    registration.handler = [&window,
                            cancel](const JsonValue &,
                                    const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            return cancelled_error();
        }
        const auto outcome = window.front_window(cancel);
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
                                 const AtomOverlayFeed &feed,
                                 const mirage::desktop::CancelToken &cancel = {},
                                 decltype(DesktopToolContext::authorize) authorize = {},
                                 std::shared_ptr<mirage::desktop::ActionCoordinator> actions = {}) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.window.activate";
    registration.spec.description = "Focuses one window by id.";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(
        {{"window_id", string_schema("Window id from desktop.window.list")}}, {"window_id"})};
    registration.handler = [gate, &window, feed, authorize, actions,
                            cancel](const JsonValue &arguments,
                                    const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            return cancelled_error();
        }
        const auto window_id = string_member(arguments, "window_id");
        if (window_id.empty() || window_id.size() > 256)
            return atom_error(ErrorCode::InvalidArgument, "invalid window id");
        if (!authorized(gate, "window.activate", window_id, context, authorize, cancel)) {
            if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled())
                return cancelled_error();
            return permission_denied("window.activate");
        }
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            return cancelled_error();
        }
        mirage::desktop::CancelToken operation_cancel{[cancel, context] {
            return cancel.cancelled() || context.cancelled_or_expired(mira::Timestamp::now());
        }};
        mirage::desktop::ActionCoordinator::Lease lease;
        if (actions) {
            lease = actions->try_begin(operation_cancel);
            if (!lease)
                return provider_error(lease.error());
        }
        // Overlay mirror (M5-09, DEC-029): the upcoming activation with the
        // target geometry, published after the judgement and before the
        // side effect. A failed geometry lookup degrades to a highlight-less
        // hint and never blocks the action.
        if (feed.show_action) {
            AtomOverlayAction action;
            const auto windows = window.list_windows(mirage::desktop::WindowListLimits{}, cancel);
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
        const auto outcome = window.activate(window_id, operation_cancel);
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

AtomRegistration application_list(mirage::desktop::ApplicationProvider &application,
                                  const mirage::desktop::CancelToken &cancel = {}) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.application.list";
    registration.spec.description = "Lists the installed applications with their running state.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(JsonValue::Object{}, {})};
    registration.handler = [&application,
                            cancel](const JsonValue &,
                                    const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            return cancelled_error();
        }
        const auto outcome =
            application.list_applications(mirage::desktop::ApplicationListLimits{}, cancel);
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
                                 const AtomOverlayFeed &feed, bool explicit_window = false,
                                 const mirage::desktop::CancelToken &cancel = {},
                                 decltype(DesktopToolContext::authorize) authorize = {},
                                 std::shared_ptr<mirage::desktop::ActionCoordinator> actions = {}) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.input.type_text";
    registration.spec.description = "Types UTF-8 text into the focused window.";
    registration.spec.has_side_effects = true;
    registration.spec.parameters_schema =
        mira::JsonSchema{object_schema({{"text", string_schema("Text to type")}}, {"text"})};
    if (explicit_window) {
        registration.spec.description =
            "Checks the explicitly named window is focused before typing UTF-8. Re-observe after "
            "typing; dispatched input does not prove the application accepted it.";
        registration.spec.parameters_schema = mira::JsonSchema{object_schema(
            {{"text", string_schema("Text to type")},
             {"window_id", string_schema("Expected focused window id; focus mismatch is refused")}},
            {"text", "window_id"})};
    }
    registration.handler = [gate, &input, window, feed, explicit_window, authorize, actions,
                            cancel](const JsonValue &arguments,
                                    const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            return cancelled_error();
        }
        const auto text = string_member(arguments, "text");
        const auto window_id = string_member(arguments, "window_id");
        if (explicit_window && (window_id.empty() || window_id.size() > 256))
            return atom_error(ErrorCode::InvalidArgument, "invalid window id");
        if (explicit_window &&
            (text.size() > (64u << 10) || !mirage::desktop::is_valid_utf8(text))) {
            return atom_error(ErrorCode::InvalidArgument, "invalid or over-budget UTF-8 text");
        }
        if (!authorized(gate, "input.inject", explicit_window ? window_id : text, context,
                        authorize, cancel)) {
            if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled())
                return cancelled_error();
            return permission_denied("input.inject");
        }
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            return cancelled_error();
        }
        mirage::desktop::CancelToken operation_cancel{[cancel, context] {
            return cancel.cancelled() || context.cancelled_or_expired(mira::Timestamp::now());
        }};
        mirage::desktop::ActionCoordinator::Lease lease;
        if (actions) {
            lease = actions->try_begin(operation_cancel);
            if (!lease)
                return provider_error(lease.error());
        }
        if (explicit_window) {
            if (window == nullptr) {
                return atom_error(ErrorCode::UnsupportedCapability, "window provider unavailable");
            }
            const auto front = window->front_window(cancel);
            if (!front.ok) {
                return provider_error(front.error);
            }
            if (!front.found || front.window.id != window_id) {
                return atom_error(ErrorCode::InvalidState, "focused window changed; re-observe");
            }
        }
        // Overlay mirror (M5-09, DEC-029): the upcoming typing with the
        // focused window's geometry; a failed lookup degrades to a
        // highlight-less hint and never blocks the action.
        if (feed.show_action) {
            AtomOverlayAction action;
            // The hint counts codepoints, not bytes: the tool input is
            // UTF-8, so raw size() would overcount multi-byte text.
            std::size_t codepoints = 0;
            for (const char character : text) {
                codepoints += (static_cast<unsigned char>(character) & 0xC0) != 0x80 ? 1u : 0u;
            }
            action.hint = "typing " + std::to_string(codepoints) + " character(s)";
            if (window != nullptr) {
                const auto front = window->front_window(cancel);
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
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            if (feed.show_action)
                feed.show_action(AtomOverlayAction{});
            return cancelled_error();
        }
        mirage::desktop::InputLimits limits;
        if (context.deadline) {
            limits.timeout =
                std::min(limits.timeout, std::chrono::duration_cast<std::chrono::milliseconds>(
                                             *context.deadline - std::chrono::steady_clock::now()));
            if (limits.timeout.count() <= 0)
                return cancelled_error();
        }
        mirage::desktop::InputOutcome outcome;
        try {
            outcome = input.type_text(text, limits, operation_cancel);
        } catch (...) {
            lease.release_failed();
            return atom_error(ErrorCode::PlatformError,
                              "input provider exception; cleanup unconfirmed");
        }
        if (!outcome.input_released) {
            lease.release_failed();
            return atom_error(ErrorCode::PlatformError,
                              "input_release_failed: cleanup unconfirmed");
        }
        if (feed.show_action) {
            feed.show_action(AtomOverlayAction{}); // the action settled
        }
        if (!outcome.ok) {
            return provider_error(outcome.error);
        }
        JsonValue::Object result;
        if (explicit_window) {
            result.emplace_back("window_id", json_string(window_id));
            result.emplace_back("status", json_string("dispatched"));
            result.emplace_back("verified", json_bool(false));
        }
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

AtomRegistration accessibility_semantic_snapshot(
    mirage::desktop::AccessibilityProvider &accessibility, const AtomOverlayFeed &feed,
    const mirage::desktop::CancelToken &cancel = {}, bool scoped = false) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.accessibility.semantic_snapshot";
    registration.spec.description =
        "Takes the semantic snapshot (interactable element tree) of one window and returns it in "
        "the deterministic rendered form. Budget: 4096 nodes. If unavailable or too large, "
        "use desktop.vision.ocr/mark with a bounded ROI. Never invent @e refs.";
    registration.spec.parameters_schema = mira::JsonSchema{object_schema(
        {{"window_id", string_schema("Window id from desktop.window.list")}}, {"window_id"})};
    registration.handler = [&accessibility, feed, scoped,
                            cancel](const JsonValue &arguments,
                                    const mira::OperationContext &context) -> Result<JsonValue> {
        if (context.cancelled_or_expired(mira::Timestamp::now()) || cancel.cancelled()) {
            return cancelled_error();
        }
        mirage::desktop::SemanticSnapshotLimits limits;
        limits.max_nodes = kAtomSnapshotNodes;
        const auto outcome =
            accessibility.semantic_snapshot(string_member(arguments, "window_id"), limits, cancel);
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
        if (scoped)
            result.emplace_back("backend_scope", json_string(outcome.scope));
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

// M7-01: one issued observation per turn; no cross-session reference cache.
struct HarnessObservation {
    std::string scope, backend_scope, session, task, window;
    // DEC-067: an exact page identity, required only by local knowledge actions.
    std::optional<std::string> window_title;
    std::uint64_t epoch = 0;
    std::chrono::steady_clock::time_point captured;
    mirage::desktop::WindowGeometry geometry;
    std::vector<std::string> windows, applications;
};

Error action_failure(const mirage::desktop::ProviderError &failure, bool effect = false,
                     bool released = true) {
    auto error = provider_error(failure);
    error.safe_message = mira::to_json_string(
        JsonValue{JsonValue::Object{{"schema", json_int(2)},
                                    {"status", json_string(effect ? "uncertain" : "rejected")},
                                    {"verified", json_bool(false)},
                                    {"side_effect_may_have_occurred", json_bool(effect)},
                                    {"input_released", json_bool(released)},
                                    {"error_code", json_string(failure.code)},
                                    {"message", json_string(failure.message)}}});
    return error;
}

AtomRegistration scoped_mutation(const std::string &name, const DesktopToolContext &desktop,
                                 const std::shared_ptr<HarnessObservation> &observation,
                                 const mirage::desktop::CancelToken &parent_cancel) {
    AtomRegistration registration;
    registration.spec.wire_name = name;
    registration.spec.version = {2, 2, 0};
    const std::string use =
        name == "desktop.input.click" ? "Click a semantic @e reference from the latest snapshot. "
        : name == "desktop.input.key" ? "Send a key chord to the observed focused window. "
        : name == "desktop.input.type_text" ? "Type text into the observed focused field. "
                                            : "Activate an observed window. ";
    registration.spec.description =
        use +
        "Complete desktop action bound to the latest observation. "
        "Declare verify_kind before dispatch: foreground_changed (UI progress only), "
        "foreground_title_equals (expected_title), display_text_exists (display_id/expected_text), "
        "window_focused, window_title_equals "
        "(expected_title), "
        "semantic_node_exists (expected_role/expected_name), or visual_text_exists (expected_text; "
        "fresh OCR, one exact text inside the focused window, confidence >= 0.8). Without a "
        "condition input remains "
        "unverified. Re-observe after dispatch. Never retry uncertain side effects automatically.";
    registration.spec.has_side_effects = true;
    JsonValue::Object properties{
        {"window_id", string_schema("Observed target window")},
        {"observation_scope", string_schema("Latest tool-issued observation scope")},
        {"environment_epoch", integer_schema(1, INT64_MAX, "Epoch from that observation")}};
    std::vector<std::string> required{"window_id", "observation_scope", "environment_epoch"};
    for (const auto *field : {"verify_kind", "expected_title", "expected_role", "expected_name",
                              "expected_text", "display_id"})
        properties.emplace_back(
            field, string_schema("Fixed postcondition; text at most 1024 UTF-8 bytes"));
    const bool typing = name == "desktop.input.type_text";
    const bool key = name == "desktop.input.key";
    const bool click = name == "desktop.input.click";
    if (typing || key || click) {
        const auto field = typing ? "text" : key ? "key" : "ref";
        properties.emplace_back(field,
                                string_schema(typing ? "UTF-8, at most 64 KiB"
                                              : key  ? "Complete canonical chord, e.g. ctrl+shift+t"
                                                     : "Semantic @eN from this observation"));
        required.emplace_back(field);
    }
    registration.spec.parameters_schema =
        mira::JsonSchema{object_schema(std::move(properties), std::move(required))};
    registration.handler = [desktop, observation, parent_cancel, typing, key,
                            click](const JsonValue &args,
                                   const mira::OperationContext &ctx) -> Result<JsonValue> {
        mirage::desktop::CancelToken cancel{[parent_cancel, ctx] {
            return parent_cancel.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now());
        }};
        const auto window_id = string_member(args, "window_id");
        const auto scope = string_member(args, "observation_scope");
        const auto epoch = int_member(args, "environment_epoch").value_or(0);
        const auto text = string_member(args, "text");
        const auto key_name = string_member(args, "key");
        const auto ref = string_member(args, "ref");
        if (window_id.empty() || window_id.size() > 256 || scope.size() > 128 ||
            (typing && (text.size() > (64u << 10) || !mirage::desktop::is_valid_utf8(text))) ||
            (key && !mirage::desktop::is_valid_key_name(key_name)) ||
            (click && (ref.size() > 32 || !ref.starts_with("@e"))))
            return action_failure({"invalid_argument", "invalid or over-budget action target"});
        if (cancel.cancelled())
            return action_failure({"cancelled", "action cancelled before authorization"});
        const bool activation = !typing && !key && !click;
        const auto valid_observation = [&] {
            return !scope.empty() && scope == observation->scope && epoch > 0 &&
                   static_cast<std::uint64_t>(epoch) == observation->epoch &&
                   observation->session == ctx.session.to_string() &&
                   std::chrono::steady_clock::now() - observation->captured <=
                       std::chrono::seconds(5) &&
                   (activation ? std::find(observation->windows.begin(), observation->windows.end(),
                                           window_id) != observation->windows.end()
                               : observation->window == window_id) &&
                   (!click || !observation->backend_scope.empty());
        };
        if (!valid_observation())
            return action_failure(
                {"stale_observation", "target requires a new matching observation"});
        const auto cap = activation ? "window.activate" : "input.inject";
        if (!authorized(desktop.gate, cap, window_id, ctx, desktop.authorize, cancel))
            return action_failure({cancel.cancelled() ? "cancelled" : "permission_denied",
                                   cancel.cancelled() ? "action cancelled during authorization"
                                                      : "permission denied by policy"});
        if (!valid_observation())
            return action_failure(
                {"stale_observation", "observation expired during authorization"});
        auto lease = desktop.actions->try_begin(cancel, static_cast<std::uint64_t>(epoch));
        if (!lease)
            return action_failure(lease.error(), false,
                                  lease.error().code != "input_release_failed");
        auto *window = desktop.environment->window();
        mirage::desktop::InputLimits limits;
        if (ctx.deadline)
            limits.timeout =
                std::min(limits.timeout, std::chrono::duration_cast<std::chrono::milliseconds>(
                                             *ctx.deadline - std::chrono::steady_clock::now()));
        if (cancel.cancelled() || limits.timeout.count() <= 0)
            return action_failure({"cancelled", "action cancelled before dispatch"});
        if (!activation) {
            const auto front = window->front_window(cancel);
            if (!front.ok)
                return action_failure(front.error);
            if (!front.found || front.window.id != window_id ||
                !detail::geometry_equals(front.window.geometry, observation->geometry) ||
                (observation->window_title && front.window.title != *observation->window_title))
                return action_failure(
                    {"focus_changed", "focused window changed or moved; re-observe"});
        }
        mirage::desktop::InputOutcome outcome;
        bool entered = false;
        try {
            if (cancel.cancelled())
                return action_failure({"cancelled", "action cancelled before provider call"});
            entered = true;
            if (activation) {
                const auto result = window->activate(window_id, cancel);
                outcome.ok = result.ok;
                outcome.cancelled = result.cancelled;
                outcome.error = result.error;
                outcome.side_effect_may_have_occurred =
                    result.ok || (!result.ok && result.error.code == "io_error");
            } else if (click) {
                mirage::desktop::ElementTarget target;
                target.reference.id = ref;
                target.snapshot_scope = observation->backend_scope;
                const auto result =
                    desktop.environment->accessibility()->activate_element(target, cancel);
                outcome.ok = result.ok;
                outcome.cancelled = result.cancelled;
                outcome.error = result.error;
                outcome.side_effect_may_have_occurred =
                    result.ok || (!result.ok && result.error.code == "io_error");
            } else {
                auto *input = desktop.environment->input();
                outcome = key ? input->key_chord({key_name}, limits, cancel)
                              : input->type_text(text, limits, cancel);
            }
        } catch (...) {
            // Complete input providers must settle their presses before throwing.
            // An exception cannot prove that cleanup happened: fence the owner.
            if (typing || key)
                lease.release_failed();
            return action_failure({"provider_exception", "desktop provider failed"}, entered,
                                  !(typing || key));
        }
        if (!outcome.input_released) {
            lease.release_failed();
            return action_failure({"input_release_failed", "desktop input cleanup unconfirmed"},
                                  outcome.side_effect_may_have_occurred || outcome.ok, false);
        }
        if (!outcome.ok)
            return action_failure(outcome.error, outcome.side_effect_may_have_occurred,
                                  outcome.input_released);
        // The lease cancellation includes takeover during the provider call.
        // A provider's success cannot override withdrawal of autonomous authority.
        if (cancel.cancelled())
            return action_failure({"cancelled", "action cancelled during provider dispatch"}, true,
                                  outcome.input_released);
        return JsonValue{JsonValue::Object{{"schema", json_int(2)},
                                           {"status", json_string("dispatched")},
                                           {"verified", json_bool(false)},
                                           {"window_id", json_string(window_id)},
                                           {"side_effect_may_have_occurred", json_bool(true)},
                                           {"input_released", json_bool(outcome.input_released)}}};
    };
    return registration;
}

// DEC-071: installed application handoff has its own list-issued scope.
AtomRegistration scoped_application(const std::string &name, const DesktopToolContext &desktop,
                                    const std::shared_ptr<HarnessObservation> &observation,
                                    const mirage::desktop::CancelToken &parent_cancel) {
    AtomRegistration registration;
    registration.spec.wire_name = name;
    registration.spec.version = {2, 1, 0};
    registration.spec.description =
        "Launch an installed application or hand off one HTTP(S) URI to it, even when running. "
        "Use the latest application.list scope/epoch and exact application_id. A candidate is "
        "valid "
        "for at most 60 seconds; the host re-enumerates locally before and after authorization. "
        "The fixed "
        "application_running postcondition verifies process presence only; it does not prove "
        "a browser window, navigation or task completion. Then obtain a fresh window.list and "
        "capture/semantic/OCR evidence of the target page. Never substitute an unrelated window.";
    registration.spec.has_side_effects = true;
    JsonValue::Object properties{
        {"application_id", string_schema("Exact installed id from application.list")},
        {"observation_scope", string_schema("Latest application.list scope")},
        {"environment_epoch", integer_schema(1, INT64_MAX, "Epoch from application.list")}};
    std::vector<std::string> required{"application_id", "observation_scope", "environment_epoch"};
    const bool uri_handoff = name == "desktop.application.open_uri";
    if (uri_handoff) {
        properties.emplace_back(
            "uri", string_schema("HTTP(S), host required, no userinfo/space; <=2048 bytes"));
        required.emplace_back("uri");
    }
    registration.spec.parameters_schema =
        mira::JsonSchema{object_schema(std::move(properties), std::move(required))};
    registration.handler = [desktop, observation, parent_cancel,
                            uri_handoff](const JsonValue &args,
                                         const mira::OperationContext &ctx) -> Result<JsonValue> {
        auto cancel = mirage::desktop::CancelToken{[parent_cancel, ctx] {
            return parent_cancel.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now());
        }};
        const auto id = string_member(args, "application_id");
        const auto uri = string_member(args, "uri");
        const auto scope = string_member(args, "observation_scope");
        const auto epoch = int_member(args, "environment_epoch").value_or(0);
        if (id.empty() || id.size() > 256 || !mirage::desktop::is_valid_utf8(id) ||
            scope.size() > 128 || (uri_handoff && !mirage::desktop::valid_application_uri(uri)))
            return action_failure({"invalid_argument", "invalid application id or HTTP(S) URI"});
        const auto candidate_valid = [&] {
            return !scope.empty() && scope == observation->scope && epoch > 0 &&
                   static_cast<std::uint64_t>(epoch) == observation->epoch &&
                   observation->session == ctx.session.to_string() &&
                   observation->task == ctx.task.to_string() &&
                   desktop.actions->epoch() == observation->epoch &&
                   std::chrono::steady_clock::now() - observation->captured <=
                       std::chrono::seconds(60) &&
                   std::find(observation->applications.begin(), observation->applications.end(),
                             id) != observation->applications.end();
        };
        if (cancel.cancelled())
            return action_failure(
                {"cancelled", "application action cancelled before authorization"});
        if (!candidate_valid())
            return action_failure(
                {"stale_observation", "obtain a fresh application.list for this target"});
        // DEC-074: the cached list names a candidate only. All lookup failures
        // occur before dispatch and therefore cannot imply a side effect.
        auto revalidate = [&]() -> mirage::desktop::ProviderError {
            if (cancel.cancelled())
                return {"cancelled", "application revalidation cancelled"};
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            const mirage::desktop::CancelToken bounded(
                [&] { return cancel.cancelled() || std::chrono::steady_clock::now() >= end; });
            try {
                const auto listed =
                    desktop.environment->application()->list_applications({}, bounded);
                if (bounded.cancelled())
                    return {cancel.cancelled() ? "cancelled" : "application_lookup_timeout",
                            "application revalidation did not complete"};
                if (!listed.ok)
                    return {"application_lookup_failed", "application revalidation failed"};
                if (listed.applications.size() > 512 ||
                    std::count_if(listed.applications.begin(), listed.applications.end(),
                                  [&](const auto &app) { return app.id == id; }) != 1)
                    return {"stale_observation",
                            "application target missing or ambiguous; refresh application.list"};
            } catch (...) {
                return {"provider_exception", "application revalidation failed before dispatch"};
            }
            if (!candidate_valid())
                return {"stale_observation",
                        "application candidate invalidated during revalidation"};
            return {};
        };
        if (const auto issue = revalidate(); !issue.code.empty())
            return action_failure(issue);
        // Do not put URI/query contents in permission diagnostics or persistent history.
        if (!authorized(desktop.gate, "application.launch", id, ctx, desktop.authorize, cancel))
            return action_failure({cancel.cancelled() ? "cancelled" : "permission_denied",
                                   "application authorization refused"});
        if (!candidate_valid())
            return action_failure(
                {"stale_observation", "application observation expired during authorization"});
        if (const auto issue = revalidate(); !issue.code.empty())
            return action_failure(issue);
        auto lease = desktop.actions->try_begin(cancel, static_cast<std::uint64_t>(epoch));
        if (!lease)
            return action_failure(lease.error(), false,
                                  lease.error().code != "input_release_failed");
        mirage::desktop::ApplicationLaunchLimits limits;
        if (ctx.deadline)
            limits.timeout =
                std::min(limits.timeout, std::chrono::duration_cast<std::chrono::milliseconds>(
                                             *ctx.deadline - std::chrono::steady_clock::now()));
        const auto bounded = cancel;
        if (bounded.cancelled() || limits.timeout.count() <= 0)
            return action_failure({"cancelled", "application action cancelled before dispatch"});
        mirage::desktop::ApplicationLaunchOutcome outcome;
        try {
            auto *application = desktop.environment->application();
            outcome = uri_handoff ? application->open_uri(id, uri, limits, bounded)
                                  : application->launch(id, limits, bounded);
        } catch (...) {
            return action_failure({"provider_exception", "application handoff failed"}, true);
        }
        if (!outcome.ok)
            return action_failure(outcome.error, outcome.side_effect_may_have_occurred);
        if (bounded.cancelled())
            return action_failure({"cancelled", "application action cancelled after dispatch"},
                                  true);
        return JsonValue{JsonValue::Object{{"schema", json_int(2)},
                                           {"status", "dispatched"},
                                           {"verified", false},
                                           {"application_id", id},
                                           {"target_revalidated", true},
                                           {"input_released", true},
                                           {"side_effect_may_have_occurred", true},
                                           {"uri_digest", mira::digest_string(uri).to_string()}}};
    };
    return registration;
}

// DEC-067: reusable knowledge contains semantics, never a snapshot-issued ref.
AtomRegistration knowledge_activate(const DesktopToolContext &desktop) {
    AtomRegistration registration;
    registration.spec.wire_name = "desktop.knowledge.activate";
    registration.spec.has_side_effects = true;
    registration.spec.description =
        "Locally resolves one enabled semantic target in the current application/page and "
        "activates it once. Takes new references each time and requires a fresh unique semantic "
        "postcondition that was absent before dispatch. Unknown or ambiguous state fails closed; "
        "uncertain effects must not be retried automatically. Returns a canonical JSON receipt "
        "as a string, allowing Mira's scalar step_result Contains predicate to check verified.";
    JsonValue::Object properties{
        {"knowledge_schema", integer_schema(1, 1, "Knowledge contract version")}};
    std::vector<std::string> required{"knowledge_schema"};
    for (const auto *field :
         {"application", "window_title", "role", "name", "expected_role", "expected_name"}) {
        properties.emplace_back(field, string_schema("Exact UTF-8 text, 1..1024 bytes"));
        required.emplace_back(field);
    }
    registration.spec.parameters_schema =
        mira::JsonSchema{object_schema(std::move(properties), std::move(required))};
    registration.handler = [desktop](const JsonValue &args,
                                     const mira::OperationContext &ctx) -> Result<JsonValue> {
        for (const auto *field :
             {"application", "window_title", "role", "name", "expected_role", "expected_name"}) {
            const auto value = string_member(args, field);
            if (value.empty() || value.size() > 1024 || !mirage::desktop::is_valid_utf8(value) ||
                std::any_of(value.begin(), value.end(),
                            [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
                return action_failure({"invalid_argument", "invalid local knowledge selector"});
        }
        if (int_member(args, "knowledge_schema") != 1)
            return action_failure({"invalid_argument", "unsupported local knowledge version"});
        const auto deadline =
            std::min(std::chrono::steady_clock::now() + std::chrono::seconds{5},
                     ctx.deadline.value_or(std::chrono::steady_clock::time_point::max()));
        const mirage::desktop::CancelToken cancel{[ctx, deadline] {
            return ctx.cancelled_or_expired(mira::Timestamp::now()) ||
                   std::chrono::steady_clock::now() >= deadline;
        }};
        const auto failure = [](const char *code) -> Result<JsonValue> {
            return action_failure({code, "local knowledge requires a new unambiguous state"});
        };
        if (cancel.cancelled())
            return failure("cancelled");
        bool entered_action = false;
        try {
            const auto epoch = desktop.actions->epoch();
            auto *window = desktop.environment->window();
            auto *accessibility = desktop.environment->accessibility();
            const auto front = window->front_window(cancel);
            if (!front.ok)
                return action_failure(front.error);
            if (!front.found || front.window.title != string_member(args, "window_title"))
                return failure("knowledge_page_mismatch");
            const auto read = accessibility->semantic_snapshot(front.window.id, {512}, cancel);
            if (read.cancelled)
                return failure("cancelled");
            if (!read.ok)
                return action_failure(read.error);
            if (read.scope.empty() || read.scope.size() > 128 || read.snapshot.nodes.size() > 512 ||
                read.snapshot.application != string_member(args, "application") ||
                read.snapshot.window_title != front.window.title)
                return failure("knowledge_identity_mismatch");
            const auto role = string_member(args, "role"), name = string_member(args, "name");
            const auto expected_role = string_member(args, "expected_role");
            const auto expected_name = string_member(args, "expected_name");
            const mirage::desktop::SemanticNode *target = nullptr;
            std::size_t matches = 0, post_matches = 0;
            for (const auto &node : read.snapshot.nodes) {
                if (node.role == role && node.name == name) {
                    ++matches;
                    target = &node;
                }
                if (node.role == expected_role && node.name == expected_name)
                    ++post_matches;
            }
            if (matches != 1)
                return failure(matches ? "knowledge_target_ambiguous" : "knowledge_target_missing");
            if (!target->enabled)
                return failure("knowledge_target_disabled");
            if (post_matches)
                return failure("knowledge_postcondition_already_present");
            if (!target->ref.starts_with("@e") || target->ref.size() > 32)
                return failure("knowledge_reference_invalid");
            if (cancel.cancelled() || epoch != desktop.actions->epoch())
                return failure("stale_observation");
            auto observation = std::make_shared<HarnessObservation>();
            observation->scope = mira::OperationId::generate().to_string();
            observation->backend_scope = read.scope;
            observation->window = front.window.id;
            observation->window_title = front.window.title;
            observation->geometry = front.window.geometry;
            observation->epoch = epoch;
            observation->session = ctx.session.to_string();
            observation->task = ctx.task.to_string();
            observation->captured = std::chrono::steady_clock::now();
            const JsonValue action{
                JsonValue::Object{{"window_id", front.window.id},
                                  {"observation_scope", observation->scope},
                                  {"environment_epoch", static_cast<std::int64_t>(epoch)},
                                  {"ref", target->ref},
                                  {"verify_kind", "semantic_node_exists"},
                                  {"expected_role", expected_role},
                                  {"expected_name", expected_name},
                                  {"expected_application", string_member(args, "application")},
                                  {"expected_page_title", front.window.title}}};
            auto mutation = scoped_mutation("desktop.input.click", desktop, observation, cancel);
            detail::DesktopActionVerification verification(desktop, ctx, cancel);
            entered_action = true;
            auto result =
                verification.execute("desktop.input.click", action, ctx, mutation.handler);
            if (!result)
                return result;
            const auto *verified = result.value().find("verified");
            if (!verified || !verified->as_boolean().value_or(false)) {
                auto error =
                    atom_error(ErrorCode::ExecutionUncertain, mira::to_json_string(result.value()));
                error.retryable = false;
                return error;
            }
            auto value = *result.value().as_object();
            value.emplace_back("knowledge_digest", mira::canonical_json_digest(args).to_string());
            // Pinned Mira predicates compare scalar step_result values and do not
            // project nested fields. Preserve the whole proof as canonical JSON text.
            return JsonValue{mira::canonical_json_string(JsonValue{std::move(value)})};
        } catch (...) {
            // Once the action boundary is entered, an escaping exception cannot prove
            // absence of effects (including failures while constructing the receipt).
            return action_failure({"knowledge_observation_failed", "local observation failed"},
                                  entered_action);
        }
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
                          const AtomOverlayFeed &feed,
                          std::shared_ptr<mirage::desktop::ActionCoordinator> actions,
                          detail::PetExclusion exclude_pet) {
    auto toolset = std::shared_ptr<DesktopAtomToolset>(new DesktopAtomToolset());
    toolset->impl_ = std::make_unique<Impl>();
    if (environment == nullptr) {
        return toolset; // No environment, no atoms: the empty registry is honest.
    }

    // Candidates are materialized per available provider; registration is
    // fail closed on the pinned side (schema subset, wire-name collisions),
    // so a rejected atom aborts the build instead of silently shrinking the
    // catalog.
    const auto register_atom = [&registry = *toolset->impl_->registry,
                                exclude_pet](AtomRegistration &&candidate) {
        candidate.handler = detail::exclude_pet_during(candidate.spec.wire_name,
                                                       std::move(candidate.handler), exclude_pet);
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
        register_atom(window_activate(gate, *provider, feed, {}, {}, actions));
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
        register_atom(
            input_type_text(gate, *provider, window_provider, feed, false, {}, {}, actions));
    }
    if (auto *provider = environment->notification()) {
        register_atom(notification_post(gate, *provider));
    }
    if (auto *provider = environment->accessibility()) {
        register_atom(accessibility_semantic_snapshot(*provider, feed));
        if (provider->scoped_references_supported() && window_provider && actions) {
            DesktopToolContext desktop;
            desktop.environment =
                std::shared_ptr<mirage::desktop::DesktopEnvironment>(environment, [](auto *) {
                }); // existing build contract: external owner outlives registry
            desktop.gate = gate;
            desktop.overlay = feed;
            desktop.actions = actions;
            register_atom(knowledge_activate(desktop));
        }
    }
    return toolset;
}

std::shared_ptr<DesktopAtomToolset> DesktopAtomToolset::build(const DesktopToolContext &desktop) {
    auto tools = build(desktop.environment.get(), desktop.gate, desktop.overlay, desktop.actions,
                       desktop.exclude_pet);
    detail::register_local_visual_knowledge(*tools->registry(), desktop);
    return tools;
}

DesktopAtomToolset::~DesktopAtomToolset() = default;

void DesktopAtomToolset::register_harness_tools(mira::BuiltinToolRegistry &registry,
                                                const DesktopToolContext &desktop,
                                                bool allow_mutations,
                                                const mirage::desktop::CancelToken &cancel,
                                                const Decorator &decorate) {
    if (!desktop.environment)
        return;
    auto observation = std::make_shared<HarnessObservation>();
    auto application_observation = std::make_shared<HarnessObservation>();
    const auto add = [&](AtomRegistration candidate, bool observes = false) {
        if (observes && desktop.actions) {
            candidate.spec.version = {2, 0, 0};
            const auto name = candidate.spec.wire_name;
            auto target_observation =
                name == "desktop.application.list" ? application_observation : observation;
            candidate.handler = [handler = std::move(candidate.handler), desktop,
                                 observation = target_observation, name,
                                 cancel](const auto &args,
                                         const mira::OperationContext &ctx) -> Result<JsonValue> {
                const auto epoch = desktop.actions->epoch();
                auto result = handler(args, ctx);
                if (!result)
                    return result;
                if (epoch != desktop.actions->epoch())
                    return action_failure({"stale_observation", "desktop changed during capture"});
                *observation = HarnessObservation{};
                observation->scope = mira::OperationId::generate().to_string();
                observation->epoch = epoch;
                observation->session = ctx.session.to_string();
                observation->task = ctx.task.to_string();
                observation->captured = std::chrono::steady_clock::now();
                if (name == "desktop.application.list") {
                    if (const auto *entries = result.value().find("applications");
                        entries && entries->as_array())
                        for (const auto &entry : *entries->as_array())
                            observation->applications.push_back(string_member(entry, "id"));
                } else if (name == "desktop.window.list") {
                    if (const auto *windows = result.value().find("windows");
                        windows && windows->as_array())
                        for (const auto &entry : *windows->as_array())
                            observation->windows.push_back(string_member(entry, "id"));
                } else if (auto *window = desktop.environment->window()) {
                    const auto front = window->front_window(cancel);
                    const auto expected = name == "desktop.window.front"
                                              ? string_member(result.value(), "id")
                                              : string_member(args, "window_id");
                    const bool no_front = name == "desktop.window.front" && expected.empty() &&
                                          front.ok && !front.found;
                    if (!front.ok || (!no_front && (!front.found || front.window.id != expected)) ||
                        epoch != desktop.actions->epoch())
                        return action_failure(
                            {"stale_observation", "focused window changed during capture"});
                    if (front.found) {
                        observation->window = front.window.id;
                        observation->geometry = front.window.geometry;
                    }
                    observation->backend_scope = string_member(result.value(), "backend_scope");
                }
                auto object = *result.value().as_object();
                object.emplace_back("schema", json_int(2));
                object.emplace_back("observation_scope", json_string(observation->scope));
                object.emplace_back("environment_epoch",
                                    json_int(static_cast<std::int64_t>(epoch)));
                object.emplace_back(
                    "captured_at_ms",
                    json_int(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count()));
                return JsonValue{std::move(object)};
            };
        }
        if (decorate)
            candidate.handler = decorate(candidate.spec.wire_name, std::move(candidate.handler));
        auto result =
            registry.register_tool(std::move(candidate.spec), std::move(candidate.handler));
        if (!result)
            throw std::logic_error("harness desktop registration rejected");
    };
    auto *window = desktop.environment->window();
    if (window) {
        add(window_list(*window, cancel), true);
        add(window_front(*window, cancel), true);
    }
    if (auto *application = desktop.environment->application()) {
        add(application_list(*application, cancel), true);
        if (desktop.actions && allow_mutations && (desktop.gate || desktop.authorize)) {
            add(scoped_application("desktop.application.launch", desktop, application_observation,
                                   cancel));
            if (application->supports_open_uri())
                add(scoped_application("desktop.application.open_uri", desktop,
                                       application_observation, cancel));
        }
    }
    if (auto *accessibility = desktop.environment->accessibility())
        add(accessibility_semantic_snapshot(*accessibility, desktop.overlay, cancel, true), true);
    if (window && desktop.actions && allow_mutations && (desktop.gate || desktop.authorize)) {
        add(scoped_mutation("desktop.window.activate", desktop, observation, cancel));
        if (auto *input = desktop.environment->input();
            input && input->complete_actions_supported()) {
            add(scoped_mutation("desktop.input.type_text", desktop, observation, cancel));
            add(scoped_mutation("desktop.input.key", desktop, observation, cancel));
        }
        if (auto *accessibility = desktop.environment->accessibility();
            accessibility && accessibility->scoped_references_supported())
            add(scoped_mutation("desktop.input.click", desktop, observation, cancel));
    }
}

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
