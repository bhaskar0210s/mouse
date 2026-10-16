// Everything the device logic asks the operating system to do. The agent implements it with
// real events; tests record the calls.
#pragma once

#include <cstdint>
#include <string>

#include "config.hpp"

namespace mouse {

enum class Dock : uint8_t { MissionControl, AppExpose, ShowDesktop };

// Media keys (macOS NX_KEYTYPE values).
enum MediaKey : int { kVolumeUp = 0, kVolumeDown = 1, kMute = 7, kPlayPause = 16 };

enum MouseButton : int { kButtonLeft = 0, kButtonRight = 1, kButtonMiddle = 2, kButtonBack = 3, kButtonForward = 4 };

class Output {
public:
    virtual ~Output() = default;
    virtual double now() = 0;                                      // monotonic seconds
    virtual void key(const Shortcut& s) = 0;                       // press and release
    virtual void button(int button, bool down, uint32_t mods) = 0;
    virtual void dock(Dock d) = 0;
    virtual void media(int key) = 0;                               // press and release
    virtual void dictation() = 0;
    // A modifier key (virtual key code, e.g. 0x36 right Command) going down or up on its own.
    virtual void modifier(uint16_t key, bool down) = 0;
    virtual void navigate(bool forward) = 0;                       // Back / Forward
    // Positive dy scrolls up (towards the top of a page), positive dx scrolls left.
    // continuous = pixel deltas like a trackpad; otherwise whole lines like a classic wheel.
    virtual void scroll(int dy, int dx, bool continuous) = 0;
    // Bundle id of the app in front ("" if unknown); an answer up to maxAge seconds old will do.
    // Only asked when apps have layouts of their own.
    virtual std::string frontApp(double maxAge) {
        (void)maxAge;
        return "";
    }
};

}  // namespace mouse
