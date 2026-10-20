#pragma once

// Minimal test harness: TEST(name) registers a case, CHECK/CHECK_EQ record failures.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(const char* v) { return show(std::string(v)); }
inline std::string show(bool v) { return v ? "true" : "false"; }
template <typename T>
std::string show(const T& v) { return std::to_string(v); }

} // namespace check

#define CHECK_CONCAT2(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT2(a, b)

#define TEST(name)                                                                \
    static void name();                                                           \
    static check::Registrar CHECK_CONCAT(name, _registrar)(#name, name);          \
    static void name()

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            ++check::failures();                                                  \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                         \
    } while (0)

#define CHECK_EQ(actual, expected)                                                \
    do {                                                                          \
        auto _a = (actual);                                                       \
        auto _e = (expected);                                                     \
        if (!(_a == _e)) {                                                        \
            ++check::failures();                                                  \
            std::printf("  FAIL %s:%d  %s == %s\n    got %s, want %s\n",          \
                        __FILE__, __LINE__, #actual, #expected,                   \
                        check::show(_a).c_str(), check::show(_e).c_str());        \
        }                                                                         \
    } while (0)
