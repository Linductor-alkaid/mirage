#include "perception_journal.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <kairo/comm.hpp>
#include <mira/model_digest.hpp>
#include <mirador/resize.hpp>
#include <mirage/integration/continuous_capture_host.hpp>
#include <mirage/integration/continuous_desktop_session.hpp>
#include <mutex>

namespace mirage::integration {
namespace {
using Clock = std::chrono::steady_clock;
struct Command {
    std::uint64_t cursor = 0;
    std::string frame;
};
struct Core {
    std::unique_ptr<desktop::DesktopCaptureSession> raw;
    PerceptionSessionOptions options;
    std::string frame_prefix;
    ContinuousImageObserver observer;
    // Serializes a platform resource, never transports work or event payloads.
    std::mutex resource;
    std::atomic<unsigned> paused{0};
    std::atomic<bool> stopped{false};
    kairo::comm::LatestMailbox<PerceptionView> reports{"task-perception-report"};
    kairo::comm::LatestMailbox<desktop::WindowGeometry> focus{"task-perception-focus"};
    kairo::comm::MpscChannel<Command> commands{{.capacity = 64, .name = "task-perception-ack"}};
    detail::PerceptionJournal journal; // capture worker is its sole owner
    std::uint64_t frames = 0, analyzed = 0, skipped = 0, failed = 0;
    bool valid() const {
        try {
            return !stopped && !options.cancelled.cancelled() &&
                   (!options.authorized || options.authorized());
        } catch (...) {
            return false;
        }
    }
    void publish(const char *state, std::string error = {}) {
        auto view = journal.view();
        view.state = state;
        view.error = std::move(error);
        view.frames = frames;
        view.analyzed = analyzed;
        view.skipped = skipped;
        view.analysis_failed = failed;
        reports.publish(std::move(view));
    }
};
bool scaled(const desktop::ImageFrame &src, int max_side, std::size_t budget,
            desktop::ImageFrame &dst) {
    const double scale = std::min({1.0, double(max_side) / std::max(src.width, src.height),
                                   std::sqrt(double(budget) / 4 / src.width / src.height)});
    const int w = std::max(1, int(src.width * scale)), h = std::max(1, int(src.height * scale));
    mirador::ImageView view;
    view.data = reinterpret_cast<const std::byte *>(src.pixels.data());
    view.width = src.width;
    view.height = src.height;
    view.row_stride_bytes = static_cast<std::int64_t>(src.stride);
    view.format = mirador::PixelFormat::kBgra8;
    auto result = mirador::resize_area(view, w, h, static_cast<std::int64_t>(budget));
    if (!result)
        return false;
    const auto pixels = result.value().view();
    dst.width = w;
    dst.height = h;
    dst.stride = static_cast<std::size_t>(pixels.row_stride_bytes);
    dst.source_sequence = src.source_sequence;
    dst.pixels.resize(dst.stride * std::size_t(h));
    std::memcpy(dst.pixels.data(), pixels.data, dst.pixels.size());
    return true;
}
desktop::CaptureOutcome sample(const std::shared_ptr<Core> &c, const desktop::CaptureLimits &limits,
                               const desktop::CancelToken &host_cancel) {
    const auto now = Clock::now();
    Command command;
    while (c->commands.try_receive(command)) {
        if (command.frame.empty())
            c->journal.acknowledge(command.cursor, now);
        else
            c->journal.acknowledge_frame(command.frame);
    }
    c->journal.collect(now);
    if (!c->valid()) {
        c->journal.clear();
        c->publish("stopped", "task_or_authority_retired");
        return {false, true, {}, {"cancelled", "continuous task retired"}};
    }
    if (c->paused.load()) {
        ++c->skipped;
        c->publish("paused");
        return {false, false, {}, {"capture_busy", "foreground input owns session"}};
    }
    desktop::CancelToken cancel{
        [c, host_cancel] { return host_cancel.cancelled() || !c->valid() || c->paused.load(); }};
    desktop::CaptureOutcome captured;
    {
        std::unique_lock lock(c->resource, std::try_to_lock);
        if (!lock || c->paused.load())
            return {false, false, {}, {"capture_busy", "foreground resource contention"}};
        captured = c->raw->capture_presented(limits, cancel, c->options.presentation);
    }
    if (host_cancel.cancelled()) {
        c->stopped = true;
        c->journal.clear();
        c->publish("stopped", "capture_cancelled_or_timed_out");
        return {false, true, {}, {"cancelled", "capture host stopped or timed out"}};
    }
    if (!captured.ok) {
        if (c->valid() && c->paused.load()) {
            ++c->skipped;
            c->publish("paused");
            return {false, false, {}, {"capture_busy", "foreground superseded capture"}};
        }
        c->journal.clear();
        c->publish("failed", captured.error.code.substr(0, 64));
        return captured;
    }
    const auto &frame = captured.frame;
    if (frame.format != desktop::ImageFormat::Bgra8 || frame.width <= 0 || frame.height <= 0 ||
        frame.width > 65535 || frame.height > 65535 ||
        frame.stride < static_cast<std::size_t>(frame.width) * 4 ||
        frame.stride > limits.max_bytes / static_cast<std::size_t>(frame.height) ||
        frame.pixels.size() != frame.stride * static_cast<std::size_t>(frame.height) ||
        frame.pixels.capacity() > limits.max_bytes) {
        c->journal.clear();
        c->publish("failed", "invalid_capture");
        return {false,
                false,
                {},
                {"invalid_capture", "background frame layout or byte budget invalid"}};
    }
    const auto captured_at = Clock::now();
    ++c->frames;
    desktop::ImageFrame analysis;
    if (!scaled(captured.frame, 1600, 10u << 20, analysis)) {
        ++c->failed;
        c->publish("degraded", "analysis_resize_failed");
        return captured;
    }
    desktop::WindowGeometry raw_focus;
    std::optional<desktop::WindowGeometry> roi;
    if (c->focus.try_load(raw_focus)) {
        c->journal.focus(raw_focus);
        const double sx = double(analysis.width) / captured.frame.width;
        const double sy = double(analysis.height) / captured.frame.height;
        roi = desktop::WindowGeometry{int(raw_focus.x * sx), int(raw_focus.y * sy),
                                      std::max(1, int(raw_focus.width * sx)),
                                      std::max(1, int(raw_focus.height * sy))};
    }
    auto observed = c->observer(analysis, c->options.source_id, roi, cancel,
                                Clock::now() + std::chrono::seconds(3));
    if (host_cancel.cancelled()) {
        c->stopped = true;
        c->journal.clear();
        c->publish("stopped", "capture_cancelled_or_timed_out");
        return {false, true, {}, {"cancelled", "capture host stopped or timed out"}};
    }
    if (!c->valid()) {
        c->journal.clear();
        c->publish("stopped", "task_or_authority_retired");
        return {false, true, {}, {"cancelled", "continuous task retired"}};
    }
    if (cancel.cancelled() && !observed.ok) {
        ++c->skipped;
        c->publish("paused");
        return {false, false, {}, {"capture_busy", "foreground superseded analysis"}};
    }
    if (observed.cancelled) {
        ++c->skipped;
        c->publish("sampling");
    } else if (!observed.ok) {
        ++c->failed;
        c->publish("degraded",
                   observed.error.code.substr(0, 64) + ":" + observed.error.message.substr(0, 192));
    } else if (observed.skipped) {
        ++c->skipped;
        c->publish("sampling");
    } else {
        ++c->analyzed;
        for (auto &e : observed.events) {
            e.x *= double(captured.frame.width) / analysis.width;
            e.y *= double(captured.frame.height) / analysis.height;
            e.width *= double(captured.frame.width) / analysis.width;
            e.height *= double(captured.frame.height) / analysis.height;
        }
        std::shared_ptr<PerceptionPreview> preview;
        if (!observed.events.empty()) {
            preview = std::make_shared<PerceptionPreview>();
            preview->id = c->frame_prefix + std::to_string(c->frames);
            preview->observed_at = captured_at;
            if (!scaled(captured.frame, 640, 1u << 20, preview->image))
                preview.reset();
        }
        c->journal.append(std::move(observed), std::move(preview), Clock::now());
        c->publish("sampling");
    }
    return captured;
}
class Pause final : public PerceptionPause {
    std::shared_ptr<Core> core_;

