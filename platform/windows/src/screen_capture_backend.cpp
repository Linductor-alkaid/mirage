#include "screen_capture_backend.hpp"

#include "win32_util.hpp"

// Tiered capture (DEC-086): DXGI Desktop Duplication first, the
// Windows.Graphics.Capture API (COM interop surface, no C++/WinRT) second,
// GDI BitBlt last. Every platform type stays inside this translation unit;
// the Pimpl boundary in screen_capture_backend.hpp is the only include face.

#include <d3d11.h>
#include <dxgi1_2.h>
#include <roapi.h>
#include <windows.foundation.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.graphics.directx.h>
#include <winstring.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mirage::platform::windows_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::CaptureLimits;
using mirage::desktop::CaptureOutcome;
using mirage::desktop::ImageFormat;
using mirage::desktop::ProviderError;
using mirage::desktop::WindowGeometry;

using win32_util::error;

ProviderError make_error(std::string code, std::string message) {
    return error(std::move(code), std::move(message));
}

std::string hresult_suffix(HRESULT hr) {
    char text[32];
    std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(hr));
    return " (HRESULT " + std::string(text) + ")";
}

/// Owning COM interface pointer (uia_backend discipline): releases exactly
/// once, movable so helpers can hand interfaces down their call stacks.
template <typename T> class ComPtr {
  public:
    ComPtr() = default;
    explicit ComPtr(T *ptr) : ptr_(ptr) {}
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ComPtr(ComPtr &&other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    ComPtr &operator=(ComPtr &&other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }
    T **out() { return &ptr_; }
    T *get() const { return ptr_; }
    T *operator->() const { return ptr_; }
    operator bool() const { return ptr_ != nullptr; }
    void reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

  private:
    T *ptr_ = nullptr;
};

/// One call = one Windows Runtime apartment scope (DEC-017 decision 4
/// discipline). RO_INIT_MULTITHREADED joins the process MTA; a thread that
/// already sits there yields S_FALSE (released by the scope like S_OK), and
/// RPC_E_CHANGED_MODE means a foreign apartment lives on the thread — the
/// Graphics-Capture activation is still served, without a pairing teardown.
class WinRtApartment {
  public:
    WinRtApartment() : hr_(::RoInitialize(RO_INIT_MULTITHREADED)) {}
    ~WinRtApartment() {
        if (hr_ == S_OK || hr_ == S_FALSE) {
            ::RoUninitialize();
        }
    }
    WinRtApartment(const WinRtApartment &) = delete;
    WinRtApartment &operator=(const WinRtApartment &) = delete;
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
    HRESULT hr() const { return hr_; }

  private:
    HRESULT hr_;
};

/// The provider coordinate space (what list_displays / list_windows report)
/// and the physical desktop pixel space coincide only when the process sees
/// the desktop unscaled. A DPI-unaware process on a scaled desktop gets its
/// GDI coordinates virtualized, while the duplication and Graphics-Capture
/// tiers are locked to physical pixels — so the Auto chain measures both
/// extents per call and only engages the GPU tiers when the two spaces agree
/// (DEC-086; the provider-wide DPI story stays with the M7 plan).
struct DpiSpace {
    std::int32_t logical_origin_x = 0, logical_origin_y = 0;
    std::int32_t logical_width = 0, logical_height = 0;
    std::int32_t physical_origin_x = 0, physical_origin_y = 0;
    std::int32_t physical_width = 0, physical_height = 0;

    bool identity() const {
        return logical_width == physical_width && logical_height == physical_height;
    }
};

void measure_virtual_screen(std::int32_t *origin_x, std::int32_t *origin_y, std::int32_t *width,
                            std::int32_t *height) {
    *origin_x = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    *origin_y = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    *width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    *height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

DpiSpace measure_dpi_space() {
    DpiSpace space;
    measure_virtual_screen(&space.logical_origin_x, &space.logical_origin_y, &space.logical_width,
                           &space.logical_height);
    // The per-thread switch isolates the physical measurement from the
    // caller's context; the previous context is restored before anything
    // else runs so no later GDI call sees the aware view.
    const DPI_AWARENESS_CONTEXT previous =
        ::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (previous != nullptr) {
        measure_virtual_screen(&space.physical_origin_x, &space.physical_origin_y,
                               &space.physical_width, &space.physical_height);
        ::SetThreadDpiAwarenessContext(previous);
    } else {
        // Pre-1607 system: no per-thread virtualization exists, GDI
        // coordinates are physical already.
        space.physical_origin_x = space.logical_origin_x;
        space.physical_origin_y = space.logical_origin_y;
        space.physical_width = space.logical_width;
        space.physical_height = space.logical_height;
    }
    return space;
}

/// Maps a provider-space rectangle into physical pixels (DEC-086); an
/// identity space maps the rectangle onto itself. Per-coordinate rounding,
/// clamped into the physical virtual screen.
WindowGeometry to_physical(const WindowGeometry &rect, const DpiSpace &space) {
    if (space.identity() || space.logical_width <= 0 || space.logical_height <= 0) {
        return rect;
    }
    const double scale_x = static_cast<double>(space.physical_width) / space.logical_width;
    const double scale_y = static_cast<double>(space.physical_height) / space.logical_height;
    const auto map_edge = [&](std::int32_t value, std::int32_t logical_origin,
                              std::int32_t physical_origin, double scale) {
        return static_cast<std::int32_t>(
            std::llround(physical_origin + (value - logical_origin) * scale));
    };
    WindowGeometry physical;
    physical.x = map_edge(rect.x, space.logical_origin_x, space.physical_origin_x, scale_x);
    physical.y = map_edge(rect.y, space.logical_origin_y, space.physical_origin_y, scale_y);
    const std::int32_t right =
        map_edge(rect.x + rect.width, space.logical_origin_x, space.physical_origin_x, scale_x);
    const std::int32_t bottom =
        map_edge(rect.y + rect.height, space.logical_origin_y, space.physical_origin_y, scale_y);
    physical.width = right - physical.x;
    physical.height = bottom - physical.y;
    const std::int32_t max_x = space.physical_origin_x + space.physical_width;
    const std::int32_t max_y = space.physical_origin_y + space.physical_height;
    physical.x = std::clamp(physical.x, space.physical_origin_x, max_x);
    physical.y = std::clamp(physical.y, space.physical_origin_y, max_y);
    physical.width = std::clamp(physical.width, 1, max_x - physical.x);
    physical.height = std::clamp(physical.height, 1, max_y - physical.y);
    return physical;
}

/// The provider-space bounds of the monitor nearest the request rectangle,
/// when the rectangle lies fully inside it. GPU tiers are single-output
/// (both duplication and Graphics-Capture items are per-monitor); spanning
/// rectangles stay on the BitBlt tier, which composes across the virtual
/// screen natively.
std::optional<WindowGeometry> containing_monitor_rect(const WindowGeometry &rect) {
    const RECT native{rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
    const HMONITOR monitor = ::MonitorFromRect(&native, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr) {
        return std::nullopt;
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetMonitorInfoW(monitor, &info) == 0) {
        return std::nullopt;
    }
    const WindowGeometry bounds{info.rcMonitor.left, info.rcMonitor.top,
                                info.rcMonitor.right - info.rcMonitor.left,
                                info.rcMonitor.bottom - info.rcMonitor.top};
    if (rect.x < bounds.x || rect.y < bounds.y || rect.x + rect.width > bounds.x + bounds.width ||
        rect.y + rect.height > bounds.y + bounds.height) {
        return std::nullopt;
    }
    return bounds;
}

/// A monitor's bounds in physical pixels, measured under the per-thread
/// aware context regardless of the process view — the GPU tiers crop against
/// this, never against the (possibly virtualized) provider space.
WindowGeometry physical_monitor_rect(HMONITOR monitor) {
    const DPI_AWARENESS_CONTEXT previous =
        ::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    const BOOL described = ::GetMonitorInfoW(monitor, &info);
    if (previous != nullptr) {
        ::SetThreadDpiAwarenessContext(previous);
    }
    if (described == 0) {
        return {};
    }
    return WindowGeometry{info.rcMonitor.left, info.rcMonitor.top,
                          info.rcMonitor.right - info.rcMonitor.left,
                          info.rcMonitor.bottom - info.rcMonitor.top};
}

/// Copies the `rect` (texture-local physical coordinates) region out of a
/// mapped staging texture into the packed Bgra8 result frame. B8G8R8A8 rows
/// move with memcpy; the R8G8B8A8 variant swaps the chroma channels on the
/// way out (some compositions and remote sessions present RGBA).
CaptureOutcome read_staging_region(ID3D11DeviceContext *context, ID3D11Texture2D *staging,
                                   const WindowGeometry &rect, DXGI_FORMAT format,
                                   const CaptureLimits &limits) {
    CaptureOutcome outcome;
    D3D11_TEXTURE2D_DESC staging_desc{};
    staging->GetDesc(&staging_desc);
    if (rect.x < 0 || rect.y < 0 || rect.x + rect.width > static_cast<LONG>(staging_desc.Width) ||
        rect.y + rect.height > static_cast<LONG>(staging_desc.Height)) {
        outcome.error = make_error("io_error", "captured frame is smaller than the request");
        return outcome;
    }
    const std::size_t stride = static_cast<std::size_t>(rect.width) * 4u;
    const std::size_t bytes = stride * static_cast<std::size_t>(rect.height);
    if (bytes > limits.max_bytes) {
        outcome.error =
            make_error("capture_too_large", "captured frame exceeds the byte budget of " +
                                                std::to_string(limits.max_bytes) + " bytes");
        return outcome;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT map = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(map)) {
        outcome.error =
            make_error("io_error", "staging texture could not be mapped" + hresult_suffix(map));
        return outcome;
    }
    outcome.frame.format = ImageFormat::Bgra8;
    outcome.frame.width = rect.width;
    outcome.frame.height = rect.height;
    outcome.frame.stride = stride;
    outcome.frame.pixels.resize(bytes);
    const auto *base = static_cast<const std::uint8_t *>(mapped.pData) +
                       static_cast<std::size_t>(rect.y) * mapped.RowPitch;
    for (LONG row = 0; row < rect.height; ++row) {
        const std::uint8_t *source = base + static_cast<std::size_t>(row) * mapped.RowPitch +
                                     static_cast<std::size_t>(rect.x) * 4u;
        std::uint8_t *target = outcome.frame.pixels.data() + static_cast<std::size_t>(row) * stride;
        if (format == DXGI_FORMAT_B8G8R8A8_UNORM) {
            std::memcpy(target, source, stride);
        } else {
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(rect.width); ++pixel) {
                target[pixel * 4u + 0u] = source[pixel * 4u + 2u];
                target[pixel * 4u + 1u] = source[pixel * 4u + 1u];
                target[pixel * 4u + 2u] = source[pixel * 4u + 0u];
                target[pixel * 4u + 3u] = source[pixel * 4u + 3u];
            }
        }
    }
    context->Unmap(staging, 0);
    outcome.ok = true;
    return outcome;
}

/// Samples the frame on a coarse grid; true when every sampled pixel is
/// pure black. Desktop duplication's placeholder frames read as a uniform
/// void — no real desktop region is — so this is the backstop that turns
/// the empty frame into a tier failure instead of a false success.
bool duplication_frame_is_empty(const mirage::desktop::ImageFrame &frame) {
    const std::size_t row_stride = frame.stride;
    for (std::int32_t y = 0; y < frame.height; y += 8) {
        const std::uint8_t *row = frame.pixels.data() + static_cast<std::size_t>(y) * row_stride;
        for (std::int32_t x = 0; x < frame.width; x += 8) {
            const std::uint8_t *pixel = row + static_cast<std::size_t>(x) * 4u;
            if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

struct ScreenCaptureEngine::Impl { /// The D3D11 device of the adapter that owns the last captured
                                   /// output.
    /// Recreated when an output on a different adapter becomes the target
    /// (hybrid-graphics machines); all per-call sessions use it and die with
    /// it. BGRA support is required by the Graphics-Capture tier and harmless
    /// to the duplication tier.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    LUID device_adapter{}; // all-zero before the first successful creation

    /// One CPU-readable copy target, reused across calls while its geometry
    /// matches the source; recreated when the source size or format changes
    /// (mode switch, HDR handoff). D3D-lifetime state only — no platform
    /// session lives inside it.
    ComPtr<ID3D11Texture2D> staging;
    UINT staging_width = 0;
    UINT staging_height = 0;
    DXGI_FORMAT staging_format = DXGI_FORMAT_UNKNOWN;

    void release_gpu_state() {
        staging.reset();
        staging_width = 0;
        staging_height = 0;
        staging_format = DXGI_FORMAT_UNKNOWN;
        context.reset();
        device.reset();
        device_adapter = LUID{};
    }

    HRESULT ensure_device_for_output(IDXGIAdapter1 *adapter) {
        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT described = adapter->GetDesc1(&desc);
        if (FAILED(described)) {
            return described;
        }
        if (device && device_adapter.LowPart == desc.AdapterLuid.LowPart &&
            device_adapter.HighPart == desc.AdapterLuid.HighPart) {
            return S_OK;
        }
        release_gpu_state();
        static constexpr D3D_FEATURE_LEVEL kLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        const HRESULT created = ::D3D11CreateDevice(
            adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels,
            static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, device.out(), nullptr,
            context.out());
        if (FAILED(created)) {
            release_gpu_state();
            return created;
        }
        device_adapter = desc.AdapterLuid;
        return S_OK;
    }

    HRESULT ensure_staging(UINT width, UINT height, DXGI_FORMAT format) {
        if (staging && staging_width == width && staging_height == height &&
            staging_format == format) {
            return S_OK;
        }
        staging.reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        const HRESULT created = device->CreateTexture2D(&desc, nullptr, staging.out());
        if (FAILED(created)) {
            staging.reset();
            return created;
        }
        staging_width = width;
        staging_height = height;
        staging_format = format;
        return S_OK;
    }
};

ScreenCaptureEngine::ScreenCaptureEngine() : impl_(std::make_unique<Impl>()) {}

ScreenCaptureEngine::~ScreenCaptureEngine() = default;

std::unique_ptr<ScreenCaptureEngine> ScreenCaptureEngine::create() {
    // Intentionally cannot fail: every platform refusal surfaces per call.
    return std::unique_ptr<ScreenCaptureEngine>(new ScreenCaptureEngine());
}

namespace {

/// Resolves the adapter that owns `monitor`; null when no output matches.
ComPtr<IDXGIAdapter1> adapter_for_monitor(IDXGIFactory1 *factory, HMONITOR monitor) {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory->EnumAdapters1(index, adapter.out()) != DXGI_ERROR_NOT_FOUND;
         ++index) {
        bool owns = false;
        for (UINT output_index = 0;; ++output_index) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(output_index, output.out()) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            DXGI_OUTPUT_DESC desc{};
            if (output->GetDesc(&desc) == S_OK && desc.Monitor == monitor) {
                owns = true;
                break;
            }
        }
        if (owns) {
            return adapter;
        }
        adapter.reset();
    }
    return {};
}

/// DXGI Desktop Duplication tier (DEC-086): one fresh duplication session
/// per call — the first AcquireNextFrame of a new session carries the full
/// current desktop image, so the tier never serves a stale frame, and a lost
/// session (mode switch, another duplication client) costs one call, not the
/// engine. Rotated and HDR surfaces are refused honestly and fall through to
/// the tiers that handle them.
CaptureOutcome capture_with_dxgi(ScreenCaptureEngine::Impl &impl,
                                 const WindowGeometry &physical_rect, const CaptureLimits &limits,
                                 const CancelToken &cancel) {
    CaptureOutcome outcome;
    const RECT native{physical_rect.x, physical_rect.y, physical_rect.x + physical_rect.width,
                      physical_rect.y + physical_rect.height};
    const HMONITOR monitor = ::MonitorFromRect(&native, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr) {
        outcome.error = make_error("io_error", "no monitor for capture rectangle");
        return outcome;
    }

    ComPtr<IDXGIFactory1> factory;
    const HRESULT factory_created =
        ::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.out()));
    if (FAILED(factory_created)) {
        outcome.error =
            make_error("io_error", "DXGI factory unavailable" + hresult_suffix(factory_created));
        return outcome;
    }
    const ComPtr<IDXGIAdapter1> adapter = adapter_for_monitor(factory.get(), monitor);
    if (!adapter) {
        outcome.error = make_error("unsupported_platform", "no duplication output for monitor");
        return outcome;
    }

    ComPtr<IDXGIOutput> output;
    for (UINT output_index = 0;; ++output_index) {
        if (adapter->EnumOutputs(output_index, output.out()) == DXGI_ERROR_NOT_FOUND) {
            output.reset();
            break;
        }
        DXGI_OUTPUT_DESC desc{};
        if (output->GetDesc(&desc) == S_OK && desc.Monitor == monitor) {
            if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY) {
                output.reset();
                outcome.error =
                    make_error("unsupported_platform",
                               "rotated output is not supported by desktop duplication; falling "
                               "back");
                return outcome;
            }
            break;
        }
        output.reset();
    }
    if (!output) {
        outcome.error = make_error("unsupported_platform", "no duplication output for monitor");
        return outcome;
    }
    DXGI_OUTPUT_DESC output_desc{};
    if (output->GetDesc(&output_desc) != S_OK) {
        outcome.error = make_error("io_error", "output description unavailable");
        return outcome;
    }

    const HRESULT device_ready = impl.ensure_device_for_output(adapter.get());
    if (FAILED(device_ready)) {
        outcome.error =
            make_error("io_error", "D3D11 device unavailable" + hresult_suffix(device_ready));
        return outcome;
    }
    ComPtr<IDXGIOutput1> output1;
    const HRESULT queried =
        output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void **>(output1.out()));
    if (FAILED(queried)) {
        outcome.error =
            make_error("io_error", "IDXGIOutput1 unavailable" + hresult_suffix(queried));
        return outcome;
    }

    // One bounded wait for the initial frame, then one longer retry: a
    // genuinely static desktop still delivers the session's first frame, so
    // a double timeout means the session is not usable — reported, not
    // papered over with an unbounded wait.
    ComPtr<IDXGIOutputDuplication> duplication;
    const HRESULT duplicated = output1->DuplicateOutput(impl.device.get(), duplication.out());
    if (FAILED(duplicated)) {
        if (duplicated == E_ACCESSDENIED) {
            outcome.error = make_error(
                "io_error",
                "desktop duplication is occupied (another capture client or the secure desktop)");
        } else {
            outcome.error =
                make_error("io_error", "desktop duplication refused" + hresult_suffix(duplicated));
        }
        return outcome;
    }

    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    ComPtr<IDXGIResource> resource;
    HRESULT acquired = DXGI_ERROR_WAIT_TIMEOUT;
    // A fresh duplication can deliver placeholder frames before the first
    // real composition reaches the output — they carry a zero present time
    // and zero accumulated frames (observed live as all-black images on a
    // hybrid-graphics mixed-DPI desktop). Each retry releases the
    // placeholder and waits a bounded slice; the whole loop stays inside the
    // tier's wait budget and observes cancellation between slices.
    for (int attempt = 0; attempt < 5; ++attempt) {
        acquired =
            duplication->AcquireNextFrame(attempt == 0 ? 200u : 100u, &frame_info, resource.out());
        if (FAILED(acquired)) {
            break;
        }
        if (frame_info.LastPresentTime.QuadPart != 0 || frame_info.AccumulatedFrames != 0) {
            break;
        }
        duplication->ReleaseFrame();
        resource.reset();
        if (cancel.cancelled()) {
            outcome.cancelled = true;
            outcome.error = make_error("cancelled", "capture cancelled");
            return outcome;
        }
    }
    if (acquired == DXGI_ERROR_WAIT_TIMEOUT) {
        outcome.error = make_error("io_error", "duplication delivered no frame within its budget");
        return outcome;
    }
    if (acquired == DXGI_ERROR_ACCESS_LOST) {
        outcome.error =
            make_error("io_error", "duplication session was lost; the next capture re-creates it");
        return outcome;
    }
    if (FAILED(acquired)) {
        outcome.error =
            make_error("io_error", "frame acquisition failed" + hresult_suffix(acquired));
        return outcome;
    }

    ComPtr<ID3D11Texture2D> desktop_texture;
    const HRESULT textured = resource->QueryInterface(
        __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(desktop_texture.out()));
    resource.reset();
    if (FAILED(textured)) {
        duplication->ReleaseFrame();
        outcome.error = make_error("io_error", "desktop image is not a 2D texture");
        return outcome;
    }
    D3D11_TEXTURE2D_DESC texture_desc{};
    desktop_texture->GetDesc(&texture_desc);
    if (texture_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        texture_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        duplication->ReleaseFrame();
        // HDR compositions present float formats; the Graphics-Capture tier
        // tone-maps them into the contract's Bgra8.
        outcome.error = make_error("unsupported_content",
                                   "desktop image format is not Bgra8-compatible; falling back");
        return outcome;
    }
    const HRESULT staged =
        impl.ensure_staging(texture_desc.Width, texture_desc.Height, texture_desc.Format);
    if (FAILED(staged)) {
        duplication->ReleaseFrame();
        outcome.error =
            make_error("io_error", "staging texture unavailable" + hresult_suffix(staged));
        return outcome;
    }

    impl.context->CopyResource(impl.staging.get(), desktop_texture.get());
    // The desktop image is only valid inside the acquire/release pair; the
    // copy is submitted, so the session frame goes back before the CPU reads
    // the staging copy (Map on a staging texture synchronizes the GPU).
    duplication->ReleaseFrame();
    desktop_texture.reset();

    const WindowGeometry region{physical_rect.x - output_desc.DesktopCoordinates.left,
                                physical_rect.y - output_desc.DesktopCoordinates.top,
                                physical_rect.width, physical_rect.height};
    CaptureOutcome read = read_staging_region(impl.context.get(), impl.staging.get(), region,
                                              texture_desc.Format, limits);
    if (read.ok && duplication_frame_is_empty(read.frame)) {
        // The placeholder frame slipped through the present-time check (a
        // driver may stamp a present time on it) — the image is a uniform
        // void, which no real desktop region is, so the tier refuses and the
        // chain falls through instead of reporting a false success.
        read.ok = false;
        read.frame = mirage::desktop::ImageFrame{};
        read.error = make_error("io_error", "desktop duplication delivered an empty frame; "
                                            "falling back");
    }
    return read;
}

