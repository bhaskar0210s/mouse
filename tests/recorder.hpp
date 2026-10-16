// An Output that records what the device logic asked for.
#pragma once

#include <string>
#include <vector>

#include "core/output.hpp"

struct Recorder : mouse::Output {
    std::vector<std::string> log;
    double clock = 100.0;
    int scrolledY = 0, scrolledX = 0, scrollEvents = 0;
    bool lastContinuous = false;

    double now() override { return clock; }
    void key(const mouse::Shortcut& s) override { log.push_back("key " + mouse::shortcutDisplay(s)); }
    void button(int b, bool down, uint32_t mods) override {
        log.push_back("button " + std::to_string(b) + (down ? " down" : " up") + (mods ? " mods=" + std::to_string(mods) : ""));
    }
    void dock(mouse::Dock d) override {
        const char* n = d == mouse::Dock::MissionControl ? "mission-control" : d == mouse::Dock::AppExpose ? "app-expose" : "show-desktop";
        log.push_back(std::string("dock ") + n);
    }
    void media(int k) override { log.push_back("media " + std::to_string(k)); }
    void dictation() override { log.push_back("dictation"); }
    void modifier(uint16_t key, bool down) override {
        log.push_back("modifier " + std::to_string(key) + (down ? " down" : " up"));
    }
    void navigate(bool forward) override { log.push_back(forward ? "navigate forward" : "navigate back"); }
    void scroll(int dy, int dx, bool continuous) override {
        scrolledY += dy;
        scrolledX += dx;
        scrollEvents++;
        lastContinuous = continuous;
    }
    std::string last() const { return log.empty() ? "" : log.back(); }
    void clear() {
        log.clear();
        scrolledX = scrolledY = scrollEvents = 0;
    }
};
