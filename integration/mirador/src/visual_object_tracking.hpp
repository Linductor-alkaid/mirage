#pragma once
#include <mirador/continuous_perception.hpp>
#include <mirador/frame.hpp>
#include <mirador/object_tracker.hpp>
#include <mirage/integration/desktop_perception.hpp>

namespace mirage::integration::detail {
// One serial analysis or capture worker owns each instance. No retained frames.
class VisualObjectTracking {
    struct Binding {
        std::uint64_t token, entity, instance, lost_sequence = 0;
    };
    std::optional<mirador::ObjectTracker> pool_;
    std::vector<Binding> bindings_;
    std::uint64_t next_ = 0;
    PerceptionTrack project(const Binding &) const;
    mirador::Result<void> ensure_pool();
    void geometry(const mirador::Frame &, const mirador::ChangeReport &, PerceptionTracking &,
                  const mirador::ExecutionContext &);

  public:
    void reset();
    PerceptionTracking seed(const mirador::Frame &, const mirador::SemanticSnapshot &,
                            const mirador::TemporalSemanticState &, const mirador::EventDrain &,
                            const mirador::ExecutionContext &);
    // Adapter reconstruction from actual confirmed recognition, for templates
    // only; never submitted to a coordinator as fusion/coverage evidence.
    PerceptionTracking seed_observed(const mirador::Frame &, const PerceptionSample &,
                                     const mirador::ExecutionContext &);
    mirador::Result<PerceptionTracking> sample(const mirador::Frame &,
                                               const mirador::ChangeReport &,
                                               const mirador::ExecutionContext &);
};
} // namespace mirage::integration::detail
