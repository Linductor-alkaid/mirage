#pragma once
#include <memory>
#include <mirage/desktop/frontend_process.hpp>
#include <string>
namespace mirage::platform {
std::string current_executable_path();
std::shared_ptr<desktop::FrontendProcess> make_frontend_process(std::string binary);
} // namespace mirage::platform
