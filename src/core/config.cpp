#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace mouse {
namespace {

struct KeyEntry {
    uint16_t code;
    const char* name;     // spec name
    const char* display;  // shown in the window
};

// macOS virtual key codes (positions on a US keyboard).
constexpr KeyEntry kKeys[] = {
    {0x00, "a", "A"}, {0x0B, "b", "B"}, {0x08, "c", "C"}, {0x02, "d", "D"}, {0x0E, "e", "E"},
    {0x03, "f", "F"}, {0x05, "g", "G"}, {0x04, "h", "H"}, {0x22, "i", "I"}, {0x26, "j", "J"},
    {0x28, "k", "K"}, {0x25, "l", "L"}, {0x2E, "m", "M"}, {0x2D, "n", "N"}, {0x1F, "o", "O"},
    {0x23, "p", "P"}, {0x0C, "q", "Q"}, {0x0F, "r", "R"}, {0x01, "s", "S"}, {0x11, "t", "T"},
    {0x20, "u", "U"}, {0x09, "v", "V"}, {0x0D, "w", "W"}, {0x07, "x", "X"}, {0x10, "y", "Y"},
    {0x06, "z", "Z"},
    {0x1D, "0", "0"}, {0x12, "1", "1"}, {0x13, "2", "2"}, {0x14, "3", "3"}, {0x15, "4", "4"},
    {0x17, "5", "5"}, {0x16, "6", "6"}, {0x1A, "7", "7"}, {0x1C, "8", "8"}, {0x19, "9", "9"},
    {0x1B, "-", "-"}, {0x18, "=", "="}, {0x21, "[", "["}, {0x1E, "]", "]"}, {0x2A, "\\", "\\"},
    {0x29, ";", ";"}, {0x27, "'", "'"}, {0x2B, ",", ","}, {0x2F, ".", "."}, {0x2C, "/", "/"},
    {0x32, "`", "`"},
    {0x30, "tab", "⇥"}, {0x31, "space", "Space"}, {0x24, "return", "↩"}, {0x35, "escape", "⎋"},
    {0x33, "delete", "⌫"}, {0x75, "forward-delete", "⌦"},
    {0x7B, "left", "←"}, {0x7C, "right", "→"}, {0x7E, "up", "↑"}, {0x7D, "down", "↓"},
    {0x73, "home", "↖"}, {0x77, "end", "↘"}, {0x74, "page-up", "⇞"}, {0x79, "page-down", "⇟"},
    {0x7A, "f1", "F1"}, {0x78, "f2", "F2"}, {0x63, "f3", "F3"}, {0x76, "f4", "F4"}, {0x60, "f5", "F5"},
    {0x61, "f6", "F6"}, {0x62, "f7", "F7"}, {0x64, "f8", "F8"}, {0x65, "f9", "F9"}, {0x6D, "f10", "F10"},
    {0x67, "f11", "F11"}, {0x6F, "f12", "F12"}, {0x69, "f13", "F13"}, {0x6B, "f14", "F14"},
    {0x71, "f15", "F15"}, {0x6A, "f16", "F16"}, {0x40, "f17", "F17"}, {0x4F, "f18", "F18"},
    {0x50, "f19", "F19"}, {0x5A, "f20", "F20"},
    // Modifier keys on their own.
    {0x37, "left-cmd", "Left ⌘"}, {0x36, "right-cmd", "Right ⌘"}, {0x3A, "left-opt", "Left ⌥"},
    {0x3D, "right-opt", "Right ⌥"}, {0x3B, "left-ctrl", "Left ⌃"}, {0x3E, "right-ctrl", "Right ⌃"},
    {0x38, "left-shift", "Left ⇧"}, {0x3C, "right-shift", "Right ⇧"}, {0x3F, "fn", "fn"},
};

// Modifier keys and the flag macOS sets while each is down (device-dependent bits tell left from
// right; fn has only its device-independent one).
struct ModifierBit {
    uint16_t key;
    uint64_t flag;
};

constexpr ModifierBit kModifierBits[] = {
    {0x3B, 0x01}, {0x38, 0x02}, {0x3C, 0x04}, {0x37, 0x08}, {0x36, 0x10},
    {0x3A, 0x20}, {0x3D, 0x40}, {0x3E, 0x2000}, {0x3F, 0x800000},
};

uint64_t modifierBit(uint16_t key) {
    for (const auto& m : kModifierBits)
        if (m.key == key) return m.flag;
    return 0;
}

const KeyEntry* findKey(uint16_t code) {
    for (const auto& k : kKeys)
        if (k.code == code) return &k;
    return nullptr;
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

struct ActEntry {
    Act act;
    const char* id;
    const char* name;
};

constexpr ActEntry kActs[] = {
    {Act::None, "none", "Do nothing"},
    {Act::MiddleClick, "middle-click", "Middle click"},
    {Act::Back, "back", "Back"},
    {Act::Forward, "forward", "Forward"},
    {Act::CmdClick, "cmd-click", "⌘-click"},
    {Act::MissionControl, "mission-control", "Mission Control"},
    {Act::AppExpose, "app-expose", "App Exposé"},
    {Act::ShowDesktop, "show-desktop", "Show Desktop"},
    {Act::SwitchApp, "switch-app", "Switch to last app"},
    {Act::Dictation, "dictation", "Dictation"},
    {Act::PlayPause, "play-pause", "Play / Pause"},
    {Act::Mute, "mute", "Mute"},
    {Act::SpaceLeft, "space-left", "Previous desktop"},
    {Act::SpaceRight, "space-right", "Next desktop"},
    {Act::WheelMode, "wheel-mode", "Switch wheel mode"},
    {Act::Gestures, "gestures", "Gestures"},
    {Act::Shortcut, "shortcut", "Keyboard shortcut"},
};

struct WheelEntry {
    WheelAct act;
    const char* id;
    const char* name;
};

constexpr WheelEntry kWheelActs[] = {
    {WheelAct::HorizontalScroll, "horizontal-scroll", "Horizontal scroll"},
    {WheelAct::Volume, "volume", "Volume"},
    {WheelAct::Zoom, "zoom", "Zoom"},
    {WheelAct::Tabs, "tabs", "Switch tabs"},
    {WheelAct::Shortcuts, "shortcuts", "Keyboard shortcuts"},
    {WheelAct::None, "none", "Do nothing"},
};

constexpr const char* kControlKeys[kControlCount] = {"middle", "back", "forward", "thumb", "top"};
constexpr const char* kDirKeys[kDirCount] = {"click", "up", "down", "left", "right"};

int toInt(const std::string& v, int fallback, int lo, int hi) {
    if (v.empty()) return fallback;
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0') return fallback;
    return static_cast<int>(std::clamp<long>(n, lo, hi));
}

bool toBool(const std::string& v, bool fallback) {
    std::string s = lower(v);
    if (s == "on" || s == "yes" || s == "true" || s == "1") return true;
    if (s == "off" || s == "no" || s == "false" || s == "0") return false;
    return fallback;
}

bool allowedOn(Control c, Act a) { return a != Act::Gestures || c == Control::Thumb; }

}  // namespace

// ---------------------------------------------------------------- shortcuts

std::string keyName(uint16_t key) {
    const KeyEntry* k = findKey(key);
    return k ? k->name : "";
}

bool isModifierKey(uint16_t key) { return modifierBit(key) != 0; }

uint16_t ModifierTap::changed(uint16_t key, uint64_t flags) {
    uint64_t bit = modifierBit(key);
    if (!bit) {  // Caps Lock and the like
        candidate_ = kNone;
        return kNone;
    }
    if (flags & bit) {  // went down: a candidate only while it is the one modifier held
        candidate_ = single(flags) == key ? key : kNone;
        return kNone;
    }
    // Went up: a lone press if it was the candidate and no modifier is held any more.
    bool held = false;
    for (const auto& m : kModifierBits) held = held || (flags & m.flag);
    bool lone = candidate_ == key && !held;
    candidate_ = kNone;
    return lone ? key : kNone;
}

uint16_t ModifierTap::single(uint64_t flags) {
    uint16_t found = kNone;
    for (const auto& m : kModifierBits) {
        if (!(flags & m.flag)) continue;
        if (found != kNone) return kNone;
        found = m.key;
    }
    return found;
}

Shortcut parseShortcut(const std::string& text) {
    Shortcut s;
    std::string t = lower(trim(text));
    if (t.empty()) return s;
    // Split on '+', but a trailing "+" segment would be empty: "+" is not a key we support.
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t p = t.find('+', start);
        parts.push_back(trim(t.substr(start, p == std::string::npos ? std::string::npos : p - start)));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    uint32_t mods = 0;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        const std::string& m = parts[i];
        if (m == "cmd" || m == "command") mods |= kCmd;
        else if (m == "shift") mods |= kShift;
        else if (m == "opt" || m == "option" || m == "alt") mods |= kOpt;
        else if (m == "ctrl" || m == "control") mods |= kCtrl;
        else if (m == "fn") mods |= kFn;
        else return {};
    }
    const std::string& keyPart = parts.back();
    for (const auto& k : kKeys) {
        if (keyPart == k.name) {
            if (mods && isModifierKey(k.code)) return {};  // a modifier key stands alone
            s.key = k.code;
            s.mods = mods;
            return s;
        }
    }
    return {};
}

