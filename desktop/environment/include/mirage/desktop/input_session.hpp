#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// DEC-110 (M7-25 freeze): declared input capabilities. A backend declares
/// each capability it actually delivers on the current desktop/source/
/// authorization; absence refuses the affected action before any side
/// effect — a working click never implies timed press or held keys, and a
/// successful device enumeration never implies that targets accept the
/// input. Capabilities are orthogonal: compatibility with filtered targets
/// (kernel_virtual_device / external_hid) is independent from timing
/// features (smooth_move / timed_press / ...), so every combination is
/// declared, never inferred.
enum class InputCapability : std::uint16_t {
    /// Software event injection (XTEST / SendInput / authorized Portal).
    software_injection = 1 << 0,
    /// Timed pointer travel from the read-back current position to a target.
    smooth_move = 1 << 1,
    /// Complete press with an explicit non-zero dwell time.
    timed_press = 1 << 2,
    /// Modifier keys pressed, held and released as standalone inputs,
    /// including left/right physical sides.
    independent_modifiers = 1 << 3,
    /// A key or button stays held while the task observes and decides
    /// (cross-tool, cross-model-round ownership).
    cross_observation_hold = 1 << 4,
    /// Capture (screenshot) is deliverable while inputs are held.
    hold_capture = 1 << 5,
    /// The backend's repeat semantics for sustained keys are verified;
    /// without this, sustained hold must not be faked by repeated taps.
    key_repeat = 1 << 6,
    /// IME/layout/conversion/preedit state is observable (projected, M7-28).
    ime_state = 1 << 7,
    /// IME conversion can be switched by explicit, honestly reported actions.
    ime_control = 1 << 8,
    /// Kernel virtual input device backend (uinput / VHF); deployment and
    /// permission premises are declared separately from this flag.
    kernel_virtual_device = 1 << 9,
    /// External USB HID device backend; needs hardware plus release-supervision
    /// verification before held keys are allowed on it.
    external_hid = 1 << 10,
};

/// Declared capability set of one backend at admission time. The set is
/// fixed when an input session is admitted (DEC-110 section 5: no hot
/// device/backend swap on an active session).
class InputCapabilitySet {
  public:
    constexpr InputCapabilitySet() = default;
    constexpr InputCapabilitySet with(InputCapability capability) const {
        return InputCapabilitySet{
            static_cast<std::uint16_t>(mask_ | static_cast<std::uint16_t>(capability))};
    }
    constexpr bool has(InputCapability capability) const {
        return (mask_ & static_cast<std::uint16_t>(capability)) != 0;
    }
    constexpr std::uint16_t mask() const { return mask_; }
    constexpr bool operator==(const InputCapabilitySet &) const = default;

  private:
    constexpr explicit InputCapabilitySet(std::uint16_t mask) : mask_(mask) {}
    std::uint16_t mask_ = 0;
};

/// Stable name of one InputCapability (wire/golden vocabulary).
std::string_view input_capability_name(InputCapability);
/// Parses a capability name; nullopt on unknown names.
std::optional<InputCapability> parse_input_capability(std::string_view);
/// Every capability in declaration (bit) order.
std::array<InputCapability, 11> all_input_capabilities();

/// Session-layer key names (DEC-110 section 1: explicit physical/logical
/// semantics, frozen vocabulary at M7-25). A session expresses modifier
/// keys as standalone down/up commands, so chord prefixes ("ctrl+...") are
/// NOT session names — combinations are composed by the command sequence:
/// the complete-chord vocabulary of the existing tools is unchanged.
///
/// - Base keys reuse the InputProvider KeySym base vocabulary (printable
///   characters, "enter", "tab", ..., "f1".."f12") and mean the logical
///   character/position the platform maps them to.
/// - Bare modifiers "ctrl", "alt", "shift", "meta" mean the logical
///   modifier; when the platform distinguishes sides the backend maps a
///   bare modifier to the left physical key and reports that mapping.
/// - Sided modifiers "ctrlleft"/"ctrlright", "altleft"/"altright",
///   "shiftleft"/"shiftright", "metaleft"/"metaright" address one physical
///   key; backends without side distinction refuse them.
struct SessionKeyName {
    enum class Modifier : std::uint8_t { none, ctrl, alt, shift, meta };
    enum class Side : std::uint8_t { none, left, right };
    Modifier modifier = Modifier::none; ///< set only for bare/sided modifiers
    Side side = Side::none;             ///< left/right for sided modifiers
    std::string base;                   ///< base key (empty for modifiers)
};

