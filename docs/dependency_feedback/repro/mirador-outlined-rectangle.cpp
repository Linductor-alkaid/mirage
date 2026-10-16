// M7-21: no models, capture, or platform; pinned public geometry pipeline.
#include <cstddef>
#include <iostream>
#include <mirador/geometric_proposal.hpp>
#include <mirador/segment_growing_line_detector.hpp>
#include <vector>
int main() {
    for (const int thickness : {0, 1, 2, 3, 4, 8}) {
        for (const double deviation : {1.5, 2.5, 4.}) {
            std::vector<std::byte> pixels(300 * 180, std::byte{255});
            for (int y = 50; y < 130; ++y)
                for (int x = 70; x < 220; ++x)
                    if (thickness == 0 || x < 70 + thickness || x >= 220 - thickness ||
                        y < 50 + thickness || y >= 130 - thickness)
                        pixels[static_cast<std::size_t>(y * 300 + x)] = std::byte{0};
            mirador::ImageView image;
            image.data = pixels.data();
            image.width = 300;
            image.height = 180;
            image.row_stride_bytes = 300;
            image.format = mirador::PixelFormat::kGray8;
            mirador::SegmentGrowingParams params;
            params.deviation_tolerance = deviation;
            mirador::SegmentGrowingLineDetector detector(params);
            auto detected = detector.detect(image, {}, {});
            if (!detected)
                return 2;
            auto filtered = mirador::filter_segments(detected.value().segments, {});
            if (!filtered)
                return 2;
            auto merged = mirador::merge_collinear(filtered.value(), {});
            if (!merged)
                return 2;
            auto regions = mirador::propose_regions(merged.value(), {}, {});
            if (!regions)
                return 2;
            std::cout << "thickness=" << thickness << " deviation=" << deviation
                      << " segments=" << detected.value().segments.size()
                      << " merged=" << merged.value().size()
                      << " proposals=" << regions.value().size() << '\n';
        }
    }
}
