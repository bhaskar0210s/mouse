#include "mx3s.hpp"

#include <algorithm>
#include <cstdio>

namespace mouse::sim {
using namespace hidpp;

namespace {

struct FeatureEntry {
    uint16_t id;
    uint8_t version;
    uint8_t type;
};

// Feature table read from an MX Master 3S (Bluetooth, firmware RBM 22.01).
constexpr FeatureEntry kFeatures[] = {
    {0x0000, 0, 0},    {0x0001, 2, 0},    {0x0003, 4, 0},    {0x0005, 0, 0},    {0x1D4B, 0, 0},
    {0x0020, 0, 0},    {0x0021, 1, 0},    {0x0007, 0, 0},    {0x1004, 3, 0},    {0x1B04, 5, 0},
    {0x1814, 1, 0},    {0x1815, 2, 0},    {0x2250, 1, 0},    {0x2201, 2, 0},    {0x2110, 0, 0},
    {0x2121, 1, 0},    {0x2150, 0, 0},    {0x2251, 0, 0},    {0x00C3, 0, 0},    {0x1802, 0, 0x60},
    {0x1803, 0, 0x60}, {0x1806, 8, 0x60}, {0x1816, 0, 0x60}, {0x1805, 0, 0x60}, {0x1830, 0, 0x60},
    {0x1891, 7, 0x60}, {0x18A1, 0, 0x60}, {0x1E00, 0, 0x40}, {0x1E02, 0, 0x60}, {0x1602, 0, 0},
    {0x1EB0, 0, 0x60}, {0x1861, 1, 0x60}, {0x9300, 0, 0x60}, {0x9001, 0, 0x60}, {0x1E22, 0, 0x60},
    {0x9205, 0, 0x60},
};
constexpr uint8_t kFeatureCount = sizeof(kFeatures) / sizeof(kFeatures[0]);

constexpr char kName[] = "MX Master 3S For Mac";
constexpr uint8_t kWheelMultiplier = 15;

uint8_t hi(int v) { return static_cast<uint8_t>((v >> 8) & 0xFF); }
uint8_t lo(int v) { return static_cast<uint8_t>(v & 0xFF); }

}  // namespace

MXMaster3S::MXMaster3S() {
    controls = {
        {0x0050, 0x0038, 0x01, 0x02}, {0x0051, 0x0039, 0x01, 0x02}, {0x0052, 0x003A, 0x31, 0x03},
        {0x0053, 0x003C, 0x31, 0x03}, {0x0056, 0x003E, 0x31, 0x03}, {0x00C3, 0x00A9, 0x31, 0x03},
        {0x00C4, 0x009D, 0x31, 0x03}, {0x00D7, 0x00B4, 0xA0, 0x05},
    };
}

MXMaster3S::Ctl* MXMaster3S::control(uint16_t cid) {
    for (auto& c : controls)
        if (c.cid == cid) return &c;
    return nullptr;
}

uint8_t MXMaster3S::indexOf(uint16_t id) const {
    for (uint8_t i = 0; i < kFeatureCount; i++)
        if (kFeatures[i].id == id) return i;
    return 0;
}

uint16_t MXMaster3S::featureAt(uint8_t index) const { return index < kFeatureCount ? kFeatures[index].id : 0xFFFF; }

void MXMaster3S::reply(const Report& req, std::initializer_list<uint8_t> params) {
    Report r;
    r.b[0] = kReportLong;
    r.b[1] = req.device();
    r.b[2] = req.feature();
    r.b[3] = req.b[3];
    size_t i = 4;
    for (uint8_t v : params)
        if (i < kLongSize) r.b[i++] = v;
    outbox.push_back(r);
}

void MXMaster3S::error(const Report& req, uint8_t code) {
    Report r;
    r.b[0] = kReportLong;
    r.b[1] = req.device();
    r.b[2] = kErrorFeature;
    r.b[3] = req.feature();
    r.b[4] = req.b[3];
    r.b[5] = code;
    outbox.push_back(r);
}

void MXMaster3S::event(uint8_t fi, uint8_t ev, std::initializer_list<uint8_t> params) {
    Report r;
    r.b[0] = kReportLong;
    r.b[1] = kDeviceDirect;
    r.b[2] = fi;
    r.b[3] = static_cast<uint8_t>(ev << 4);
    size_t i = 4;
    for (uint8_t v : params)
        if (i < kLongSize) r.b[i++] = v;
    outbox.push_back(r);
}

void MXMaster3S::receive(const Report& req) {
    requestCount++;
    if (silent) return;
    if (req.id() != kReportLong || req.device() != kDeviceDirect) return;  // Bluetooth: long reports only
    if (failNextRequests > 0) {
        failNextRequests--;
        error(req, kErrBusy);
        return;
    }
    uint16_t fid = featureAt(req.feature());
    if (fid == 0xFFFF) return error(req, kErrInvalidFeatureIndex);
    if (brokenFeature && fid == brokenFeature) return error(req, kErrHardware);
    uint8_t fn = req.function();

    switch (fid) {
        case 0x0000:
            if (fn == 0) {
                uint8_t i = indexOf(req.u16(0));
                if (i == 0 && req.u16(0) != 0) return reply(req, {0, 0, 0});
                return reply(req, {i, kFeatures[i].type, kFeatures[i].version});
            }
            if (fn == 1) return reply(req, {4, 5, req.p(2)});
            return error(req, kErrInvalidFunction);

        case 0x0001:
            if (fn == 0) return reply(req, {static_cast<uint8_t>(kFeatureCount - 1)});
            if (fn == 1) {
                uint8_t i = req.p(0);
                if (i >= kFeatureCount) return error(req, kErrInvalidArgument);
                return reply(req, {hi(kFeatures[i].id), lo(kFeatures[i].id), kFeatures[i].type, kFeatures[i].version});
            }
            return error(req, kErrInvalidFunction);

        case 0x0005: {
            constexpr int len = sizeof(kName) - 1;
            if (fn == 0) return reply(req, {static_cast<uint8_t>(len)});
            if (fn == 1) {
                int start = req.p(0);
                if (start >= len) return error(req, kErrInvalidArgument);
                Report r;
                r.b[0] = kReportLong;
                r.b[1] = req.device();
                r.b[2] = req.feature();
                r.b[3] = req.b[3];
                for (int i = 0; i < 16 && start + i < len; i++) r.b[4 + i] = static_cast<uint8_t>(kName[start + i]);
                outbox.push_back(r);
                return;
            }
            if (fn == 2) return reply(req, {3});
            return error(req, kErrInvalidFunction);
        }

        case 0x1004:
            if (fn == 0) return reply(req, {0x0F, 0x02});
            if (fn == 1) {
                uint8_t level = battery >= 50 ? 0x08 : battery >= 20 ? 0x04 : battery >= 5 ? 0x02 : 0x01;
                return reply(req, {static_cast<uint8_t>(battery), level, static_cast<uint8_t>(charging ? 1 : 0),
                                   static_cast<uint8_t>(charging ? 1 : 0)});
            }
            return error(req, kErrInvalidFunction);

        case 0x1B04: {
            if (fn == 0) return reply(req, {static_cast<uint8_t>(controls.size())});
            if (fn == 1) {
                uint8_t i = req.p(0);
                if (i >= controls.size()) return error(req, kErrInvalidArgument);
                const Ctl& c = controls[i];
                uint8_t group = c.cid <= 0x51 ? 1 : (c.flags & 0x80) ? 3 : 2;
                uint8_t gmask = group == 1 ? 0x01 : group == 2 ? 0x03 : 0x00;
                return reply(req, {hi(c.cid), lo(c.cid), hi(c.task), lo(c.task), c.flags, 0, group, gmask, c.add});
            }
            Ctl* c = control(req.u16(0));
            if (fn == 2) {
                if (!c) return error(req, kErrInvalidArgument);
                uint8_t flags = static_cast<uint8_t>((c->divert ? kDivert : 0) | (c->rawXY ? kRawXY : 0));
                return reply(req, {hi(c->cid), lo(c->cid), flags, 0, 0, 0});
            }
            if (fn == 3) {
                if (!c) return error(req, kErrInvalidArgument);
                uint8_t flags = req.p(2);
                bool divert = c->divert, raw = c->rawXY;
                if (flags & kDivertValid) {
                    if ((flags & kDivert) && !(c->flags & 0x20)) return error(req, kErrInvalidArgument);
                    divert = flags & kDivert;
                }
                if (flags & kRawXYValid) {
                    if ((flags & kRawXY) && !(c->add & 0x01)) return error(req, kErrInvalidArgument);
                    raw = flags & kRawXY;
                }
                c->divert = divert;
                c->rawXY = raw;
                if (!divert) held_.erase(std::remove(held_.begin(), held_.end(), c->cid), held_.end());
                return reply(req, {req.p(0), req.p(1), req.p(2), req.p(3), req.p(4)});
            }
            return error(req, kErrInvalidFunction);
        }

        case 0x2201:
            if (fn == 0) return reply(req, {1});
            if (fn == 1) return reply(req, {0, 0x00, 0xC8, 0xE0, 0x32, 0x1F, 0x40, 0x00, 0x00});
            if (fn == 2) return reply(req, {0, hi(dpi), lo(dpi), 0x03, 0xE8});
            if (fn == 3) {
                int v = req.u16(1);
                if (v < 200 || v > 8000 || (v - 200) % 50) return error(req, kErrInvalidArgument);
                dpi = v;
                return reply(req, {0, hi(v), lo(v)});
            }
            return error(req, kErrInvalidFunction);

        case 0x2110:
            if (fn == 0) return reply(req, {ratchetMode, threshold, 10});
            if (fn == 1) {
                uint8_t m = req.p(0), t = req.p(1);
                if (m > 2) return error(req, kErrInvalidArgument);
                if (m) ratchetMode = m;
                if (t) threshold = t;
                return reply(req, {ratchetMode, threshold, 10});
            }
            return error(req, kErrInvalidFunction);

        case 0x2121:
            if (fn == 0) return reply(req, {kWheelMultiplier, 0x0C, 0, 0});
            if (fn == 1) return reply(req, {wheelMode});
            if (fn == 2) {
                wheelMode = req.p(0) & 0x07;
                return reply(req, {wheelMode});
            }
            if (fn == 3) return reply(req, {static_cast<uint8_t>(ratchetMode == 2 ? 1 : 0)});
            return error(req, kErrInvalidFunction);

        case 0x2150:
            if (fn == 0) return reply(req, {0x00, 0x12, 0x00, 0x78, 0x00, 0x0F, 0x03, 0xE8});
            if (fn == 1)
                return reply(req, {static_cast<uint8_t>(thumbDivert), static_cast<uint8_t>(thumbInvert)});
            if (fn == 2) {
                thumbDivert = req.p(0) & 1;
                thumbInvert = req.p(1) & 1;
                return reply(req, {static_cast<uint8_t>(thumbDivert), static_cast<uint8_t>(thumbInvert)});
            }
            return error(req, kErrInvalidFunction);

        default:
            if (fn == 0) return reply(req, {});
            return error(req, kErrInvalidFunction);
    }
}

// ---------------------------------------------------------------- the hand on the mouse

void MXMaster3S::sendHeld() {
    uint16_t c[4] = {};
    for (size_t i = 0; i < held_.size() && i < 4; i++) c[i] = held_[i];
    event(indexOf(0x1B04), 0, {hi(c[0]), lo(c[0]), hi(c[1]), lo(c[1]), hi(c[2]), lo(c[2]), hi(c[3]), lo(c[3])});
}

void MXMaster3S::press(uint16_t cid) {
    Ctl* c = control(cid);
    if (!c) return;
    char buf[48];
    if (c->divert) {
        if (std::find(held_.begin(), held_.end(), cid) == held_.end() && held_.size() < 4) held_.push_back(cid);
        sendHeld();
    } else {
        std::snprintf(buf, sizeof buf, "native down 0x%02X", cid);
        nativeLog.emplace_back(buf);
    }
}

void MXMaster3S::release(uint16_t cid) {
    auto it = std::find(held_.begin(), held_.end(), cid);
    if (it != held_.end()) {
        held_.erase(it);
        sendHeld();
    } else {
        char buf[48];
        std::snprintf(buf, sizeof buf, "native up 0x%02X", cid);
        nativeLog.emplace_back(buf);
    }
}

void MXMaster3S::move(int dx, int dy) {
    bool raw = false;
    for (uint16_t cid : held_)
        if (Ctl* c = control(cid); c && c->rawXY) raw = true;
    if (raw) {
        event(indexOf(0x1B04), 1, {hi(dx), lo(dx), hi(dy), lo(dy)});
    } else {
        nativeLog.push_back("native move " + std::to_string(dx) + "," + std::to_string(dy));
    }
}

void MXMaster3S::turnWheel(int notches) { turnWheelHiRes(notches * kWheelMultiplier); }

void MXMaster3S::turnWheelHiRes(int units) {
    bool invert = wheelMode & kWheelInvert;
    if (wheelMode & kWheelDivert) {
        bool hires = wheelMode & kWheelHiRes;
        int v = hires ? units : units / kWheelMultiplier;
        if (invert) v = -v;
        if (v) event(indexOf(0x2121), 0, {static_cast<uint8_t>((hires ? 0x10 : 0) | 1), hi(v), lo(v)});
    } else {
        int v = units / kWheelMultiplier;
        nativeLog.push_back("native wheel " + std::to_string(invert ? -v : v));
    }
}

void MXMaster3S::turnThumbwheel(int units) {
    int v = thumbInvert ? -units : units;
    if (thumbDivert) {
        ts_ = static_cast<uint16_t>(ts_ + 8);
        event(indexOf(0x2150), 0, {hi(v), lo(v), hi(ts_), lo(ts_), 2, 0});
    } else {
        nativeLog.push_back("native hscroll " + std::to_string(v));
    }
}

void MXMaster3S::setBattery(int percent, bool isCharging) {
    battery = percent;
    charging = isCharging;
    uint8_t level = percent >= 50 ? 0x08 : percent >= 20 ? 0x04 : percent >= 5 ? 0x02 : 0x01;
    event(indexOf(0x1004), 0, {static_cast<uint8_t>(percent), level, static_cast<uint8_t>(isCharging ? 1 : 0),
                                static_cast<uint8_t>(isCharging ? 1 : 0)});
}

void MXMaster3S::powerCycle() {
    for (auto& c : controls) c.divert = c.rawXY = false;
    held_.clear();
    wheelMode = 0;
    thumbDivert = thumbInvert = false;
    dpi = 1000;
    event(indexOf(0x1D4B), 0, {1, 1, 0});
}

// ---------------------------------------------------------------- transport

bool SimTransport::write(const Report& r) {
    writes++;
    mouse.receive(r);
    return true;
}

bool SimTransport::read(Report& out, int) {
    if (mouse.outbox.empty()) return false;
    out = mouse.outbox.front();
    mouse.outbox.pop_front();
    return true;
}

void SimTransport::pump(Device& d) {
    for (int guard = 0; guard < 10000 && !mouse.outbox.empty(); guard++) {
        Report r = mouse.outbox.front();
        mouse.outbox.pop_front();
        d.handle(r);
        d.processPending();
    }
}

}  // namespace mouse::sim
