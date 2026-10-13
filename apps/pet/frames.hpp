#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mirage/runtime/pet/animation.hpp>
#include <string>
#include <vector>

namespace mirage::pet_ui {
struct DecodedClip {
    const runtime::pet::Clip *clip = nullptr;
    std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> rgba_frames;
};

// Decode exactly one approved v5 state. The 96-frame cap is 96 MiB RGBA;
// callers release the previous clip before loading another one.
bool decode_clip(const std::filesystem::path &asset_root, const runtime::pet::Clip &clip,
                 DecodedClip &out, std::string &diagnostic,
                 const std::function<bool()> &cancelled = {});
} // namespace mirage::pet_ui
