// Settings, shortcuts, protocol framing and motion maths.
#include <cmath>
#include <cstdlib>
#include <unistd.h>

#include "core/config.hpp"
#include "core/hidpp.hpp"
#include "core/motion.hpp"
#include "core/store.hpp"
#include "test.hpp"

using namespace mouse;

TEST(config_defaults_round_trip) {
    Config c;
    Config back = parseConfig(serializeConfig(c));
    CHECK(back == c);
    CHECK(c.button(Control::Thumb).act == Act::Gestures);
    CHECK(c.button(Control::Top).act == Act::WheelMode);
    CHECK_EQ(c.dpi, 1000);
    CHECK(c.smoothScroll);
    CHECK(c.autoUpdate);
}

TEST(config_custom_round_trip) {
    Config c;
    c.button(Control::Middle).act = Act::Dictation;
    c.button(Control::Thumb) = {Act::Shortcut, parseShortcut("cmd+tab")};
    c.button(Control::Top).act = Act::CmdClick;
    c.gestures[static_cast<int>(Dir::Left)] = Act::Back;
    c.thumbWheel = WheelAct::Shortcuts;
    c.thumbUp = parseShortcut("cmd+w");
    c.thumbDown = parseShortcut("shift+cmd+t");
    c.dpi = 650;
    c.scrollSpeed = 70;
    c.naturalScroll = false;
    c.smoothScroll = false;
    c.wheelMode = WheelMode::FreeSpin;
    c.sensitivity = 12;
    c.thumbSpeed = 20;
    c.thumbInverted = true;
    c.autoUpdate = false;
    std::string text = serializeConfig(c);
    Config back = parseConfig(text);
    CHECK(back == c);
    CHECK(text.find("thumb.keys = cmd+tab") != std::string::npos);
    CHECK(text.find("thumbwheel.down = shift+cmd+t") != std::string::npos);
    CHECK(text.find("scroll.mode = freespin") != std::string::npos);
}

TEST(config_ignores_garbage) {
    Config c = parseConfig("# comment\n\n = x\nmiddle = rocket-launch\nthumb=GESTURES\nback = gestures\n"
                           "pointer.dpi = -5\nscroll.speed = 400\nscroll.mode = sideways\nupdates = maybe\n"
                           "gesture.up = shortcut\ngesture.down = show-desktop\nnot a line\r\nforward=none\r\n");
    Config d;
    CHECK(c.button(Control::Middle).act == Act::MiddleClick);  // unknown action ignored
    CHECK(c.button(Control::Thumb).act == Act::Gestures);       // case-insensitive
    CHECK(c.button(Control::Back).act == Act::Back);            // gestures only on the thumb button
    CHECK_EQ(c.dpi, 100);                                       // clamped
    CHECK_EQ(c.scrollSpeed, 100);
    CHECK(c.wheelMode == d.wheelMode);
    CHECK(c.autoUpdate == d.autoUpdate);
    CHECK(c.gestures[static_cast<int>(Dir::Up)] == d.gestures[static_cast<int>(Dir::Up)]);
    CHECK(c.gestures[static_cast<int>(Dir::Down)] == Act::ShowDesktop);
    CHECK(c.button(Control::Forward).act == Act::None);  // \r tolerated
}

TEST(shortcut_parse_and_display) {
    Shortcut s = parseShortcut("shift+cmd+t");
    CHECK(s.valid());
    CHECK_EQ(s.key, 0x11);
    CHECK_EQ(s.mods, uint32_t(kShift | kCmd));
    CHECK_EQ(shortcutDisplay(s), std::string("⇧⌘T"));
    CHECK_EQ(shortcutSpec(s), std::string("shift+cmd+t"));
    CHECK_EQ(shortcutDisplay(parseShortcut("Command + Tab")), std::string("⌘⇥"));
    CHECK_EQ(shortcutSpec(parseShortcut("cmd+ctrl+opt+left")), std::string("ctrl+opt+cmd+left"));
    CHECK_EQ(shortcutDisplay(parseShortcut("cmd+[")), std::string("⌘["));
    CHECK_EQ(shortcutDisplay(parseShortcut("cmd+=")), std::string("⌘="));
    CHECK_EQ(shortcutDisplay(parseShortcut("f5")), std::string("F5"));
    CHECK(!parseShortcut("hyper+x").valid());
    CHECK(!parseShortcut("cmd+").valid());
    CHECK(!parseShortcut("").valid());
    CHECK(!parseShortcut("cmd+notakey").valid());
    CHECK_EQ(keyName(0x30), std::string("tab"));
    CHECK_EQ(keyName(0x39), std::string(""));  // Caps Lock
    // every key the table knows survives a round trip (modifier keys stand alone)
    for (int code = 0; code < 0x80; code++) {
        std::string n = keyName(static_cast<uint16_t>(code));
        if (n.empty()) continue;
        Shortcut k{static_cast<uint16_t>(code), isModifierKey(static_cast<uint16_t>(code)) ? 0u : uint32_t(kCmd | kOpt)};
        CHECK(parseShortcut(shortcutSpec(k)) == k);
    }
}

