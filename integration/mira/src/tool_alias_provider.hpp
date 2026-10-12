#pragma once

#include <mira/model_provider.hpp>

#include <memory>
#include <span>
#include <string>

namespace mirage::integration::detail {
// DEC-043: all pinned dialects use conservative ASCII function names on the wire.
// Keep Mirage's canonical names in the loop and registry; translate only at
// the provider boundary, including assistant tool-call replay.
// Canonical names are bounded to 64 bytes; normalization collisions fail closed.
std::string model_tool_name(const std::string &canonical);
// Only pass host-authored guidance before appending user text or replay content.
void alias_host_tool_names(std::string &guidance, std::span<const mira::ExposedToolSpec> tools);
std::shared_ptr<mira::IModelProvider>
make_tool_alias_provider(std::shared_ptr<mira::IModelProvider> provider);
} // namespace mirage::integration::detail
