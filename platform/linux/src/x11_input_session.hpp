#pragma once
#include <memory>
#include <mirage/desktop/input_session.hpp>
#include <string>
namespace mirage::platform::linux_backend {
std::unique_ptr<desktop::InputSessionBackend>
open_x11_input_session(const std::string &, const desktop::PointerActionScope &,
                       const desktop::CancelToken &);
}
