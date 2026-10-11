#include "device.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace mouse {
using namespace hidpp;

namespace {

constexpr uint16_t kCids[kControlCount] = {kCidMiddle, kCidBack, kCidForward, kCidGesture, kCidModeShift};

// HiResWheel getWheelCapability flag: direction can be inverted in firmware.
constexpr uint8_t kWheelCanInvert = 0x08;
// Ratchet modes of the SmartShift features.
constexpr uint8_t kFreeSpin = 1, kRatchet = 2;
constexpr uint8_t kNeverDisengage = 0xFF;

// Virtual key codes used by built-in actions.
constexpr uint16_t kKeyTab = 0x30, kKeyLeft = 0x7B, kKeyRight = 0x7C, kKeyEqual = 0x18, kKeyMinus = 0x1B;

}  // namespace

int controlForCid(uint16_t cid) {
    for (int i = 0; i < kControlCount; i++)
        if (kCids[i] == cid) return i;
    return -1;
}

uint16_t cidForControl(Control c) { return kCids[static_cast<int>(c)]; }

bool isNative(Control c, Act a) {
    switch (c) {
        case Control::Middle: return a == Act::MiddleClick;
        case Control::Back: return a == Act::Back;
        case Control::Forward: return a == Act::Forward;
        case Control::Top: return a == Act::WheelMode;
        case Control::Thumb: return false;
    }
    return false;
}

Device::Device(Transport& transport, Output& output, uint8_t index) : t_(transport), out_(output), index_(index) {}

// ---------------------------------------------------------------- requests

bool Device::request(uint8_t fi, uint8_t fn, std::initializer_list<uint8_t> params, Report* reply, int timeoutMs) {
    uint8_t buf[kParamSize] = {};
    size_t n = 0;
    for (uint8_t v : params)
        if (n < kParamSize) buf[n++] = v;
    Report req = makeRequest(index_, fi, fn, buf, n);
    if (!t_.write(req)) {
        lastError_ = -1;
        return false;
    }
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        long left = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count());
        Report in;
        if (left <= 0 || !t_.read(in, static_cast<int>(left))) {
            lastError_ = -2;
            return false;
        }
        if (isReplyTo(in, req)) {
            if (in.isError()) {
                lastError_ = in.b[2] == kErrorFeature ? in.errorCode() : in.b[5];
                return false;
            }
            lastError_ = 0;
            if (reply) *reply = in;
            return true;
        }
        // Not our reply: keep device notifications for later, drop replies meant for other software.
        if (!in.isError() && in.software() == 0 && pending_.size() < 256) pending_.push_back(in);
    }
}

uint8_t Device::featureIndex(uint16_t id) {
    Report r;
    if (!request(0, 0, {static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id & 0xFF)}, &r)) return 0;
    return r.p(0);
}

// ---------------------------------------------------------------- discovery

bool Device::start() {
    started_ = false;
    Report r;
    // Root.getProtocolVersion doubles as a ping; a sleeping mouse may need a second try.
    if (!request(0, 1, {0, 0, 0x5A}, &r, 2000) && !request(0, 1, {0, 0, 0x5A}, &r, 2000)) return false;
    if (r.p(0) < 2 || r.p(2) != 0x5A) return false;  // HID++ 1.0 only, or a garbled answer

    idx_ = Indexes{};
    idx_.name = featureIndex(kDeviceName);
    idx_.battery = featureIndex(kUnifiedBattery);
    idx_.reprog = featureIndex(kReprogControls);
    idx_.wireless = featureIndex(kWirelessStatus);
    idx_.smartShift = featureIndex(kSmartShiftEnhanced);
    idx_.smartShiftEnhanced = idx_.smartShift != 0;
    if (!idx_.smartShift) idx_.smartShift = featureIndex(kSmartShift);
    idx_.hiRes = featureIndex(kHiResWheel);
    idx_.thumb = featureIndex(kThumbwheel);
    idx_.dpi = featureIndex(kAdjustableDpi);
    if (!idx_.reprog && !idx_.dpi) return false;

    readName();
    readBattery();
    readDpi();
    readControls();
    readWheel();
    readThumbwheel();

    for (auto& f : reporting_) f = 0xFF;
    for (auto& p : pressed_) p = 0;
    for (auto& h : held_) h = false;
    applied_ = false;
    reapply_ = false;
    started_ = true;
    return true;
}