TEST(action_ids_round_trip) {
    for (int i = 0; i <= static_cast<int>(Act::Shortcut); i++) {
        Act a = static_cast<Act>(i), back;
        CHECK(parseAct(actId(a), back));
        CHECK(back == a);
        CHECK(std::string(actName(a)).size() > 0);
    }
    for (WheelAct w : wheelMenu()) {
        WheelAct back;
        CHECK(parseWheelAct(wheelActId(w), back));
        CHECK(back == w);
    }
    // Gestures only appear in the thumb button's menu.
    for (int c = 0; c < kControlCount; c++) {
        auto m = buttonMenu(static_cast<Control>(c));
        bool has = false;
        for (Act a : m) has = has || a == Act::Gestures;
        CHECK(has == (static_cast<Control>(c) == Control::Thumb));
    }
}

TEST(shortcut_single_modifier_keys) {
    const char* names[] = {"left-cmd", "right-cmd", "left-opt", "right-opt", "left-ctrl",
                           "right-ctrl", "left-shift", "right-shift", "fn"};
    for (const char* n : names) {
        Shortcut s = parseShortcut(n);
        CHECK(s.valid());
        CHECK(isModifierKey(s.key));
        CHECK_EQ(s.mods, 0u);
        CHECK_EQ(shortcutSpec(s), std::string(n));
        CHECK(parseShortcut(shortcutSpec(s)) == s);
    }
    CHECK_EQ(shortcutDisplay(parseShortcut("right-cmd")), std::string("Right ⌘"));
    CHECK_EQ(shortcutDisplay(parseShortcut("left-opt")), std::string("Left ⌥"));
    CHECK_EQ(parseShortcut("right-cmd").key, 0x36);
    CHECK(!parseShortcut("cmd+right-opt").valid());  // a modifier key stands alone
    CHECK(!isModifierKey(parseShortcut("a").key));
}

TEST(modifier_tap_records_a_lone_modifier) {
    ModifierTap t;
    // Right Command pressed and let go.
    CHECK_EQ(t.changed(0x36, 0x100010), ModifierTap::kNone);
    CHECK_EQ(t.changed(0x36, 0x0), 0x36);
    // Left Option alone, with Caps Lock on all the while.
    CHECK_EQ(t.changed(0x3A, 0x10000 | 0x80020), ModifierTap::kNone);
    CHECK_EQ(t.changed(0x3A, 0x10000), 0x3A);
    // A combination of modifiers records nothing.
    CHECK_EQ(t.changed(0x36, 0x100010), ModifierTap::kNone);
    CHECK_EQ(t.changed(0x3A, 0x100010 | 0x80020), ModifierTap::kNone);
    CHECK_EQ(t.changed(0x3A, 0x100010), ModifierTap::kNone);
    CHECK_EQ(t.changed(0x36, 0x0), ModifierTap::kNone);
    // A key pressed in between (a shortcut like ⌘W) records nothing.
    CHECK_EQ(t.changed(0x37, 0x100008), ModifierTap::kNone);
    t.otherKey();
    CHECK_EQ(t.changed(0x37, 0x0), ModifierTap::kNone);
    // fn, and Caps Lock (not a key to hold).
    CHECK_EQ(t.changed(0x3F, 0x800000), ModifierTap::kNone);
    CHECK_EQ(t.changed(0x3F, 0x0), 0x3F);
    CHECK_EQ(t.changed(0x39, 0x10000), ModifierTap::kNone);
    CHECK_EQ(t.changed(0x39, 0x0), ModifierTap::kNone);
    // What the recorder shows while keys are held.
    CHECK_EQ(ModifierTap::single(0x100010), 0x36);
    CHECK_EQ(ModifierTap::single(0x100018), ModifierTap::kNone);  // both Command keys
    CHECK_EQ(ModifierTap::single(0x10000), ModifierTap::kNone);   // only Caps Lock
}

