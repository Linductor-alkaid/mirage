#include "uia_backend.hpp"

#include "win32_util.hpp"

// UI Automation client surface (M4-02, DEC-017 decision 4). The umbrella
// header provides the IUIAutomation* interfaces; GUID resolution goes
// through __uuidof (both gate toolchains ship the __CRT_UUID_DECL
// annotations), so no uuid.lib is needed and MinGW and MSVC compile the
// exact same declarations. Every COM / UIA type stays inside this
// translation unit (RULE-01).
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mirage::platform::windows_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::ElementActionOutcome;
using mirage::desktop::ElementTarget;
using mirage::desktop::InputLimits;
using mirage::desktop::ProviderError;
using mirage::desktop::SemanticNode;
using mirage::desktop::SemanticSnapshotLimits;
using mirage::desktop::SnapshotOutcome;
using mirage::desktop::WindowGeometry;
using win32_util::error;
using win32_util::parse_window_id;
using win32_util::utf16_to_utf8;
using win32_util::utf8_to_utf16;

// Both gate toolchains name the control-type type CONTROLTYPEID and the
// values UIA_<Name>ControlTypeId (macro constants on MinGW-w64, enum
// ControlTypeIds members in the MSVC SDK); this TU speaks that one
// spelling.
using ControlTypeId = CONTROLTYPEID;

/// One call = one COM scope (DEC-017 decision 4): the calling thread joins
/// the process MTA for the duration of the call. S_OK and S_FALSE both own
/// one MTA reference. `promote_to_anchor()` skips the paired
/// CoUninitialize — the first successful scope of the backend stays open
/// as the process-lifetime MTA anchor (see uia_backend.hpp) without which
/// registry pointers could dangle in the gap between calls.
class ComScope {
  public:
    ComScope() : hr_(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_) && !keep_alive_) {
            ::CoUninitialize();
        }
    }
    ComScope(const ComScope &) = delete;
    ComScope &operator=(const ComScope &) = delete;
    bool ok() const { return SUCCEEDED(hr_); }
    void promote_to_anchor() { keep_alive_ = true; }

  private:
    HRESULT hr_;
    bool keep_alive_ = false;
};

/// Owning wrapper for COM interface pointers acquired through an
/// out-parameter (already AddRef'd): releases exactly once, movable so
/// walks can hand elements down their stacks.
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
    T **out() {
        reset();
        return &ptr_;
    }
    T *get() const { return ptr_; }
    T *operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }
    void reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }
    /// An independent reference to the same object (AddRef), e.g. for
    /// lending a registry entry out while the registry keeps its own.
    ComPtr clone() const {
        if (ptr_ != nullptr) {
            ptr_->AddRef();
        }
        return ComPtr(ptr_);
    }

  private:
    T *ptr_ = nullptr;
};