/// Parses one session-layer key name; nullopt when the name is outside the
/// frozen vocabulary. Shared by callers and backends so validation cannot
/// diverge from injection.
std::optional<SessionKeyName> parse_session_key_name(std::string_view name);
bool is_valid_session_key_name(std::string_view name);
/// Stable spelling of a parsed name (canonical round trip).
std::string session_key_name_text(const SessionKeyName &);

/// Frozen session budgets (DEC-110 sections 2/3; M7-25 freeze, budget
/// version below). Every bound refuses before effects, never truncates:
/// exceeding a budget ends the session through the release path instead of
/// silently extending it. Automatic renewal does not exist; only explicit
/// re-admission starts a new budgeted session.
namespace input_session_budget {
/// Held keys + buttons per session (projection and cleanup bound).
inline constexpr std::uint32_t max_held_inputs = 4;
/// Mutation commands (down/up/tap/move) per session; observe is not a
/// mutation and is bounded by the idle budget instead.
inline constexpr std::uint32_t max_session_commands = 64;
/// Sampled points of one smooth-move trajectory.
inline constexpr std::uint32_t max_path_points = 64;
/// Default sampling when the caller does not pin a point count.
inline constexpr std::uint32_t default_path_points = 32;
/// Wall-clock duration of one smooth move: long enough for full-desktop
/// crossings at a calm pace, short enough to bound hover side effects.
inline constexpr std::chrono::milliseconds min_move_duration{50};
inline constexpr std::chrono::milliseconds max_move_duration{2000};
/// One complete timed press (tap with dwell) finishes inside one command;
/// longer presses are session holds that must pass the decision budgets.
inline constexpr std::chrono::milliseconds max_press_dwell{1500};
/// No-command deadline, covering screenshot/OCR/model feedback wait: held
/// inputs are released before it expires; repeated observation never
/// renews it (DEC-110 section 3).
inline constexpr std::chrono::milliseconds max_idle{15000};
/// Total lifetime of one session including all feedback waits.
inline constexpr std::chrono::milliseconds max_session_total{60000};
/// Reproducible acceleration baseline for smooth moves (DEC-110 section 2:
/// minimum-jerk, no random jitter/overshoot in the frozen default).
inline constexpr std::string_view default_move_profile = "minimum_jerk_v1";
/// Version of this budget table; receipts carry it so a stale client
/// cannot misread remaining budgets.
inline constexpr std::uint32_t budget_version = 1;
} // namespace input_session_budget

/// Lifecycle of one Task-owned input session (DEC-110 section 3). The task
/// is the logical owner; the host keeps the single-writer lease under the
/// one ActionCoordinator. `held keys/buttons` live in the host — models
/// only ever see the receipt projection and cannot drive cleanup by
/// claiming state.
enum class InputSessionState : std::uint8_t {
    /// Admitted, no input held yet.
    ready,
    /// Inputs may be held; commands settle here.
    executing,
    /// Waiting for screenshot/OCR/model feedback; budgets keep running.
    awaiting_feedback,
    /// Releasing held inputs (also the cleanup path for cancel/timeout).
    releasing,
    /// Terminal: all held inputs confirmed released.
    released,
    /// Terminal: release or state confirmation failed; further desktop
    /// mutations stay fenced (mirrors ActionCoordinator input_released=false).
    fenced,
};
const char *input_session_state_name(InputSessionState);
std::optional<InputSessionState> parse_input_session_state(std::string_view);

