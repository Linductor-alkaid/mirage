#pragma once

// Test-only bounded observations. No names, PIDs, foreign pixels, or retries.
// Kept separate from the product's DEC-065 ownership decision.
#include <X11/Xlib.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <mira/json.hpp>
#ifdef MIRAGE_TEST_X11_DRAG_HELPERS
#include <X11/extensions/XRes.h>
#endif

namespace mirage::testing {
class X11PointerDiagnostics {
    struct Sample {
        const char *phase = "";
        bool queried = false, held = false, logical_target = false, logical_source = false;
        bool top_target = false, top_source = false, top_new = false, top_override = false;
        bool logical_top = false;
        int top_class = 0;
        unsigned int roots = 0;
    };
    Display *display_;
    Window target_;
    XID base_ = 0, mask_ = 0;
    std::array<Window, 128> visible_{};
    std::size_t visible_count_ = 0;
    bool prepared_ = false;
    std::array<Sample, 4> samples_{};
    std::size_t size_ = 0;
    bool truncated_ = false;

  public:
    X11PointerDiagnostics(Display *display, Window target) : display_(display), target_(target) {
#ifdef MIRAGE_TEST_X11_DRAG_HELPERS
        XResClient *clients = nullptr;
        int count = 0;
        if (XResQueryClients(display_, &count, &clients) && clients && count > 0 && count <= 256)
            for (int n = 0; n < count; ++n)
                if ((target_ & ~clients[n].resource_mask) == clients[n].resource_base) {
                    base_ = clients[n].resource_base;
                    mask_ = clients[n].resource_mask;
                    break;
                }
        if (clients)
            XFree(clients);
#endif
        Window root = None, parent = None, *children = nullptr;
        unsigned int count_roots = 0;
        prepared_ = base_ &&
                    XQueryTree(display_, DefaultRootWindow(display_), &root, &parent, &children,
                               &count_roots) &&
                    count_roots <= visible_.size();
        for (unsigned int n = 0; prepared_ && n < count_roots; ++n) {
            XWindowAttributes a{};
            prepared_ = XGetWindowAttributes(display_, children[n], &a) != 0;
            if (prepared_ && a.map_state == IsViewable)
                visible_[visible_count_++] = children[n];
        }
        if (children)
            XFree(children);
    }
    void sample(const char *phase) noexcept {
        if (size_ == samples_.size()) {
            truncated_ = true;
            return;
        }
        auto &s = samples_[size_++];
        s.phase = phase;
        Window root = None, logical = None;
        int x = 0, y = 0, wx = 0, wy = 0;
        unsigned int held = 0;
        if (!XQueryPointer(display_, DefaultRootWindow(display_), &root, &logical, &x, &y, &wx, &wy,
                           &held))
            return;
        s.held = held & Button1Mask;
        s.logical_target = logical == target_;
        s.logical_source = logical != None && base_ && (logical & ~mask_) == base_;
        Window parent = None, *children = nullptr;
        s.queried = XQueryTree(display_, root, &root, &parent, &children, &s.roots) != 0;
        if (s.queried && s.roots <= visible_.size())
            for (unsigned int n = s.roots; n > 0; --n) {
                XWindowAttributes a{};
                if (!XGetWindowAttributes(display_, children[n - 1], &a)) {
                    s.queried = false;
                    break;
                }
                if (a.map_state != IsViewable || x < std::int64_t{a.x} - a.border_width ||
                    y < std::int64_t{a.y} - a.border_width ||
                    x >= std::int64_t{a.x} + a.width + a.border_width ||
                    y >= std::int64_t{a.y} + a.height + a.border_width)
                    continue;
                const auto top = children[n - 1];
                s.top_target = top == target_;
                s.top_source = base_ && (top & ~mask_) == base_;
                s.top_new = prepared_ &&
                            std::find(visible_.begin(), visible_.begin() + visible_count_, top) ==
                                visible_.begin() + visible_count_;
                s.top_override = a.override_redirect;
                s.top_class = a.c_class;
                s.logical_top = logical == top;
                break;
            }
        if (children)
            XFree(children);
    }
    mira::JsonValue report() const {
        mira::JsonValue::Array rows;
        for (std::size_t n = 0; n < size_; ++n) {
            const auto &s = samples_[n];
            rows.emplace_back(mira::JsonValue::Object{{"phase", s.phase},
                                                      {"queried", s.queried},
                                                      {"held", s.held},
                                                      {"roots", std::int64_t{s.roots}},
                                                      {"logical_target", s.logical_target},
                                                      {"logical_source", s.logical_source},
                                                      {"top_target", s.top_target},
                                                      {"top_source", s.top_source},
                                                      {"top_new", s.top_new},
                                                      {"top_override", s.top_override},
                                                      {"top_class", std::int64_t{s.top_class}},
                                                      {"logical_top", s.logical_top}});
        }
        return mira::JsonValue::Object{
            {"prepared", prepared_}, {"truncated", truncated_}, {"samples", std::move(rows)}};
    }
};
} // namespace mirage::testing
