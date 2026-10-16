// Settings shared by the agent and the settings window.
//
// Stored as a small "key = value" text file so it is easy to read, diff and repair by hand.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mouse {

// Modifier bits used by Shortcut::mods.
enum : uint32_t { kCtrl = 1, kOpt = 2, kShift = 4, kCmd = 8, kFn = 16 };

// A keyboard shortcut: a macOS virtual key code (US layout positions) plus modifiers.
struct Shortcut {
    uint16_t key = 0xFFFF;  // 0xFFFF = none
    uint32_t mods = 0;

    bool valid() const { return key != 0xFFFF; }
    bool operator==(const Shortcut&) const = default;
};

// "cmd+shift+t" <-> Shortcut. Unknown text gives an invalid shortcut.
Shortcut parseShortcut(const std::string& text);
std::string shortcutSpec(const Shortcut& s);     // canonical "shift+cmd+t"
std::string shortcutDisplay(const Shortcut& s);  // "⇧⌘T"
// Name of a key code for the spec format ("t", "tab", "f5", "left"), empty if unsupported.
std::string keyName(uint16_t key);

// What a button (or a gesture direction) does.
enum class Act : uint8_t {
    None,
    MiddleClick,
    Back,
    Forward,
    CmdClick,
    MissionControl,
    AppExpose,
    ShowDesktop,
    SwitchApp,
    Dictation,
    PlayPause,
    Mute,
    SpaceLeft,
    SpaceRight,
    WheelMode,     // toggle ratchet / free spin
    Gestures,      // thumb button only: click + hold-and-move
    RightCommand,  // the right Command key, held as long as the button (dictation apps use it)
    Shortcut,      // the binding's keyboard shortcut
};

// What the thumb wheel does.
enum class WheelAct : uint8_t { HorizontalScroll, Volume, Zoom, Tabs, Shortcuts, None };

enum class WheelMode : uint8_t { Auto, Ratchet, FreeSpin };

struct Binding {
    Act act = Act::None;
    Shortcut keys;  // used when act == Act::Shortcut
    bool operator==(const Binding&) const = default;
};

// The physical controls that can be customised.
enum class Control : uint8_t { Middle, Back, Forward, Thumb, Top };
constexpr int kControlCount = 5;

enum class Dir : uint8_t { Click, Up, Down, Left, Right };
constexpr int kDirCount = 5;

struct Config {
    Binding buttons[kControlCount] = {
        {Act::MiddleClick, {}},  // Middle (wheel click)
        {Act::Back, {}},         // Back
        {Act::Forward, {}},      // Forward
        {Act::Gestures, {}},     // Thumb button
        {Act::WheelMode, {}},    // Top button behind the wheel
    };
    Act gestures[kDirCount] = {Act::MissionControl, Act::MissionControl, Act::AppExpose, Act::SpaceLeft,
                               Act::SpaceRight};

    WheelAct thumbWheel = WheelAct::HorizontalScroll;
    Shortcut thumbUp, thumbDown;  // used when thumbWheel == Shortcuts

    int dpi = 1000;            // pointer speed
    int scrollSpeed = 50;      // 0..100, 50 = normal
    bool naturalScroll = true; // content follows the wheel like a trackpad
    bool smoothScroll = true;
    WheelMode wheelMode = WheelMode::Auto;
    int sensitivity = 83;      // 0..100, auto-shift sensitivity
    int thumbSpeed = 50;       // 0..100
    bool thumbInverted = false;

    bool autoUpdate = true;

    Binding& button(Control c) { return buttons[static_cast<int>(c)]; }
    const Binding& button(Control c) const { return buttons[static_cast<int>(c)]; }
    bool operator==(const Config&) const = default;
};

Config parseConfig(const std::string& text);
std::string serializeConfig(const Config& c);

// Stable identifiers used in the file ("middle-click", "mission-control", ...).
const char* actId(Act a);
const char* wheelActId(WheelAct a);
bool parseAct(const std::string& id, Act& out);
bool parseWheelAct(const std::string& id, WheelAct& out);

// Human readable names for the settings window.
const char* actName(Act a);
const char* wheelActName(WheelAct a);
const char* controlName(Control c);
const char* dirName(Dir d);

// Which actions each place offers, in menu order. Act::None marks a separator.
std::vector<Act> buttonMenu(Control c);
std::vector<Act> gestureMenu();
std::vector<WheelAct> wheelMenu();

// Auto-shift sensitivity (0..100) -> device auto-disengage threshold (1..60).
int sensitivityToThreshold(int sensitivity);

// Pointer speed slider helpers: DPI <-> 0..1 position on a logarithmic scale.
double dpiToPosition(int dpi, int minDpi, int maxDpi);
int positionToDpi(double pos, int minDpi, int maxDpi, int step);

}  // namespace mouse
