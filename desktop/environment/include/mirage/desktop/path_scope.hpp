#pragma once

#include <filesystem>
#include <utility>
#include <vector>

namespace mirage::desktop {

/// Configured set of filesystem roots a FilesystemProvider may access
/// (design doc section 5, M1-05 path scope constraint; section 15 resource
/// scopes). The provider-level scope is a hard containment boundary that
/// holds without any permission decision; the Desktop Permission framework
/// (M1-06) layers capability decisions on top of it.
///
/// An empty scope denies every path (fail closed): a backend without a
/// declared scope exposes no filesystem surface. Containment is decided on
/// canonical paths, so a symlink inside a root that resolves outside the
/// scope is rejected; roots are canonicalized once at construction.
class PathScope {
public:
    PathScope() = default;

    /// Builds a scope from the declared roots. Non-absolute roots never
    /// match an absolute request path (component comparison starts at the
    /// filesystem root), so callers passing relative paths get a deny-all
    /// scope instead of a surprise. Unresolvable roots are kept verbatim
    /// and still work for paths beneath them that come into existence.
    explicit PathScope(std::vector<std::filesystem::path> roots)
        : roots_(std::move(roots)) {
        std::erase_if(roots_, [](const std::filesystem::path& root) {
            return root.empty();
        });
        for (auto& root : roots_) {
            std::error_code ec;
            auto canonical = std::filesystem::weakly_canonical(root, ec);
            if (!ec) {
                root = std::move(canonical);
            }
        }
    }

    /// True when `path` lies at or beneath one of the roots after resolving
    /// the existing part of the path (symlink-aware). Component-wise, so a
    /// root "/a/b" does not match "/a/bc" and a root of "/" contains all
    /// absolute paths.
    [[nodiscard]] bool contains(const std::filesystem::path& path) const {
        if (path.empty()) {
            return false;
        }
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (ec) {
            return false;
        }
        for (const auto& root : roots_) {
            auto root_component = root.begin();
            const auto root_end = root.end();
            auto path_component = canonical.begin();
            for (; root_component != root_end;
                 ++root_component, ++path_component) {
                if (path_component == canonical.end() ||
                    *path_component != *root_component) {
                    break;
                }
            }
            if (root_component == root_end) {
                return true; // every root component matched as a prefix
            }
        }
        return false;
    }

    /// The canonicalized roots; exposed for diagnostics and tests.
    [[nodiscard]] const std::vector<std::filesystem::path>& roots() const {
        return roots_;
    }

  private:
    std::vector<std::filesystem::path> roots_;
};

} // namespace mirage::desktop
