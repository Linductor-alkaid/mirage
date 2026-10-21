// M8-07 (upstream mirador DEC-025) controlled acceptance: the interactive
// image-OCR path runs budgeted end to end on the real weight bundle. One
// process performs three consecutive read_image_ocr calls on a dense scene
// under the DEC-076 five-second deadline; every call must return a consumable
// report with verifiable completion state, line counts, geometry and stop
// reason, and the text chain must be identical across calls. A partial report
// is a fact, not an authorization: the visual reference registry must stay
// untouched (no actionable refs, same scope). Real weights and the scene stay
// on caller-provided paths; nothing is downloaded and no pixels persist.
//
// Usage: ppocr_budget_acceptance <manifest.json> <scene.ppm> [calls=3]
// Output: one CALL line per call, then BUDGET_ACCEPTANCE PASS|FAIL.
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include "../support/fake_desktop_environment.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t text_digest(const mirage::integration::ImageOcrOutcome &outcome) {
    std::uint64_t hash = kFnvOffset;
    for (const auto &region : outcome.regions) {
        for (const char c : region.text) {
            hash = (hash ^ static_cast<std::uint8_t>(c)) * kFnvPrime;
        }
        hash = (hash ^ static_cast<std::uint8_t>('\n')) * kFnvPrime;
    }
    return hash;
}

// Same bounded P6 loader contract as the composition contract test.
bool load_ppm(const std::filesystem::path &path, std::string &rgb, int &width, int &height) {
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int max = 0;
    f >> magic >> width >> height >> max;
    f.get();
    if (magic != "P6" || width <= 0 || height <= 0 ||
        static_cast<std::int64_t>(width) * height > 2000000 || max != 255)
        return false;
    rgb.resize(static_cast<std::size_t>(width) * height * 3);
    return static_cast<bool>(f.read(rgb.data(), static_cast<std::streamsize>(rgb.size())));
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: ppocr_budget_acceptance <manifest.json> <scene.ppm> [calls]\n";
        return 2;
    }
    const int calls = argc > 3 ? std::atoi(argv[3]) : 3;
    if (calls < 1 || calls > 10)
        return 2;
    if (!mirage::integration::PpOcrPipeline::supported()) {
        std::cerr << "BUDGET_ACCEPTANCE FAIL (pipeline unavailable)\n";
        return 3;
    }
    std::string rgb;
    int width = 0, height = 0;
    if (!load_ppm(argv[2], rgb, width, height)) {
        std::cerr << "BUDGET_ACCEPTANCE FAIL (scene invalid)\n";
        return 2;
    }

    kairo::Executor executor;
    if (!executor.initialize({}).ok) {
        std::cerr << "BUDGET_ACCEPTANCE FAIL (executor)\n";
        return 3;
    }
    mirage::testing::FakeDesktopEnvironment env;
    env.displays = {{"d1", {0, 0, width, height}, true}};
    env.capture_frame_edit = [rgb](auto &frame) {
        for (std::size_t n = 0; n < rgb.size() / 3; ++n) {
            frame.pixels[n * 4] = static_cast<std::uint8_t>(rgb[n * 3 + 2]);
            frame.pixels[n * 4 + 1] = static_cast<std::uint8_t>(rgb[n * 3 + 1]);
            frame.pixels[n * 4 + 2] = static_cast<std::uint8_t>(rgb[n * 3]);
        }
    };
    std::string error;
    auto pipeline =
        mirage::integration::PpOcrPipeline::create(executor, *env.screen(), argv[1], error);
    if (!pipeline) {
        std::cerr << "BUDGET_ACCEPTANCE FAIL (create: " << error << ")\n";
        return 3;
    }
    auto image = env.screen()->capture_display("d1", {}, {});
    if (!image.ok) {
        std::cerr << "BUDGET_ACCEPTANCE FAIL (capture)\n";
        return 3;
    }
    const auto registry_scope = pipeline->registry().current().scope_ref;

    bool pass = true;
    std::uint64_t first_digest = 0;
    for (int index = 1; index <= calls; ++index) {
        const auto begin = std::chrono::steady_clock::now();
        auto outcome = executor
                           .submit_auto([&] {
                               return pipeline->read_image_ocr(image.frame, {},
                                                               std::chrono::steady_clock::now() +
                                                                   std::chrono::milliseconds{5000});
                           })
                           .get();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - begin)
                            .count();
        const std::uint64_t digest = text_digest(outcome);
        if (index == 1)
            first_digest = digest;
        const bool ok = outcome.ok && ms <= 5000 && !outcome.regions.empty() &&
                        outcome.diagnostics.has_value() && outcome.diagnostics->recognized >= 32 &&
                        digest == first_digest;
        pass = pass && ok;
        std::cout << "CALL " << index << " ok=" << (outcome.ok ? 1 : 0) << " ms=" << ms
                  << " regions=" << outcome.regions.size()
                  << " total=" << outcome.total_region_count;
        if (outcome.diagnostics) {
            const auto &d = *outcome.diagnostics;
            std::cout << " partial=" << (d.partial ? 1 : 0) << " stop=" << d.stop_reason
                      << " detected=" << d.detected << " recognized=" << d.recognized
                      << " deferred=" << d.deferred
                      << " det_limit_hit=" << (d.det_limit_hit ? 1 : 0);
        }
        std::cout << " digest=" << std::hex << digest << std::dec
                  << (ok ? " GATE=ok" : " GATE=fail") << "\n";
    }
    // DEC-025: a partial report never authorizes actions — the read-only
    // image path must not touch the actionable reference registry at all.
    const bool registry_untouched = pipeline->registry().current().scope_ref == registry_scope;
    std::cout << "REGISTRY untouched=" << (registry_untouched ? 1 : 0) << "\n";
    pass = pass && registry_untouched;
    std::cout << "BUDGET_ACCEPTANCE " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}
