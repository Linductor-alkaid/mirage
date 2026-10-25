#pragma once

#include <memory>
#include <mirage/desktop/input_method_provider.hpp>
#include <string>

namespace mirage::platform::linux_backend {

/// GIO platform continuation only; callbacks capture completion, never dispatch
/// business work. The environment outlives all synchronous observe calls.
class InputMethodBackend final : public desktop::InputMethodProvider {
  public:
    static std::unique_ptr<InputMethodBackend> open();
    desktop::InputMethodOutcome observe(const desktop::InputMethodLimits &,
                                        const desktop::CancelToken &) override;

  private:
    std::string ibus_address_file_;
};

} // namespace mirage::platform::linux_backend