/// Owns one BSTR allocation.
class Bstr {
  public:
    explicit Bstr(const std::wstring &text)
        : bstr_(::SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {}
    ~Bstr() { ::SysFreeString(bstr_); }
    Bstr(const Bstr &) = delete;
    Bstr &operator=(const Bstr &) = delete;
    BSTR get() const { return bstr_; }
    explicit operator bool() const { return bstr_ != nullptr; }

  private:
    BSTR bstr_;
};

struct HandleCloser {
    void operator()(void *handle) const {
        if (handle != nullptr) {
            ::CloseHandle(handle);
        }
    }
};
using ProcessHandle = std::unique_ptr<void, HandleCloser>;

/// Normalizes a UIA control type into the stable snapshot vocabulary
/// (DEC-005): the same targets the AT-SPI backend projects onto (M2-03
/// freeze), extended with UIA-specific types in the same lowercase style.
/// UIA control types carry no stable non-localized name string (the
/// LocalizedControlType property is locale-dependent), so anything outside
/// the table reports "unknown" — stable and honest — instead of text that
/// changes with the OS language.
std::string snapshot_role(ControlTypeId type) {
    switch (type) {
    case UIA_ButtonControlTypeId:
    case UIA_SplitButtonControlTypeId:
        return "button";
    case UIA_CheckBoxControlTypeId:
        return "checkbox";
    case UIA_RadioButtonControlTypeId:
        return "radio";
    case UIA_MenuControlTypeId:
        return "menu";
    case UIA_MenuItemControlTypeId:
        return "menuitem";
    case UIA_MenuBarControlTypeId:
        return "menubar";
    case UIA_TreeControlTypeId:
        return "tree";
    case UIA_TreeItemControlTypeId:
        return "treeitem";
    case UIA_ListControlTypeId:
        return "list";
    case UIA_ListItemControlTypeId:
        return "listitem";
    case UIA_DataItemControlTypeId:
        return "cell";
    case UIA_TabControlTypeId:
        return "page-tab-list";
    case UIA_TabItemControlTypeId:
        return "pagetab";
    case UIA_ComboBoxControlTypeId:
        return "combobox";
    case UIA_ScrollBarControlTypeId:
        return "scrollbar";
    case UIA_SpinnerControlTypeId:
        return "spinbutton";
    case UIA_ProgressBarControlTypeId:
        return "progressbar";
    case UIA_StatusBarControlTypeId:
        return "statusbar";
    case UIA_ToolBarControlTypeId:
        return "toolbar";
    case UIA_EditControlTypeId:
        return "text";
    case UIA_DocumentControlTypeId:
        return "document";
    case UIA_WindowControlTypeId:
        return "window";
    case UIA_PaneControlTypeId:
    case UIA_GroupControlTypeId:
        return "panel";
    case UIA_TableControlTypeId:
        return "table";
    case UIA_HyperlinkControlTypeId:
        return "link";
    case UIA_ImageControlTypeId:
        return "image";
    case UIA_SliderControlTypeId:
        return "slider";
    case UIA_HeaderControlTypeId:
        return "header";
    case UIA_TitleBarControlTypeId:
        return "titlebar";
    case UIA_SeparatorControlTypeId:
        return "separator";
    case UIA_ToolTipControlTypeId:
        return "tooltip";
    case UIA_CalendarControlTypeId:
        return "calendar";
    case UIA_CustomControlTypeId:
        return "custom";
    default:
        return "unknown";
    }
}

/// DFS walk bookkeeping: an element together with the index its snapshot
/// node will carry (`parent`, kNoParent at the root).
struct WalkEntry {
    ComPtr<IUIAutomationElement> element;
    std::size_t parent;
};

std::vector<std::string> split_path(const std::string &path) {
    std::vector<std::string> segments;
    std::string current;
    for (const char ch : path) {
        if (ch == '/') {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        segments.push_back(current);
    }
    return segments;
}

std::string bstr_to_utf8(BSTR value) {
    if (value == nullptr) {
        return {};
    }
    return utf16_to_utf8(std::wstring(value, ::SysStringLen(value)));
}

/// Window title from the same source the WindowProvider enumerates
/// (GetWindowTextW), so a snapshot reports the title the id was listed
/// with (DEC-017 decision 7 identity chain).
std::string window_title_utf8(HWND window) {
    const int title_len = ::GetWindowTextLengthW(window);
    if (title_len <= 0) {
        return {};
    }
    std::wstring title(static_cast<std::size_t>(title_len), L'\0');
    const int copied = ::GetWindowTextW(window, title.data(), title_len + 1);
    if (copied <= 0) {
        return {};
    }
    title.resize(static_cast<std::size_t>(copied));
    return utf16_to_utf8(title);
}

/// Owning process image name for the snapshot's application field (the
/// honest UIA analog of the AT-SPI application node): the executable file
/// name of the window's process, or empty when the process is gone or
/// denies the limited-information query.
std::string application_name_for(HWND window) {
    DWORD pid = 0;
    ::GetWindowThreadProcessId(window, &pid);
    if (pid == 0) {
        return {};
    }
    ProcessHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (process == nullptr) {
        return {};
    }
    WCHAR path[1024];
    DWORD size = static_cast<DWORD>(std::size(path));
    if (::QueryFullProcessImageNameW(process.get(), 0, path, &size) == 0 || size == 0) {
        return {};
    }
    const std::wstring full(path, size);
    const auto slash = full.find_last_of(L"\\/");
    return utf16_to_utf8(slash == std::wstring::npos ? full : full.substr(slash + 1));
}

} // namespace

struct UiaBackend::Impl {
    /// Snapshot-issued handles from the latest snapshot -> owning element
    /// proxies. Replaced wholesale per snapshot: older handles stop
    /// resolving ("not_found"), the behavior-result-driven refresh policy
    /// v1 (DEC-005). Safe to hold across calls only because of the MTA
    /// anchor — see uia_backend.hpp.
    std::map<std::string, ComPtr<IUIAutomationElement>> registry;

