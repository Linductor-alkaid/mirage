#include <mirage/integration/continuous_desktop_session.hpp>

#include <algorithm>
#include <mira/core_contracts.hpp>
#include <mutex>
#include <set>
#include <utility>

namespace mirage::integration {
namespace {
class SharedSession final : public desktop::DesktopCaptureSession {
    std::shared_ptr<desktop::DesktopCaptureSession> source_;

  public:
    explicit SharedSession(std::shared_ptr<desktop::DesktopCaptureSession> source)
        : source_(std::move(source)) {}
    FrameView latest_frame() const override { return source_->latest_frame(); }
    std::uint64_t source_sequence() const override { return source_->source_sequence(); }
    desktop::CaptureOutcome capture(const desktop::CaptureLimits &limits,
                                    const desktop::CancelToken &cancel) override {
        return source_->capture(limits, cancel);
    }
    desktop::CaptureOutcome
    capture_presented(const desktop::CaptureLimits &limits, const desktop::CancelToken &cancel,
                      const desktop::CapturePresentation &presentation) override {
        return source_->capture_presented(limits, cancel, presentation);
    }
    desktop::SharedPointerQueryOutcome
    pointer_position(const desktop::CancelToken &cancel) override {
        return source_->pointer_position(cancel);
    }
    desktop::InputOutcome input(const desktop::SharedInputAction &action,
                                const desktop::InputLimits &limits,
                                const desktop::CancelToken &cancel) override {
        return source_->input(action, limits, cancel);
    }
    bool reusable() const override { return source_->reusable(); }
    bool input_supported() const override { return source_->input_supported(); }
    bool timed_press_supported() const override { return source_->timed_press_supported(); }
};
struct Resource {
    mutable std::mutex mutex; // Resource lifetime only; payloads stay in the existing mailboxes.
    std::shared_ptr<desktop::DesktopCaptureSession> session;
    std::shared_ptr<DesktopPerceptionFeed> feed;
    PerceptionSessionOptions options;
    desktop::CancelToken stopped;
    bool interactive = false;

    bool valid() const {
        try {
            return !stopped.cancelled() && !options.cancelled.cancelled() &&
                   (!options.authorized || options.authorized());
        } catch (...) {
            return false;
        }
    }
    std::shared_ptr<desktop::DesktopCaptureSession> acquire() const {
        std::lock_guard lock(mutex);
        return valid() ? session : nullptr;
    }
    void stop() {
        stopped.request_cancel();
        std::shared_ptr<desktop::DesktopCaptureSession> retired;
        {
            std::lock_guard lock(mutex);
            retired = std::move(session);
        }
        // Join outside the lifetime lock. Active finite calls keep their own reference.
        retired.reset();
    }
};
struct Access {
    std::shared_ptr<Resource> resource;
    PerceptionSessionOptions task;
    bool input_allowed = false;
    bool valid() const {
        try {
            return resource->valid() && !task.cancelled.cancelled() &&
                   (!task.authorized || task.authorized());
        } catch (...) {
            return false;
        }
    }
    desktop::CancelToken cancel(const desktop::CancelToken &parent) const {
        return desktop::CancelToken{
            [self = *this, parent] { return parent.cancelled() || !self.valid(); }};
    }
};
class SessionLease final : public desktop::DesktopCaptureSession {
    Access access_;