void Device::readName() {
    if (!idx_.name) return;
    Report r;
    if (!request(idx_.name, 0, {}, &r)) return;
    int len = std::min<int>(r.p(0), 48);
    std::string name;
    for (int i = 0; i < len; i += static_cast<int>(kParamSize)) {
        if (!request(idx_.name, 1, {static_cast<uint8_t>(i)}, &r)) break;
        for (size_t j = 0; j < kParamSize && static_cast<int>(name.size()) < len; j++) {
            char ch = static_cast<char>(r.p(j));
            if (ch == '\0') break;
            if (ch >= 0x20 && ch < 0x7F) name += ch;
        }
    }
    if (!name.empty()) info_.name = name;
}

void Device::readBattery() {
    if (!idx_.battery) return;
    Report r;
    if (!request(idx_.battery, 1, {}, &r)) return;
    onBattery(r);
}

void Device::readDpi() {
    if (!idx_.dpi) return;
    Report r;
    if (request(idx_.dpi, 1, {0}, &r)) {
        // [sensor][u16 values...0]; "a, 0xE000|step, b" describes a range.
        int values[7], n = 0;
        for (size_t i = 1; i + 1 < kParamSize && n < 7; i += 2) {
            int v = r.u16(i);
            if (v == 0) break;
            values[n++] = v;
        }
        if (n >= 3 && (values[1] & 0xE000) == 0xE000 && values[0] > 0 && values[2] > values[0]) {
            info_.dpiMin = values[0];
            info_.dpiStep = std::max(1, values[1] & 0x1FFF);
            info_.dpiMax = values[2];
        } else if (n >= 1) {
            info_.dpiMin = values[0];
            info_.dpiMax = values[n - 1];
            info_.dpiStep = n >= 2 ? std::max(1, values[1] - values[0]) : 50;
        }
    }
    if (request(idx_.dpi, 2, {0}, &r)) info_.dpi = r.u16(1);
}

void Device::readControls() {
    divertable_ = 0;
    if (!idx_.reprog) return;
    Report r;
    if (!request(idx_.reprog, 0, {}, &r)) return;
    int count = std::min<int>(r.p(0), 32);
    for (int i = 0; i < count; i++) {
        if (!request(idx_.reprog, 1, {static_cast<uint8_t>(i)}, &r)) continue;
        int c = controlForCid(r.u16(0));
        if (c >= 0 && (r.p(4) & 0x20)) divertable_ |= static_cast<uint16_t>(1u << c);
    }
}

void Device::readWheel() {
    Report r;
    if (idx_.hiRes) {
        if (request(idx_.hiRes, 0, {}, &r)) {
            if (r.p(0) > 0) info_.wheelMultiplier = r.p(0);
            wheelInvertable_ = (r.p(1) & kWheelCanInvert) != 0;
        }
        wheelFlags_ = request(idx_.hiRes, 1, {}, &r) ? r.p(0) & 0x07 : -1;
    }
    if (idx_.smartShift) {
        if (request(idx_.smartShift, idx_.smartShiftEnhanced ? 1 : 0, {}, &r)) {
            smartMode_ = r.p(0);
            smartThreshold_ = r.p(1);
        }
    }
}

void Device::readThumbwheel() {
    if (!idx_.thumb) return;
    Report r;
    if (request(idx_.thumb, 0, {}, &r)) {
        if (r.u16(2) > 0) info_.thumbResolution = r.u16(2);
        info_.thumbReversedByDefault = r.p(4) != 0;
    }
    thumbFlags_ = request(idx_.thumb, 1, {}, &r) ? ((r.p(0) & 1) | ((r.p(1) & 1) << 1)) : -1;
}