std::string shortcutSpec(const Shortcut& s) {
    if (!s.valid()) return "";
    const KeyEntry* k = findKey(s.key);
    if (!k) return "";
    std::string out;
    if (s.mods & kFn) out += "fn+";
    if (s.mods & kCtrl) out += "ctrl+";
    if (s.mods & kOpt) out += "opt+";
    if (s.mods & kShift) out += "shift+";
    if (s.mods & kCmd) out += "cmd+";
    return out + k->name;
}

std::string shortcutDisplay(const Shortcut& s) {
    if (!s.valid()) return "";
    const KeyEntry* k = findKey(s.key);
    if (!k) return "";
    std::string out;
    if (s.mods & kFn) out += "fn ";
    if (s.mods & kCtrl) out += "⌃";
    if (s.mods & kOpt) out += "⌥";
    if (s.mods & kShift) out += "⇧";
    if (s.mods & kCmd) out += "⌘";
    return out + k->display;
}

// ---------------------------------------------------------------- actions

const char* actId(Act a) {
    for (const auto& e : kActs)
        if (e.act == a) return e.id;
    return "none";
}

const char* actName(Act a) {
    for (const auto& e : kActs)
        if (e.act == a) return e.name;
    return "Do nothing";
}

bool parseAct(const std::string& id, Act& out) {
    std::string s = lower(trim(id));
    for (const auto& e : kActs) {
        if (s == e.id) {
            out = e.act;
            return true;
        }
    }
    return false;
}

