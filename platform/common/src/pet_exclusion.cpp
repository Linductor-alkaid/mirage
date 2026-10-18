#include "frontend_activation.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>

#if defined(MIRAGE_PET_X11_EXCLUSION)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/XRes.h>
#include <X11/extensions/shape.h>
#endif

namespace mirage::platform::detail {
namespace {
using desktop::ProviderError;
ProviderError unavailable(const char *message) { return {"pet_exclusion_unavailable", message}; }
#if defined(MIRAGE_PET_X11_EXCLUSION)
// Same Xlib error policy as X11Backend: a disappeared desktop window is an
// ordinary provider failure. Never install/restore a per-call global handler.
int window_error(Display *, XErrorEvent *) { return 0; }

class X11Excluder final : public desktop::PresentationExcluder,
                          public std::enable_shared_from_this<X11Excluder> {
  public:
    explicit X11Excluder(std::int64_t pid) : pid_(pid) {}
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
    ProviderError verify_locked() {
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
        if ((wayland && *wayland) || (session && std::string_view(session) == "wayland"))
            return unavailable(
                "pet exclusion requires a verified X11 capture path; Wayland is pending");
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
    Display *display_ = nullptr;
    Window window_ = None;
    std::vector<XRectangle> original_;
    bool original_shaped_ = false;
    unsigned int leases_ = 0;
};
#endif
class Unsupported final : public desktop::PresentationExcluder {
  public:
    desktop::PresentationExclusion acquire(const desktop::CancelToken &cancel) override {
        return {{},
                cancel.cancelled()
                    ? ProviderError{"cancelled", "pet exclusion cancelled"}
                    : unavailable("pet exclusion requires the Linux X11/Shape/XRes backend")};
    }
};
} // namespace
std::shared_ptr<desktop::PresentationExcluder> make_pet_excluder(std::int64_t pid) {
#if defined(MIRAGE_PET_X11_EXCLUSION)
    if (pid > 0)
        return std::make_shared<X11Excluder>(pid);
#else
    (void)pid;
#endif
    return std::make_shared<Unsupported>();
}
} // namespace mirage::platform::detail
