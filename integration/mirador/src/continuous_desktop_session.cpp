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
    explicit Core(const std::string &source) : journal(source) {}
    std::unique_ptr<desktop::DesktopCaptureSession> raw;
    PerceptionSessionOptions options;
    std::string frame_prefix;
    ContinuousImageObserver observer;
    // Serializes a platform resource, never transports work or event payloads.
    std::mutex resource;
    std::atomic<unsigned> paused{0};
    std::atomic<bool> stopped{false};
    std::atomic<std::uint64_t> observed_sequence{0};
    kairo::comm::LatestMailbox<PerceptionView> reports{"task-perception-report"};
    kairo::comm::Topic<std::uint64_t> notices{"desktop-perception-change"};
    std::atomic<unsigned> waiters{0}; // At most 16 bounded Topic subscriptions.
    std::uint64_t notice_cursor = 0, notice_epoch = 0, notice_dropped = 0;
    std::string notice_state, notice_error;
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
        if (std::string_view(state) == "failed" || std::string_view(state) == "stopped" ||
            std::string_view(state) == "degraded")
            view.scene.reset();
        const bool changed = view.newest != notice_cursor || view.source_epoch != notice_epoch ||
                             view.dropped != notice_dropped || view.state != notice_state ||
                             view.error != notice_error;
        notice_cursor = view.newest;
        notice_epoch = view.source_epoch;
        notice_dropped = view.dropped;
        notice_state = view.state;
        notice_error = view.error;
        reports.publish(std::move(view));
        if (changed)
            (void)notices.publish(
                notice_cursor); // Journal owns the events; notices only wake readers.
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
        auto background_limits = limits;
        background_limits.allow_authorization_prompt = false;
        captured = c->raw->capture_presented(background_limits, cancel, c->options.presentation);
        if (captured.ok)
            c->observed_sequence = captured.frame.source_sequence;
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
        // DEC-116: a failed background sample neither ends perception nor the
        // shared session — the platform layer rebuilds stream or session on
        // demand. Report degraded; the capture worker's failure budget decides
        // when sustained failure stops this advisory feed.
        c->publish("degraded", captured.error.code.substr(0, 64));
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
        for (auto &entry : observed.objects) {
            auto &b = entry.object.bounds;
            const auto sx = double(captured.frame.width) / analysis.width;
            const auto sy = double(captured.frame.height) / analysis.height;
            b = {int(std::floor(b.x * sx)), int(std::floor(b.y * sy)),
                 std::max(1, int(std::ceil(b.width * sx))),
                 std::max(1, int(std::ceil(b.height * sy)))};
        }
        std::shared_ptr<PerceptionPreview> preview;
        if (!observed.events.empty()) {
            preview = std::make_shared<PerceptionPreview>();
            preview->id = c->frame_prefix + std::to_string(c->frames);
            preview->observed_at = captured_at;
            if (!scaled(captured.frame, 640, 1u << 20, preview->image))
                preview.reset();
        }
        c->journal.append(std::move(observed), std::move(preview), captured_at);
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
    std::string wait(std::uint64_t after, std::chrono::milliseconds timeout,
                     const desktop::CancelToken &cancel) override {
        auto c = core_.lock();
        if (!c || !c->valid())
            return "unavailable";
        if (c->waiters.fetch_add(1) >= 16) {
            --c->waiters;
            return "wait_capacity_exceeded";
        }
        struct Release {
            std::atomic<unsigned> &count;
            ~Release() { --count; }
        } release{c->waiters};
        auto subscription =
            c->notices.subscribe({.capacity = 1,
                                  .drop_policy = kairo::comm::DropPolicy::RejectNewest,
                                  .name = "desktop-perception-wait"});
        const auto initial = view();
        const auto deadline = Clock::now() + std::clamp(timeout, std::chrono::milliseconds{0},
                                                        std::chrono::milliseconds{30000});
        for (;;) {
            if (cancel.cancelled())
                return "cancelled";
            const auto current = view();
            if (!c->valid() || current.state == "failed" || current.state == "stopped")
                return "unavailable";
            if (current.source_epoch != initial.source_epoch || current.dropped != initial.dropped)
                return "gap";
            if (current.newest > after)
                return "event";
            if (Clock::now() >= deadline)
                return "timeout";
            std::uint64_t notice = 0;
            (void)subscription.receive_for(
                notice, std::min(std::chrono::milliseconds{50},
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     deadline - Clock::now())));
        }
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
        core_->notices.close();
        host_->stop();
    }
    FrameView latest_frame() const override {
        if (!core_->valid())
            return {};
        auto latest = host_->latest();
        return latest
                   ? FrameView{std::shared_ptr<const desktop::ImageFrame>(latest, &latest->image),
                               latest->received_at}
                   : FrameView{};
    }
    std::uint64_t source_sequence() const override { return core_->observed_sequence.load(); }
    desktop::CaptureOutcome capture(const desktop::CaptureLimits &l,
                                    const desktop::CancelToken &c) override {
        Pause priority(core_);
        std::lock_guard lock(core_->resource);
        auto result = core_->raw->capture(l, c);
        if (result.ok)
            core_->observed_sequence = result.frame.source_sequence;
        return result;
    }
    desktop::CaptureOutcome capture_presented(const desktop::CaptureLimits &l,
                                              const desktop::CancelToken &c,
                                              const desktop::CapturePresentation &p) override {
        Pause priority(core_);
        const auto cached = latest_frame();
        if (cached.image && Clock::now() - cached.received_at < std::chrono::seconds(1) &&
            cached.image->pixels.size() <= l.max_bytes &&
            cached.image->source_sequence > l.newer_than_sequence)
            return {true, false, *cached.image, {}};
        std::lock_guard lock(core_->resource);
        auto result = core_->raw->capture_presented(l, c, p);
        if (result.ok)
            core_->observed_sequence = result.frame.source_sequence;
        return result;
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
        const auto state = host_->stats().state;
        return core_->valid() && state != ContinuousCaptureStats::State::Failed &&
               state != ContinuousCaptureStats::State::Stopped;
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
    auto core = std::make_shared<Core>(options.source_id);
    core->raw = std::move(raw);
    core->options = std::move(options);
    core->frame_prefix = "event-frame-" +
                         mira::digest_string(core->options.source_id).to_string().substr(0, 24) +
                         "-";
    core->observer = std::move(observer);
    ContinuousCaptureConfig config;
    config.max_frame_bytes = 32u << 20;
    config.capture_timeout = std::chrono::seconds(8);
    // DEC-118: caller-tunable sampling period (0 keeps the 250ms default).
    if (core->options.sample_period.count() > 0)
        config.sample_period = std::clamp(core->options.sample_period,
                                          std::chrono::milliseconds{100},
                                          std::chrono::milliseconds{1000});
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
