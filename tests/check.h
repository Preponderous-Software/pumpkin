// A minimal test harness: CHECK records a failure and keeps going, and main()
// returns the failure count, so ctest reports every broken expectation at once.
#pragma once

#include <cstdio>

namespace pumpkin_test {
inline int failures = 0;
inline int checks = 0;
}  // namespace pumpkin_test

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++pumpkin_test::checks;                                                    \
        if (!(cond)) {                                                              \
            ++pumpkin_test::failures;                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while (0)

#define TEST_MAIN_END()                                                                       \
    std::printf("%d checks, %d failed\n", pumpkin_test::checks, pumpkin_test::failures);  \
    return pumpkin_test::failures == 0 ? 0 : 1
