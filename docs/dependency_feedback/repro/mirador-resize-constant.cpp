// MIRADOR-20261009-001: public API only; no models, threads, or platform SDK.
#include <iostream>
#include <mirador/resize.hpp>
#include <string_view>
int main(int argc, char **argv) {
    std::byte pixels[6];
    for (auto &pixel : pixels)
        pixel = std::byte{255};
    mirador::ImageView image;
    image.data = pixels;
    image.width = 2;
    image.height = 1;
    image.row_stride_bytes = 6;
    image.format = mirador::PixelFormat::kRgb8;
    auto resized = mirador::resize_area(image, 3, 1, 1024);
    if (!resized)
        return 2;
    bool preserved = true;
    for (int x = 0; x < 3; ++x) {
        const auto value = std::to_integer<int>(resized.value().view().data[x * 3]);
        std::cout << value << (x == 2 ? '\n' : ' ');
        preserved = preserved && value == 255;
    }
    const bool expect_fixed = argc == 2 && std::string_view{argv[1]} == "--expect-fixed";
    return (expect_fixed ? preserved : !preserved) ? 0 : 1;
}