    /// Set once the first successful COM scope was promoted to the anchor;
    /// registry entries and the cached `automation` exist only after it.
    bool mta_anchored = false;

    /// The UIA client core, created inside the anchoring scope and reused
    /// by every later call (valid while the anchored MTA lives).
    ComPtr<IUIAutomation> automation;

    void clear_registry() { registry.clear(); }

    /// Requires an open ComScope on the calling thread. Forms the anchor on
    /// the first successful call and returns the control view walker; on
    /// failure sets `failure` and returns null. No contract validation
    /// happens here — callers run their checks first so refusals never
    /// depend on COM at all.
    ComPtr<IUIAutomationTreeWalker> ui_walker_locked(ProviderError &failure) {
        if (!mta_anchored) {
            if (::CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IUIAutomation),
                                   reinterpret_cast<void **>(automation.out())) != S_OK ||
                !automation) {
                automation.reset();
                failure = error("io_error", "UI Automation client core is unavailable");
                return {};
            }
            mta_anchored = true; // the scope stays open from here on
        }
        ComPtr<IUIAutomationTreeWalker> walker;
        if (automation->get_ControlViewWalker(walker.out()) != S_OK || !walker) {
            failure = error("io_error", "UI Automation control view walker is unavailable");
            return {};
        }
        return walker;
    }

    /// Reads one node's contract fields; a property the provider refuses
    /// (or a vanished element) degrades that field, never the whole walk —
    /// a window changing mid-enumeration is not an error (M4-01
    /// discipline). Every property is one cross-process call; the whole
    /// walk stays bounded by the node budget.
    SemanticNode describe(IUIAutomationElement *element) const {
        SemanticNode node;
        BSTR name = nullptr;
        if (element->get_CurrentName(&name) == S_OK) {
            node.name = bstr_to_utf8(name);
        }
        ::SysFreeString(name);
        BSTR help = nullptr;
        if (element->get_CurrentHelpText(&help) == S_OK) {
            node.description = bstr_to_utf8(help); // UIA's closest analog of a description
        }
        ::SysFreeString(help);
        ControlTypeId type = UIA_CustomControlTypeId;
        node.role =
            element->get_CurrentControlType(&type) == S_OK ? snapshot_role(type) : "unknown";
        RECT rect{};
        if (element->get_CurrentBoundingRectangle(&rect) == S_OK && rect.right >= rect.left &&
            rect.bottom >= rect.top) {
            node.geometry =
                WindowGeometry{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
        }
        BOOL flag = FALSE;
        node.focused = element->get_CurrentHasKeyboardFocus(&flag) == S_OK && flag != 0;
        node.enabled = element->get_CurrentIsEnabled(&flag) != S_OK || flag != 0;
        return node;
    }

    /// Walks the control-view subtree of `root` in DFS order. Stops and
    /// returns nullopt as soon as collecting another node would exceed
    /// `budget` (refusal, never truncation). Children that fail to acquire
    /// (element vanished mid-walk) end that branch quietly.
    std::optional<std::vector<SemanticNode>> collect_tree(IUIAutomationTreeWalker *walker,
                                                          IUIAutomationElement *root,
                                                          std::size_t budget) const {
        std::vector<SemanticNode> nodes;
        std::vector<WalkEntry> stack;
        // The walk owns its own reference to the root (the caller's stays).
        root->AddRef();
        stack.push_back({ComPtr<IUIAutomationElement>(root), mirage::desktop::kNoParent});
        while (!stack.empty()) {
            WalkEntry current = std::move(stack.back());
            stack.pop_back();
            if (nodes.size() >= budget) {
                return std::nullopt; // remaining stack entries release themselves
            }
            SemanticNode node = describe(current.element.get());
            node.parent = current.parent;
            node.ref = "@e" + std::to_string(nodes.size() + 1);
            nodes.push_back(std::move(node));
            // Children keep the DFS order: collect first-to-last, push
            // reversed so the first child is visited next (deterministic
            // snapshots).
            std::vector<ComPtr<IUIAutomationElement>> kids;
            ComPtr<IUIAutomationElement> child;
            HRESULT hr = walker->GetFirstChildElement(current.element.get(), child.out());
            while (SUCCEEDED(hr) && child) {
                kids.push_back(std::move(child));
                hr = walker->GetNextSiblingElement(kids.back().get(), child.out());
            }
            const std::size_t parent_index = nodes.size() - 1;
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                stack.push_back({std::move(*it), parent_index});
            }
        }
        return nodes;
    }

    /// True when the element matches the semantic hint: projected role
    /// equality (when a role is given) and name equality (when a name is
    /// given). Role comparison goes through the same projection the
    /// snapshot uses, so hint roles are snapshot-vocabulary roles.
    bool node_matches(IUIAutomationElement *element, const std::string &role,
                      const std::string &name) const {
        if (!name.empty()) {
            BSTR raw_name = nullptr;
            const std::string node_name =
                element->get_CurrentName(&raw_name) == S_OK ? bstr_to_utf8(raw_name) : "";
            ::SysFreeString(raw_name);
            if (node_name != name) {
                return false;
            }
        }
        if (role.empty()) {
            return true;
        }
        ControlTypeId type = UIA_CustomControlTypeId;
        return element->get_CurrentControlType(&type) == S_OK && snapshot_role(type) == role;
    }

    /// Live-tree search for the semantic hint (role and/or name), scanning
    /// the desktop's control view from the root in BFS order; bounded by
    /// `budget` node visits (same shape and bound as the AT-SPI backend).
    ComPtr<IUIAutomationElement> find_semantic(IUIAutomationTreeWalker *walker,
                                               const std::string &role, const std::string &name,
                                               std::size_t budget) const {
        ComPtr<IUIAutomationElement> root;
        if (automation->GetRootElement(root.out()) != S_OK || !root) {
            return {};
        }
        std::size_t visited = 0;
        std::vector<ComPtr<IUIAutomationElement>> queue;
        queue.push_back(std::move(root));
        std::size_t head = 0;
        while (head < queue.size() && visited < budget) {
            IUIAutomationElement *current = queue[head].get();
            if (head != 0 && node_matches(current, role, name)) {
                return std::move(queue[head]);
            }
            ++visited;
            ComPtr<IUIAutomationElement> child;
            HRESULT hr = walker->GetFirstChildElement(current, child.out());
            while (SUCCEEDED(hr) && child) {
                queue.push_back(std::move(child));
                hr = walker->GetNextSiblingElement(current, child.out());
            }
            ++head;
        }
        return {};
    }

    /// Structural path walk ("/role/name/role/name...", same frozen syntax
    /// as the AT-SPI backend): the first pair must match a top-level window
    /// child of the desktop root (the UIA analog of the AT-SPI application
    /// root; desktop enumeration order = Z order), deeper pairs match
    /// children. First full match wins. Returns an owning pointer or null.
    ComPtr<IUIAutomationElement> find_structural(IUIAutomationTreeWalker *walker,
                                                 const std::string &path) const {
        const auto segments = split_path(path);
        if (segments.empty() || segments.size() % 2 != 0) {
            return {};
        }
        ComPtr<IUIAutomationElement> root;
        if (automation->GetRootElement(root.out()) != S_OK || !root) {
            return {};
        }
        ComPtr<IUIAutomationElement> child;
        HRESULT hr = walker->GetFirstChildElement(root.get(), child.out());
        while (SUCCEEDED(hr) && child) {
            ComPtr<IUIAutomationElement> cursor = std::move(child);
            bool failed = false;
            for (std::size_t s = 0; s + 1 < segments.size(); s += 2) {
                if (!node_matches(cursor.get(), segments[s], segments[s + 1])) {
                    failed = true;
                    break;
                }
                if (s + 2 >= segments.size()) {
                    break;
                }
                ComPtr<IUIAutomationElement> next;
                ComPtr<IUIAutomationElement> kid;
                HRESULT kid_hr = walker->GetFirstChildElement(cursor.get(), kid.out());
                while (SUCCEEDED(kid_hr) && kid) {
                    if (node_matches(kid.get(), segments[s + 2], segments[s + 3])) {
                        next = std::move(kid);
                        break;
                    }
                    // A separate out-slot: `kid` must stay alive while it
                    // names the sibling source, and argument evaluation
                    // order must not matter.
                    ComPtr<IUIAutomationElement> sibling;
                    kid_hr = walker->GetNextSiblingElement(kid.get(), sibling.out());
                    kid = std::move(sibling);
                }
                if (!next) {
                    failed = true;
                    break;
                }
                cursor = std::move(next);
            }
            if (!failed) {
                return cursor;
            }
            hr = walker->GetNextSiblingElement(root.get(), child.out());
        }
        return {};
    }

    struct Resolved {
        ComPtr<IUIAutomationElement> element;
        bool found = false; // a hint group was set (element may still be null = unresolvable)
    };

    /// Contract resolution order (DEC-005): snapshot reference ->
    /// accessibility semantic -> accessibility structural. Visual, spatial
    /// and raw hints are rejected by the caller before this runs. Callers
    /// distinguish: !found = no hint at all ("invalid_argument"), found
    /// with null element = unresolvable ("not_found") — the reference-miss
    /// path is found=true precisely so it reports not_found, not
    /// invalid_argument.
    Resolved resolve_locked(IUIAutomationTreeWalker *walker, const ElementTarget &target) const {
        if (!target.reference.id.empty()) {
            const auto it = registry.find(target.reference.id);
            if (it == registry.end()) {
                // A reference is a hint: a stale or unknown handle is
                // unresolvable (not_found), not hint-less
                // (invalid_argument).
                return Resolved{ComPtr<IUIAutomationElement>{}, true};
            }
            return Resolved{it->second.clone(), true};
        }
        if (!target.semantic.role.empty() || !target.semantic.name.empty()) {
            return Resolved{find_semantic(walker, target.semantic.role, target.semantic.name,
                                          SemanticSnapshotLimits{}.max_nodes),
                            true};
        }
        if (!target.structural.path.empty()) {
            return Resolved{find_structural(walker, target.structural.path), true};
        }
        return Resolved{}; // no hint at all -> invalid_argument
    }
};