// ---------------------------------------------------------------- settings

void Device::apply(const Config& cfg, bool canPost, bool systemNatural) {
    if (!started_) return;
    // Let go of anything synthesised for a held button before its meaning changes.
    releaseAll();

    cfg_ = cfg;
    canPost_ = canPost;
    systemNatural_ = systemNatural;
    reapply_ = false;
    Report r;

    if (idx_.dpi) setDpi(cfg.dpi);
    if (idx_.smartShift) setWheelMode(cfg.wheelMode, cfg.sensitivity);

    if (idx_.hiRes) {
        bool divert = canPost && (cfg.smoothScroll || cfg.scrollSpeed != 50);
        bool hires = divert && cfg.smoothScroll;
        bool invert = !divert && wheelInvertable_ && cfg.naturalScroll != systemNatural;
        int flags = (divert ? kWheelDivert : 0) | (hires ? kWheelHiRes : 0) | (invert ? kWheelInvert : 0);
        if (flags != wheelFlags_ || !applied_)
            wheelFlags_ = request(idx_.hiRes, 2, {static_cast<uint8_t>(flags)}, &r) ? flags : -1;
        wheelDiverted_ = divert && wheelFlags_ == flags;
        wheelHiRes_ = hires;
        scroll_.reset();
    }

    if (idx_.thumb) {
        bool native = cfg.thumbWheel == WheelAct::HorizontalScroll && cfg.thumbSpeed == 50;
        bool divert = !native && (canPost || cfg.thumbWheel == WheelAct::None);
        bool invert = !divert && cfg.thumbInverted;
        int flags = (divert ? 1 : 0) | (invert ? 2 : 0);
        if (flags != thumbFlags_ || !applied_)
            thumbFlags_ = request(idx_.thumb, 2, {static_cast<uint8_t>(divert), static_cast<uint8_t>(invert)}, &r) ? flags : -1;
        thumbDiverted_ = divert && thumbFlags_ == flags;
        stepper_.reset();
        thumbScroll_.reset();
    }

    if (idx_.reprog) {
        for (int i = 0; i < kControlCount; i++) {
            Control c = static_cast<Control>(i);
            Act a = cfg.button(c).act;
            // Actions that only talk to the mouse (or do nothing) work without Accessibility.
            bool needsPost = a != Act::None && a != Act::WheelMode;
            bool divert = !isNative(c, a) && (divertable_ & (1u << i)) && (canPost || !needsPost);
            bool rawXY = divert && c == Control::Thumb && a == Act::Gestures;
            setReporting(kCids[i], divert, rawXY);
        }
    }
    applied_ = true;
}

void Device::setDpi(int dpi) {
    int lo = info_.dpiMin, hi = std::max(info_.dpiMax, lo), step = std::max(info_.dpiStep, 1);
    int v = std::clamp(dpi, lo, hi);
    v = lo + static_cast<int>(std::lround(double(v - lo) / step)) * step;
    v = std::clamp(v, lo, hi);
    if (v == info_.dpi && applied_) return;
    Report r;
    if (request(idx_.dpi, 3, {0, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF)}, &r)) info_.dpi = v;
}

void Device::setWheelMode(WheelMode mode, int sensitivity) {
    uint8_t m = mode == WheelMode::FreeSpin ? kFreeSpin : kRatchet;
    uint8_t threshold = mode == WheelMode::Auto ? static_cast<uint8_t>(sensitivityToThreshold(sensitivity))
                        : mode == WheelMode::Ratchet ? kNeverDisengage
                                                     : 0;  // 0 = keep
    if (applied_ && smartMode_ == m && (threshold == 0 || smartThreshold_ == threshold)) return;
    Report r;
    if (request(idx_.smartShift, idx_.smartShiftEnhanced ? 2 : 1, {m, threshold, 0}, &r)) {
        smartMode_ = m;
        if (threshold) smartThreshold_ = threshold;
    }
}