const char* wheelActId(WheelAct a) {
    for (const auto& e : kWheelActs)
        if (e.act == a) return e.id;
    return "none";
}

const char* wheelActName(WheelAct a) {
    for (const auto& e : kWheelActs)
        if (e.act == a) return e.name;
    return "Do nothing";
}

bool parseWheelAct(const std::string& id, WheelAct& out) {
    std::string s = lower(trim(id));
    for (const auto& e : kWheelActs) {
        if (s == e.id) {
            out = e.act;
            return true;
        }
    }
    return false;
}

const char* controlName(Control c) {
    switch (c) {
        case Control::Middle: return "Middle button";
        case Control::Back: return "Back button";
        case Control::Forward: return "Forward button";
        case Control::Thumb: return "Thumb button";
        case Control::Top: return "Top button";
    }
    return "";
}

const char* dirName(Dir d) {
    switch (d) {
        case Dir::Click: return "Click";
        case Dir::Up: return "Up";
        case Dir::Down: return "Down";
        case Dir::Left: return "Left";
        case Dir::Right: return "Right";
    }
    return "";
}

std::vector<Act> buttonMenu(Control c) {
    std::vector<Act> m = {Act::MiddleClick, Act::Back,       Act::Forward,     Act::CmdClick, Act::None,
                          Act::MissionControl, Act::AppExpose, Act::ShowDesktop, Act::SwitchApp,
                          Act::Dictation, Act::None,         Act::PlayPause,   Act::Mute,     Act::None,
                          Act::WheelMode};
    if (c == Control::Thumb) m.push_back(Act::Gestures);
    m.push_back(Act::Shortcut);
    return m;
}

std::vector<Act> gestureMenu() {
    return {Act::MissionControl, Act::AppExpose, Act::ShowDesktop, Act::SpaceLeft, Act::SpaceRight, Act::None,
            Act::Back,           Act::Forward,   Act::SwitchApp,   Act::Dictation, Act::PlayPause};
}

std::vector<WheelAct> wheelMenu() {
    return {WheelAct::HorizontalScroll, WheelAct::Volume, WheelAct::Zoom, WheelAct::Tabs, WheelAct::Shortcuts};
}

