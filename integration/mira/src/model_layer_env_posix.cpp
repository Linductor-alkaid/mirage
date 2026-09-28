#include "model_layer_env.hpp"

#include <cstdlib>

namespace mirage::integration::detail {

std::string read_environment_value(const std::string &name) {
    if (const char *value = std::getenv(name.c_str()); value != nullptr) {
        return std::string{value};
    }
    return {};
}

} // namespace mirage::integration::detail
