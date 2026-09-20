#pragma once

#include <memory>
#include <string>

#include <mirage/desktop/desktop_environment.hpp>

#include <mira/environment.hpp>

#include <mirage/integration/mira_adapter.hpp>

namespace mirage::integration {

/// Concrete binding that hosts one mirage::desktop::DesktopEnvironment inside
/// the pinned Mira runtime (design doc section 11, DEC-008). The most derived
/// type implements both the pinned-free binding interface and the pinned
/// environment contract, so MiraHost::start() recovers the contract with a
/// runtime cross-cast.
///
/// The pinned surface is adapted honestly to what the bound desktop
/// environment can deliver (M2-06):
///
/// - capabilities() is derived from the environment's provider accessors:
///   foreground_app follows window(), ui_tree needs window() and
///   accessibility() (the semantic snapshot is taken against the focused
///   window). screen_capture stays false until the M3 Mirador integration
///   gives the binding an artifact store and published frame payloads
///   (pinned ScreenFrameDescriptor contract); discrete_input stays false
///   because the pinned canonical InputSequence is not mapped onto the
///   desktop input surface — desktop actions run through the harness-side
///   provider surface under the permission gate (DEC-008, RULE-05).
///   atomic_observation / max_component_skew stay unset: components are
///   captured sequentially, no skew bound is claimable. epoch_invalidation
///   stays false: M2 has no topology-change detection, every observe() is a
///   fresh on-demand capture.
/// - observe() maps required structure/foreground onto the desktop-layer
///   ObservationAssembler (design doc section 6): structure delivers the
///   AccessibilityProvider's SemanticSnapshot projected onto the pinned
///   UiTreeSnapshot (the @eN refs survive in StableNodeHint), foreground
///   delivers the focused window's application name and title. A required
///   component the environment cannot deliver fails the whole request (fail
///   closed); optional components are best-effort and their absence is
///   recorded in the observation quality, never silent.
/// - execute() refuses input dispatch before any side effect (Rejected
///   receipt).
/// - interrupt() is an idempotent best-effort release (the input surface is
///   not dispatched through this binding, so there is no in-flight platform
///   input to release; Xlib calls cannot be unblocked from another thread,
///   hence input_release stays undeclared).
///
/// Filesystem and Process capabilities stay on the mirage provider surface
/// (DesktopEnvironment::filesystem()/process()); the harness-side driver
/// loop brackets each action with the host operation surface so the work is
/// visible in the pinned control plane (DEC-008). When pinned Mira grows a
/// hosted-environment tool surface, this adapter migrates onto it.
class MiraEnvironmentBinding final : public DesktopEnvironmentBinding, public mira::IEnvironment {
  public:
    /// Takes ownership of one environment reference. Throws
    /// std::invalid_argument when `environment` is null: a binding without an
    /// environment can never host.
    explicit MiraEnvironmentBinding(
        std::shared_ptr<mirage::desktop::DesktopEnvironment> environment);

    const char *binding_name() const override;

    std::shared_ptr<mirage::desktop::DesktopEnvironment> bound_environment() const override {
        return environment_;
    }

    mira::EnvironmentCapabilities capabilities() const override;
    mira::Result<mira::Observation> observe(const mira::ObservationRequest &request,
                                            const mira::OperationContext &context) override;
    mira::Result<mira::ExecutionReceipt> execute(const mira::InputSequence &input,
                                                 const mira::OperationContext &context) override;
    mira::Result<void> interrupt(const mira::OperationContext &context) override;

  private:
    std::shared_ptr<mirage::desktop::DesktopEnvironment> environment_;
    mira::ClockDomainId clock_domain_;
    std::string name_;
};

} // namespace mirage::integration
