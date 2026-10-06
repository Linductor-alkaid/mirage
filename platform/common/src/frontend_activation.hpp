#pragma once
#include <cstdint>
#include <string>
namespace mirage::platform::detail {
// A product Open action targets only the still-owned child, never Agent windows.
bool activate_frontend(std::int64_t pid, std::string &diagnostic);
} // namespace mirage::platform::detail
