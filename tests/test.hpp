// A tiny test harness: TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace t {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Register {
    Register(const char* name, std::function<void()> fn) { cases().push_back({name, std::move(fn)}); }
};

template <class T>
std::string show(const T& v) {
    if constexpr (requires(std::ostream& o) { o << v; }) {
        std::ostringstream s;
        s << v;
        return s.str();
    } else {
        return "?";
    }
}

}  // namespace t

#define T_CAT2(a, b) a##b
#define T_CAT(a, b) T_CAT2(a, b)
#define TEST(name)                                                   \
    static void T_CAT(test_, name)();                                \
    static t::Register T_CAT(reg_, name)(#name, T_CAT(test_, name)); \
    static void T_CAT(test_, name)()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            t::failures()++;                                                     \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        auto _va = (a);                                                                             \
        auto _vb = (b);                                                                             \
        if (!(_va == _vb)) {                                                                        \
            std::printf("    FAIL %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, #a, #b,         \
                        t::show(_va).c_str(), t::show(_vb).c_str());                                 \
            t::failures()++;                                                                        \
        }                                                                                           \
    } while (0)
