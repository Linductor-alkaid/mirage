// MIRA-20260922-001: compile-only reduction of the pinned Executor expression.
// This type reference does not create a thread or provide a runtime workaround.
#include <thread>
#include <windows.h>

void reproduce_native_handle_cast() {
    auto handle = static_cast<std::thread::native_handle_type>(GetCurrentThread());
    (void)handle;
}
