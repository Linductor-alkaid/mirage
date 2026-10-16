#pragma once

// Minimal test harness in the style of the pinned mira test suite: plain
// main() per test binary, MIRAGE_CHECK records failures, finish() prints a
// summary and returns the process exit code. No external test framework; if
// the suite outgrows this, adopt one deliberately via a decision record.

#include <cstdio>
#include <string_view>

namespace mirage::testing {

inline int &failure_count() {
    static int count = 0;
    return count;
}

inline int &check_count() {
    static int count = 0;
    return count;
}

/// Prints the summary and returns the process exit code.
inline int finish(std::string_view test_name) {
    std::fprintf(stderr, "[%s] %d checks, %d failures\n", test_name.data(), check_count(),
                 failure_count());
    return failure_count() == 0 ? 0 : 1;
}

} // namespace mirage::testing

#define MIRAGE_CHECK(expr)                                                                         \
    do {                                                                                           \
        ++::mirage::testing::check_count();                                                        \
        if (!(expr)) {                                                                             \
            ++::mirage::testing::failure_count();                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr);        \
        }                                                                                          \
    } while (false)
