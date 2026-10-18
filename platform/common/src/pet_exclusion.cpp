#include "frontend_activation.hpp"
#include "presentation_context.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>

#if defined(MIRAGE_PET_X11_EXCLUSION)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XRes.h>
#include <X11/extensions/shape.h>
#include <poll.h>
#include <time.h>
#endif

namespace mirage::platform::detail {
namespace {
using desktop::ProviderError;
ProviderError unavailable(const char *message) { return {"pet_exclusion_unavailable", message}; }
class VisibleLease final : public desktop::PresentationLease {
  public:
    bool excluded() const override { return false; }
    ProviderError verify(const desktop::CancelToken &cancel) override {
        if (released_)
            return {"pet_exclusion_failed", "passive pet lease was released"};
        return cancel.cancelled() ? ProviderError{"cancelled", "pet operation cancelled"}
                                  : ProviderError{};
    }
    ProviderError release() override {
        released_ = true;
        return {};
    }

  private:
    bool released_ = false;
};
#if defined(MIRAGE_PET_X11_EXCLUSION)
// Same Xlib error policy as X11Backend: a disappeared desktop window is an
// ordinary provider failure. Never install/restore a per-call global handler.
int window_error(Display *, XErrorEvent *) { return 0; }

class X11Excluder final : public desktop::PresentationExcluder,
                          public std::enable_shared_from_this<X11Excluder> {
  public:
    explicit X11Excluder(std::int64_t pid, std::shared_ptr<PresentationContext> presentation)
        : pid_(pid), presentation_(std::move(presentation)) {}
    ~X11Excluder() override {
        if (display_)
            XCloseDisplay(display_);
    }

    class Lease final : public desktop::PresentationLease {
      public:
        explicit Lease(std::shared_ptr<X11Excluder> owner) : owner_(std::move(owner)) {}
        ~Lease() override {
            const auto error = release();
            if (!error.code.empty())
                std::cerr << "mira_pet: " << error.code << ": " << error.message << '\n';
        }
        ProviderError verify(const desktop::CancelToken &cancel) override {
            if (!owner_)
                return unavailable("pet exclusion lease was already released");
            std::lock_guard lock(owner_->mutex_);
            if (cancel.cancelled())
                return {"cancelled", "pet exclusion cancelled"};
            return owner_->verify_locked();
        }
        ProviderError release() override {
            if (!owner_)
                return {};
            auto owner = std::move(owner_);
            std::lock_guard lock(owner->mutex_);
            if (--owner->leases_)
                return {};
            return owner->restore_locked();
        }

      private:
        std::shared_ptr<X11Excluder> owner_;
    };

    desktop::PresentationExclusion acquire(const desktop::CancelToken &cancel) override {
        std::lock_guard lock(mutex_);
        if (cancel.cancelled())
            return {{}, {"cancelled", "pet exclusion cancelled"}};
        if (leases_ >= 16)
            return {{}, unavailable("pet exclusion capacity exceeded")};
        if (!leases_) {
            const char *wayland = std::getenv("WAYLAND_DISPLAY"),
                       *session = std::getenv("XDG_SESSION_TYPE");
            const bool wayland_session =
                (wayland && *wayland) || (session && std::string_view(session) == "wayland");
            if (wayland_session) {
                if (!display_) {
                    XSetErrorHandler(window_error);
                    display_ = XOpenDisplay(nullptr);
                }
                // Capability probe only: do not touch any surface on an
                // unadapted compositor, including a pure Wayland session.
                if (!display_ || !gnome_window_manager()) {
                    if (presentation_)
                        presentation_->not_before_ns.store(0);
                    return {std::make_unique<VisibleLease>(), {}};
                }
            }
        }
        if (leases_) {
            if (auto error = verify_locked(); !error.code.empty())
                return {{}, error};
        } else if (auto error = hide_locked(cancel); !error.code.empty()) {
            return {{}, error};
        }
        // Allocation before increment: allocation failure cannot leak a lease.
        try {
            auto lease = std::make_unique<Lease>(shared_from_this());
            ++leases_;
            return {std::move(lease), {}};
        } catch (...) {
            if (!leases_) {
                const auto restored = restore_locked();
                if (!restored.code.empty())
                    std::cerr << "mira_pet: " << restored.code << ": " << restored.message << '\n';
            }
            throw;
        }
    }

  private:
    bool cardinal(Window window, const char *name, unsigned long expected) {
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, rest = 0;
        unsigned char *data = nullptr;
        const int result =
            XGetWindowProperty(display_, window, XInternAtom(display_, name, False), 0, 1, False,
                               XA_CARDINAL, &actual, &format, &count, &rest, &data);
        const bool matches = result == Success && actual == XA_CARDINAL && format == 32 &&
                             count == 1 && rest == 0 && data &&
                             *reinterpret_cast<unsigned long *>(data) == expected;
        if (data)
            XFree(data);
        return matches;
    }
    bool identity(Window window) {
        if (!cardinal(window, "_MIRAGE_PET_SURFACE", 1) ||
            !cardinal(window, "_NET_WM_PID", static_cast<unsigned long>(pid_)))
            return false;
        XResClientIdSpec spec{window, XRES_CLIENT_ID_PID_MASK};
        XResClientIdValue *ids = nullptr;
        long count = 0;
        // The 1.2 ID API returns Success (0), unlike the Bool query helpers.
        const int queried = XResQueryClientIds(display_, 1, &spec, &count, &ids);
        const bool matches =
            queried == Success && count == 1 && ids && XResGetClientPid(ids) == pid_;
        if (ids)
            XResClientIdsDestroy(count, ids);
        return matches;
    }
    bool extents(bool &shaped, unsigned int &width, unsigned int &height) {
        Bool bounding = False, input = False;
        int x = 0, y = 0, cx = 0, cy = 0;
        unsigned int cw = 0, ch = 0;
        const bool ok = XShapeQueryExtents(display_, window_, &bounding, &x, &y, &width, &height,
                                           &input, &cx, &cy, &cw, &ch);
        shaped = bounding;
        return ok;
    }
    bool property(Window window, const char *name, Atom type, unsigned long expected) {
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, rest = 0;
        unsigned char *data = nullptr;
        const int status =
            XGetWindowProperty(display_, window, XInternAtom(display_, name, False), 0, 2, False,
                               type, &actual, &format, &count, &rest, &data);
        const bool matches = status == Success && actual == type && format == 32 && count &&
                             !rest && data && *reinterpret_cast<unsigned long *>(data) == expected;
        if (data)
            XFree(data);
        return matches;
    }
    bool gnome_window_manager() {
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, rest = 0;
        unsigned char *data = nullptr;
        const auto root = DefaultRootWindow(display_);
        const auto name = XInternAtom(display_, "_NET_SUPPORTING_WM_CHECK", False);
        const auto status = XGetWindowProperty(display_, root, name, 0, 1, False, XA_WINDOW,
                                               &actual, &format, &count, &rest, &data);
        wm_ =
            status == Success && actual == XA_WINDOW && format == 32 && count == 1 && !rest && data
                ? *reinterpret_cast<unsigned long *>(data)
                : None;
        if (data)
            XFree(data);
        if (!wm_ || !property(wm_, "_NET_SUPPORTING_WM_CHECK", XA_WINDOW, wm_))
            return false;
        data = nullptr;
        const auto utf8 = XInternAtom(display_, "UTF8_STRING", False);
        const auto result =
            XGetWindowProperty(display_, wm_, XInternAtom(display_, "_NET_WM_NAME", False), 0, 32,
                               False, utf8, &actual, &format, &count, &rest, &data);
        const bool gnome = result == Success && actual == utf8 && format == 8 && !rest && data &&
                           std::string_view(reinterpret_cast<char *>(data), count) == "GNOME Shell";
        if (data)
            XFree(data);
        return gnome;
    }
    bool managed(bool expected) {
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, rest = 0;
        unsigned char *data = nullptr;
        const auto status = XGetWindowProperty(
            display_, DefaultRootWindow(display_), XInternAtom(display_, "_NET_CLIENT_LIST", False),
            0, 256, False, XA_WINDOW, &actual, &format, &count, &rest, &data);
        const bool valid = status == Success && actual == XA_WINDOW && format == 32 && !rest &&
                           count <= 256 && (!count || data);
        bool found = false;
        if (valid && data)
            for (unsigned long i = 0; i < count; ++i)
                found |= reinterpret_cast<unsigned long *>(data)[i] == window_;
        if (data)
            XFree(data);
        return valid && found == expected;
    }
    bool mapped_state(bool mapped) {
        XWindowAttributes attributes{};
        return XGetWindowAttributes(display_, window_, &attributes) &&
               attributes.map_state == (mapped ? IsViewable : IsUnmapped) &&
               (mapped ? (managed(true) &&
                          property(window_, "WM_STATE", XInternAtom(display_, "WM_STATE", False),
                                   NormalState))
                       : ((managed(false) &&
                           property(window_, "WM_STATE", XInternAtom(display_, "WM_STATE", False),
                                    WithdrawnState)) ||
                          (managed(true) &&
                           property(window_, "WM_STATE", XInternAtom(display_, "WM_STATE", False),
                                    IconicState))));
    }
    bool save_mapping() {
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, rest = 0;
        unsigned char *data = nullptr;
        const auto status =
            XGetWindowProperty(display_, window_, XInternAtom(display_, "_NET_WM_STATE", False), 0,
                               64, False, XA_ATOM, &actual, &format, &count, &rest, &data);
        original_states_.clear();
        const bool valid =
            status == Success && !rest && (actual == None || (actual == XA_ATOM && format == 32));
        if (valid && data && count)
            original_states_.assign(reinterpret_cast<unsigned long *>(data),
                                    reinterpret_cast<unsigned long *>(data) + count);
        if (data)
            XFree(data);
        Window child = None;
        XWindowAttributes attrs{};
        long supplied = 0;
        original_hints_ = {};
        original_hints_valid_ = XGetWMNormalHints(display_, window_, &original_hints_, &supplied);
        if (!valid || !XGetWindowAttributes(display_, window_, &attrs) ||
            !XTranslateCoordinates(display_, window_, DefaultRootWindow(display_), 0, 0,
                                   &original_x_, &original_y_, &child))
            return false;
        original_width_ = attrs.width;
        original_height_ = attrs.height;
        return true;
    }
    bool wait_mapping(bool mapped, const desktop::CancelToken &cancel) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1500};
        do {
            if (cancel.cancelled() || !identity(window_))
                return false;
            if (mapped_state(mapped))
                return true;
            (void)::poll(nullptr, 0, 10);
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    ProviderError verify_locked() {
        if (wayland_) {
            if (!display_ || !window_ || !identity(window_) || !mapped_state(false) ||
                !property(DefaultRootWindow(display_), "_NET_SUPPORTING_WM_CHECK", XA_WINDOW,
                          wm_) ||
                !property(window_, "_NET_WM_WINDOW_TYPE", XA_ATOM,
                          XInternAtom(display_, "_NET_WM_WINDOW_TYPE_UTILITY", False)))
                return {"pet_exclusion_failed", "owned pet is no longer hidden by the verified WM"};
            return {};
        }
        bool shaped = false;
        unsigned int width = 0, height = 0;
        if (!display_ || !window_ || !identity(window_) || !extents(shaped, width, height) ||
            !shaped || width || height)
            return {"pet_exclusion_failed", "owned pet identity or empty bounding shape changed"};
        return {};
    }
    ProviderError hide_locked(const desktop::CancelToken &cancel) {
        const char *wayland = std::getenv("WAYLAND_DISPLAY"),
                   *session = std::getenv("XDG_SESSION_TYPE");
        wayland_ = (wayland && *wayland) || (session && std::string_view(session) == "wayland");
        if (wayland_ && !presentation_)
            return unavailable("Wayland pet exclusion requires the Host-owned Portal frame fence");
        if (!display_) {
            XSetErrorHandler(window_error);
            display_ = XOpenDisplay(nullptr);
        }
        if (!display_)
            return unavailable("pet exclusion X11 connection unavailable");
        int major = 0, minor = 0, event = 0, error = 0;
        if (!XResQueryVersion(display_, &major, &minor) || major < 1 || (major == 1 && minor < 2) ||
            !XShapeQueryExtension(display_, &event, &error))
            return unavailable("pet exclusion requires X-Resource 1.2 and Shape");
        std::vector<Window> candidates;
        const Window root = DefaultRootWindow(display_);
        Window returned_root = None, parent = None, *children = nullptr;
        unsigned int count = 0;
        if (!XQueryTree(display_, root, &returned_root, &parent, &children, &count))
            return unavailable("cannot enumerate pet surface");
        if (count <= 256 && children)
            candidates.assign(children, children + count);
        if (children)
            XFree(children);
        if (count > 256)
            return unavailable("pet root window capacity exceeded");
        Atom actual = None;
        int format = 0;
        unsigned long total = 0, rest = 0;
        unsigned char *data = nullptr;
        const int status =
            XGetWindowProperty(display_, root, XInternAtom(display_, "_NET_CLIENT_LIST", False), 0,
                               256, False, XA_WINDOW, &actual, &format, &total, &rest, &data);
        if (status == Success && actual == XA_WINDOW && format == 32 && data && !rest) {
            const auto *windows = reinterpret_cast<Window *>(data);
            candidates.insert(candidates.end(), windows, windows + total);
        }
        if (data)
            XFree(data);
        if (rest)
            return unavailable("pet client list capacity exceeded");
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        if (candidates.size() > 256)
            return unavailable("pet window capacity exceeded");
        window_ = None;
        for (const auto candidate : candidates) {
            if (cancel.cancelled())
                return {"cancelled", "pet exclusion cancelled"};
            if (identity(candidate)) {
                if (window_ != None) {
                    window_ = None;
                    return unavailable("pet surface is not unique");
                }
                window_ = candidate;
            }
        }
        if (!window_)
            return unavailable("owned pet surface identity could not be verified");
        if (wayland_) {
            if (!gnome_window_manager() ||
                !property(window_, "_NET_WM_WINDOW_TYPE", XA_ATOM,
                          XInternAtom(display_, "_NET_WM_WINDOW_TYPE_UTILITY", False))) {
                window_ = None;
                return unavailable("Wayland pet exclusion currently requires GNOME Shell and an "
                                   "owned utility pet");
            }
            original_mapped_ = !mapped_state(false);
            if (original_mapped_ && !mapped_state(true)) {
                window_ = None;
                return unavailable("pet mapping state is not settled");
            }
            if (original_mapped_) {
                if (!save_mapping()) {
                    window_ = None;
                    return unavailable("pet mapping metadata is unavailable or exceeds capacity");
                }
                XWithdrawWindow(display_, window_, DefaultScreen(display_));
                XSync(display_, False);
            }
            if (!wait_mapping(false, cancel)) {
                const auto restored = restore_locked();
                return !restored.code.empty() ? restored
                       : cancel.cancelled() ? ProviderError{"cancelled", "pet exclusion cancelled"}
                                            : ProviderError{"pet_exclusion_timeout",
                                                            "WM did not confirm pet withdrawal"};
            }
            timespec now{};
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
                const auto restored = restore_locked();
                return restored.code.empty() ? unavailable("pet capture timestamp unavailable")
                                             : restored;
            }
            presentation_->not_before_ns.store(now.tv_sec * INT64_C(1000000000) + now.tv_nsec);
            return {};
        }
        unsigned int width = 0, height = 0;
        if (!extents(original_shaped_, width, height))
            return unavailable("pet shape unavailable");
        int rectangles = 0, order = 0;
        auto *shape = XShapeGetRectangles(display_, window_, ShapeBounding, &rectangles, &order);
        original_.clear();
        if (shape && rectangles > 0 && rectangles <= 4096)
            original_.assign(shape, shape + rectangles);
        if (shape)
            XFree(shape);
        if (rectangles < 0 || rectangles > 4096 || (rectangles && original_.empty()))
            return unavailable("pet original shape exceeds capacity or is unavailable");
        XShapeCombineRectangles(display_, window_, ShapeBounding, 0, 0, nullptr, 0, ShapeSet,
                                Unsorted);
        XSync(display_, False);
        if (auto failed = verify_locked(); !failed.code.empty()) {
            (void)restore_locked();
            return failed;
        }
        if (cancel.cancelled()) {
            const auto restored = restore_locked();
            return restored.code.empty() ? ProviderError{"cancelled", "pet exclusion cancelled"}
                                         : restored;
        }
        return {};
    }
    ProviderError restore_locked() {
        if (!window_)
            return {};
        const auto target = window_;
        // A destroyed surface no longer obstructs the desktop. Never mutate a
        // recycled XID that no longer belongs to the exact owned client.
        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display_, target, &attributes)) {
            window_ = None;
            return {};
        }
        if (!identity(target)) {
            window_ = None;
            return {"pet_restore_failed", "pet identity changed before restore"};
        }
        if (wayland_) {
            bool restored = true;
            if (original_mapped_) {
                const auto state = XInternAtom(display_, "_NET_WM_STATE", False);
                XChangeProperty(display_, target, state, XA_ATOM, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char *>(original_states_.data()),
                                static_cast<int>(original_states_.size()));
                auto hints = original_hints_;
                hints.flags |= USPosition;
                hints.x = original_x_;
                hints.y = original_y_;
                XSetWMNormalHints(display_, target, &hints);
                XMoveResizeWindow(display_, target, original_x_, original_y_, original_width_,
                                  original_height_);
                XMapWindow(display_, target);
                XSync(display_, False);
                restored = wait_mapping(true, {});
                if (!identity(target)) {
                    window_ = None;
                    return {"pet_restore_failed", "pet identity changed while restoring mapping"};
                }
                if (original_hints_valid_)
                    XSetWMNormalHints(display_, target, &original_hints_);
                else
                    XDeleteProperty(display_, target,
                                    XInternAtom(display_, "WM_NORMAL_HINTS", False));
                XSync(display_, False);
            }
            window_ = None;
            return restored
                       ? ProviderError{}
                       : ProviderError{"pet_restore_failed", "WM did not restore the owned pet"};
        }
        if (original_shaped_)
            XShapeCombineRectangles(display_, target, ShapeBounding, 0, 0, original_.data(),
                                    static_cast<int>(original_.size()), ShapeSet, Unsorted);
        else
            XShapeCombineMask(display_, target, ShapeBounding, 0, 0, None, ShapeSet);
        XSync(display_, False);
        bool shaped = false;
        unsigned int width = 0, height = 0;
        bool restored = extents(shaped, width, height) && shaped == original_shaped_;
        if (restored && original_shaped_) {
            int count = 0, order = 0;
            auto *rectangles = XShapeGetRectangles(display_, target, ShapeBounding, &count, &order);
            restored = count >= 0 && static_cast<std::size_t>(count) == original_.size() &&
                       (count == 0 || rectangles);
            for (int index = 0; restored && index < count; ++index) {
                const auto &a = rectangles[index];
                const auto &b = original_[static_cast<std::size_t>(index)];
                restored = a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
            }
            if (rectangles)
                XFree(rectangles);
        }
        window_ = None;
        original_.clear();
        return restored ? ProviderError{}
                        : ProviderError{"pet_restore_failed", "pet bounding shape restore failed"};
    }
    std::mutex mutex_; // owns this X connection and overlapping resource leases; no task queue
    std::int64_t pid_;
    std::shared_ptr<PresentationContext> presentation_;
    Display *display_ = nullptr;
    Window window_ = None;
    Window wm_ = None;
    std::vector<XRectangle> original_;
    bool original_shaped_ = false;
    bool wayland_ = false, original_mapped_ = false;
    std::vector<unsigned long> original_states_;
    XSizeHints original_hints_{};
    bool original_hints_valid_ = false;
    int original_x_ = 0, original_y_ = 0, original_width_ = 0, original_height_ = 0;
    unsigned int leases_ = 0;
};
#endif
class Unsupported final : public desktop::PresentationExcluder {
  public:
    explicit Unsupported(bool valid_pid, std::shared_ptr<PresentationContext> presentation)
        : valid_pid_(valid_pid), presentation_(std::move(presentation)) {}
    desktop::PresentationExclusion acquire(const desktop::CancelToken &cancel) override {
        if (!cancel.cancelled() && valid_pid_) {
            // DEC-106: passive fallback also drops a stale exclusion watermark.
            if (presentation_)
                presentation_->not_before_ns.store(0);
            return {std::make_unique<VisibleLease>(), {}};
        }
        return {{},
                cancel.cancelled()
                    ? ProviderError{"cancelled", "pet exclusion cancelled"}
                    : unavailable("pet exclusion requires the Linux X11/Shape/XRes backend")};
    }

  private:
    bool valid_pid_;
    std::shared_ptr<PresentationContext> presentation_;
};
} // namespace
std::shared_ptr<desktop::PresentationExcluder>
make_pet_excluder(std::int64_t pid, std::shared_ptr<PresentationContext> presentation) {
#if defined(MIRAGE_PET_X11_EXCLUSION)
    if (pid > 0)
        return std::make_shared<X11Excluder>(pid, std::move(presentation));
#else
    (void)pid;
#endif
    return std::make_shared<Unsupported>(pid > 0, std::move(presentation));
}
} // namespace mirage::platform::detail
