// Minimal test helper: CHECK(cond, "message") prints PASS/FAIL, REPORT() returns the exit code.
#pragma once

#include <cstdio>

namespace testing {
inline int& Failures() { static int f = 0; return f; }
}  // namespace testing

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) std::printf("PASS  %s\n", msg);                     \
        else { std::printf("FAIL  %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++testing::Failures(); } \
    } while (0)

inline int Report() {
    std::printf("\n%s (%d failure(s))\n", testing::Failures() ? "FAILED" : "ALL PASSED", testing::Failures());
    return testing::Failures() ? 1 : 0;
}