  public:
    explicit Pause(std::shared_ptr<Core> c) : core_(std::move(c)) { ++core_->paused; }
    ~Pause() override { --core_->paused; }
};
class Feed final : public DesktopPerceptionFeed {
    std::weak_ptr<Core> core_;

  public:
    explicit Feed(const std::shared_ptr<Core> &c) : core_(c) {}
    PerceptionView view() const override {
        PerceptionView v;
        auto c = core_.lock();
        if (!c || !c->valid()) {
            v.state = "stopped";
            return v;
        }
        (void)c->reports.try_load(v);
        const auto stats = c->commands.stats();
        v.ack_rejected = stats.dropped_count + stats.closed_send_count;
        v.ack_depth = stats.current_depth;
        return v;
    }
    bool acknowledge(std::uint64_t n) override {
        auto c = core_.lock();
        return c && c->valid() && c->commands.try_send(Command{n, {}});
    }
    bool acknowledge_frame(const std::string &id) override {
        auto c = core_.lock();
        return id.size() <= 64 && c && c->valid() && c->commands.try_send(Command{0, id});
    }
    void focus(const desktop::WindowGeometry &r) override {
        if (auto c = core_.lock(); c && c->valid() && r.width > 0 && r.height > 0)
            c->focus.publish(r);
    }
    std::unique_ptr<PerceptionPause> pause() override {
        auto c = core_.lock();
        return c ? std::make_unique<Pause>(std::move(c)) : nullptr;
    }
};
class Session final : public desktop::DesktopCaptureSession {
    std::shared_ptr<Core> core_;
    std::unique_ptr<ContinuousCaptureHost> host_;

