#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mirage/runtime/pet/animation.hpp>
#include <string>
#include <vector>

namespace mirage::pet_ui {
struct DecodedClip {
    struct Crop {
        std::uint16_t x = 0, y = 0, width = 0, height = 0;
    };
    const runtime::pet::Clip *clip = nullptr;
    std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> rgba_frames;
    // Empty for full-canvas test/adapter frames; otherwise one lossless crop
    // per source frame. Even RGB under zero alpha is preserved.
    std::vector<Crop> crops;

    std::shared_ptr<const std::vector<std::uint8_t>> pixels(std::size_t index) const {
        if (index >= rgba_frames.size())
            return {};
        if (crops.empty())
            return rgba_frames[index];
        const auto &crop = crops.at(index);
        auto canvas = std::make_shared<std::vector<std::uint8_t>>(512u * 512u * 4u, 0);
        const auto &packed = *rgba_frames[index];
        for (std::size_t y = 0; y < crop.height; ++y)
            std::copy_n(packed.data() + y * crop.width * 4, crop.width * 4,
                        canvas->data() + ((crop.y + y) * 512u + crop.x) * 4);
        return canvas;
    }
};

// Decode exactly one approved v5 state. The 96-frame cap is 96 MiB RGBA;
// callers release the previous clip before loading another one.
bool decode_clip(const std::filesystem::path &asset_root, const runtime::pet::Clip &clip,
                 DecodedClip &out, std::string &diagnostic,
                 const std::function<bool()> &cancelled = {});
} // namespace mirage::pet_ui
