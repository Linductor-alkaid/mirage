// M2-05 application backend test helper: a tiny stand-in application
// process. The test copies this binary under a globally unique name per
// fixture application and points the desktop entries' Exec lines at the
// copies, so the backend's /proc name matching (exe basename, argv[0]
// basename and comm all equal the copy's basename) sees exactly the
// processes this test created — never a system process that happens to
// share a name.
//
// Usage: <copy> hold|ignore-term|exit [seconds] [ready-file]
//
// With a ready-file path, the file is written AFTER the mode's signal
// disposition is installed, so a waiter that sees the file knows the
// process reached its main loop (a terminate request before that point
// would land on the exec chain with the default disposition and kill the
// instance early).

#include <signal.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
    const std::string mode = argc >= 2 ? argv[1] : "hold";
    if (mode == "exit") {
        return 0;
    }
    if (mode == "ignore-term") {
        // The stuck variant: a terminate request must be ignored so the
        // backend's cooperative wait runs into its deadline.
        ::signal(SIGTERM, SIG_IGN);
    }
    if (argc >= 4) {
        // Readiness marker, written only after the disposition above.
        if (std::FILE *ready = std::fopen(argv[3], "w")) {
            std::fclose(ready);
        }
    }
    const int seconds = argc >= 3 ? std::atoi(argv[2]) : 3600;
    if (seconds <= 0) {
        return 0;
    }
    // Sleep in short slices up to the safety-net bound, so stray copies can
    // never outlive a crashed test by more than the given budget (the test
    // reaps its own children well before that).
    const timespec slice{0, 100 * 1000 * 1000};
    for (int i = 0; i < seconds * 10; ++i) {
        nanosleep(&slice, nullptr);
    }
    return 0;
}
