#include "atspi_backend.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <atspi/atspi.h>

namespace mirage::platform::linux_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::ElementActionOutcome;
using mirage::desktop::ElementTarget;
using mirage::desktop::InputLimits;
using mirage::desktop::ProviderError;
using mirage::desktop::SemanticNode;
using mirage::desktop::SemanticSnapshot;
using mirage::desktop::SemanticSnapshotLimits;
using mirage::desktop::SnapshotOutcome;
using mirage::desktop::WindowGeometry;
using mirage::desktop::WindowInfo;
using mirage::desktop::WindowListLimits;
using mirage::desktop::WindowProvider;

ProviderError error(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

/// Normalizes a platform role into the stable snapshot vocabulary
/// (DEC-005): explicit overrides for the roles the contract names, all
/// others fall back to the lowercase hyphenated platform role name.
std::string snapshot_role(AtspiRole role) {
    switch (role) {
    case ATSPI_ROLE_PUSH_BUTTON:
    case ATSPI_ROLE_TOGGLE_BUTTON:
        return "button";
    case ATSPI_ROLE_CHECK_BOX:
        return "checkbox";
    case ATSPI_ROLE_RADIO_BUTTON:
        return "radio";
    case ATSPI_ROLE_MENU:
        return "menu";
    case ATSPI_ROLE_MENU_ITEM:
        return "menuitem";
    case ATSPI_ROLE_CHECK_MENU_ITEM:
        return "check-menuitem";
    case ATSPI_ROLE_RADIO_MENU_ITEM:
        return "radio-menuitem";
    case ATSPI_ROLE_MENU_BAR:
        return "menubar";
    case ATSPI_ROLE_TREE_ITEM:
        return "treeitem";
    case ATSPI_ROLE_LIST_ITEM:
        return "listitem";
    case ATSPI_ROLE_PAGE_TAB:
        return "pagetab";
    case ATSPI_ROLE_COMBO_BOX:
        return "combobox";
    case ATSPI_ROLE_SCROLL_BAR:
        return "scrollbar";
    case ATSPI_ROLE_SCROLL_PANE:
        return "scrollpane";
    case ATSPI_ROLE_SPIN_BUTTON:
        return "spinbutton";
    case ATSPI_ROLE_TABLE_CELL:
        return "cell";
    case ATSPI_ROLE_TOOL_BAR:
        return "toolbar";
    case ATSPI_ROLE_STATUS_BAR:
        return "statusbar";
    case ATSPI_ROLE_PROGRESS_BAR:
        return "progressbar";
    case ATSPI_ROLE_DOCUMENT_FRAME:
    case ATSPI_ROLE_DOCUMENT_TEXT:
    case ATSPI_ROLE_DOCUMENT_WEB:
    case ATSPI_ROLE_DOCUMENT_EMAIL:
    case ATSPI_ROLE_DOCUMENT_SPREADSHEET:
    case ATSPI_ROLE_DOCUMENT_PRESENTATION:
        return "document";
    case ATSPI_ROLE_TEXT:
    case ATSPI_ROLE_ENTRY:
    case ATSPI_ROLE_PASSWORD_TEXT:
        return "text";
    case ATSPI_ROLE_WINDOW:
        return "window";
    case ATSPI_ROLE_FRAME:
        return "frame";
    case ATSPI_ROLE_DIALOG:
        return "dialog";
    case ATSPI_ROLE_LABEL:
        return "label";
    case ATSPI_ROLE_PANEL:
        return "panel";
    default: {
        gchar *raw = atspi_role_get_name(role);
        std::string name = raw != nullptr ? raw : "unknown";
        g_free(raw);
        for (char &ch : name) {
            if (ch == ' ') {
                ch = '-';
            } else {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
        }
        return name;
    }
    }
}

/// Ownership note (M2-03): libatspi keeps a weak global table of accessible
/// wrappers; releasing a wrapper can leave that table dangling. The backend
/// therefore adopts every wrapper it touches into a process-lifetime pool
/// instead of unref-ing, with a capacity cap as the RULE-07 bound.
inline constexpr std::size_t kObjectPoolCap = 8192;

/// Non-owning view used while walking.
struct WalkNode {
    AtspiAccessible *object;
    std::size_t parent; // index into the collected vector, kNoParent at roots
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

bool state_set_has(AtspiAccessible *object, AtspiStateType state) {
    AtspiStateSet *states = atspi_accessible_get_state_set(object);
    if (states == nullptr) {
        return false;
    }
    const gboolean has = atspi_state_set_contains(states, state);
    g_object_unref(states);
    return has != 0;
}

} // namespace

struct AtspiBackend::Impl {
    WindowProvider *windows = nullptr;
    std::vector<AtspiAccessible *> pool; // process-lifetime wrapper ownership

    void adopt(AtspiAccessible *object) {
        if (object == nullptr) {
            return;
        }
        if (pool.size() >= kObjectPoolCap) {
            // Cap reached: keep the newest entries, release the oldest. The
            // release is safe only for entries libatspi no longer serves
            // from its cache; the cap makes that overwhelmingly likely.
            g_object_unref(pool.front());
            pool.erase(pool.begin());
        }
        pool.push_back(object);
    }

    void adopt_all(std::vector<AtspiAccessible *> &objects) {
        for (AtspiAccessible *object : objects) {
            adopt(object);
        }
        objects.clear();
    }
    /// Snapshot-issued handles from the latest snapshot -> owning accessible
    /// references. Replaced wholesale per snapshot: older handles stop
    /// resolving ("not_found"), which is the behavior-result-driven refresh
    /// policy v1 (M2 plan, DEC-005).
    std::map<std::string, AtspiAccessible *> registry;

    void clear_registry() {
        for (auto &[ref, object] : registry) {
            adopt(object);
        }
        registry.clear();
    }

    /// Application name for one desktop child.
    std::string application_name(AtspiAccessible *application) const {
        gchar *name = atspi_accessible_get_name(application, nullptr);
        std::string result = name != nullptr ? name : "";
        g_free(name);
        return result;
    }

    /// Direct children of one accessible (owning references).
    std::vector<AtspiAccessible *> children(AtspiAccessible *object) const {
        std::vector<AtspiAccessible *> result;
        const gint n = atspi_accessible_get_child_count(object, nullptr);
        for (gint i = 0; i < n; ++i) {
            AtspiAccessible *child = atspi_accessible_get_child_at_index(object, i, nullptr);
            if (child != nullptr) {
                result.push_back(child);
            }
        }
        return result;
    }

    static bool node_matches(AtspiAccessible *object, const std::string &role,
                             const std::string &name) {
        gchar *raw_name = atspi_accessible_get_name(object, nullptr);
        const std::string node_name = raw_name != nullptr ? raw_name : "";
        g_free(raw_name);
        if (!name.empty() && node_name != name) {
            return false;
        }
        return role.empty() || snapshot_role(atspi_accessible_get_role(object, nullptr)) == role;
    }

    /// Walks the whole subtree of `root` in DFS order. Stops and returns
    /// nullopt as soon as collecting another node would exceed `budget`
    /// (refusal, never truncation). `nodes` collects owning references.
    std::optional<std::vector<SemanticNode>> collect_tree(AtspiAccessible *root, std::size_t budget,
                                                          std::vector<AtspiAccessible *> &objects) {
        std::vector<SemanticNode> nodes;
        std::vector<WalkNode> stack{{root, mirage::desktop::kNoParent}};
        while (!stack.empty()) {
            const WalkNode current = stack.back();
            stack.pop_back();
            if (nodes.size() >= budget) {
                adopt_all(objects);
                // The stack still holds owning references from children().
                for (const WalkNode &pending : stack) {
                    adopt(pending.object);
                }
                return std::nullopt;
            }
            SemanticNode node;
            gchar *name = atspi_accessible_get_name(current.object, nullptr);
            node.name = name != nullptr ? name : "";
            g_free(name);
            gchar *description = atspi_accessible_get_description(current.object, nullptr);
            node.description = description != nullptr ? description : "";
            g_free(description);
            node.role = snapshot_role(atspi_accessible_get_role(current.object, nullptr));
            node.parent = current.parent;
            node.focused = state_set_has(current.object, ATSPI_STATE_FOCUSED);
            node.enabled = state_set_has(current.object, ATSPI_STATE_ENABLED);
            AtspiComponent *component = atspi_accessible_get_component(current.object);
            if (component != nullptr) {
                AtspiRect *extents =
                    atspi_component_get_extents(component, ATSPI_COORD_TYPE_SCREEN, nullptr);
                if (extents != nullptr) {
                    node.geometry = WindowGeometry{static_cast<std::int32_t>(extents->x),
                                                   static_cast<std::int32_t>(extents->y),
                                                   static_cast<std::int32_t>(extents->width),
                                                   static_cast<std::int32_t>(extents->height)};
                    g_free(extents);
                }
                g_object_unref(component);
            }
            node.ref = "@e" + std::to_string(nodes.size() + 1);
            nodes.push_back(std::move(node));
            objects.push_back(current.object); // registry keeps it alive
            // Children keep the DFS order; push reversed so the first child
            // is visited first (stable, deterministic snapshots).
            auto kids = children(current.object);
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                stack.push_back({*it, nodes.size() - 1});
            }
        }
        return nodes;
    }

    /// Finds the application + window accessible whose accessible window
    /// node carries `title`. Matches by accessible name of a window/frame
    /// child of each application (the honest v1 window mapping, M2-03).
    std::optional<std::pair<std::string, AtspiAccessible *>>
    find_window_by_title(AtspiAccessible *desktop, const std::string &title) {
        for (auto *application : children(desktop)) {
            for (auto *window : children(application)) {
                gchar *name = atspi_accessible_get_name(window, nullptr);
                const std::string window_name = name != nullptr ? name : "";
                g_free(name);
                const AtspiRole role = atspi_accessible_get_role(window, nullptr);
                const bool window_like = role == ATSPI_ROLE_WINDOW || role == ATSPI_ROLE_FRAME ||
                                         role == ATSPI_ROLE_DIALOG;
                if (window_like && window_name == title) {
                    auto found = std::make_pair(application_name(application), window);
                    adopt(application);
                    return found;
                }
                adopt(window);
            }
            adopt(application);
        }
        return std::nullopt;
    }

    /// Live-tree search for the semantic hint (role and/or name), scanning
    /// the desktop in enumeration order; bounded by `budget` node visits.
    AtspiAccessible *find_semantic(const std::string &role, const std::string &name,
                                   std::size_t budget) {
        // atspi_get_desktop returns libatspi's cached singleton without
        // transferring a reference: treat it as borrowed everywhere.
        AtspiAccessible *desktop = atspi_get_desktop(0);
        if (desktop == nullptr) {
            return nullptr;
        }
        std::size_t visited = 0;
        std::vector<AtspiAccessible *> queue{desktop};
        std::size_t head = 0;
        AtspiAccessible *match = nullptr;
        while (head < queue.size() && visited < budget && match == nullptr) {
            AtspiAccessible *current = queue[head++];
            if (current != desktop && node_matches(current, role, name)) {
                match = current; // already pool-owned through the queue
            } else {
                for (auto *child : children(current)) {
                    queue.push_back(child); // owning references
                }
            }
            ++visited;
        }
        // Every queued entry except the desktop itself is an owning
        // reference, visited or not; the desktop singleton stays owned by
        // libatspi.
        for (std::size_t i = 1; i < queue.size(); ++i) {
            adopt(queue[i]);
        }
        if (match != nullptr) {
            adopt(match); // the pool keeps it alive; hand back a raw view
        }
        return match;
    }

    /// Structural path walk ("/role/name/role/name...") from each
    /// application root, first resolution in desktop order wins. Returns an
    /// owning reference or null.
    AtspiAccessible *find_structural(const std::string &path) {
        const auto segments = split_path(path);
        if (segments.empty() || segments.size() % 2 != 0) {
            return nullptr;
        }
        AtspiAccessible *desktop = atspi_get_desktop(0); // borrowed singleton
        if (desktop == nullptr) {
            return nullptr;
        }
        AtspiAccessible *match = nullptr;
        for (auto *application : children(desktop)) { // owning
            AtspiAccessible *cursor = application;    // alias or owning deeper node
            bool failed = false;
            for (std::size_t s = 0; s + 1 < segments.size(); s += 2) {
                if (!node_matches(cursor, segments[s], segments[s + 1])) {
                    failed = true;
                    break;
                }
                if (s + 2 >= segments.size()) {
                    break;
                }
                AtspiAccessible *next = nullptr;
                for (auto *child : children(cursor)) { // owning
                    if (node_matches(child, segments[s + 2], segments[s + 3])) {
                        next = child;
                        break;
                    }
                    adopt(child);
                }
                if (next == nullptr) {
                    failed = true;
                    break;
                }
                if (cursor != application) {
                    adopt(cursor);
                }
                cursor = next;
            }
            if (!failed) {
                match = cursor; // pool-owned through adoption below
            } else if (cursor != application) {
                adopt(cursor);
            }
            adopt(application);
            if (match != nullptr) {
                break;
            }
        }
        return match;
    }
    struct Resolved {
        AtspiAccessible *object = nullptr;
        bool owning = false; // registry entries are borrowed; live finds own
    };

    /// Contract resolution order (DEC-005): snapshot reference ->
    /// accessibility semantic -> accessibility structural. Visual, spatial
    /// and raw hints are rejected by the callers before this runs.
    std::optional<Resolved> resolve_locked(const ElementTarget &target) {
        if (!target.reference.id.empty()) {
            const auto it = registry.find(target.reference.id);
            if (it == registry.end()) {
                return Resolved{}; // stale or unknown handle
            }
            return Resolved{it->second, false};
        }
        if (!target.semantic.role.empty() || !target.semantic.name.empty()) {
            AtspiAccessible *found =
                find_semantic(target.semantic.role, target.semantic.name,
                              mirage::desktop::SemanticSnapshotLimits{}.max_nodes);
            if (found == nullptr) {
                return Resolved{};
            }
            return Resolved{found, true};
        }
        if (!target.structural.path.empty()) {
            AtspiAccessible *found = find_structural(target.structural.path);
            if (found == nullptr) {
                return Resolved{};
            }
            return Resolved{found, true};
        }
        return std::nullopt; // no hint at all
    }
};

AtspiBackend::~AtspiBackend() {
    if (impl_ != nullptr) {
        impl_->clear_registry();
    }
}

std::unique_ptr<AtspiBackend> AtspiBackend::open(WindowProvider *windows) {
    const int init_status = atspi_init();
    if (init_status < 0) {
        return nullptr; // no accessibility bus: fail closed (DEC-015)
    }
    auto backend = std::unique_ptr<AtspiBackend>(new AtspiBackend());
    backend->impl_ = std::make_unique<Impl>();
    backend->impl_->windows = windows;
    return backend;
}

SnapshotOutcome AtspiBackend::semantic_snapshot(const std::string &window_id,
                                                const SemanticSnapshotLimits &limits,
                                                const CancelToken &cancel) {
    SnapshotOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "snapshot cancelled");
        return outcome;
    }
    if (window_id.empty()) {
        outcome.error = error("invalid_argument", "window id must not be empty");
        return outcome;
    }
    // Map the opaque window id to a title via the environment's window
    // surface (honest v1 mapping: accessibility window name == X title).
    std::string title;
    if (impl_->windows != nullptr) {
        WindowListLimits unlimited;
        unlimited.max_windows = 4096;
        const auto listed = impl_->windows->list_windows(unlimited, cancel);
        for (const auto &window : listed.windows) {
            if (window.id == window_id) {
                title = window.title;
                break;
            }
        }
    }
    if (title.empty()) {
        outcome.error = error("not_found", "no such window");
        return outcome;
    }
    AtspiAccessible *desktop = atspi_get_desktop(0); // borrowed singleton
    if (desktop == nullptr) {
        outcome.error = error("io_error", "accessibility desktop unavailable");
        return outcome;
    }
    const auto found = impl_->find_window_by_title(desktop, title);
    if (!found.has_value()) {
        outcome.error = error("unsupported_window", "no accessible window carries this title");
        return outcome;
    }
    auto [application_name, window_object] = *found;
    std::vector<AtspiAccessible *> objects;
    auto nodes = impl_->collect_tree(window_object, limits.max_nodes, objects);
    if (!nodes.has_value()) {
        outcome.error = error("snapshot_too_large", "snapshot exceeds the node budget of " +
                                                        std::to_string(limits.max_nodes));
        return outcome;
    }
    // A fresh snapshot replaces the registry wholesale. `objects` is
    // index-aligned with the collected nodes, so each ref maps to the
    // accessible it was issued for.
    impl_->clear_registry();
    const auto collected = *nodes;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        impl_->registry[collected[i].ref] = objects[i];
    }
    outcome.ok = true;
    outcome.snapshot.application = std::move(application_name);
    outcome.snapshot.window_title = title;
    outcome.snapshot.nodes = std::move(*nodes);
    return outcome;
}

