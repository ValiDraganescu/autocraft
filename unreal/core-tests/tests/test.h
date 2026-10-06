// A tiny test framework for core-tests: TEST(name) { ... } registers a test;
// EXPECT_EQ, EXPECT_NEAR, EXPECT_TRUE record a failure and go on;
// ASSERT_TRUE records one and leaves the test. main.cpp runs them all,
// prints a summary and exits non-zero on any failure.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*run)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline bool add(const char* name, void (*run)()) {
    registry().push_back({name, run});
    return true;
}

struct Counts {
    int64_t expectations = 0;
    int64_t failures = 0;
    int64_t failuresInTest = 0;
};

inline Counts& counts() {
    static Counts c;
    return c;
}

/// Something printable for a failure message.
template <class T> std::string show(const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<int64_t>(v));
    } else if constexpr (std::is_floating_point_v<T>) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.17g", static_cast<double>(v));
        return buf;
    } else if constexpr (std::is_integral_v<T>) {
        return std::to_string(v);
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        return "\"" + std::string(std::string_view(v)) + "\"";
    } else {
        return "(value)";
    }
}

inline void fail(const char* file, int line, const std::string& what) {
    counts().failures += 1;
    counts().failuresInTest += 1;
    std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
}

/// Doubles compared exactly: equal, or both NaN.
template <class A, class B> bool same(const A& a, const B& b) {
    if constexpr (std::is_floating_point_v<A> && std::is_floating_point_v<B>) {
        if (std::isnan(a) && std::isnan(b)) return true;
    }
    return a == b;
}

} // namespace test

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)

#define TEST(name)                                                                  \
    static void TEST_CONCAT(test_, name)();                                         \
    static const bool TEST_CONCAT(registered_, name) =                              \
        test::add(#name, &TEST_CONCAT(test_, name));                                \
    static void TEST_CONCAT(test_, name)()

/// Variadic, so a condition may hold braces with commas.
#define EXPECT_TRUE(...)                                                            \
    do {                                                                            \
        test::counts().expectations += 1;                                           \
        if (!(__VA_ARGS__)) test::fail(__FILE__, __LINE__, "expected " #__VA_ARGS__); \
    } while (0)

#define ASSERT_TRUE(...)                                                            \
    do {                                                                            \
        test::counts().expectations += 1;                                           \
        if (!(__VA_ARGS__)) { test::fail(__FILE__, __LINE__, "required " #__VA_ARGS__); return; } \
    } while (0)

/// Equal; doubles exactly (NaN equals NaN).
#define EXPECT_EQ(a, b)                                                             \
    do {                                                                            \
        test::counts().expectations += 1;                                           \
        const auto ac_test_a = (a);                                                \
        const auto ac_test_b = (b);                                                \
        if (!test::same(ac_test_a, ac_test_b))                                      \
            test::fail(__FILE__, __LINE__, std::string(#a " == " #b ": ") +         \
                       test::show(ac_test_a) + " vs " + test::show(ac_test_b));     \
    } while (0)

#define EXPECT_NEAR(a, b, tolerance)                                                \
    do {                                                                            \
        test::counts().expectations += 1;                                           \
        const double ac_test_a = (a), ac_test_b = (b);                              \
        if (!(std::fabs(ac_test_a - ac_test_b) <= (tolerance)))                     \
            test::fail(__FILE__, __LINE__, std::string(#a " ~ " #b ": ") +          \
                       test::show(ac_test_a) + " vs " + test::show(ac_test_b));     \
    } while (0)
