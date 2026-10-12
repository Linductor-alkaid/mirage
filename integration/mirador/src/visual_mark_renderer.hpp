#pragma once
#include <mirage/integration/visual_session_host.hpp>

namespace mirage::integration::detail {
// DEC-058: called only on the existing per-source blocking worker.
mirador::Result<void> render_marks(const VisualAnalysisRequest &, VisualAnalysisResult &,
                                   const mirador::ExecutionContext &);
} // namespace mirage::integration::detail
