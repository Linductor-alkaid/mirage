#include <mirage/desktop/visual_snapshot.hpp>

#include <mutex>
#include <sstream>

#include <mirage/desktop/visual_reference_registry.hpp>

namespace mirage::desktop {
namespace {

/// Renders one entry in the design doc section 8 line forms: OCR text ->
/// `@vN text "..."`, template -> `@vN icon cache:<id>`, detector/geometry ->
/// `@vN geometry <label>`.
std::string render_region(const VisualRegionEntry &region) {
    std::ostringstream line;
    line << region.ref << ' ';
    switch (region.source) {
    case VisualRegionSource::kOcr:
        line << "text \"" << region.text << '"';
        break;
    case VisualRegionSource::kTemplate:
        line << "icon cache:" << region.template_id;
        break;
    case VisualRegionSource::kDetector:
    case VisualRegionSource::kGeometry:
        line << "geometry";
        if (!region.text.empty()) {
            line << ' ' << region.text;
        }
        break;
    }
    return line.str();
}

} // namespace

std::string render_visual_snapshot(const VisualSnapshot &snapshot) {
    std::ostringstream text;
    for (const VisualRegionEntry &region : snapshot.regions) {
        text << render_region(region) << '\n';
    }
    return text.str();
}

VisualPublishOutcome VisualReferenceRegistry::publish(const VisualSnapshot &snapshot,
                                                      const VisualSnapshotLimits &limits) {
    if (snapshot.regions.size() > limits.max_regions) {
        // Fail closed on the whole publication (DEC-016 decision 1): the
        // active set stays intact, nothing is truncated.
        return VisualPublishOutcome{
            false,
            {},
            ProviderError{"snapshot_too_large",
                          "visual snapshot has " + std::to_string(snapshot.regions.size()) +
                              " regions, budget is " + std::to_string(limits.max_regions)}};
    }

    VisualSnapshot published = snapshot;
    for (std::size_t index = 0; index < published.regions.size(); ++index) {
        published.regions[index].ref = "@v" + std::to_string(index + 1);
    }

    const std::lock_guard<std::mutex> guard(mutex_);
    ++generation_;
    published.scope_ref = "@vs" + std::to_string(generation_);
    active_ = std::move(published);
    return VisualPublishOutcome{true, active_, {}};
}

std::optional<VisualRegionEntry> VisualReferenceRegistry::resolve(const std::string &ref) const {
    const std::lock_guard<std::mutex> guard(mutex_);
    for (const VisualRegionEntry &region : active_.regions) {
        if (region.ref == ref) {
            return region;
        }
    }
    return std::nullopt;
}

VisualSnapshot VisualReferenceRegistry::current() const {
    const std::lock_guard<std::mutex> guard(mutex_);
    return active_;
}

ProviderError VisualReferenceRegistry::with_reference(
    const std::string &scope, const std::string &ref,
    const std::function<void(const VisualRegionEntry &)> &action) const {
    const std::lock_guard guard(mutex_);
    if (scope.empty() || scope != active_.scope_ref)
        return {"stale_observation", "visual snapshot scope changed"};
    for (const auto &region : active_.regions) {
        if (region.ref == ref) {
            if (!action)
                return {"invalid_argument", "scoped action is missing"};
            action(region);
            return {};
        }
    }
    return {"not_found", "visual reference is not in this snapshot"};
}

std::size_t VisualReferenceRegistry::size() const {
    const std::lock_guard<std::mutex> guard(mutex_);
    return active_.regions.size();
}

ProviderError VisualReferenceRegistry::with_references(
    const std::string &scope, const std::string &from, const std::string &to,
    const std::function<void(const VisualRegionEntry &, const VisualRegionEntry &)> &action) const {
    const std::lock_guard guard(mutex_);
    if (scope.empty() || scope != active_.scope_ref)
        return {"stale_observation", "visual snapshot scope changed"};
    const VisualRegionEntry *first = nullptr, *second = nullptr;
    for (const auto &region : active_.regions) {
        if (region.ref == from)
            first = &region;
        if (region.ref == to)
            second = &region;
    }
    if (!first || !second)
        return {"not_found", "visual endpoint is not in this snapshot"};
    if (!action)
        return {"invalid_argument", "scoped action is missing"};
    action(*first, *second);
    return {};
}

void VisualReferenceRegistry::clear() {
    const std::lock_guard<std::mutex> guard(mutex_);
    active_ = VisualSnapshot{};
}

} // namespace mirage::desktop
