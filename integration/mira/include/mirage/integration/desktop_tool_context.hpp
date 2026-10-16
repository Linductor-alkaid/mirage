#pragma once

#include <mira/tool_executor.hpp>

#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/overlay_surface.hpp>
#include <mirage/desktop/visual_snapshot.hpp>
#include <mirage/integration/basic_output_retention.hpp>
#include <mirage/integration/environment_visual_pipeline.hpp>

#include <cstddef>
#include <filesystem>
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

/// Shared handler decorator seam (M7-02): wraps one builtin handler with
/// capture, verification and failure tracking. Declared here so the desktop
/// atom toolset and the M10 basic agent toolset share one type.
using Decorator =
    std::function<mira::BuiltinToolHandler(const std::string &, mira::BuiltinToolHandler)>;
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

struct DesktopOcrResult {
    bool ok = false, cancelled = false;
    desktop::ProviderError error;
    desktop::VisualSnapshot snapshot;
    std::string display_id, model_revision;
    std::uint64_t backend_calls = 0;
    /// DEC-059: fresh capture/analysis interval and configured display space.
    /// Verification rejects missing, old or out-of-request evidence.
    desktop::WindowGeometry display_geometry;
    std::chrono::steady_clock::time_point capture_begin{}, capture_end{};
};

/// Service-owned desktop dependencies for a conversational model layer.
/// The gate is shared with task/workflow execution; a missing gate denies
/// mutations. Shared ownership keeps providers alive through model teardown.
/// Enablement and budgets of the four basic agent tools (DEC-095 frozen
/// values; centered config, boundary refusal). The service decides
/// enablement — the model layer never flips it on by itself, and the default
/// registers nothing, so existing products keep their exact tool surface.
struct BasicAgentToolsOptions {
    /// Product switch for the whole group (DEC-095 decision 6: never
    /// implicitly enabled by directory or provider presence).
    bool enabled = false;
    /// Product switch for exec specifically (host-permission execution is
    /// not implied by read/write enablement).
    bool exec_enabled = false;
    /// Stable per-session workspace directory; relative path/workdir
    /// arguments resolve against it. Empty disables relative paths and exec.
    std::filesystem::path workspace;
    std::chrono::milliseconds exec_timeout_default{30000};
    std::chrono::milliseconds exec_timeout_max{120000};
    /// Model-facing output preview per stream (bytes); the frozen combined
    /// budget is 50 KiB across both streams.
    std::size_t exec_preview_bytes = 25u << 10;
};

struct DesktopToolContext {
    std::shared_ptr<mirage::desktop::DesktopEnvironment> environment;
    AtomPermissionGate gate;
    /// Context-aware service gate; preferred over gate when present. IDs
    /// come from the current tool operation, without exposing pinned types.
    std::function<bool(const std::string &, const std::string &, const AtomCancelProbe &,
                       const std::string &task_id, const std::string &operation_id)>
        authorize;
    AtomOverlayFeed overlay;
    /// Explicit external owner, shared with every writer of this desktop.
    /// Missing owner exposes observations only; adapters never create one.
    std::shared_ptr<mirage::desktop::ActionCoordinator> actions;
    /// DEC-062: optional visual supply scope as a window id. When set, the
    /// vision tools only ever supply pixels or text from this window, and
    /// only while it is the focused front window; crops outside it are
    /// rejected. Empty keeps the DEC-058 whole-display observation default.
    /// Host acceptance sets this to the public test window so no user
    /// desktop pixels or text reach the model.
    std::string capture_scope;
    /// DEC-057: bounded capture/analysis over the service-owned OCR pipeline.
    /// Caller authorizes screen.capture first; null means no OCR tool.
    std::function<DesktopOcrResult(const desktop::CancelToken &,
                                   const std::optional<std::chrono::steady_clock::time_point> &)>
        ocr;
    /// DEC-075: caller already acquired and authorized this observation-only
    /// image. Analysis does not capture, publish refs, or infer desktop mapping.
    std::function<ImageOcrOutcome(const desktop::ImageFrame &, const desktop::CancelToken &,
                                  const std::optional<std::chrono::steady_clock::time_point> &)>
        ocr_image;
    /// DEC-058: same-frame SoM/crop. Caller authorizes capture before invoking.
    std::function<EnvironmentVisualRefresh(
        const std::optional<desktop::WindowGeometry> &, const desktop::CancelToken &,
        const std::optional<std::chrono::steady_clock::time_point> &)>
        mark;
    /// Bounded execution against a scope/ref under the producing registry lock.
    std::function<desktop::ProviderError(
        const std::string &, const std::string &,
        const std::function<void(const desktop::VisualRegionEntry &)> &)>
        with_visual_reference;
    std::function<desktop::ProviderError(
        const std::string &, const std::string &, const std::string &,
        const std::function<void(const desktop::VisualRegionEntry &,
                                 const desktop::VisualRegionEntry &)> &)>
        with_visual_references;
    /// M10-07 (DEC-095): the basic agent tools group; see
    /// BasicAgentToolsOptions above.
    BasicAgentToolsOptions basic_tools;
    /// M10-06 (DEC-095): runtime-owned bounded output retention. Null keeps
    /// exec preview-only; when set, exec admits retention before spawn and
    /// read resolves output_ref arguments through it.
    BasicOutputRetention *output_retention = nullptr;
};

} // namespace mirage::integration