  public:
    explicit SessionLease(Access access) : access_(std::move(access)) {}
    FrameView latest_frame() const override {
        auto session = access_.valid() ? access_.resource->acquire() : nullptr;
        return session ? session->latest_frame() : FrameView{};
    }
    std::uint64_t source_sequence() const override {
        auto session = access_.valid() ? access_.resource->acquire() : nullptr;
        return session ? session->source_sequence() : 0;
    }
    desktop::CaptureOutcome capture(const desktop::CaptureLimits &limits,
                                    const desktop::CancelToken &cancel) override {
        return capture_presented(limits, cancel, {});
    }
    desktop::CaptureOutcome
    capture_presented(const desktop::CaptureLimits &limits, const desktop::CancelToken &cancel,
                      const desktop::CapturePresentation &presentation) override {
        auto session = access_.valid() ? access_.resource->acquire() : nullptr;
        return session ? session->capture_presented(limits, access_.cancel(cancel), presentation)
                       : desktop::CaptureOutcome{
                             false, true, {}, {"cancelled", "Perception lease retired"}};
    }
    desktop::SharedPointerQueryOutcome
    pointer_position(const desktop::CancelToken &cancel) override {
        auto session = access_.valid() ? access_.resource->acquire() : nullptr;
        return session ? session->pointer_position(access_.cancel(cancel))
                       : desktop::SharedPointerQueryOutcome{
                             false, true, {}, 0, 0, 0, {"cancelled", "Perception lease retired"}};
    }
    desktop::InputOutcome input(const desktop::SharedInputAction &action,
                                const desktop::InputLimits &limits,
                                const desktop::CancelToken &cancel) override {
        if (!access_.input_allowed)
            return {false,
                    false,
                    {"permission_denied", "Observation lease has no input access"},
                    false,
                    true};
        auto session = access_.valid() ? access_.resource->acquire() : nullptr;
        return session ? session->input(action, limits, access_.cancel(cancel))
                       : desktop::InputOutcome{
                             false, true, {"cancelled", "Perception lease retired"}, false, true};
    }
    bool reusable() const override { return access_.valid(); }
    bool input_supported() const override {
        if (!access_.input_allowed)
            return false;
        auto session = access_.valid() ? access_.resource->acquire() : nullptr;
        return session && session->input_supported();
    }
    bool timed_press_supported() const override {
        if (!access_.input_allowed)
            return false;
        auto session = access_.valid() ? access_.resource->acquire() : nullptr;
        return session && session->timed_press_supported();
    }
};
class FeedLease final : public DesktopPerceptionFeed {
    Access access_;
    std::uint64_t beginning_ = 0;
    std::set<std::string> consumed_; // <=8 previews, task-serial owner.
    std::optional<desktop::WindowGeometry> focus_;

  public:
    explicit FeedLease(Access access) : access_(std::move(access)) {
        beginning_ = access_.resource->feed->view().newest;
    }
    PerceptionView view() const override {
        PerceptionView result;
        if (!access_.valid()) {
            result.state = "stopped";
            return result;
        }
        result = access_.resource->feed->view();
        std::erase_if(result.events, [&](const auto &event) {
            if (event.sequence <= beginning_)
                return true;
            if (!focus_ || event.kind == "appeared" || event.kind == "disappeared" ||
                event.kind == "source_reset" || event.kind == "observed_unconfirmed")
                return false;
            return event.x >= focus_->x + focus_->width || event.y >= focus_->y + focus_->height ||
                   event.x + event.width <= focus_->x || event.y + event.height <= focus_->y;
        });
        std::erase_if(result.previews, [&](const auto &preview) {
            return consumed_.contains(preview.first) ||
                   std::none_of(result.events.begin(), result.events.end(),
                                [&](const auto &event) { return event.frame_id == preview.first; });
        });
        return result;
    }
    bool acknowledge(std::uint64_t cursor) override {
        // Each task owns its cursor. Do not globally ACK another task's unread history;
        // the resident journal retains its existing bounded 120s window.
        return access_.valid() && cursor && cursor <= view().newest;
    }
    bool acknowledge_frame(const std::string &id) override {
        if (!access_.valid())
            return false;
        const auto current = view();
        if (std::none_of(current.previews.begin(), current.previews.end(),
                         [&](const auto &entry) { return entry.first == id; }))
            return false;
        if (consumed_.size() >= 8)
            consumed_.erase(consumed_.begin());
        consumed_.insert(id);
        return true;
    }
    void focus(const desktop::WindowGeometry &region) override {
        // Focus is consumer-local; one task must not suppress another task's events.
        if (access_.valid() && region.width > 0 && region.height > 0)
            focus_ = region;
    }
    std::unique_ptr<PerceptionPause> pause() override {
        return access_.valid() ? access_.resource->feed->pause() : nullptr;
    }
    std::string wait(std::uint64_t after, std::chrono::milliseconds timeout,
                     const desktop::CancelToken &cancel) override {
        after = std::max(after, beginning_);
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::clamp(timeout, std::chrono::milliseconds{0}, std::chrono::milliseconds{30000});
        const auto initial = view();
        for (;;) {
            if (cancel.cancelled() || !access_.valid())
                return "cancelled";
            const auto current = view();
            if (current.oldest > after && current.oldest - after > 1)
                return "gap";
            if (current.source_epoch != initial.source_epoch || current.dropped != initial.dropped)
                return "gap";
            if (std::any_of(current.events.begin(), current.events.end(),
                            [&](const auto &event) { return event.sequence > after; }))
                return "event";
            if (std::chrono::steady_clock::now() >= deadline)
                return "timeout";
            const auto status =
                access_.resource->feed->wait(current.newest,
                                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 deadline - std::chrono::steady_clock::now()),
                                             access_.cancel(cancel));
            if (status != "event")
                return status;
        }
    }
};
} // namespace

