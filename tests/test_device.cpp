// End-to-end device sessions against the simulated MX Master 3S.
#include <random>

#include "core/device.hpp"
#include "recorder.hpp"
#include "sim/mx3s.hpp"
#include "test.hpp"

using namespace mouse;
using mouse::sim::MXMaster3S;
using mouse::sim::SimTransport;

namespace {

struct Rig {
    MXMaster3S mouse;
    SimTransport link{mouse};
    Recorder out;
    Device dev{link, out};
    bool started = dev.start();
    void pump() { link.pump(dev); }
};

// A customised setup that exercises every kind of action.
Config customisedConfig() {
    Config c;
    c.button(Control::Middle).act = Act::Dictation;
    c.button(Control::Back).act = Act::Back;
    c.button(Control::Forward).act = Act::Forward;
    c.button(Control::Thumb) = {Act::Shortcut, parseShortcut("cmd+tab")};
    c.button(Control::Top).act = Act::CmdClick;
    c.thumbWheel = WheelAct::Shortcuts;
    c.thumbUp = parseShortcut("cmd+w");
    c.thumbDown = parseShortcut("shift+cmd+t");
    c.dpi = 650;
    c.naturalScroll = false;
    c.smoothScroll = true;
    c.wheelMode = WheelMode::FreeSpin;
    return c;
}

}  // namespace

TEST(device_discovers_features) {
    Rig r;
    CHECK(r.started);
    const auto& i = r.dev.indexes();
    CHECK_EQ(int(i.name), 3);
    CHECK_EQ(int(i.wireless), 4);
    CHECK_EQ(int(i.battery), 8);
    CHECK_EQ(int(i.reprog), 9);
    CHECK_EQ(int(i.dpi), 13);
    CHECK_EQ(int(i.smartShift), 14);
    CHECK(!i.smartShiftEnhanced);
    CHECK_EQ(int(i.hiRes), 15);
    CHECK_EQ(int(i.thumb), 16);
    CHECK_EQ(r.dev.info().name, std::string("MX Master 3S For Mac"));
    CHECK_EQ(r.dev.info().battery, 85);
    CHECK_EQ(r.dev.info().dpiMin, 200);
    CHECK_EQ(r.dev.info().dpiMax, 8000);
    CHECK_EQ(r.dev.info().dpiStep, 50);
    CHECK_EQ(r.dev.info().dpi, 1000);
    CHECK_EQ(r.dev.info().wheelMultiplier, 15);
    CHECK_EQ(r.dev.info().thumbResolution, 120);
}

TEST(device_applies_defaults) {
    Rig r;
    Config c;
    r.dev.apply(c, true, true);
    CHECK_EQ(int(r.mouse.wheelMode), 0x03);  // diverted + hi-res for smooth scrolling
    CHECK(!r.mouse.thumbDivert);             // horizontal scrolling stays native
    CHECK(!r.mouse.control(0x52)->divert);   // middle click native
    CHECK(!r.mouse.control(0x53)->divert);   // back native
    CHECK(!r.mouse.control(0x56)->divert);   // forward native
    CHECK(!r.mouse.control(0xC4)->divert);   // wheel mode button native
    CHECK(r.mouse.control(0xC3)->divert);    // thumb button: gestures
    CHECK(r.mouse.control(0xC3)->rawXY);
    CHECK_EQ(int(r.mouse.ratchetMode), 2);
    CHECK_EQ(int(r.mouse.threshold), 10);
    CHECK_EQ(r.mouse.dpi, 1000);
}

TEST(device_without_permission_keeps_everything_native) {
    Rig r;
    Config c;
    c.button(Control::Middle).act = Act::MissionControl;
    c.naturalScroll = false;
    r.dev.apply(c, /*canPost=*/false, /*systemNatural=*/true);
    for (auto& ctl : r.mouse.controls) CHECK(!ctl.divert);
    CHECK_EQ(int(r.mouse.wheelMode), int(hidpp::kWheelInvert));  // standard direction via firmware
    // wheel-mode and do-nothing need no permission
    c.button(Control::Middle).act = Act::None;
    c.button(Control::Forward).act = Act::WheelMode;
    r.dev.apply(c, false, true);
    CHECK(r.mouse.control(0x52)->divert);
    CHECK(r.mouse.control(0x56)->divert);
}

