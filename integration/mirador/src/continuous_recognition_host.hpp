#pragma once
#include <mirage/integration/desktop_perception.hpp>

namespace kairo {
class Executor;
}

namespace mirage::integration::detail {
// DEC-124 / M9-28. Private dispatch primitive; product opt-in is composed by the source owner.
// Lifecycle owner calls start/stop outside workers. Other operations are thread-safe.
// Observer and its dependencies must outlive stop; observer polls cancellation.
class ContinuousRecognitionHost {
  public:
    using Clock = std::chrono::steady_clock;
    struct Input {
        desktop::ImageFrame image;
        Clock::time_point captured_at;
        Clock::time_point deadline;
        std::uint64_t generation = 0, sampling_sequence = 0;
        std::int32_t source_width = 0, source_height = 0;
        std::optional<desktop::WindowGeometry> focus;
        desktop::CancelToken cancel;
        PerceptionRecognitionWork work;
    };
    // Capture provenance is available BEFORE invoking OCR/fusion, so the
    // adapter can preserve the original timestamp in Mirador Frame/WorkPlan.
    using Observer = std::function<PerceptionSample(const Input &, const std::string &,
                                                    const desktop::CancelToken &)>;
    struct Admission {
        bool accepted = false;
        std::uint64_t ticket = 0, superseded = 0;
        std::string error;
    };
    struct Result {
        enum class Kind { completed, cancelled, timed_out, failed };
        Kind kind = Kind::failed;
        std::uint64_t ticket = 0;
        // Exact source pixels and capture time, including on delayed completion.
        std::shared_ptr<const Input> input;
        PerceptionSample sample;
    };
    struct Stats {
        bool accepting = false, running = false, result_pending = false;
        std::uint64_t generation = 1;
        std::uint64_t admitted = 0, superseded = 0, rejected = 0;
        std::uint64_t completed = 0, cancelled = 0, timed_out = 0, failed = 0;
        std::uint64_t discarded_results = 0, publication_failures = 0;
        std::uint64_t queued_ticket = 0, active_ticket = 0;
        std::uint64_t request_overwrites = 0;
        std::size_t live_generations = 0, live_pixel_bytes = 0;
    };
    ContinuousRecognitionHost(kairo::Executor &, std::string source, Observer);
    ~ContinuousRecognitionHost();
    ContinuousRecognitionHost(const ContinuousRecognitionHost &) = delete;
    ContinuousRecognitionHost &operator=(const ContinuousRecognitionHost &) = delete;
    bool start(std::string &error);
    Admission submit(Input);
    // Does not start another callback until the previous completion is taken.
    // Retaining returned pixels consumes one of the two generation slots.
    std::shared_ptr<const Result> take();
    // Revokes old generation; even a callback ignoring cancellation cannot publish it.
    std::uint64_t invalidate();
    Stats stats() const;
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::integration::detail