/// Driving events of the session state machine.
enum class InputSessionEvent : std::uint8_t {
    /// Admission succeeded (permissions, freshness, capabilities, budget).
    admit,
    /// A command was accepted and dispatched.
    command_admitted,
    /// A dispatched command settled (delivered, failed, or cancelled).
    command_settled,
    /// The task asked for observation before the next command.
    observe,
    /// Another observation while already awaiting feedback; the idle
    /// deadline is NOT extended by this event.
    observe_repeat,
    /// Explicit release, or the forced release path (cancel/timeout/
    /// takeover/authority loss/device loss/shutdown/budget exhaustion).
    release_requested,
    /// Held inputs confirmed released.
    release_confirmed,
    /// Release or state confirmation failed -> fenced.
    release_failed,
};
const char *input_session_event_name(InputSessionEvent);

/// Pure transition step. nullopt means the event is invalid in the state
/// (late commands, double admission, events on terminals) and must be
/// refused without side effects; terminals never transition.
std::optional<InputSessionState> advance_input_session(InputSessionState, InputSessionEvent);

/// One logical session mutation (DEC-110 section 3: begin/advance/observe/
/// release are logical operations, not registered tool names and not a
/// free-form input scripting surface). Plain structs keep the frozen
/// schema reviewable; the runtime binding maps them to platform events.
struct InputSessionCommand {
    enum class Kind : std::uint8_t {
        key_down,
        key_up,
        /// Complete press of one base key with an explicit dwell.
        key_tap,
        button_down,
        button_up,
        /// Complete press of one button with an explicit dwell.
        button_tap,
        /// Timed travel from the read-back position to (x, y).
        smooth_move,
    };
    Kind kind = Kind::key_tap;
    /// Session key vocabulary; required by key_* kinds.
    std::string key_name;
    /// InputProvider button vocabulary ("left"/"middle"/"right");
    /// required by button_* kinds.
    std::string button;
    /// Global desktop coordinates; required by smooth_move.
    std::int32_t x = 0, y = 0;
    /// key_tap/button_tap hold time; 0 means the platform minimal press.
    std::chrono::milliseconds dwell{0};
    /// smooth_move total duration; must be inside the frozen bounds.
    std::chrono::milliseconds duration{0};
    /// smooth_move sampled points; 0 selects default_path_points.
    std::uint32_t path_points = 0;
};

const char *input_command_kind_name(InputSessionCommand::Kind);
std::optional<InputSessionCommand::Kind> parse_input_command_kind(std::string_view);

/// Admission-time facts a command is validated against. `held_inputs` is
/// the host-tracked count; `commands_used` counts settled+dispatched
/// mutations of this session.
struct InputSessionCommandContext {
    InputCapabilitySet capabilities;
    std::uint32_t held_inputs = 0;
    std::uint32_t commands_used = 0;
};

/// Validates one command against the frozen vocabulary, budgets and
/// declared capabilities. Returns nullopt when the command is acceptable;
/// otherwise the ProviderError is stable ("invalid_argument",
/// "unsupported_platform" for absent capabilities, "result_too_large" for
/// budget exhaustion) and the caller must refuse before any injection.
std::optional<ProviderError> validate_input_session_command(const InputSessionCommand &,
                                                            const InputSessionCommandContext &);

/// Validates smooth-move timing against the frozen M7-25 budgets: duration
/// inside [min_move_duration, max_move_duration] and path_points within
/// max_path_points (0 selects default_path_points). Shared by the provider
/// default, backends and tool validation so the frozen bounds cannot
/// diverge between layers.
std::optional<ProviderError> validate_smooth_move_timing(std::chrono::milliseconds duration,
                                                         std::uint32_t path_points);

/// Automatic approach policy, independent of the held-session budget table.
/// Model-facing click tools expose intent only; backends receive this local plan.
namespace pointer_motion_policy {
inline constexpr std::uint32_t version = 2;
inline constexpr std::chrono::milliseconds min_duration{120}, max_duration{650};
inline constexpr std::chrono::milliseconds action_timeout{3000};
inline constexpr std::int32_t default_target_width = 32;
} // namespace pointer_motion_policy
/// Deterministic distance/width policy: ceil(120 + 90*log2(1 + distance/width)),
/// clamped to [120,650] ms; width defaults to 32 and is floored at 8 pixels.
/// Zero distance returns zero (no motion). No random jitter or model timing.
std::chrono::milliseconds automatic_pointer_duration(PointerPoint from, PointerPoint to,
                                                     std::int32_t target_width = 0);
