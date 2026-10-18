#pragma once

#include <mira/tool_executor.hpp>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/overlay_surface.hpp>
#include <mirage/integration/desktop_tool_context.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mirage::integration {

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
/// `workflow.atom.catalog` projects the exposed view. Provider atoms and the
/// DEC-067 local semantic composite register only when their prerequisites
/// are present — the catalog
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
    /// With scoped Accessibility, Window and `actions`, also registers
    /// desktop.knowledge.activate: fresh local resolution, one gated semantic
    /// activation and a new verified postcondition. It stores no execution refs.
    static std::shared_ptr<DesktopAtomToolset>
    build(mirage::desktop::DesktopEnvironment *environment, AtomPermissionGate gate,
          const AtomOverlayFeed &feed = {},
          std::shared_ptr<mirage::desktop::ActionCoordinator> actions = {},
          std::function<desktop::PresentationExclusion(const desktop::CancelToken &)> exclude_pet =
              {});

    /// DEC-069: same provider atoms plus workflow-only OCR/template knowledge when
    /// the external context supplies the running vision and complete action path.
    static std::shared_ptr<DesktopAtomToolset> build(const DesktopToolContext &desktop);

    using Decorator =
        std::function<mira::BuiltinToolHandler(const std::string &, mira::BuiltinToolHandler)>;

    /// M7-02: fresh per-turn registry; four observation atoms and, when
    /// allowed, scoped window activation, semantic click and complete key/text
    /// into an explicit focused window. Input providers must opt in to cleanup.
    /// No process, filesystem, clipboard, raw pointer or held-key tools.
    /// Registration failure throws; callers settle it as a failed turn.
    static void register_harness_tools(mira::BuiltinToolRegistry &registry,
                                       const DesktopToolContext &desktop, bool allow_mutations,
                                       const mirage::desktop::CancelToken &cancel,
                                       const Decorator &decorate);

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
