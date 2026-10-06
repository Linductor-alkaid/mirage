#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace mirage::apps {

// Return owned storage: callers may subsequently change the process environment.
inline std::optional<std::string> environment_variable(const char *name) {
#ifdef _WIN32
    char *value = nullptr;
    std::size_t size = 0;
    if (::_dupenv_s(&value, &size, name) != 0 || !value)
        return std::nullopt;
    const std::unique_ptr<char, decltype(&std::free)> storage(value, &std::free);
    return std::string{storage.get()};
#else
    if (const char *value = std::getenv(name))
        return std::string{value};
    return std::nullopt;
#endif
}

} // namespace mirage::apps
