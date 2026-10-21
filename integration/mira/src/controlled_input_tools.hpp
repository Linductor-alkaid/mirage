#pragma once
#include "controlled_input_session.hpp"
#include <mirage/integration/desktop_tool_context.hpp>
namespace mirage::integration::detail {
class ControlledInputTools {
  public:
    ControlledInputTools(kairo::Executor &, DesktopToolContext, mira::OperationContext,
                         desktop::CancelToken, std::shared_ptr<ControlledInputWorker> = {});
    void register_tools(mira::BuiltinToolRegistry &, const Decorator &);
    mira::JsonValue snapshot() const;
    desktop::ProviderError finish();
    void note_observation(std::uint64_t observed_epoch);

  private:
    kairo::Executor &executor_;
    DesktopToolContext desktop_;
    mira::OperationContext owner_;
    desktop::CancelToken cancel_;
    std::shared_ptr<ControlledInputWorker> worker_;
    std::unique_ptr<ControlledInputSession> session_;
    std::string id_, window_;
    bool recovery_observed_ = false;
    mira::Error state_error(const desktop::InputSessionReceipt &) const;
    mira::Result<mira::JsonValue> invoke(const std::string &, const mira::JsonValue &,
                                         const mira::OperationContext &);
};
} // namespace mirage::integration::detail
