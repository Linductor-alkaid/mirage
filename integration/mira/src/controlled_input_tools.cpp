#include "controlled_input_tools.hpp"
#include <algorithm>
#include <mirage/desktop/input_cadence.hpp>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using J = mira::JsonValue;
std::string text(const J &v, const char *key) {
    const auto *p = v.find(key);
    return p && p->as_string() ? *p->as_string() : "";
}
std::optional<std::int64_t> integer(const J &v, const char *key) {
    const auto *p = v.find(key);
    return p ? p->as_integer() : std::nullopt;
}
mira::Error failure(const char *code) {
    mira::Error error;
    error.code = std::string_view{code} == "permission_denied" ? mira::ErrorCode::PermissionDenied
                 : std::string_view{code} == "cancelled"       ? mira::ErrorCode::Cancelled
                                                               : mira::ErrorCode::InvalidState;
    error.domain = "mirage.input_session";
    error.safe_message = code;
    return error;
}
J string_schema() { return J{J::Object{{"type", "string"}, {"maxLength", 128}}}; }
J integer_schema() { return J{J::Object{{"type", "integer"}, {"minimum", 1}}}; }
J enumeration(std::initializer_list<const char *> values) {
    J::Array items;
    for (const auto *value : values)
        items.emplace_back(value);
    return J{J::Object{{"type", "string"}, {"enum", std::move(items)}}};
}
} // namespace
ControlledInputTools::ControlledInputTools(kairo::Executor &executor, DesktopToolContext desktop,
                                           mira::OperationContext owner,
                                           desktop::CancelToken cancel,
                                           std::shared_ptr<ControlledInputWorker> worker)
    : executor_(executor), desktop_(std::move(desktop)), owner_(owner),
      cancel_([owner, parent = std::move(cancel)] {
          return parent.cancelled() || owner.cancelled_or_expired(mira::Timestamp::now());
      }),
      worker_(worker ? std::move(worker) : std::make_shared<ControlledInputWorker>(executor)) {}