  public:
    Session(std::shared_ptr<Core> c, std::unique_ptr<ContinuousCaptureHost> host)
        : core_(std::move(c)), host_(std::move(host)) {}
    ~Session() override {
        core_->stopped = true;
        host_->stop();
    }
    desktop::CaptureOutcome capture(const desktop::CaptureLimits &l,
                                    const desktop::CancelToken &c) override {
        Pause priority(core_);
        std::lock_guard lock(core_->resource);
        return core_->raw->capture(l, c);
    }
    desktop::CaptureOutcome capture_presented(const desktop::CaptureLimits &l,
                                              const desktop::CancelToken &c,
                                              const desktop::CapturePresentation &p) override {
        Pause priority(core_);
        std::lock_guard lock(core_->resource);
        return core_->raw->capture_presented(l, c, p);
    }
    desktop::SharedPointerQueryOutcome pointer_position(const desktop::CancelToken &c) override {
        Pause priority(core_);
        std::lock_guard lock(core_->resource);
        return core_->raw->pointer_position(c);
    }
    desktop::InputOutcome input(const desktop::SharedInputAction &a, const desktop::InputLimits &l,
                                const desktop::CancelToken &c) override {
        Pause priority(core_);
        std::lock_guard lock(core_->resource);
        return core_->raw->input(a, l, c);
    }
    bool reusable() const override {
        std::lock_guard lock(core_->resource);
        return core_->raw->reusable();
    }
    bool input_supported() const override {
        std::lock_guard lock(core_->resource);
        return core_->raw->input_supported();
    }
    bool timed_press_supported() const override {
        std::lock_guard lock(core_->resource);
        return core_->raw->timed_press_supported();
    }
};
} // namespace
PerceptionConnection connect_continuous_desktop(kairo::Executor &owner,
                                                std::unique_ptr<desktop::DesktopCaptureSession> raw,
                                                PerceptionSessionOptions options,
                                                ContinuousImageObserver observer) {
    if (!raw || !observer || options.source_id.empty() || options.source_id.size() > 128)
        return {std::move(raw), {}};
    auto core = std::make_shared<Core>();
    core->raw = std::move(raw);
    core->options = std::move(options);
    core->frame_prefix = "event-frame-" +
                         mira::digest_string(core->options.source_id).to_string().substr(0, 24) +
                         "-";
    core->observer = std::move(observer);
    ContinuousCaptureConfig config;
    config.max_frame_bytes = 32u << 20;
    config.capture_timeout = std::chrono::seconds(8);
    auto host = std::make_unique<ContinuousCaptureHost>(
        owner, core->options.source_id,
        [core](const auto &l, const auto &c) {
            try {
                return sample(core, l, c);
            } catch (...) {
                core->journal.clear();
                core->publish("failed", "analysis_exception");
                throw;
            }
        },
        config);
    auto feed = std::make_shared<Feed>(core);
    std::string error;
    if (!host->start(error) || !host->set_demand(true, error)) {
        host->stop();
        core->publish("failed", "capture_admission_rejected");
    }
    return {std::make_unique<Session>(core, std::move(host)), std::move(feed)};
}
} // namespace mirage::integration
