// `make simulate` builds build/mouse-simulate, a development tool that is not part of the app: a
// simulated MX Master 3S, driven by a script, whose events produce real macOS input.
//
//   mouse-simulate <script> [--dry]   (--dry prints the actions instead of performing them)
//
// Script lines: press c3 | release c3 | move dx dy | wheel n | hires n | thumb n | battery p |
//               sleep s | apply
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "core/device.hpp"
#include "core/store.hpp"
#include "mac/output.hpp"
#include "mac/system.hpp"
#include "sim/mx3s.hpp"

using namespace mouse;

int main(int argc, char** argv) {
    const char* path = nullptr;
    bool dryRun = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--dry")) dryRun = true;
        else path = argv[i];
    }
    if (!path) {
        std::fprintf(stderr, "usage: mouse-simulate <script> [--dry]\n");
        return 2;
    }
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", path);
        return 2;
    }
    sim::MXMaster3S mouse;
    sim::SimTransport link(mouse);
    struct Printer : Output {
        Output* real;
        bool dry;
        double now() override { return real->now(); }
        void key(const Shortcut& s) override { std::printf("key %s\n", shortcutDisplay(s).c_str()); if (!dry) real->key(s); }
        void button(int b, bool d, uint32_t m) override { std::printf("button %d %s %u\n", b, d ? "down" : "up", m); if (!dry) real->button(b, d, m); }
        void dock(Dock d) override { std::printf("dock %d\n", static_cast<int>(d)); if (!dry) real->dock(d); }
        void media(int k) override { std::printf("media %d\n", k); if (!dry) real->media(k); }
        void dictation() override { std::printf("dictation\n"); if (!dry) real->dictation(); }
        void modifier(uint16_t k, bool d) override { std::printf("modifier 0x%x %s\n", k, d ? "down" : "up"); if (!dry) real->modifier(k, d); }
        void navigate(bool f) override { std::printf("navigate %s\n", f ? "forward" : "back"); if (!dry) real->navigate(f); }
        void scroll(int dy, int dx, bool c) override { std::printf("scroll %d %d %s\n", dy, dx, c ? "px" : "lines"); if (!dry) real->scroll(dy, dx, c); }
        std::string frontApp(double maxAge) override { return real->frontApp(maxAge); }  // reading it is harmless
    };
    mac::MacOutput real;
    Printer out;
    out.real = &real;
    out.dry = dryRun;
    Device dev(link, out);
    if (!dev.start()) return 3;
    dev.apply(loadConfig(), true, mac::systemNaturalScrolling());
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd.empty() || cmd[0] == '#') continue;
        if (cmd == "sleep") {
            double s = 0;
            ss >> s;
            usleep(static_cast<useconds_t>(s * 1e6));
            continue;
        }
        if (cmd == "apply") {
            dev.apply(loadConfig(), true, mac::systemNaturalScrolling());
            continue;
        }
        long a = 0, b = 0;
        std::string first;
        ss >> first;
        a = std::strtol(first.c_str(), nullptr, cmd == "press" || cmd == "release" ? 16 : 10);
        ss >> b;
        if (cmd == "press") mouse.press(static_cast<uint16_t>(a));
        else if (cmd == "release") mouse.release(static_cast<uint16_t>(a));
        else if (cmd == "move") mouse.move(static_cast<int>(a), static_cast<int>(b));
        else if (cmd == "wheel") mouse.turnWheel(static_cast<int>(a));
        else if (cmd == "hires") mouse.turnWheelHiRes(static_cast<int>(a));
        else if (cmd == "thumb") mouse.turnThumbwheel(static_cast<int>(a));
        else if (cmd == "battery") mouse.setBattery(static_cast<int>(a), b != 0);
        link.pump(dev);
        std::fflush(stdout);
    }
    dev.restore();
    return 0;
}
