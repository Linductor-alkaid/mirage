#pragma once
#include <memory>
#include <mirage/desktop/frontend_process.hpp>
#include <string>
namespace mirage::platform {
struct PresentationContext;
std::shared_ptr<PresentationContext> make_presentation_context();
std::string current_executable_path();
std::int64_t current_process_id();
std::shared_ptr<desktop::FrontendProcess> make_frontend_process(std::string binary);
std::shared_ptr<desktop::FrontendProcess>
make_pet_process(std::string binary, std::shared_ptr<PresentationContext> presentation = {});
} // namespace mirage::platform
