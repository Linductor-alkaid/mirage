#pragma once

#include "frames.hpp"
#include <chrono>
#include <functional>
#include <memory>

namespace kairo {
class Executor;
}
namespace mirage::pet_ui {
enum class LoadState { Loading, Ready, Failed, Rejected, Cancelled, TimedOut };
struct LoadUpdate {
    std::uint64_t generation = 0;
    LoadState state = LoadState::Loading;
    DecodedClip decoded;
    std::string diagnostic;
};

// UI-owned adapter to the process's existing Executor. Exactly one retained
// future, one latest desired clip, and no decoded cache. Clips must outlive it.
class ResourceLoader {
  public:
    using Clock = std::chrono::steady_clock;
    using Decoder = std::function<bool(const runtime::pet::Clip &, DecodedClip &, std::string &,
                                       const std::function<bool()> &)>;
    ResourceLoader(kairo::Executor &executor, Decoder decoder,
                   std::chrono::milliseconds timeout = std::chrono::seconds{2});
    ~ResourceLoader();
    std::uint64_t request(const runtime::pet::Clip &clip, Clock::time_point now);
    bool poll(LoadUpdate &out, Clock::time_point now);
    bool busy() const;
    void cancel(); // drop the desired generation; drain the retained future in poll/stop
    void stop();   // cancel and consume future before the external owner shuts down

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::pet_ui
