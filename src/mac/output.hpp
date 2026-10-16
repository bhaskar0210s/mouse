#pragma once

#include <IOKit/IOKitLib.h>
#include <unistd.h>

#include <string>

#include "core/output.hpp"

namespace mouse::mac {

class MacOutput : public Output {
public:
    MacOutput();
    ~MacOutput() override;
    double now() override;
    void key(const Shortcut& s) override;
    void button(int button, bool down, uint32_t mods) override;
    void dock(Dock d) override;
    void media(int key) override;
    void dictation() override;
    void modifier(uint16_t key, bool down) override;
    void navigate(bool forward) override;
    void scroll(int dy, int dx, bool continuous) override;
    std::string frontApp(double maxAge) override;

private:
    bool connect();
    void post(uint32_t type, uint16_t key, uint32_t flags);
    io_connect_t hid_ = 0;
    double tick_ = 1e-9;
    std::string front_;  // bundle id of the app in front when last asked
    pid_t frontPid_ = -1;
    double frontAt_ = 0;
};

}  // namespace mouse::mac
