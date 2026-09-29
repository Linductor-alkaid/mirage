#pragma once

#include <mira/tool_executor.hpp>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/overlay_surface.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mirage::integration {

/// Cancellation probe the permission gate polls while it waits (DEC-020
/// discipline: bounded wait, poll the caller's probe, silence never
/// approves). Same shape as the runtime permission face's CancelProbe; kept
/// a distinct alias because this layer does not include runtime headers.
using AtomCancelProbe = std::function<bool()>;

/// RULE-05 seam between the atom execution path and the host's permission
/// face (DEC-024): judged before an atom handler touches its provider.
/// `capability` is a stable permission vocabulary name ("clipboard.write",
/// "process.execute", ...); `resource` is the atom's primary target (path,
/// command line, window id, ...) already capped by the tool input budget.
/// Returns true when the capability use is allowed. A null gate denies
/// every gated atom (fail closed); observation atoms without a capability
/// entry are never judged through it.
using AtomPermissionGate = std::function<bool(
    const std::string &capability, const std::string &resource, const AtomCancelProbe &cancelled)>;

/// One upcoming desktop action mirrored onto the Desktop Overlay (M5-09,
/// DEC-029): the "即将执行操作提示" line plus the target highlights. An
/// empty update clears the action face (published after the action
/// settles).
struct AtomOverlayAction {
    std::string hint;
    std::vector<mirage::desktop::OverlayHighlight> highlights;
};

/// Overlay seam between the atom execution path and the service's Desktop
/// Overlay presenter (M5-09, DEC-029): wired per build, null members keep
/// the mirror dark with zero per-call cost (the AtomPermissionGate seam
/// precedent). `show_action` fires after the permission judgement and
/// before the provider side effect; `show_observation` fires when an
/// observation atom delivered a snapshot (the Observation debug face
/// consumes it only when the service enabled that face). Both callbacks
/// must be bounded and must not throw.
struct AtomOverlayFeed {
    std::function<void(const AtomOverlayAction &)> show_action;
    std::function<void(const mirage::desktop::SemanticSnapshot &)> show_observation;
};

/// Wire-facing projection of one registered desktop atom — the
/// `mirage.ipc` ExposedTool shape (DEC-023), pinned-free. `version` is the
/// semantic version rendered "major.minor.patch"; `parameters_schema_json`
/// is the serialized JSON Schema object of the tool input.
struct DesktopAtomView final {
    std::string wire_name;
    std::string version;
    std::string description;
    bool has_side_effects = false;
    std::string parameters_schema_json;
};

/// The desktop atom toolset (DEC-024): builds the pinned BuiltIn tool
/// registry that carries Mirage's desktop capability atoms, so workflow
/// ToolCall steps dispatch real provider actions and
/// `workflow.atom.catalog` projects the exposed view. One atom per provider
/// call; atoms for absent providers are simply not registered — the catalog
/// reports what the bound environment can actually deliver, never what a
/// static table claims.
///
/// The execution path is bounded and fail closed: every handler checks the
/// drive's cancellation probe, judges its permission capability through the
/// gate (when the atom has one), and maps provider outcomes onto the pinned
/// error vocabulary. Handlers never throw; a provider failure becomes a
/// failed tool record carrying the stable provider code.
class DesktopAtomToolset final {
  public:
    /// Builds the toolset over `environment`'s non-null providers. A null
    /// environment yields the empty toolset (zero atoms): a binding without
    /// a mirage desktop environment has no capabilities to expose. The
    /// environment must outlive the returned registry's runtime attachment.
    /// `gate` may be null, which denies every gated atom. `feed` may be
    /// default (dark members): the desktop-position atoms then mirror
    /// nothing onto the Desktop Overlay (M5-09, DEC-029).
    static std::shared_ptr<DesktopAtomToolset>
    build(mirage::desktop::DesktopEnvironment *environment, AtomPermissionGate gate,
          const AtomOverlayFeed &feed = {});

    ~DesktopAtomToolset();
    DesktopAtomToolset(const DesktopAtomToolset &) = delete;
    DesktopAtomToolset &operator=(const DesktopAtomToolset &) = delete;

    /// The registry behind the toolset; never null. The workflow surface
    /// installs it via the pinned set_tool_registry() at attach time.
    [[nodiscard]] std::shared_ptr<mira::BuiltinToolRegistry> registry() const;

    /// The exposed view in the registry's deterministic (wire-name sorted)
    /// order — the catalog face's projection source.
    [[nodiscard]] std::vector<DesktopAtomView> exposed_atoms() const;

  private:
    DesktopAtomToolset() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::integration