ElementActionOutcome AtspiBackend::activate_element(const ElementTarget &target,
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
    const auto resolved = impl_->resolve_locked(target);
    if (!resolved.has_value()) {
        outcome.error = error("invalid_argument", "target carries no hint");
        return outcome;
    }
    if (resolved->object == nullptr) {
        outcome.error = error("not_found", "target does not resolve to a live element");
        return outcome;
    }
    AtspiAction *action = atspi_accessible_get_action(resolved->object);
    if (action == nullptr) {
        outcome.error = error("unsupported_element", "element exposes no action interface");
        return outcome;
    }
    const gboolean done = atspi_action_do_action(action, 0, nullptr);
    g_object_unref(action);
    if (done == 0) {
        outcome.error = error("io_error", "semantic action failed on the accessibility bus");
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

ElementActionOutcome AtspiBackend::set_text(const ElementTarget &target, const std::string &text,
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
    const auto resolved = impl_->resolve_locked(target);
    if (!resolved.has_value()) {
        outcome.error = error("invalid_argument", "target carries no hint");
        return outcome;
    }
    if (resolved->object == nullptr) {
        outcome.error = error("not_found", "target does not resolve to a live element");
        return outcome;
    }
    AtspiEditableText *editable = atspi_accessible_get_editable_text(resolved->object);
    if (editable == nullptr) {
        outcome.error = error("unsupported_element", "element exposes no editable text");
        return outcome;
    }
    const gboolean done = atspi_editable_text_set_text_contents(editable, text.c_str(), nullptr);
    g_object_unref(editable);
    if (done == 0) {
        outcome.error = error("io_error", "setting text failed on the accessibility bus");
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

} // namespace mirage::platform::linux_backend