/// Windows.Graphics.Capture tier (DEC-086): a free-threaded frame pool per
/// call, polled for its first frame and closed — no DispatcherQueue, no
/// event worker, no resident session. CreateFreeThreaded needs the 1809+
/// activation statics; an older runtime reports the tier unavailable
/// honestly. Cursor exclusion keeps the tier's pixels consistent with the
/// other two (the hardware cursor is not part of the desktop image), and the
/// system border overlay is dropped where the OS allows it.
CaptureOutcome capture_with_graphics_capture(ScreenCaptureEngine::Impl &impl,
                                             const WindowGeometry &physical_rect,
                                             const CaptureLimits &limits,
                                             const CancelToken &cancel) {
    CaptureOutcome outcome;
    WinRtApartment apartment;
    if (!apartment.ok()) {
        outcome.error = make_error("io_error", "Windows Runtime apartment unavailable" +
                                                   hresult_suffix(apartment.hr()));
        return outcome;
    }

    const RECT native{physical_rect.x, physical_rect.y, physical_rect.x + physical_rect.width,
                      physical_rect.y + physical_rect.height};
    const HMONITOR monitor = ::MonitorFromRect(&native, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr) {
        outcome.error = make_error("io_error", "no monitor for capture rectangle");
        return outcome;
    }
    const WindowGeometry monitor_bounds = physical_monitor_rect(monitor);
    if (monitor_bounds.width <= 0 || monitor_bounds.height <= 0) {
        outcome.error = make_error("io_error", "monitor bounds unavailable");
        return outcome;
    }

    ComPtr<IDXGIFactory1> factory;
    const HRESULT factory_created =
        ::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.out()));
    if (FAILED(factory_created)) {
        outcome.error =
            make_error("io_error", "DXGI factory unavailable" + hresult_suffix(factory_created));
        return outcome;
    }
    const ComPtr<IDXGIAdapter1> adapter = adapter_for_monitor(factory.get(), monitor);
    if (!adapter) {
        outcome.error = make_error("unsupported_platform", "no capture output for monitor");
        return outcome;
    }
    const HRESULT device_ready = impl.ensure_device_for_output(adapter.get());
    if (FAILED(device_ready)) {
        outcome.error =
            make_error("io_error", "D3D11 device unavailable" + hresult_suffix(device_ready));
        return outcome;
    }

    const WCHAR *item_class = RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem;
    HSTRING class_name = nullptr;
    HSTRING_HEADER class_header{};
    const HRESULT named = ::WindowsCreateStringReference(
        item_class, static_cast<UINT32>(wcslen(item_class)), &class_header, &class_name);
    if (FAILED(named)) {
        outcome.error =
            make_error("io_error", "capture class name unavailable" + hresult_suffix(named));
        return outcome;
    }
    ComPtr<IGraphicsCaptureItemInterop> interop;
    const HRESULT interop_got =
        ::RoGetActivationFactory(class_name, __uuidof(IGraphicsCaptureItemInterop),
                                 reinterpret_cast<void **>(interop.out()));
    if (FAILED(interop_got)) {
        // Pre-1803 runtimes have no capture activation factory at all.
        outcome.error = make_error("unsupported_platform",
                                   "Windows Graphics Capture is unavailable on this runtime" +
                                       hresult_suffix(interop_got));
        return outcome;
    }
    ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> item;
    const HRESULT item_created = interop->CreateForMonitor(
        monitor, __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureItem),
        reinterpret_cast<void **>(item.out()));
    interop.reset();
    if (FAILED(item_created)) {
        outcome.error =
            make_error("io_error", "capture item creation failed" + hresult_suffix(item_created));
        return outcome;
    }
    ABI::Windows::Graphics::SizeInt32 item_size{};
    if (item->get_Size(&item_size) != S_OK || item_size.Width <= 0 || item_size.Height <= 0) {
        outcome.error = make_error("io_error", "capture item size unavailable");
        return outcome;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    const HRESULT dxgi_queried = impl.device->QueryInterface(
        __uuidof(IDXGIDevice), reinterpret_cast<void **>(dxgi_device.out()));
    if (FAILED(dxgi_queried)) {
        outcome.error =
            make_error("io_error", "DXGI device unavailable" + hresult_suffix(dxgi_queried));
        return outcome;
    }
    ComPtr<IInspectable> inspectable_device;
    const HRESULT device_wrapped =
        ::CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), inspectable_device.out());
    dxgi_device.reset();
    if (FAILED(device_wrapped)) {
        outcome.error = make_error("io_error", "capture device wrapper unavailable" +
                                                   hresult_suffix(device_wrapped));
        return outcome;
    }
    ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice> capture_device;
    const HRESULT capture_device_queried = inspectable_device->QueryInterface(
        __uuidof(ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice),
        reinterpret_cast<void **>(capture_device.out()));
    inspectable_device.reset();
    if (FAILED(capture_device_queried)) {
        outcome.error = make_error("io_error", "capture device unavailable" +
                                                   hresult_suffix(capture_device_queried));
        return outcome;
    }

    const WCHAR *pool_class = RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool;
    HSTRING pool_name = nullptr;
    HSTRING_HEADER pool_header{};
    const HRESULT pool_named = ::WindowsCreateStringReference(
        pool_class, static_cast<UINT32>(wcslen(pool_class)), &pool_header, &pool_name);
    if (FAILED(pool_named)) {
        outcome.error = make_error("io_error", "frame pool class name unavailable" +
                                                   hresult_suffix(pool_named));
        return outcome;
    }
    ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics> pool_statics;
    const HRESULT statics_got = ::RoGetActivationFactory(
        pool_name, __uuidof(ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics),
        reinterpret_cast<void **>(pool_statics.out()));
    if (FAILED(statics_got)) {
        outcome.error = make_error("unsupported_platform",
                                   "frame pool activation is unavailable on this runtime" +
                                       hresult_suffix(statics_got));
        return outcome;
    }
    ComPtr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics2> pool_statics2;
    const HRESULT statics2_queried = pool_statics->QueryInterface(
        __uuidof(ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePoolStatics2),
        reinterpret_cast<void **>(pool_statics2.out()));
    if (FAILED(statics2_queried)) {
        // The dispatcher-bound `Create` would need a message loop, which the
        // provider discipline does not own (DEC-017 decision 3); without the
        // free-threaded pool the tier is honestly unavailable.
        outcome.error =
            make_error("unsupported_platform",
                       "free-threaded frame pool unavailable (needs Windows 10 1809+); falling "
                       "back");
        return outcome;
    }

    using ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool;
    ComPtr<IDirect3D11CaptureFramePool> pool;
    const HRESULT pool_created = pool_statics2->CreateFreeThreaded(
        capture_device.get(),
        ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized, 2, item_size,
        pool.out());
    pool_statics2.reset();
    pool_statics.reset();
    capture_device.reset();
    if (FAILED(pool_created)) {
        outcome.error =
            make_error("io_error", "frame pool creation failed" + hresult_suffix(pool_created));
        return outcome;
    }

    ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession> session;
    const HRESULT session_created = pool->CreateCaptureSession(item.get(), session.out());
    if (FAILED(session_created)) {
        outcome.error = make_error("io_error", "capture session creation failed" +
                                                   hresult_suffix(session_created));
        return outcome;
    }
    {
        // Pixel-consistency tuning only: a refused property keeps the OS
        // default and the capture stays correct. IsCursorCaptureEnabled
        // ships on IGraphicsCaptureSession2 (1903+), IsBorderRequired on
        // IGraphicsCaptureSession3 (Windows 11) — older runtimes keep their
        // defaults and the tier still captures.
        ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession2> cursor;
        if (session->QueryInterface(
                __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureSession2),
                reinterpret_cast<void **>(cursor.out())) == S_OK) {
            cursor->put_IsCursorCaptureEnabled(FALSE);
        }
        ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureSession3> border;
        if (session->QueryInterface(
                __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureSession3),
                reinterpret_cast<void **>(border.out())) == S_OK) {
            border->put_IsBorderRequired(FALSE);
        }
    }
    const HRESULT started = session->StartCapture();
    if (FAILED(started)) {
        if (started == E_ACCESSDENIED) {
            outcome.error = make_error("permission_denied",
                                       "screen capture was denied by the system capture setting");
        } else {
            outcome.error = make_error("io_error", "capture session refused to start" +
                                                       hresult_suffix(started));
        }
        return outcome;
    }

    // Bounded first-frame poll: StartCapture produces the initial frame
    // asynchronously on the pool's own thread; the bounded slices keep the
    // call synchronous with cancellation observed between slices.
    using ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame;
    ComPtr<IDirect3D11CaptureFrame> frame;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1500};
    for (;;) {
        const HRESULT polled = pool->TryGetNextFrame(frame.out());
        if (SUCCEEDED(polled) && frame) {
            break;
        }
        frame.reset();
        if (std::chrono::steady_clock::now() >= deadline) {
            outcome.error = make_error("io_error", "capture delivered no frame within its budget");
            return outcome;
        }
        if (cancel.cancelled()) {
            outcome.cancelled = true;
            outcome.error = make_error("cancelled", "capture cancelled");
            return outcome;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{15});
    }

    ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface> surface;
    const HRESULT surfaced = frame->get_Surface(surface.out());
    frame.reset();
    if (FAILED(surfaced) || !surface) {
        outcome.error = make_error("io_error", "capture frame surface unavailable");
        return outcome;
    }
    ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
    const HRESULT access_queried = surface->QueryInterface(
        __uuidof(Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess),
        reinterpret_cast<void **>(access.out()));
    surface.reset();
    if (FAILED(access_queried)) {
        outcome.error = make_error("io_error", "capture surface interface access unavailable");
        return outcome;
    }
    ComPtr<ID3D11Texture2D> frame_texture;
    const HRESULT texture_got = access->GetInterface(
        __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(frame_texture.out()));
    access.reset();
    if (FAILED(texture_got) || !frame_texture) {
        outcome.error = make_error("io_error", "capture frame texture unavailable");
        return outcome;
    }

    D3D11_TEXTURE2D_DESC texture_desc{};
    frame_texture->GetDesc(&texture_desc);
    if (texture_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        // The pool was created for B8G8R8A8; anything else means the runtime
        // renegotiated underneath the session — refuse, do not guess.
        outcome.error =
            make_error("unsupported_content", "capture frame format is not Bgra8; falling back");
        return outcome;
    }
    const HRESULT staged =
        impl.ensure_staging(texture_desc.Width, texture_desc.Height, texture_desc.Format);
    if (FAILED(staged)) {
        outcome.error =
            make_error("io_error", "staging texture unavailable" + hresult_suffix(staged));
        return outcome;
    }
    impl.context->CopyResource(impl.staging.get(), frame_texture.get());
    frame_texture.reset();

    // Session teardown before the frame pool: the session drives the pool's
    // production side. Both closes are best-effort; the ComPtr releases are
    // the backstop and the engine keeps no session state across calls.
    {
        ComPtr<ABI::Windows::Foundation::IClosable> closable;
        if (session->QueryInterface(__uuidof(ABI::Windows::Foundation::IClosable),
                                    reinterpret_cast<void **>(closable.out())) == S_OK) {
            closable->Close();
        }
    }
    session.reset();
    {
        ComPtr<ABI::Windows::Foundation::IClosable> closable;
        if (pool->QueryInterface(__uuidof(ABI::Windows::Foundation::IClosable),
                                 reinterpret_cast<void **>(closable.out())) == S_OK) {
            closable->Close();
        }
    }
    pool.reset();

    const WindowGeometry region{physical_rect.x - monitor_bounds.x,
                                physical_rect.y - monitor_bounds.y, physical_rect.width,
                                physical_rect.height};
    return read_staging_region(impl.context.get(), impl.staging.get(), region, texture_desc.Format,
                               limits);
}