void Device::toggleWheelMode() {
    if (!idx_.smartShift) return;
    Report r;
    if (request(idx_.smartShift, idx_.smartShiftEnhanced ? 1 : 0, {}, &r)) smartMode_ = r.p(0);
    uint8_t next = smartMode_ == kRatchet ? kFreeSpin : kRatchet;
    if (request(idx_.smartShift, idx_.smartShiftEnhanced ? 2 : 1, {next, 0, 0}, &r)) smartMode_ = next;
}

void Device::setReporting(uint16_t cid, bool divert, bool rawXY) {
    int c = controlForCid(cid);
    if (c < 0) return;
    uint8_t want = static_cast<uint8_t>((divert ? kDivert : 0) | (rawXY ? kRawXY : 0));
    if (reporting_[c] == want) return;
    uint8_t flags = static_cast<uint8_t>(kDivertValid | kRawXYValid | want);
    Report r;
    bool ok = request(idx_.reprog, 3, {static_cast<uint8_t>(cid >> 8), static_cast<uint8_t>(cid & 0xFF), flags, 0, 0}, &r);
    if (!ok && rawXY) {
        // Older firmware may refuse raw XY; gestures then fall back to the click action.
        flags = static_cast<uint8_t>(kDivertValid | kRawXYValid | (divert ? kDivert : 0));
        want = static_cast<uint8_t>(divert ? kDivert : 0);
        ok = request(idx_.reprog, 3, {static_cast<uint8_t>(cid >> 8), static_cast<uint8_t>(cid & 0xFF), flags, 0, 0}, &r);
    }
    reporting_[c] = ok ? want : 0xFF;
}

void Device::restore() {
    if (!started_) return;
    releaseAll();
    Report r;
    if (idx_.reprog)
        for (int i = 0; i < kControlCount; i++)
            if (reporting_[i] != 0) setReporting(kCids[i], false, false);
    if (idx_.hiRes && wheelFlags_ != -1 && (wheelFlags_ & (kWheelDivert | kWheelHiRes))) {
        int flags = wheelFlags_ & kWheelInvert;
        if (applied_) flags = (wheelInvertable_ && cfg_.naturalScroll != systemNatural_) ? kWheelInvert : 0;
        wheelFlags_ = request(idx_.hiRes, 2, {static_cast<uint8_t>(flags)}, &r) ? flags : -1;
    }
    if (idx_.thumb && thumbFlags_ != -1 && (thumbFlags_ & 1)) {
        bool invert = applied_ && cfg_.thumbInverted;
        thumbFlags_ = request(idx_.thumb, 2, {0, static_cast<uint8_t>(invert)}, &r) ? (invert ? 2 : 0) : -1;
    }
    wheelDiverted_ = thumbDiverted_ = false;
}

// ---------------------------------------------------------------- events

void Device::processPending() {
    // handle() may issue requests, which can queue more notifications; the loop drains those too.
    for (int guard = 0; guard < 512 && !pending_.empty(); guard++) {
        Report r = pending_.front();
        pending_.pop_front();
        handle(r);
    }
}

void Device::handle(const Report& r) {
    if (!started_ || r.isError() || r.software() != 0) return;
    if (r.device() != index_ && !(index_ == kDeviceDirect && r.device() == 0)) return;
    uint8_t f = r.feature(), ev = r.function();
    if (f == 0) return;
    if (f == idx_.reprog) {
        if (ev == 0) onButtons(r);
        else if (ev == 1) onRawXY(r.i16(0), r.i16(2));
    } else if (f == idx_.hiRes) {
        if (ev == 0) onWheel(r);
    } else if (f == idx_.thumb) {
        if (ev == 0) onThumbwheel(r);
    } else if (f == idx_.battery) {
        if (ev == 0) onBattery(r);
    } else if (f == idx_.wireless) {
        if (ev == 0) forgetDeviceState();  // reconnected: the firmware forgot our settings
    }
}

