// One connected MX Master 3S: discovers its HID++ features, applies the settings and turns the
// events of diverted controls into actions.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "config.hpp"
#include "hidpp.hpp"
#include "motion.hpp"
#include "output.hpp"

namespace mouse {

// Moves whole HID++ reports between us and the mouse.
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool write(const hidpp::Report& r) = 0;
    // Next incoming HID++ report within timeoutMs; false on timeout or failure.
    virtual bool read(hidpp::Report& out, int timeoutMs) = 0;
};

struct DeviceInfo {
    std::string name;
    int battery = -1;  // percent
    bool charging = false;
    int dpi = 0, dpiMin = 200, dpiMax = 8000, dpiStep = 50;
    int wheelMultiplier = 15;  // hi-res wheel units per notch
    int thumbResolution = 120; // diverted thumb wheel units per turn
    bool thumbReversedByDefault = false;
};

class Device {
public:
    Device(Transport& transport, Output& output, uint8_t index = hidpp::kDeviceDirect);
    ~Device();  // lets go of any button it is holding down on the mouse's behalf
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // Talks to the mouse and reads what it can do. False if it does not answer like an MX Master.
    bool start();

    // Pushes the settings. canPost = we may synthesise input (Accessibility granted); without it
    // every control stays native so nothing is lost. systemNatural = macOS natural scrolling.
    void apply(const Config& cfg, bool canPost, bool systemNatural);

    // A report that arrived while no request was waiting.
    void handle(const hidpp::Report& r);
    // Notifications that arrived while a request was waiting for its reply.
    void processPending();
    bool hasPending() const { return !pending_.empty(); }

    // Gives every control back to the mouse firmware (used when the agent stops).
    void restore();

    // The mouse reconnected or asked to be configured again.
    bool needsReapply() const { return reapply_; }
    // Forget what we believe the mouse is set to, so the next apply() sends everything again
    // (after sleep, or when another app may have changed it).
    void invalidate() { forgetDeviceState(); }

    const DeviceInfo& info() const { return info_; }
    bool started() const { return started_; }
    std::function<void()> onInfoChanged;  // battery or name changed

    // Feature indexes found by start() (0 = absent); exposed for tests and diagnostics.
    struct Indexes {
        uint8_t name = 0, battery = 0, reprog = 0, wireless = 0, smartShift = 0, hiRes = 0, thumb = 0, dpi = 0;
        bool smartShiftEnhanced = false;
    };
    const Indexes& indexes() const { return idx_; }
    int lastError() const { return lastError_; }

private:
    bool request(uint8_t featureIndex, uint8_t function, std::initializer_list<uint8_t> params,
                 hidpp::Report* reply = nullptr, int timeoutMs = 1500);
    uint8_t featureIndex(uint16_t featureId);
    void readName();
    void readBattery();
    void readDpi();
    void readControls();
    void readWheel();
    void readThumbwheel();

    void setDpi(int dpi);
    void setWheelMode(WheelMode mode, int sensitivity);
    void toggleWheelMode();
    void setReporting(uint16_t cid, bool divert, bool rawXY);

    void onButtons(const hidpp::Report& r);
    void onPress(Control c);
    void onRelease(Control c);
    void perform(Act a, const Shortcut& keys);
    void onRawXY(int dx, int dy);
    void onWheel(const hidpp::Report& r);
    void onThumbwheel(const hidpp::Report& r);
    void onBattery(const hidpp::Report& r);
    void forgetDeviceState();
    void releaseAll();

    Transport& t_;
    Output& out_;
    uint8_t index_;
    bool started_ = false;
    bool reapply_ = false;
    int lastError_ = 0;
    uint8_t nextSoftwareId_ = 1;  // rotates 1..15 so a late reply never answers a newer request
    Indexes idx_;
    DeviceInfo info_;
    std::deque<hidpp::Report> pending_;

    // What the mouse is currently set to (as far as we know).
    Config cfg_;
    bool applied_ = false;
    bool canPost_ = false;
    bool systemNatural_ = true;
    uint16_t divertable_ = 0;  // bit per control that the firmware lets us divert
    uint8_t reporting_[kControlCount] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // last flags sent, 0xFF unknown
    int wheelFlags_ = -1;
    bool wheelInvertable_ = true;
    int thumbFlags_ = -1;  // bit0 divert, bit1 invert
    int smartMode_ = 0, smartThreshold_ = 0;  // last read/set
    bool wheelDiverted_ = false, wheelHiRes_ = false;
    bool thumbDiverted_ = false;

    uint16_t pressed_[4] = {};
    bool held_[kControlCount] = {};
    Act heldAct_[kControlCount] = {};
    GestureTracker gesture_;
    ScrollEngine scroll_;
    ScrollEngine thumbScroll_{40.0};  // fed in eighths of a turn
    Stepper stepper_;
};

// Which control a CID is, or -1.
int controlForCid(uint16_t cid);
uint16_t cidForControl(Control c);

// True when an action is the firmware's own behaviour for that control (no diversion needed).
bool isNative(Control c, Act a);

}  // namespace mouse