UiaBackend::~UiaBackend() {
    if (impl_ != nullptr) {
        // Element releases land on the anchored MTA (or the registry is
        // empty because no scope ever formed) — valid from any thread.
        impl_->clear_registry();
    }
}

std::unique_ptr<UiaBackend> UiaBackend::open() {
    // Capability probe: COM in the MTA plus the UIA client core, both
    // released before returning (this is not the anchor — the anchor forms
    // on the first provider call, see Impl).
    ComScope com;
    if (!com.ok()) {
        return nullptr;
    }
    ComPtr<IUIAutomation> automation;
    if (::CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                           __uuidof(IUIAutomation),
                           reinterpret_cast<void **>(automation.out())) != S_OK ||
        !automation) {
        return nullptr;
    }
    auto backend = std::unique_ptr<UiaBackend>(new UiaBackend());
    backend->impl_ = std::make_unique<Impl>();
    return backend;
}

SnapshotOutcome UiaBackend::semantic_snapshot(const std::string &window_id,
                                              const SemanticSnapshotLimits &limits,
                                              const CancelToken &cancel) {
    SnapshotOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "snapshot cancelled");
        return outcome;
    }
    const auto handle = parse_window_id(window_id);
    if (!handle.has_value()) {
        outcome.error = error("invalid_argument", "malformed window id");
        return outcome;
    }
    if (!::IsWindow(*handle)) {
        outcome.error = error("not_found", "no such window");
        return outcome;
    }

    // The COM scope: the first successful call is promoted to the
    // process-lifetime MTA anchor (exactly one reference; uia_backend.hpp).
    ComScope com;
    if (!com.ok()) {
        outcome.error = error("io_error", "calling thread is bound to a conflicting COM apartment");
        return outcome;
    }
    const auto walker = impl_->ui_walker_locked(outcome.error);
    if (!walker) {
        return outcome;
    }
    ComPtr<IUIAutomationElement> window_element;
    if (impl_->automation->ElementFromHandle(*handle, window_element.out()) != S_OK ||
        !window_element) {
        // The window exists (IsWindow passed) but exposes no UIA element.
        outcome.error = error("unsupported_window", "window exposes no UI Automation element");
        return outcome;
    }
    std::optional<std::vector<SemanticNode>> nodes =
        impl_->collect_tree(walker.get(), window_element.get(), limits.max_nodes);
    if (!nodes.has_value()) {
        outcome.error = error("snapshot_too_large", "snapshot exceeds the node budget of " +
                                                        std::to_string(limits.max_nodes));
        return outcome;
    }
    // A fresh snapshot replaces the registry wholesale; stale handles from
    // older snapshots stop resolving (not_found).
    impl_->clear_registry();
    outcome.ok = true;
    outcome.snapshot.application = application_name_for(*handle);
    outcome.snapshot.window_title = window_title_utf8(*handle);
    outcome.snapshot.nodes = std::move(*nodes);
    return outcome;
}

