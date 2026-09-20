#include <mirage/integration/visual_execution_context.hpp>

namespace mirage::integration {

mirador::ExecutionContext
to_execution_context(const desktop::CancelToken &cancel,
                     std::optional<std::chrono::steady_clock::time_point> deadline) {
    mirador::ExecutionContext context;
    context.is_cancelled = [cancel] { return cancel.cancelled(); };
    context.deadline = deadline;
    return context;
}

} // namespace mirage::integration