// ---------------------------------------------------------------- numbers

int sensitivityToThreshold(int sensitivity) {
    sensitivity = std::clamp(sensitivity, 0, 100);
    int t = static_cast<int>(std::lround((100 - sensitivity) * 0.6));
    return std::clamp(t, 1, 60);
}

double dpiToPosition(int dpi, int minDpi, int maxDpi) {
    if (minDpi <= 0 || maxDpi <= minDpi) return 0.5;
    dpi = std::clamp(dpi, minDpi, maxDpi);
    return (std::log(double(dpi)) - std::log(double(minDpi))) / (std::log(double(maxDpi)) - std::log(double(minDpi)));
}

int positionToDpi(double pos, int minDpi, int maxDpi, int step) {
    if (minDpi <= 0 || maxDpi <= minDpi) return minDpi;
    pos = std::clamp(pos, 0.0, 1.0);
    double v = std::exp(std::log(double(minDpi)) + pos * (std::log(double(maxDpi)) - std::log(double(minDpi))));
    if (step <= 0) step = 1;
    long snapped = minDpi + std::lround((v - minDpi) / step) * step;
    return static_cast<int>(std::clamp<long>(snapped, minDpi, maxDpi));
}

// ---------------------------------------------------------------- file format

namespace {

constexpr uint16_t kKeyRightCommand = 0x36;

// Keys of a layout ("middle", "middle.keys", "gesture.up", "thumbwheel", "thumbwheel.up", ...).
// legacy[i]: control i is set with 1.0.4's "right-command" action, whose key wins over ".keys".
bool parseLayoutKey(Layout& l, const std::string& key, const std::string& val, bool* legacy) {
    for (int i = 0; i < kControlCount; i++) {
        std::string base = kControlKeys[i];
        if (key == base) {
            Act a;
            if (lower(val) == "right-command") {  // now simply a shortcut of that one key
                l.buttons[i] = {Act::Shortcut, {kKeyRightCommand, 0}};
                legacy[i] = true;
            } else if (parseAct(val, a) && allowedOn(static_cast<Control>(i), a)) {
                l.buttons[i].act = a;
                legacy[i] = false;
            }
            return true;
        }
        if (key == base + ".keys") {
            if (!legacy[i]) l.buttons[i].keys = parseShortcut(val);
            return true;
        }
    }
    for (int i = 0; i < kDirCount; i++) {
        if (key == std::string("gesture.") + kDirKeys[i]) {
            Act a;
            // A gesture happens at one moment: no shortcut of its own, nothing to hold.
            if (parseAct(val, a) && a != Act::Gestures && a != Act::Shortcut) l.gestures[i] = a;
            return true;
        }
    }
    if (key == "thumbwheel") {
        WheelAct w;
        if (parseWheelAct(val, w)) l.thumbWheel = w;
        return true;
    }
    if (key == "thumbwheel.up") {
        l.thumbUp = parseShortcut(val);
        return true;
    }
    if (key == "thumbwheel.down") {
        l.thumbDown = parseShortcut(val);
        return true;
    }
    return false;
}

std::string oneLine(std::string s) {
    for (auto& ch : s)
        if (ch == '\n' || ch == '\r') ch = ' ';
    return trim(s);
}

void writeLayout(std::string& o, const Layout& l) {
    auto line = [&o](const std::string& k, const std::string& v) { o += k + " = " + v + "\n"; };
    for (int i = 0; i < kControlCount; i++) {
        line(kControlKeys[i], actId(l.buttons[i].act));
        if (l.buttons[i].keys.valid()) line(std::string(kControlKeys[i]) + ".keys", shortcutSpec(l.buttons[i].keys));
    }
    for (int i = 0; i < kDirCount; i++) line(std::string("gesture.") + kDirKeys[i], actId(l.gestures[i]));
    line("thumbwheel", wheelActId(l.thumbWheel));
    if (l.thumbUp.valid()) line("thumbwheel.up", shortcutSpec(l.thumbUp));
    if (l.thumbDown.valid()) line("thumbwheel.down", shortcutSpec(l.thumbDown));
}

}  // namespace

const Layout& layoutFor(const Config& c, const std::string& bundleId) {
    if (!bundleId.empty())
        for (const auto& a : c.apps)
            if (a.bundleId == bundleId) return a;
    return c;
}

