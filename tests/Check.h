// Minimal self-contained test harness (no framework needed yet).
#pragma once

#include <gp_Pnt2d.hxx>

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

inline int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

inline bool near(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}

inline bool near(const gp_Pnt2d& a, const gp_Pnt2d& b, double eps = 1e-9)
{
    return a.Distance(b) <= eps;
}

inline int runTests(const std::vector<std::pair<std::string, std::function<void()>>>& tests)
{
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", name.c_str());
    }
    std::printf("%s\n", g_failures ? "FAILED" : "all passed");
    return g_failures ? 1 : 0;
}