TEST(config_migrates_right_command) {
    // 1.0.4's "right-command" action is now a one-key shortcut; its stale recorded keys lose.
    Config c = parseConfig("middle = right-command\nmiddle.keys = a\nthumb = right-command\ngesture.up = right-command\n");
    CHECK(c.button(Control::Middle).act == Act::Shortcut);
    CHECK(c.button(Control::Middle).keys == parseShortcut("right-cmd"));
    CHECK(c.button(Control::Thumb).keys == parseShortcut("right-cmd"));
    CHECK(c.gestures[static_cast<int>(Dir::Up)] == Config{}.gestures[static_cast<int>(Dir::Up)]);  // ignored
    std::string text = serializeConfig(c);
    CHECK(text.find("middle = shortcut") != std::string::npos);
    CHECK(text.find("middle.keys = right-cmd") != std::string::npos);
    CHECK(parseConfig(text) == c);
}

TEST(config_app_layouts) {
    Config c;
    c.button(Control::Middle).act = Act::Dictation;
    AppLayout editor;
    editor.bundleId = "com.example.editor";
    editor.name = "Editor";
    editor.button(Control::Back) = {Act::Shortcut, parseShortcut("cmd+z")};
    editor.thumbWheel = WheelAct::Tabs;
    editor.gestures[static_cast<int>(Dir::Left)] = Act::Back;
    AppLayout browser;
    browser.bundleId = "com.example.browser";
    browser.name = "Web Browser";
    browser.button(Control::Middle).act = Act::MiddleClick;
    c.apps = {editor, browser};
    c.dpi = 1200;
    std::string text = serializeConfig(c);
    CHECK(text.find("[app com.example.editor]\nname = Editor\n") != std::string::npos);
    CHECK(text.find("[app com.example.browser]\nname = Web Browser\n") != std::string::npos);
    CHECK(text.find("pointer.dpi = 1200") < text.find("[app "));  // settings for everything come first
    Config back = parseConfig(text);
    CHECK(back == c);
    CHECK(&layoutFor(back, "com.example.editor") == &back.apps[0]);
    CHECK(&layoutFor(back, "com.example.other") == &back);
    CHECK(&layoutFor(back, "") == &back);

    // Unknown sections and settings that belong to all apps are ignored inside a section; a second
    // section for the same app continues it.
    Config d = parseConfig("middle = mute\n[weird thing]\nmiddle = back\n[app]\nback = none\n[app com.x.y]\n"
                           "pointer.dpi = 400\nforward = mute\n[app com.x.y]\ntop = none\n");
    CHECK(d.button(Control::Middle).act == Act::Mute);
    CHECK(d.button(Control::Back).act == Act::Back);
    CHECK_EQ(d.dpi, 1000);
    CHECK_EQ(d.apps.size(), size_t(1));
    CHECK(d.apps[0].name == "com.x.y");  // no name given
    CHECK(d.apps[0].button(Control::Forward).act == Act::Mute);
    CHECK(d.apps[0].button(Control::Top).act == Act::None);
}

TEST(sensitivity_and_dpi_scales) {
    CHECK_EQ(sensitivityToThreshold(83), 10);
    CHECK_EQ(sensitivityToThreshold(100), 1);
    CHECK_EQ(sensitivityToThreshold(0), 60);
    CHECK_EQ(sensitivityToThreshold(-5), 60);
    CHECK(sensitivityToThreshold(50) > sensitivityToThreshold(80));
    for (int dpi = 200; dpi <= 8000; dpi += 50) {
        double p = dpiToPosition(dpi, 200, 8000);
        CHECK(p >= 0 && p <= 1);
        CHECK_EQ(positionToDpi(p, 200, 8000, 50), dpi);
    }
    CHECK_EQ(positionToDpi(-1, 200, 8000, 50), 200);
    CHECK_EQ(positionToDpi(2, 200, 8000, 50), 8000);
    CHECK_EQ(positionToDpi(0.5, 0, 0, 50), 0);  // degenerate range does not crash
}