struct ResidentDesktopPerception::Impl {
    explicit Impl(PerceptionSessionFactory value) : factory(std::move(value)) {}
    std::mutex mutex; // Serializes owner replacement, not task/observation transport.
    PerceptionSessionFactory factory;
    std::shared_ptr<Resource> current;
    bool stopped = false;
};
ResidentDesktopPerception::ResidentDesktopPerception(PerceptionSessionFactory factory)
    : impl_(std::make_unique<Impl>(std::move(factory))) {}
ResidentDesktopPerception::~ResidentDesktopPerception() { stop(); }
PerceptionConnection
ResidentDesktopPerception::connect(std::unique_ptr<desktop::DesktopCaptureSession> source,
                                   PerceptionSessionOptions lifetime, PerceptionSessionOptions task,
                                   bool interactive) {
    if (!source)
        return {};
    auto shared = std::shared_ptr<desktop::DesktopCaptureSession>(std::move(source));
    if (!adopt(std::make_unique<SharedSession>(shared), std::move(lifetime), interactive)) {
        auto foreground = std::make_shared<Resource>();
        foreground->session = std::move(shared);
        foreground->options = task;
        foreground->interactive = interactive;
        return {std::make_unique<SessionLease>(Access{foreground, std::move(task), interactive}),
                {}};
    }
    return attach(interactive, std::move(task));
}
bool ResidentDesktopPerception::adopt(std::unique_ptr<desktop::DesktopCaptureSession> session,
                                      PerceptionSessionOptions options, bool interactive) {
    std::shared_ptr<Resource> retired;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopped || !session || !impl_->factory)
            return false;
        if (impl_->current && (!interactive || impl_->current->interactive)) {
            const auto existing = impl_->current->acquire();
            if (existing && existing->reusable())
                return true;
        }
        auto resource = std::make_shared<Resource>();
        resource->options = options;
        resource->interactive = interactive;
        if (!resource->valid())
            return false;
        // Executor worker registrations and event-frame identities are incarnation-local.
        options.source_id = "resident-" + mira::OperationId::generate().to_string();
        const auto parent = options.cancelled;
        options.cancelled = desktop::CancelToken{
            [parent, stop = resource->stopped] { return parent.cancelled() || stop.cancelled(); }};
        auto connection = impl_->factory(std::move(session), std::move(options));
        if (!connection.session || !connection.feed || !connection.session->reusable())
            return false;
        resource->session = std::move(connection.session);
        resource->feed = std::move(connection.feed);
        retired = std::exchange(impl_->current, std::move(resource));
    }
    if (retired)
        retired->stop();
    return true;
}
PerceptionConnection ResidentDesktopPerception::attach(bool require_input,
                                                       PerceptionSessionOptions options) {
    std::lock_guard lock(impl_->mutex);
    auto resource = impl_->current;
    if (impl_->stopped || !resource || !resource->valid() ||
        (require_input && !resource->interactive))
        return {};
    const auto session = resource->acquire();
    if (!session || !session->reusable())
        return {};
    Access access{resource, std::move(options), require_input};
    if (!access.valid())
        return {};
    return {std::make_unique<SessionLease>(access), std::make_shared<FeedLease>(std::move(access))};
}
void ResidentDesktopPerception::stop() {
    std::shared_ptr<Resource> retired;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopped = true;
        retired = std::move(impl_->current);
    }
    if (retired)
        retired->stop();
}
void ResidentDesktopPerception::invalidate() {
    std::shared_ptr<Resource> retired;
    {
        std::lock_guard lock(impl_->mutex);
        retired = std::move(impl_->current);
    }
    if (retired)
        retired->stop();
}
} // namespace mirage::integration