TEST(device_customised_setup_end_to_end) {
    Rig r;
    Config c = customisedConfig();
    r.dev.apply(c, true, true);
    CHECK_EQ(r.mouse.dpi, 650);
    CHECK_EQ(int(r.mouse.ratchetMode), 1);  // free spin
    CHECK(r.mouse.thumbDivert);
    CHECK(r.mouse.control(0x52)->divert);
    CHECK(r.mouse.control(0xC3)->divert);
    CHECK(!r.mouse.control(0xC3)->rawXY);
    CHECK(r.mouse.control(0xC4)->divert);
    CHECK(!r.mouse.control(0x53)->divert);

    r.mouse.press(0x52);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("dictation"));
    r.mouse.release(0x52);
    r.pump();
    CHECK_EQ(r.out.log.size(), size_t(1));

    r.mouse.press(0xC3);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⌘⇥"));
    r.mouse.release(0xC3);
    r.pump();

    r.mouse.press(0xC4);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("button 0 down mods=8"));
    r.mouse.release(0xC4);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("button 0 up mods=8"));

    // Back stays native: the firmware handles it, we see nothing.
    size_t before = r.out.log.size();
    r.mouse.press(0x53);
    r.mouse.release(0x53);
    r.pump();
    CHECK_EQ(r.out.log.size(), before);
    CHECK_EQ(r.mouse.nativeLog.back(), std::string("native up 0x53"));

    // Thumb wheel: an eighth of a turn up closes a tab, down reopens one; never twice in 100 ms.
    r.out.clear();
    r.mouse.turnThumbwheel(15);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⌘W"));
    r.mouse.turnThumbwheel(60);
    r.pump();
    CHECK_EQ(r.out.log.size(), size_t(1));  // rate limited, nothing banked
    r.out.clock += 0.2;
    r.mouse.turnThumbwheel(-15);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⇧⌘T"));
    CHECK_EQ(r.out.log.size(), size_t(2));
}

TEST(device_gestures) {
    Rig r;
    r.dev.apply(Config{}, true, true);
    r.mouse.press(0xC3);
    r.mouse.move(0, -30);
    r.mouse.move(0, -60);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("dock mission-control"));
    r.mouse.move(0, -300);
    r.mouse.release(0xC3);
    r.pump();
    CHECK_EQ(r.out.log.size(), size_t(1));  // one action per press, no click on release

    r.out.clear();
    r.mouse.press(0xC3);
    r.mouse.move(3, 2);
    r.mouse.release(0xC3);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("dock mission-control"));  // click

    r.out.clear();
    r.mouse.press(0xC3);
    r.mouse.move(0, 120);
    r.mouse.release(0xC3);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("dock app-expose"));

    r.out.clear();
    r.mouse.press(0xC3);
    r.mouse.move(-120, 10);
    r.mouse.release(0xC3);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⌃←"));

    r.out.clear();
    r.mouse.press(0xC3);
    r.mouse.move(120, 10);
    r.mouse.release(0xC3);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⌃→"));
    // the cursor never moved while the thumb button was held
    for (auto& line : r.mouse.nativeLog) CHECK(line.find("native move") == std::string::npos);
}

TEST(device_smooth_scroll_direction) {
    Rig r;
    Config c;
    c.naturalScroll = false;
    r.dev.apply(c, true, true);
    r.mouse.turnWheel(1);
    r.pump();
    CHECK(r.out.scrolledY >= 34 && r.out.scrolledY <= 38);  // standard: away from you scrolls up
    CHECK(r.out.lastContinuous);
    r.out.clear();
    r.out.clock += 1;
    c.naturalScroll = true;
    r.dev.apply(c, true, true);
    r.mouse.turnWheel(1);
    r.pump();
    CHECK(r.out.scrolledY <= -34 && r.out.scrolledY >= -38);
    // hi-res units add up to the same distance as whole notches
    r.out.clear();
    r.out.clock += 1;
    for (int i = 0; i < 15; i++) r.mouse.turnWheelHiRes(1);
    r.pump();
    CHECK(r.out.scrolledY <= -34 && r.out.scrolledY >= -38);
    CHECK(r.out.scrollEvents >= 10);  // smooth: many small steps
}