TEST(store_round_trip) {
    char dir[] = "/tmp/mouse-test-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    setenv("MOUSE_DIR", dir, 1);
    Config c;
    c.dpi = 1600;
    c.button(Control::Back).act = Act::MissionControl;
    CHECK(saveConfig(c));
    CHECK(loadConfig() == c);
    State s;
    s.running = true;
    s.connected = true;
    s.name = "MX Master 3S";
    s.battery = 42;
    s.charging = true;
    s.trusted = true;
    s.version = "1.2.3";
    s.update = "Updated to 1.2.3";
    CHECK(saveState(s));
    CHECK(loadState() == s);
    unsetenv("MOUSE_DIR");
    std::string cmd = std::string("rm -rf ") + dir;
    CHECK(std::system(cmd.c_str()) == 0);
}

TEST(hidpp_framing) {
    using namespace hidpp;
    uint8_t shortRep[7] = {0x10, 0xFF, 0x09, 0x0D, 1, 2, 3};
    Report r;
    CHECK(parseInput(shortRep, 7, r));
    CHECK_EQ(int(r.p(0)), 1);
    CHECK_EQ(int(r.p(3)), 0);
    uint8_t mouseRep[8] = {0x02, 1, 2, 3, 4, 5, 6, 7};
    CHECK(!parseInput(mouseRep, 8, r));
    CHECK(!parseInput(shortRep, 3, r));
    uint8_t truncatedLong[10] = {0x11, 0xFF};
    CHECK(!parseInput(truncatedLong, 10, r));

    uint8_t params[3] = {0x22, 0x01, 0x5A};
    Report req = makeRequest(0xFF, 13, 3, params, 3);
    CHECK_EQ(int(req.b[3]), 0x3D);
    Report ok = req;
    CHECK(isReplyTo(ok, req));
    ok.b[1] = 0x00;  // Bluetooth devices may answer with index 0
    CHECK(isReplyTo(ok, req));
    Report other = req;
    other.b[3] = 0x31;  // someone else's software id
    CHECK(!isReplyTo(other, req));
    Report err;
    err.b[0] = 0x11;
    err.b[1] = 0xFF;
    err.b[2] = 0xFF;
    err.b[3] = 13;
    err.b[4] = 0x3D;
    err.b[5] = kErrInvalidArgument;
    CHECK(err.isError());
    CHECK(isReplyTo(err, req));
    err.b[3] = 12;
    CHECK(!isReplyTo(err, req));
}

TEST(scroll_engine_slow_notches_are_even) {
    ScrollEngine e;
    double t = 0;
    int total = 0;
    for (int n = 0; n < 5; n++) {
        t += 0.5;  // a careful notch every half second
        int px = 0;
        for (int part = 0; part < 3; part++) px += e.feed(1.0 / 3, t + part * 0.01, 50, true);
        CHECK(px >= 34 && px <= 38);
        total += px;
    }
    CHECK(total >= 175 && total <= 185);
}

TEST(scroll_engine_accelerates_and_resets) {
    ScrollEngine e;
    double t = 0;
    int slow = e.feed(1, t, 50, true);
    // spin fast: 100 notches/second for half a second
    int fast = 0;
    for (int i = 0; i < 50; i++) {
        t += 0.01;
        fast += e.feed(1, t, 50, true);
    }
    CHECK(fast > 50 * slow * 3);  // well above linear
    CHECK(e.velocity() > 50);
    // a change of direction starts again without acceleration
    t += 0.01;
    int back = e.feed(-1, t, 50, true);
    CHECK(back <= -34 && back >= -38);
    // a pause also starts again
    t += 1.0;
    CHECK_EQ(e.feed(1, t, 50, true), 36);
    // lines mode: one slow notch is one line at normal speed
    ScrollEngine l;
    CHECK_EQ(l.feed(1, 5.0, 50, false), 1);
    // speed slider scales
    ScrollEngine s1, s2;
    CHECK(s1.feed(1, 1, 100, true) > s2.feed(1, 1, 50, true));
    CHECK(speedFactor(0) < speedFactor(50));
    CHECK(std::fabs(speedFactor(50) - 1.0) < 1e-9);
    // garbage input
    CHECK_EQ(e.feed(NAN, t, 50, true), 0);
    CHECK_EQ(e.feed(0, t, 50, true), 0);
}