/// A fresh local seed per operation; exceptions must become pre-effect provider errors.
PointerMotionVariation make_pointer_motion_variation(std::int32_t target_width);
/// Nominal distance/width duration with seeded +/-12% variation, inside the same bounds.
std::chrono::milliseconds varied_pointer_duration(PointerPoint from, PointerPoint to,
                                                  const PointerMotionVariation &variation);

/// One sampled point of the frozen smooth-move schedule: `at` is the offset
/// from travel start, (x, y) the eased position.
struct SmoothMoveSample {
    std::chrono::milliseconds at{0};
    std::int32_t x = 0, y = 0;
};

/// Builds the reproducible minimum_jerk_v1 schedule (DEC-110 section 2) from
/// `from` to `to`: effective segments = min(requested-or-default, duration
/// over the 4 ms cadence floor, max_path_points), positions follow the
/// minimum-jerk easing, and the final sample is exactly `to`. Returns an
/// empty vector when from == to (nothing to travel). Callers must validate
/// duration/points first (validate_smooth_move_timing); pacing, cancellation
/// and lag handling stay with the caller because they are transport-bound.
/// A host-only variation adds bounded lateral bend and terminal correction/overshoot.
std::vector<SmoothMoveSample>
build_smooth_move_schedule(PointerPoint from, PointerPoint to, std::chrono::milliseconds duration,
                           std::uint32_t path_points,
                           std::optional<PointerMotionVariation> variation = std::nullopt);

/// Receipt classes (DEC-110 section 3): admission, delivery, controlled
/// holding, confirmed release and "cannot confirm" are distinguished. The
/// legacy `InputOutcome::input_released=false` keeps meaning failure
/// fencing only — a normal hold is reported as holding, never by reusing
/// that flag, and hold receipts are versioned so old clients never
/// consume them.
enum class InputSessionOutcome : std::uint8_t {
    /// Command admitted, not yet settled.
    admitted,
    /// Command settled as delivered.
    delivered,
    /// Inputs held under session control (cross observation).
    holding,
    /// All held inputs confirmed released (clean terminal).
    released,
    /// Delivery/release could not be confirmed; side effects uncertain.
    uncertain,
};
const char *input_session_outcome_name(InputSessionOutcome);

/// Versioned session receipt (DEC-110 section 3): state version, remaining
/// budget and side-effect facts travel with every response. Field set is
/// frozen with `version` below; additive evolution bumps the version.
struct InputSessionReceipt {
    static constexpr std::uint32_t version = 1;
    InputSessionState state = InputSessionState::ready;
    InputSessionOutcome outcome = InputSessionOutcome::admitted;
    /// Monotonic per-session state version; late consumers compare it
    /// against their last observation instead of trusting wall time.
    std::uint64_t state_version = 0;
    /// Sequence of the last settled mutation command.
    std::uint64_t command_sequence = 0;
    std::uint32_t budget_version = input_session_budget::budget_version;
    std::uint32_t commands_remaining = 0;
    std::int64_t idle_budget_ms_remaining = 0;
    /// Host-held inputs, projected for the model; the host set stays
    /// authoritative for cleanup.
    std::vector<std::string> held_keys;
    std::vector<std::string> held_buttons;
    /// True when the command may have reached the desktop but its effect
    /// is unverified (mirrors InputOutcome side-effect semantics).
    bool side_effect_may_have_occurred = false;
    /// True only in InputSessionState::released; never set while inputs
    /// are held (DEC-110: input_released semantics are not reused).
    bool release_confirmed = false;
    bool cancelled = false;
    ProviderError error; ///< meaningful for failures and uncertain outcomes
};

} // namespace mirage::desktop