/// GDI BitBlt tier (DEC-017 decision 6, moved here as the chain's last rung):
/// the only tier that composes virtual-screen-spanning rectangles and the
/// only one that follows the process's DPI virtualization, which is why the
/// Auto chain keeps it as the honest implementation for scaled unaware
/// processes. Root-frame semantics identical to the X11 backend (M2-02).
CaptureOutcome capture_with_bitblt(const WindowGeometry &rect, const CaptureLimits &limits,
                                   const CancelToken &cancel) {
    CaptureOutcome outcome;
    const std::size_t stride = static_cast<std::size_t>(rect.width) * 4u;
    const std::size_t bytes = stride * static_cast<std::size_t>(rect.height);
    if (bytes > limits.max_bytes) {
        outcome.error =
            make_error("capture_too_large", "capture exceeds the byte budget of " +
                                                std::to_string(limits.max_bytes) + " bytes");
        return outcome;
    }
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = make_error("cancelled", "capture cancelled");
        return outcome;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.width;
    info.bmiHeader.biHeight = -rect.height; // top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen_dc = ::GetDC(nullptr);
    if (screen_dc == nullptr) {
        outcome.error =
            make_error("io_error", "GetDC for the screen failed" + win32_util::last_error_suffix());
        return outcome;
    }
    HDC memory_dc = ::CreateCompatibleDC(screen_dc);
    if (memory_dc == nullptr) {
        ::ReleaseDC(nullptr, screen_dc);
        outcome.error =
            make_error("io_error", "CreateCompatibleDC failed" + win32_util::last_error_suffix());
        return outcome;
    }
    void *bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(memory_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        ::DeleteDC(memory_dc);
        ::ReleaseDC(nullptr, screen_dc);
        outcome.error =
            make_error("io_error", "CreateDIBSection failed" + win32_util::last_error_suffix());
        return outcome;
    }
    const HGDIOBJ previous = ::SelectObject(memory_dc, bitmap);
    const BOOL blitted =
        ::BitBlt(memory_dc, 0, 0, rect.width, rect.height, screen_dc, rect.x, rect.y, SRCCOPY);
    ::GdiFlush();

    if (blitted == 0) {
        outcome.error = make_error("io_error", "BitBlt failed" + win32_util::last_error_suffix());
    } else {
        outcome.frame.format = ImageFormat::Bgra8;
        outcome.frame.width = rect.width;
        outcome.frame.height = rect.height;
        outcome.frame.stride = stride;
        outcome.frame.pixels.resize(bytes);
        std::memcpy(outcome.frame.pixels.data(), bits, bytes);
        outcome.ok = true;
    }

    ::SelectObject(memory_dc, previous);
    ::DeleteObject(bitmap);
    ::DeleteDC(memory_dc);
    ::ReleaseDC(nullptr, screen_dc);
    return outcome;
}