J ControlledInputTools::snapshot() const {
    if (!session_)
        return J{J::Object{}};
    const auto state = session_->snapshot();
    J::Array keys, buttons;
    for (const auto &key : state.held_keys)
        keys.emplace_back(key);
    for (const auto &button : state.held_buttons)
        buttons.emplace_back(button);
    return J{
        J::Object{{"schema", "input_session_v1"},
                  {"session_id", id_},
                  {"window_id", window_},
                  {"environment_epoch", static_cast<std::int64_t>(session_->environment_epoch())},
                  {"state", desktop::input_session_state_name(state.state)},
                  {"outcome", desktop::input_session_outcome_name(state.outcome)},
                  {"state_version", static_cast<std::int64_t>(state.state_version)},
                  {"command_sequence", static_cast<std::int64_t>(state.command_sequence)},
                  {"commands_remaining", static_cast<std::int64_t>(state.commands_remaining)},
                  {"budget_version", static_cast<std::int64_t>(state.budget_version)},
                  {"idle_budget_ms_remaining", state.idle_budget_ms_remaining},
                  {"held_keys", std::move(keys)},
                  {"held_buttons", std::move(buttons)},
                  {"side_effect_may_have_occurred", state.side_effect_may_have_occurred},
                  {"release_confirmed", state.release_confirmed},
                  {"cancelled", state.cancelled},
                  {"error_code", state.error.code},
                  {"verified", false}}};
}
mira::Error ControlledInputTools::state_error(const desktop::InputSessionReceipt &state) const {
    auto error = failure("input_session_failed");
    error.code = !state.release_confirmed ? mira::ErrorCode::ExecutionUncertain
                 : state.cancelled        ? mira::ErrorCode::Cancelled
                 : state.error.code == "unsupported_platform"
                     ? mira::ErrorCode::UnsupportedCapability
                 : state.error.code == "input_session_rejected" ? mira::ErrorCode::Unavailable
                                                                : mira::ErrorCode::InvalidState;
    J receipt{J::Object{{"schema", "input_session_v1"},
                        {"session_id", id_},
                        {"state", desktop::input_session_state_name(state.state)},
                        {"state_version", static_cast<std::int64_t>(state.state_version)},
                        {"command_sequence", static_cast<std::int64_t>(state.command_sequence)},
                        {"release_confirmed", state.release_confirmed},
                        {"side_effect_may_have_occurred", state.side_effect_may_have_occurred},
                        {"error_code", state.error.code}}};
    error.safe_message = mira::to_json_string(receipt);
    if (error.safe_message.size() > 512) {
        receipt.set("error_code", "input_session_failed");
        error.safe_message = mira::to_json_string(receipt);
    }
    return error;
}
void ControlledInputTools::register_tools(mira::BuiltinToolRegistry &registry,
                                          const Decorator &decorate) {
    if (!desktop_.environment || !desktop_.actions || !desktop_.environment->window() ||
        !desktop_.environment->input() ||
        !desktop_.environment->input()->controlled_input_supported())
        return;
    for (const std::string operation : {"begin", "advance", "observe", "release"}) {
        mira::BuiltinToolSpec spec;
        spec.wire_name = "desktop.input_session." + operation;
        spec.version = {1, 0, 0};
        spec.has_side_effects = operation != "observe";
        spec.description =
            "Controlled input in one fixed focused window (currently X11/XInput2). "
            "Separate versioned holding receipts; no business success claim. begin needs a fresh "
            "window id and environment_epoch. advance requires session_id, latest state_version "
            "and command_sequence+1; supports key_down/up/tap and button_down/up/tap. "
            "Use press=long for a locally timed complete long press (about 1.1-1.25 seconds), "
            "without duration arguments. Explicit down/up holds across screenshot/model calls. "
            "Buttons press at the existing pointer position inside the fixed window; no cursor "
            "movement. "
            "Base keys require an unmodified physical mapping; compose Shift explicitly. "
            "OS auto-repeat is not controlled or guaranteed. "
            "Only this session may mutate input while its lease is active. Maximum 4 held inputs, "
            "64 commands, 15 seconds idle and 60 seconds total. Observation never renews idle. "
            "Use desktop.screen.capture while holding; observe reports state without capturing. "
            "Always release before answering or changing windows. Focus/geometry changes, physical "
            "input, task cancellation, takeover and deadlines force cleanup. System switchers "
            "and held pointer travel are unavailable; do not use this for Alt-Tab. "
            "Use fresh state after every observation; never retry an old sequence.";
        J::Object properties;
        J::Array required;
        if (operation == "begin") {
            properties = {{"window_id", string_schema()}, {"environment_epoch", integer_schema()}};
            required = {"window_id", "environment_epoch"};
        } else {
            properties = {{"session_id", string_schema()}};
            required = {"session_id"};
            if (operation == "advance") {
                properties.emplace_back("sequence", integer_schema());
                properties.emplace_back("state_version", integer_schema());
                properties.emplace_back("command",
                                        enumeration({"key_down", "key_up", "key_tap", "button_down",
                                                     "button_up", "button_tap"}));
                properties.emplace_back("key", string_schema());
                properties.emplace_back("button", enumeration({"left", "middle", "right"}));
                properties.emplace_back("press", enumeration({"normal", "long"}));
                required.emplace_back("sequence");
                required.emplace_back("state_version");
                required.emplace_back("command");
            }
        }
        spec.parameters_schema = mira::JsonSchema{J{J::Object{{"type", "object"},
                                                              {"properties", std::move(properties)},
                                                              {"required", std::move(required)},
                                                              {"additionalProperties", false}}}};
        mira::BuiltinToolHandler handler = [this, operation](const auto &args,
                                                             const auto &ctx) -> mira::Result<J> {
            try {
                return invoke(operation, args, ctx);
            } catch (...) {
                if (session_)
                    session_->stop();
                return failure("input_session_handler_failed");
            }
        };
        if (decorate)
            handler = decorate(spec.wire_name, std::move(handler));
        if (!registry.register_tool(std::move(spec), std::move(handler)))
            throw std::logic_error("input session registration failed");
    }
}
mira::Result<J> ControlledInputTools::invoke(const std::string &operation, const J &args,
                                             const mira::OperationContext &ctx) {
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return failure("input_session_owner_mismatch");
    if (operation == "begin") {
        if (session_ && !session_->snapshot().release_confirmed)
            return failure("input_session_already_active_or_fenced");
        if (cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()))
            return failure("cancelled");
        const auto epoch = integer(args, "environment_epoch");
        const auto window = text(args, "window_id");
        if (!epoch || *epoch <= 0 || window.empty() || window.size() > 128)
            return failure("invalid_input_session_target");
        if (!desktop_.capture_scope.empty() && window != desktop_.capture_scope)
            return failure("input_session_scope_mismatch");
        const auto cancelled = [&] {
            return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now());
        };
        const auto allowed =
            desktop_.authorize ? desktop_.authorize("input.inject", window, cancelled,
                                                    ctx.task.to_string(), ctx.operation.to_string())
                               : desktop_.gate && desktop_.gate("input.inject", window, cancelled);
        if (!allowed)
            return failure("permission_denied");
        const auto front = desktop_.environment->window()->front_window(cancel_);
        if (!front.ok || !front.found || front.window.id != window)
            return failure("input_session_target_not_focused");
        desktop::PointerActionScope scope{window, front.window.geometry};
        auto environment = desktop_.environment;
        const auto stop = cancel_;
        recovery_observed_ = false;
        session_ = std::make_unique<ControlledInputSession>(
            executor_, desktop_.actions, cancel_,
            [environment, scope, stop] {
                return environment->input()->open_input_session(scope, stop);
            },
            ControlledInputSession::Limits{}, worker_);
        id_ = mira::OperationId::generate().to_string();
        window_ = window;
        const auto state = session_->begin(static_cast<std::uint64_t>(*epoch));
        if (!state.error.code.empty())
            return state_error(state);
        return snapshot();
    }
    if (!session_ || text(args, "session_id") != id_)
        return failure("stale_input_session");
    if (operation == "release") {
        (void)session_->release();
        session_->stop();
        return snapshot();
    }
    if (operation == "observe") {
        const auto observed = session_->observe();
        if (observed.error.code == "input_session_unavailable")
            return failure("input_session_unavailable");
        return snapshot();
    }
    if (cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now())) {
        session_->stop();
        return snapshot();
    }
    const auto sequence = integer(args, "sequence"), version = integer(args, "state_version");
    const auto kind = desktop::parse_input_command_kind(text(args, "command"));
    if (!sequence || *sequence <= 0 || !version || *version <= 0 || !kind)
        return failure("invalid_input_command");
    desktop::InputSessionCommand command;
    command.kind = *kind;
    command.key_name = text(args, "key");
    command.button = text(args, "button");
    const bool key = text(args, "command").starts_with("key_");
    const bool tap = command.kind == desktop::InputSessionCommand::Kind::key_tap ||
                     command.kind == desktop::InputSessionCommand::Kind::button_tap;
    const auto press = text(args, "press");
    if ((key && (!command.button.empty() || command.key_name.empty())) ||
        (!key && (!command.key_name.empty() || command.button.empty())) ||
        (!press.empty() && (!tap || (press != "long" && press != "normal"))))
        return failure("invalid_input_command");
    if (press == "long") {
        const auto seed = desktop::make_pointer_motion_variation(0).seed;
        command.dwell = desktop::long_press_cadence_duration(seed);
    }
    const auto allowed =
        desktop_.authorize ? desktop_.authorize(
                                 "input.inject", window_, [this] { return cancel_.cancelled(); },
                                 ctx.task.to_string(), ctx.operation.to_string())
                           : desktop_.gate && desktop_.gate("input.inject", window_,
                                                            [this] { return cancel_.cancelled(); });
    if (!allowed) {
        session_->stop();
        return failure("permission_denied");
    }
    if (command.dwell.count() > 0 && ctx.deadline &&
        std::chrono::steady_clock::now() + command.dwell >= *ctx.deadline) {
        (void)session_->release();
        auto timeout = failure("input_timeout");
        timeout.code = mira::ErrorCode::DeadlineExceeded;
        return timeout;
    }
    const auto state = session_->advance(
        static_cast<std::uint64_t>(*sequence), static_cast<std::uint64_t>(*version),
        std::move(command),
        desktop::CancelToken([ctx] { return ctx.cancelled_or_expired(mira::Timestamp::now()); }));
    if (state.error.code == "stale_input_command")
        return failure("stale_input_command");
    if (state.error.code == "input_session_unavailable")
        return failure("input_session_unavailable");
    if (!state.error.code.empty())
        return state_error(state);
    return snapshot();
}
void ControlledInputTools::note_observation(std::uint64_t observed_epoch) {
    if (!session_)
        return;
    auto state = session_->snapshot();
    if (state.state != desktop::InputSessionState::released &&
        state.state != desktop::InputSessionState::fenced)
        state = session_->observe();
    if (state.release_confirmed && !state.cancelled && !state.error.code.empty() &&
        observed_epoch && observed_epoch >= session_->environment_epoch())
        recovery_observed_ = true;
}
desktop::ProviderError ControlledInputTools::finish() {
    if (!session_)
        return {};
    const auto before = session_->snapshot();
    const bool holding = !before.held_keys.empty() || !before.held_buttons.empty();
    session_->stop();
    const auto after = session_->snapshot();
    if (!after.release_confirmed)
        return {"input_release_failed", "Controlled input release could not be confirmed"};
    if (!before.error.code.empty() && before.side_effect_may_have_occurred && !recovery_observed_)
        return before.error;
    const bool active = before.state != desktop::InputSessionState::released &&
                        before.state != desktop::InputSessionState::fenced;
    if (holding || (active && before.side_effect_may_have_occurred))
        return {"input_session_unclosed",
                "Task ended before explicit input session release; host cleaned up"};
    if (!after.error.code.empty() && after.error.code != "cancelled" &&
        after.side_effect_may_have_occurred && !recovery_observed_)
        return after.error;
    return {};
}
} // namespace mirage::integration::detail
