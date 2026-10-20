// Manual, read-only GNOME/Portal diagnostic. Never sends desktop input.
#include "../../platform/linux/src/portal_screen_backend.hpp"
#include <chrono>
#include <iostream>
#include <kairo/executor.hpp>

int main() {
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    if (!executor.initialize(config))
        return 1;
    int result = 1;
    try {
        auto work = executor.submit_auto([] {
            using namespace mirage;
            auto backend = platform::linux_backend::PortalScreenBackend::open();
            if (!backend)
                return 2;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            desktop::CancelToken cancel(
                [&] { return std::chrono::steady_clock::now() >= deadline; });
            backend->configure_desktop_authorization(true);
            if (!backend->desktop_authorization(cancel).remembered) {
                std::cout << "remembered_authorization=false; no capture requested\n";
                return 3;
            }
            auto session = backend->create_desktop_interaction_session();
            if (!session)
                return 4;
            const auto frame = session->capture({32u << 20}, cancel);
            std::cout << "capture_ok=" << frame.ok << " error=" << frame.error.code << '\n';
            if (!frame.ok)
                return 5;
            const auto cursor = session->pointer_position(cancel);
            std::cout << "cursor_available=" << cursor.ok << " error=" << cursor.error.code
                      << " extent_matches="
                      << (cursor.image_width == frame.frame.width &&
                          cursor.image_height == frame.frame.height)
                      << " sequence_matches="
                      << (cursor.source_sequence == frame.frame.source_sequence) << '\n';
            return cursor.ok ? 0 : 6;
        });
        result = work.get();
    } catch (...) {
        std::cerr << "probe failed\n";
    }
    executor.shutdown(true);
    return result;
}