TEST(gesture_tracker_directions) {
    GestureTracker g;
    g.begin(80);
    CHECK(g.move(10, -5) == GestureTracker::kNone);
    CHECK(g.move(0, -80) == GestureTracker::kUp);
    CHECK(g.move(0, -500) == GestureTracker::kNone);  // once per press
    CHECK(g.end() == GestureTracker::kNone);
    g.begin(80);
    CHECK(g.end() == GestureTracker::kClick);
    g.begin(80);
    CHECK(g.move(100, 20) == GestureTracker::kRight);
    g.end();
    g.begin(80);
    CHECK(g.move(-90, 0) == GestureTracker::kLeft);
    g.end();
    g.begin(80);
    CHECK(g.move(0, 81) == GestureTracker::kDown);
    g.end();
    CHECK(g.end() == GestureTracker::kNone);  // not active
    CHECK(g.move(500, 0) == GestureTracker::kNone);
}

TEST(stepper_steps_without_banking) {
    Stepper s;
    double t = 0;
    CHECK_EQ(s.feed(10, 15, t, 0.1), 0);
    CHECK_EQ(s.feed(10, 15, t + 0.01, 0.1), 1);
    CHECK_EQ(s.feed(100, 15, t + 0.02, 0.1), 0);  // rate limited, nothing banked
    CHECK_EQ(s.feed(1, 15, t + 0.13, 0.1), 1);    // one step after the interval
    CHECK_EQ(s.feed(1, 15, t + 0.14, 0.1), 0);
    CHECK_EQ(s.feed(-20, 15, t + 0.30, 0.1), -1); // direction change
    s.reset();
    CHECK_EQ(s.feed(14, 15, 5.0, 0), 0);
    CHECK_EQ(s.feed(14, 15, 6.0, 0), 0);          // idle dropped the first partial turn
    CHECK_EQ(s.feed(1, 15, 6.01, 0), 1);          // 14 + 1 completes a step
    CHECK_EQ(s.feed(0, 15, 6.02, 0), 0);
}

TEST(config_new_app_layout_copies_a_layout) {
    Config c;
    c.button(Control::Middle).act = Act::Dictation;
    AppLayout chrome = newAppLayout(c, "com.google.Chrome", "Google Chrome");
    CHECK(static_cast<const Layout&>(chrome) == static_cast<const Layout&>(c));  // like all apps
    chrome.button(Control::Back) = {Act::Shortcut, parseShortcut("cmd+[")};
    chrome.thumbWheel = WheelAct::Tabs;
    c.apps.push_back(chrome);

    AppLayout safari = newAppLayout(c, "com.apple.Safari", "Safari", 0);  // copy of Chrome's
    CHECK(static_cast<const Layout&>(safari) == static_cast<const Layout&>(c.apps[0]));
    CHECK(safari.bundleId == "com.apple.Safari");
    CHECK(safari.name == "Safari");

    AppLayout other = newAppLayout(c, "com.example.x", "", 7);  // no such app: like all apps, named by id
    CHECK(static_cast<const Layout&>(other) == static_cast<const Layout&>(c));
    CHECK(other.name == "com.example.x");
}

TEST(bundle_folders_of_a_program) {
    using Folders = std::vector<std::string>;
    CHECK(bundleFolders("/Applications/Zed.app/Contents/MacOS/zed") == Folders{"/Applications/Zed.app"});
    // Chrome runs from a copy of itself whose name does not end in .app.
    std::string clone = "/private/var/folders/ab/cd/X/com.google.Chrome.code_sign_clone/code_sign_clone.x1/Google Chrome.app.bundle";
    CHECK(bundleFolders(clone + "/Contents/MacOS/Google Chrome") == Folders{clone});
    // A helper belongs to the app around it, which comes first.
    CHECK(bundleFolders("/Applications/Mouse.app/Contents/Helpers/MouseAgent.app/Contents/MacOS/MouseAgent") ==
          (Folders{"/Applications/Mouse.app", "/Applications/Mouse.app/Contents/Helpers/MouseAgent.app"}));
    // A folder that merely holds a Contents folder is a candidate too; it has no bundle id, so the
    // next one is tried.
    CHECK(bundleFolders("/Volumes/Contents/Tool.app/Contents/MacOS/Tool") ==
          (Folders{"/Volumes", "/Volumes/Contents/Tool.app"}));
    CHECK(bundleFolders("/usr/bin/python3").empty());
    CHECK(bundleFolders("/Contents/x").empty());
    CHECK(bundleFolders("").empty());
}