TEST(device_classic_scroll_modes) {
    Rig r;
    Config c;
    c.smoothScroll = false;
    r.dev.apply(c, true, /*systemNatural=*/true);
    CHECK_EQ(int(r.mouse.wheelMode), 0);  // fully native
    r.mouse.turnWheel(2);
    r.pump();
    CHECK_EQ(r.out.scrollEvents, 0);
    CHECK_EQ(r.mouse.nativeLog.back(), std::string("native wheel 2"));

    c.naturalScroll = false;  // macOS is natural, the user wants standard: firmware inverts
    r.dev.apply(c, true, true);
    CHECK_EQ(int(r.mouse.wheelMode), int(hidpp::kWheelInvert));
    r.mouse.turnWheel(1);
    CHECK_EQ(r.mouse.nativeLog.back(), std::string("native wheel -1"));

    c.scrollSpeed = 80;  // faster classic scrolling: diverted, whole lines
    r.dev.apply(c, true, true);
    CHECK_EQ(int(r.mouse.wheelMode), int(hidpp::kWheelDivert));
    r.mouse.turnWheel(1);
    r.pump();
    CHECK(!r.out.lastContinuous);
    CHECK_EQ(r.out.scrolledY, 2);  // 1 notch * 2x speed = 2 lines, standard direction
}

TEST(device_thumbwheel_modes) {
    Rig r;
    Config c;
    c.thumbInverted = true;
    r.dev.apply(c, true, true);
    CHECK(!r.mouse.thumbDivert);
    CHECK(r.mouse.thumbInvert);  // native, inverted in firmware

    c.thumbSpeed = 80;  // faster horizontal scrolling needs diversion
    r.dev.apply(c, true, true);
    CHECK(r.mouse.thumbDivert);
    CHECK(!r.mouse.thumbInvert);
    r.mouse.turnThumbwheel(15);
    r.pump();
    CHECK(r.out.scrolledX != 0);

    c.thumbWheel = WheelAct::Volume;
    c.thumbInverted = false;
    c.thumbSpeed = 50;
    r.out.clear();
    r.dev.apply(c, true, true);
    r.mouse.turnThumbwheel(15);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("media 0"));
    r.mouse.turnThumbwheel(-15);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("media 1"));

    c.thumbWheel = WheelAct::Zoom;
    r.dev.apply(c, true, true);
    r.mouse.turnThumbwheel(15);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⌘="));

    c.thumbWheel = WheelAct::Tabs;
    r.dev.apply(c, true, true);
    r.mouse.turnThumbwheel(-15);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⌃⇧⇥"));

    c.thumbWheel = WheelAct::None;
    r.out.clear();
    r.dev.apply(c, false, true);  // swallowing needs no permission
    CHECK(r.mouse.thumbDivert);
    r.mouse.turnThumbwheel(60);
    r.pump();
    CHECK(r.out.log.empty());
}

TEST(device_wheel_mode_toggle_and_ratchet) {
    Rig r;
    Config c;
    c.button(Control::Middle).act = Act::WheelMode;
    r.dev.apply(c, true, true);
    CHECK_EQ(int(r.mouse.ratchetMode), 2);
    r.mouse.press(0x52);
    r.pump();
    CHECK_EQ(int(r.mouse.ratchetMode), 1);
    r.mouse.release(0x52);
    r.mouse.press(0x52);
    r.pump();
    CHECK_EQ(int(r.mouse.ratchetMode), 2);
    c.wheelMode = WheelMode::Ratchet;
    r.dev.apply(c, true, true);
    CHECK_EQ(int(r.mouse.threshold), 255);
    c.wheelMode = WheelMode::Auto;
    c.sensitivity = 50;
    r.dev.apply(c, true, true);
    CHECK_EQ(int(r.mouse.threshold), 30);
}

TEST(device_applies_only_changes) {
    Rig r;
    Config c;
    r.dev.apply(c, true, true);
    int n = r.mouse.requestCount;
    r.dev.apply(c, true, true);
    CHECK_EQ(r.mouse.requestCount, n);  // nothing changed, nothing sent
    c.dpi = 1600;
    r.dev.apply(c, true, true);
    CHECK_EQ(r.mouse.requestCount, n + 1);
    CHECK_EQ(r.mouse.dpi, 1600);
    c.dpi = 1234;  // snapped to the 50 DPI grid
    r.dev.apply(c, true, true);
    CHECK_EQ(r.mouse.dpi, 1250);
    c.dpi = 99999;
    r.dev.apply(c, true, true);
    CHECK_EQ(r.mouse.dpi, 8000);
}

TEST(device_reapplies_after_reconnect) {
    Rig r;
    Config c = customisedConfig();
    r.dev.apply(c, true, true);
    r.mouse.powerCycle();
    CHECK(!r.mouse.control(0x52)->divert);
    r.pump();
    CHECK(r.dev.needsReapply());
    r.dev.apply(c, true, true);
    CHECK(!r.dev.needsReapply());
    CHECK(r.mouse.control(0x52)->divert);
    CHECK(r.mouse.thumbDivert);
    CHECK_EQ(r.mouse.dpi, 650);
    CHECK_EQ(int(r.mouse.wheelMode), 0x03);
}

