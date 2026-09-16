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
/// The pinned surface is adapted honestly to what an M1 desktop environment
/// can deliver:
///
/// - capabilities() reports the empty capability set; M1 environments have no
///   screen, structure, foreground or input surface yet (M2 scope).
/// - observe() fails closed with UnsupportedCapability for any required
///   component outside that set and returns a minimal component-free
///   Observation otherwise.
/// - execute() refuses input dispatch before any side effect (Rejected
///   receipt).
/// - interrupt() is an idempotent best-effort release (there is nothing
///   in-flight to release in the M1 set).
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
