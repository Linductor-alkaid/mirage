#pragma once
#include <cstdint>
#include <memory>
#include <mirage/desktop/presentation_exclusion.hpp>
#include <string>
namespace mirage::platform::detail {
// A product Open action targets only the still-owned child, never Agent windows.
std::shared_ptr<desktop::PresentationExcluder> make_pet_excluder(std::int64_t pid);
bool activate_frontend(std::int64_t pid, std::string &diagnostic);
} // namespace mirage::platform::detail
