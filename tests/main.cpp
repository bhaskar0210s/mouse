#include <cstdlib>
#include <cstring>

#include "test.hpp"

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& c : t::cases()) {
        if (only && !std::strstr(c.name, only)) continue;
        int before = t::failures();
        c.fn();
        run++;
        std::printf("%s %s\n", t::failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", run, t::failures());
    return t::failures() ? 1 : 0;
}