void Device::releaseAll() {
    for (int i = 0; i < kControlCount; i++) {
        if (!held_[i]) continue;
        held_[i] = false;
        if (heldAct_[i] == Act::MiddleClick) out_.button(kButtonMiddle, false, 0);
        else if (heldAct_[i] == Act::CmdClick) out_.button(kButtonLeft, false, kCmd);
    }
    gesture_.end();  // a gesture in progress is dropped, never turned into a click
}

Device::~Device() { releaseAll(); }

void Device::forgetDeviceState() {
    releaseAll();
    for (auto& f : reporting_) f = 0xFF;
    for (auto& p : pressed_) p = 0;
    wheelFlags_ = thumbFlags_ = -1;
    wheelDiverted_ = thumbDiverted_ = false;
    applied_ = false;
    reapply_ = true;
}

void Device::onButtons(const Report& r) {
    uint16_t now[4];
    for (int i = 0; i < 4; i++) now[i] = r.u16(static_cast<size_t>(i) * 2);
    auto contains = [](const uint16_t* list, uint16_t cid) {
        for (int i = 0; i < 4; i++)
            if (list[i] == cid) return true;
        return false;
    };
    for (int i = 0; i < 4; i++) {
        if (pressed_[i] && !contains(now, pressed_[i])) {
            int c = controlForCid(pressed_[i]);
            if (c >= 0) onRelease(static_cast<Control>(c));
        }
    }
    uint16_t before[4];
    std::copy(pressed_, pressed_ + 4, before);
    std::copy(now, now + 4, pressed_);
    for (int i = 0; i < 4; i++) {
        if (now[i] && !contains(before, now[i])) {
            int c = controlForCid(now[i]);
            // Only act on controls we diverted (another app might have diverted others).
            if (c >= 0 && reporting_[c] != 0xFF && (reporting_[c] & kDivert)) onPress(static_cast<Control>(c));
        }
    }
}

void Device::onPress(Control c) {
    int i = static_cast<int>(c);
    const Binding& b = cfg_.button(c);
    held_[i] = true;
    heldAct_[i] = b.act;
    switch (b.act) {
        case Act::Gestures:
            gesture_.begin(std::max(40, static_cast<int>((info_.dpi > 0 ? info_.dpi : 1000) * 0.08)));
            break;
        case Act::MiddleClick: out_.button(kButtonMiddle, true, 0); break;
        case Act::CmdClick: out_.button(kButtonLeft, true, kCmd); break;
        default: perform(b.act, b.keys); break;
    }
}

void Device::onRelease(Control c) {
    int i = static_cast<int>(c);
    if (!held_[i]) return;
    held_[i] = false;
    switch (heldAct_[i]) {
        case Act::Gestures:
            if (gesture_.end() == GestureTracker::kClick) perform(cfg_.gestures[static_cast<int>(Dir::Click)], {});
            break;
        case Act::MiddleClick: out_.button(kButtonMiddle, false, 0); break;
        case Act::CmdClick: out_.button(kButtonLeft, false, kCmd); break;
        default: break;
    }
}