TEST(device_restore_hands_back_control) {
    Rig r;
    Config c = customisedConfig();
    r.dev.apply(c, true, true);
    r.mouse.press(0xC4);  // held Cmd-click must be let go
    r.pump();
    r.dev.restore();
    CHECK_EQ(r.out.last(), std::string("button 0 up mods=8"));
    for (auto& ctl : r.mouse.controls) CHECK(!ctl.divert);
    CHECK(!r.mouse.thumbDivert);
    CHECK_EQ(int(r.mouse.wheelMode), int(hidpp::kWheelInvert));  // standard direction kept natively
}

TEST(device_releases_held_buttons_when_settings_change) {
    Rig r;
    Config c;
    c.button(Control::Top).act = Act::CmdClick;
    r.dev.apply(c, true, true);
    r.mouse.press(0xC4);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("button 0 down mods=8"));
    c.button(Control::Top).act = Act::MissionControl;
    r.dev.apply(c, true, true);
    CHECK_EQ(r.out.last(), std::string("button 0 up mods=8"));
    r.mouse.release(0xC4);  // the release after the change does nothing more
    r.pump();
    CHECK_EQ(r.out.last(), std::string("button 0 up mods=8"));
}

TEST(device_middle_click_hold_and_multi_buttons) {
    Rig r;
    Config c;
    c.button(Control::Back).act = Act::MiddleClick;  // middle click on the back button, held
    c.button(Control::Forward).act = Act::SwitchApp;
    r.dev.apply(c, true, true);
    r.mouse.press(0x53);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("button 2 down"));
    r.mouse.press(0x56);  // second button while the first is held
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⌘⇥"));
    r.mouse.release(0x56);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("key ⌘⇥"));
    r.mouse.release(0x53);
    r.pump();
    CHECK_EQ(r.out.last(), std::string("button 2 up"));
}

TEST(device_silent_mouse_fails_fast) {
    MXMaster3S m;
    m.silent = true;
    SimTransport link{m};
    Recorder out;
    Device d{link, out};
    CHECK(!d.start());
    CHECK(!d.started());
    d.apply(Config{}, true, true);  // harmless when not started
    d.restore();
    CHECK_EQ(m.requestCount, 2);  // two pings, then give up
}

TEST(device_survives_errors) {
    Rig busy;
    (void)busy;
    MXMaster3S m;
    m.failNextRequests = 1;  // first ping answers "busy"
    m.brokenFeature = 0x2201;  // DPI feature answers "hardware error"
    SimTransport link{m};
    Recorder out;
    Device d{link, out};
    CHECK(d.start());
    Config c;
    c.dpi = 3000;
    d.apply(c, true, true);
    CHECK_EQ(m.dpi, 1000);                // could not change
    CHECK(m.control(0xC3)->divert);       // everything else still applied
    CHECK_EQ(int(m.wheelMode), 0x03);
}

TEST(device_notifications_during_requests_are_kept) {
    Rig r;
    Config c;
    c.button(Control::Middle).act = Act::MissionControl;
    r.dev.apply(c, true, true);
    // A press arrives while a request is in flight: queue it before the next request's reply.
    r.mouse.press(0x52);
    c.dpi = 1200;
    r.dev.apply(c, true, true);  // reads the press as a pending notification
    CHECK(r.dev.hasPending());
    r.dev.processPending();
    CHECK_EQ(r.out.last(), std::string("dock mission-control"));
}

TEST(device_ignores_foreign_and_garbage_reports) {
    Rig r;
    r.dev.apply(customisedConfig(), true, true);
    std::mt19937 rng(1234);
    for (int i = 0; i < 200000; i++) {
        hidpp::Report rep;
        for (auto& b : rep.b) b = static_cast<uint8_t>(rng());
        if (i % 3 == 0) rep.b[1] = 0xFF;
        if (i % 5 == 0) rep.b[3] &= 0xF0;  // looks like a notification
        r.out.clock += 0.001;
        r.dev.handle(rep);
    }
    r.dev.processPending();
    CHECK(r.started);
    // still works afterwards
    r.dev.apply(customisedConfig(), true, true);
    r.out.clear();
    r.mouse.press(0x52);
    r.pump();
    CHECK(!r.out.log.empty());
}

TEST(device_rejects_non_hidpp2) {
    MXMaster3S m;
    SimTransport link{m};
    Recorder out;
    Device d{link, out, 0x01};  // wrong device index: a Bluetooth mouse never answers it
    CHECK(!d.start());
}
