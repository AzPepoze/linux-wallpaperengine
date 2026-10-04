#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <cstdio>

namespace test {

inline int checks = 0;
inline int failures = 0;

inline void check(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

inline void expect(const char* test, bool condition, const char* what) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL %s: %s\n", test, what);
        ++failures;
    }
}

inline int finish(const char* suite) {
    std::printf("%s: %d checks, %d failures\n", suite, checks, failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace test

#define CHECK(cond) ::test::expect(__FILE__ ":" #cond, (cond), #cond)

#endif  // TEST_UTIL_H