ElementActionOutcome UiaBackend::activate_element(const ElementTarget &target,
                                                  const CancelToken &cancel) {
    ElementActionOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "activation cancelled");
        return outcome;
    }
    if (!target.visual.ocr_text.empty() || !target.visual.template_id.empty() ||
        !target.spatial.relative_to.id.empty() || target.raw.x != 0 || target.raw.y != 0) {
        outcome.error = error("unsupported_hint", "hint is not accessibility-resolvable");
        return outcome;
    }

    ComScope com;
    if (!com.ok()) {
        outcome.error = error("io_error", "calling thread is bound to a conflicting COM apartment");
        return outcome;
    }
    const auto walker = impl_->ui_walker_locked(outcome.error);
    if (!walker) {
        return outcome;
    }
    const auto resolved = impl_->resolve_locked(walker.get(), target);
    if (!resolved.found) {
        outcome.error = error("invalid_argument", "target carries no hint");
        return outcome;
    }
    if (resolved.element == nullptr) {
        outcome.error = error("not_found", "target does not resolve to a live element");
        return outcome;
    }
    ComPtr<IUnknown> pattern_unknown;
    if (resolved.element->GetCurrentPattern(UIA_InvokePatternId, pattern_unknown.out()) != S_OK ||
        !pattern_unknown) {
        outcome.error = error("unsupported_element", "element exposes no Invoke pattern");
        return outcome;
    }
    ComPtr<IUIAutomationInvokePattern> invoke;
    if (pattern_unknown->QueryInterface(__uuidof(IUIAutomationInvokePattern),
                                        reinterpret_cast<void **>(invoke.out())) != S_OK ||
        !invoke) {
        outcome.error = error("unsupported_element", "element exposes no Invoke pattern");
        return outcome;
    }
    if (invoke->Invoke() != S_OK) {
        outcome.error = error("io_error", "semantic action failed on UI Automation");
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

ElementActionOutcome UiaBackend::set_text(const ElementTarget &target, const std::string &text,
                                          const InputLimits &limits, const CancelToken &cancel) {
    ElementActionOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "text input cancelled");
        return outcome;
    }
    if (text.size() > limits.max_text_bytes) {
        outcome.error = error("invalid_argument", "text exceeds the length budget");
        return outcome;
    }
    if (!mirage::desktop::is_valid_utf8(text)) {
        outcome.error = error("invalid_argument", "text must be UTF-8");
        return outcome;
    }

    ComScope com;
    if (!com.ok()) {
        outcome.error = error("io_error", "calling thread is bound to a conflicting COM apartment");
        return outcome;
    }
    const auto walker = impl_->ui_walker_locked(outcome.error);
    if (!walker) {
        return outcome;
    }
    const auto resolved = impl_->resolve_locked(walker.get(), target);
    if (!resolved.found) {
        outcome.error = error("invalid_argument", "target carries no hint");
        return outcome;
    }
    if (resolved.element == nullptr) {
        outcome.error = error("not_found", "target does not resolve to a live element");
        return outcome;
    }
    ComPtr<IUnknown> pattern_unknown;
    if (resolved.element->GetCurrentPattern(UIA_ValuePatternId, pattern_unknown.out()) != S_OK ||
        !pattern_unknown) {
        outcome.error = error("unsupported_element", "element exposes no editable text");
        return outcome;
    }
    ComPtr<IUIAutomationValuePattern> value;
    if (pattern_unknown->QueryInterface(__uuidof(IUIAutomationValuePattern),
                                        reinterpret_cast<void **>(value.out())) != S_OK ||
        !value) {
        outcome.error = error("unsupported_element", "element exposes no editable text");
        return outcome;
    }
    BOOL read_only = FALSE;
    if (value->get_CurrentIsReadOnly(&read_only) == S_OK && read_only != 0) {
        outcome.error = error("unsupported_element", "element text is read-only");
        return outcome;
    }
    const auto utf16 = utf8_to_utf16(text);
    if (!utf16.has_value()) {
        // Checked at the entry, kept as a guard for the conversion itself.
        outcome.error = error("invalid_argument", "text must be UTF-8");
        return outcome;
    }
    Bstr wide(*utf16);
    if (!wide) {
        outcome.error = error("io_error", "text allocation for UI Automation failed");
        return outcome;
    }
    if (value->SetValue(wide.get()) != S_OK) {
        outcome.error = error("io_error", "setting text failed on UI Automation");
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

} // namespace mirage::platform::windows_backend