/// Prefixes the failures of the tiers that ran before the final error so a
/// refused GPU tier stays diagnosable from the refused call alone.
ProviderError chain_errors(const std::vector<ProviderError> &prior, const ProviderError &last) {
    if (prior.empty()) {
        return last;
    }
    std::string message;
    for (const auto &failure : prior) {
        if (!message.empty()) {
            message += "; ";
        }
        message += failure.message;
    }
    message += "; " + last.message;
    return ProviderError{last.code, std::move(message)};
}

} // namespace

mirage::desktop::CaptureOutcome
ScreenCaptureEngine::capture_rect(const mirage::desktop::WindowGeometry &rect,
                                  const mirage::desktop::CaptureLimits &limits,
                                  const mirage::desktop::CancelToken &cancel, Tier tier) {
    CaptureOutcome outcome;
    if (rect.width <= 0 || rect.height <= 0) {
        outcome.error = make_error("invalid_argument", "capture extents must be positive");
        return outcome;
    }
    const std::size_t requested_bytes =
        static_cast<std::size_t>(rect.width) * 4u * static_cast<std::size_t>(rect.height);
    if (requested_bytes > limits.max_bytes) {
        outcome.error =
            make_error("capture_too_large", "capture exceeds the byte budget of " +
                                                std::to_string(limits.max_bytes) + " bytes");
        return outcome;
    }
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = make_error("cancelled", "capture cancelled");
        return outcome;
    }

    if (tier == Tier::BitBlt) {
        return capture_with_bitblt(rect, limits, cancel);
    }

    const DpiSpace space = measure_dpi_space();
    const std::optional<WindowGeometry> monitor_bounds = containing_monitor_rect(rect);

    // GPU tiers are single-output; a spanning rectangle has no honest GPU
    // representation and always lands on BitBlt (forced tiers report the
    // refusal instead of silently substituting another implementation).
    if (!monitor_bounds.has_value()) {
        if (tier == Tier::Auto) {
            return capture_with_bitblt(rect, limits, cancel);
        }
        outcome.error = make_error("invalid_argument", "capture rectangle spans multiple outputs");
        return outcome;
    }

    // The physical rectangle feeds both GPU tiers; in an identity space it
    // is the request itself. The Auto chain engages the GPU tiers only when
    // the provider and physical spaces agree (DEC-086 DPI gate).
    const WindowGeometry physical_rect = to_physical(rect, space);
    const bool gpu_eligible = space.identity();

    std::vector<ProviderError> prior_failures;
    if (tier == Tier::Dxgi || (tier == Tier::Auto && gpu_eligible)) {
        CaptureOutcome candidate = capture_with_dxgi(*impl_, physical_rect, limits, cancel);
        if (candidate.ok || candidate.cancelled) {
            return candidate;
        }
        prior_failures.push_back(std::move(candidate.error));
    }
    if (tier == Tier::GraphicsCapture || (tier == Tier::Auto && gpu_eligible)) {
        CaptureOutcome candidate =
            capture_with_graphics_capture(*impl_, physical_rect, limits, cancel);
        if (candidate.ok || candidate.cancelled) {
            return candidate;
        }
        prior_failures.push_back(std::move(candidate.error));
    }
    if (tier != Tier::Auto) {
        // A forced tier owns the failure: no silent BitBlt substitution.
        outcome.error = chain_errors({}, prior_failures.back());
        return outcome;
    }

    CaptureOutcome final_attempt = capture_with_bitblt(rect, limits, cancel);
    if (final_attempt.ok || final_attempt.cancelled) {
        return final_attempt;
    }
    outcome.error = chain_errors(prior_failures, final_attempt.error);
    return outcome;
}

} // namespace mirage::platform::windows_backend