// The layout and settings for all apps come first; each app's own layout follows in a section:
//   [app com.example.App]
//   name = Example
//   middle = back
Config parseConfig(const std::string& text) {
    Config c;
    int app = -1;     // index of the app whose section we are in, -1 before any section
    bool skip = false;  // inside a section of a kind we do not know
    bool legacy[kControlCount] = {};
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            std::fill(std::begin(legacy), std::end(legacy), false);
            std::string inner = line.back() == ']' ? trim(line.substr(1, line.size() - 2)) : "";
            size_t sp = inner.find(' ');
            std::string kind = lower(inner.substr(0, sp)), id = sp == std::string::npos ? "" : trim(inner.substr(sp + 1));
            skip = kind != "app" || id.empty();
            if (skip) continue;
            app = -1;
            for (size_t i = 0; i < c.apps.size(); i++)
                if (c.apps[i].bundleId == id) app = static_cast<int>(i);
            if (app < 0) {
                AppLayout a;
                a.bundleId = id;
                a.name = id;
                c.apps.push_back(a);
                app = static_cast<int>(c.apps.size()) - 1;
            }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || skip) continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));

        Layout& target = app < 0 ? static_cast<Layout&>(c) : c.apps[static_cast<size_t>(app)];
        if (parseLayoutKey(target, key, val, legacy)) continue;
        if (app >= 0) {  // an app's section holds its layout and its name, nothing else
            if (key == "name" && !val.empty()) c.apps[static_cast<size_t>(app)].name = val;
            continue;
        }
        if (key == "thumbwheel.speed") {
            c.thumbSpeed = toInt(val, c.thumbSpeed, 0, 100);
        } else if (key == "thumbwheel.direction") {
            std::string v = lower(val);
            if (v == "inverted") c.thumbInverted = true;
            else if (v == "default") c.thumbInverted = false;
        } else if (key == "pointer.dpi") {
            c.dpi = toInt(val, c.dpi, 100, 25600);
        } else if (key == "scroll.speed") {
            c.scrollSpeed = toInt(val, c.scrollSpeed, 0, 100);
        } else if (key == "scroll.direction") {
            std::string v = lower(val);
            if (v == "natural") c.naturalScroll = true;
            else if (v == "standard") c.naturalScroll = false;
        } else if (key == "scroll.smooth") {
            c.smoothScroll = toBool(val, c.smoothScroll);
        } else if (key == "scroll.mode") {
            std::string v = lower(val);
            if (v == "auto") c.wheelMode = WheelMode::Auto;
            else if (v == "ratchet") c.wheelMode = WheelMode::Ratchet;
            else if (v == "freespin" || v == "free-spin") c.wheelMode = WheelMode::FreeSpin;
        } else if (key == "scroll.sensitivity") {
            c.sensitivity = toInt(val, c.sensitivity, 0, 100);
        } else if (key == "updates") {
            c.autoUpdate = toBool(val, c.autoUpdate);
        }
    }
    return c;
}

std::string serializeConfig(const Config& c) {
    std::string o = "# Mouse settings. Edit in the Mouse app, or by hand (then reopen Mouse).\n";
    auto line = [&o](const std::string& k, const std::string& v) { o += k + " = " + v + "\n"; };
    writeLayout(o, c);
    line("thumbwheel.speed", std::to_string(c.thumbSpeed));
    line("thumbwheel.direction", c.thumbInverted ? "inverted" : "default");
    line("pointer.dpi", std::to_string(c.dpi));
    line("scroll.speed", std::to_string(c.scrollSpeed));
    line("scroll.direction", c.naturalScroll ? "natural" : "standard");
    line("scroll.smooth", c.smoothScroll ? "on" : "off");
    line("scroll.mode", c.wheelMode == WheelMode::Auto ? "auto" : c.wheelMode == WheelMode::Ratchet ? "ratchet" : "freespin");
    line("scroll.sensitivity", std::to_string(c.sensitivity));
    line("updates", c.autoUpdate ? "on" : "off");
    for (const auto& a : c.apps) {
        std::string id = oneLine(a.bundleId);
        if (id.empty() || id.find(' ') != std::string::npos || id.find(']') != std::string::npos) continue;
        o += "\n[app " + id + "]\n";
        line("name", oneLine(a.name).empty() ? id : oneLine(a.name));
        writeLayout(o, a);
    }
    return o;
}

}  // namespace mouse