void Device::perform(Act a, const Shortcut& keys) {
    switch (a) {
        case Act::None:
        case Act::Gestures: break;
        case Act::MiddleClick:
            out_.button(kButtonMiddle, true, 0);
            out_.button(kButtonMiddle, false, 0);
            break;
        case Act::CmdClick:
            out_.button(kButtonLeft, true, kCmd);
            out_.button(kButtonLeft, false, kCmd);
            break;
        case Act::Back: out_.navigate(false); break;
        case Act::Forward: out_.navigate(true); break;
        case Act::MissionControl: out_.dock(Dock::MissionControl); break;
        case Act::AppExpose: out_.dock(Dock::AppExpose); break;
        case Act::ShowDesktop: out_.dock(Dock::ShowDesktop); break;
        case Act::SwitchApp: out_.key({kKeyTab, kCmd}); break;
        case Act::Dictation: out_.dictation(); break;
        case Act::PlayPause: out_.media(kPlayPause); break;
        case Act::Mute: out_.media(kMute); break;
        case Act::SpaceLeft: out_.key({kKeyLeft, kCtrl}); break;
        case Act::SpaceRight: out_.key({kKeyRight, kCtrl}); break;
        case Act::WheelMode: toggleWheelMode(); break;
        case Act::Shortcut:
            if (keys.valid()) out_.key(keys);
            break;
    }
}

void Device::onRawXY(int dx, int dy) {
    if (!gesture_.active()) return;
    Dir d;
    switch (gesture_.move(dx, dy)) {
        case GestureTracker::kUp: d = Dir::Up; break;
        case GestureTracker::kDown: d = Dir::Down; break;
        case GestureTracker::kLeft: d = Dir::Left; break;
        case GestureTracker::kRight: d = Dir::Right; break;
        default: return;
    }
    perform(cfg_.gestures[static_cast<int>(d)], {});
}

void Device::onWheel(const Report& r) {
    if (!wheelDiverted_) return;
    bool hires = (r.p(0) & 0x10) != 0;
    int delta = r.i16(1);
    if (delta == 0) return;
    double notches = double(delta) / (hires ? std::max(1, info_.wheelMultiplier) : 1);
    // A wheel turned away from you scrolls up ("standard"); natural scrolling flips it.
    if (cfg_.naturalScroll) notches = -notches;
    int amount = scroll_.feed(notches, out_.now(), cfg_.scrollSpeed, wheelHiRes_);
    if (amount) out_.scroll(amount, 0, wheelHiRes_);
}

void Device::onThumbwheel(const Report& r) {
    if (!thumbDiverted_) return;
    int rot = r.i16(0);
    if (rot == 0) return;
    if (cfg_.thumbInverted) rot = -rot;
    double now = out_.now();
    if (cfg_.thumbWheel == WheelAct::HorizontalScroll) {
        // Same direction as the firmware's own horizontal scrolling (which macOS may flip).
        double eighths = (systemNatural_ ? rot : -rot) / (info_.thumbResolution / 8.0);
        int px = thumbScroll_.feed(eighths, now, cfg_.thumbSpeed, true);
        if (px) out_.scroll(0, px, true);
        return;
    }
    double perStep = (info_.thumbResolution / 8.0) / speedFactor(cfg_.thumbSpeed);
    double minInterval = cfg_.thumbWheel == WheelAct::Shortcuts ? 0.1 : 0.0;
    int step = stepper_.feed(rot, perStep, now, minInterval);
    if (!step) return;
    bool up = step > 0;
    switch (cfg_.thumbWheel) {
        case WheelAct::Volume: out_.media(up ? kVolumeUp : kVolumeDown); break;
        case WheelAct::Zoom: out_.key({up ? kKeyEqual : kKeyMinus, kCmd}); break;
        case WheelAct::Tabs: out_.key(up ? Shortcut{kKeyTab, kCtrl} : Shortcut{kKeyTab, kCtrl | kShift}); break;
        case WheelAct::Shortcuts: {
            const Shortcut& s = up ? cfg_.thumbUp : cfg_.thumbDown;
            if (s.valid()) out_.key(s);
            break;
        }
        default: break;
    }
}

void Device::onBattery(const Report& r) {
    int pct = r.p(0) <= 100 ? r.p(0) : -1;
    bool charging = r.p(2) == 1 || r.p(2) == 2;
    if (pct == info_.battery && charging == info_.charging) return;
    info_.battery = pct;
    info_.charging = charging;
    if (onInfoChanged) onInfoChanged();
}

}  // namespace mouse
