// Minimal HID++ 2.0 protocol layer: report framing, requests and the feature ids we use.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mouse::hidpp {

constexpr uint8_t kReportShort = 0x10;   // 7 bytes
constexpr uint8_t kReportLong = 0x11;    // 20 bytes (the only one Bluetooth mice accept)
constexpr size_t kLongSize = 20;
constexpr size_t kParamSize = 16;
constexpr uint8_t kDeviceDirect = 0xFF;  // device index of a mouse paired over Bluetooth
constexpr uint8_t kSoftwareId = 0x0D;    // default software id of our requests (never 0: that marks notifications)
constexpr uint8_t kErrorFeature = 0xFF;  // HID++ 2.0 error reply marker
constexpr uint8_t kError10 = 0x8F;       // HID++ 1.0 error reply marker

enum Feature : uint16_t {
    kRoot = 0x0000,
    kFeatureSet = 0x0001,
    kDeviceName = 0x0005,
    kUnifiedBattery = 0x1004,
    kReprogControls = 0x1B04,
    kWirelessStatus = 0x1D4B,
    kSmartShift = 0x2110,
    kSmartShiftEnhanced = 0x2111,
    kHiResWheel = 0x2121,
    kThumbwheel = 0x2150,
    kAdjustableDpi = 0x2201,
};

// Error codes in HID++ 2.0 error replies.
enum Error : uint8_t {
    kErrNone = 0,
    kErrUnknown = 1,
    kErrInvalidArgument = 2,
    kErrOutOfRange = 3,
    kErrHardware = 4,
    kErrNotAllowed = 5,
    kErrInvalidFeatureIndex = 6,
    kErrInvalidFunction = 7,
    kErrBusy = 8,
    kErrUnsupported = 9,
};

// Controls (CIDs) of the MX Master 3S in REPROG_CONTROLS_V4.
enum Cid : uint16_t {
    kCidMiddle = 0x0052,
    kCidBack = 0x0053,
    kCidForward = 0x0056,
    kCidGesture = 0x00C3,  // thumb button
    kCidModeShift = 0x00C4,  // top button behind the wheel
};

// setCidReporting flag bits (each value bit has a "valid" bit right above it).
enum : uint8_t {
    kDivert = 0x01,
    kDivertValid = 0x02,
    kPersist = 0x04,
    kPersistValid = 0x08,
    kRawXY = 0x10,
    kRawXYValid = 0x20,
};

// HiResWheel mode bits.
enum : uint8_t { kWheelDivert = 0x01, kWheelHiRes = 0x02, kWheelInvert = 0x04 };

struct Report {
    uint8_t b[kLongSize] = {};

    uint8_t id() const { return b[0]; }
    uint8_t device() const { return b[1]; }
    uint8_t feature() const { return b[2]; }
    uint8_t function() const { return b[3] >> 4; }
    uint8_t software() const { return b[3] & 0x0F; }
    const uint8_t* params() const { return b + 4; }
    uint8_t p(size_t i) const { return i < kParamSize ? b[4 + i] : 0; }
    uint16_t u16(size_t i) const { return static_cast<uint16_t>(p(i) << 8 | p(i + 1)); }
    int16_t i16(size_t i) const { return static_cast<int16_t>(u16(i)); }

    // HID++ 2.0 error reply: [id][dev][0xFF][featureIndex][fn|sw][code]
    bool isError() const { return b[2] == kErrorFeature || b[2] == kError10; }
    uint8_t errorFeature() const { return b[3]; }
    uint8_t errorFunction() const { return b[4] >> 4; }
    uint8_t errorSoftware() const { return b[4] & 0x0F; }
    uint8_t errorCode() const { return b[5]; }
};

// Builds a long request report.
inline Report makeRequest(uint8_t device, uint8_t featureIndex, uint8_t function, const uint8_t* params,
                          size_t count, uint8_t softwareId = kSoftwareId) {
    Report r;
    r.b[0] = kReportLong;
    r.b[1] = device;
    r.b[2] = featureIndex;
    r.b[3] = static_cast<uint8_t>((function & 0x0F) << 4 | ((softwareId & 0x0F) ? (softwareId & 0x0F) : kSoftwareId));
    if (params && count) std::memcpy(r.b + 4, params, count > kParamSize ? kParamSize : count);
    return r;
}

// Normalises an incoming HID++ report (short or long) to a 20-byte Report. False for other reports.
bool parseInput(const uint8_t* data, size_t size, Report& out);

// True if `reply` answers `request` (same device, feature index, function and our software id),
// including an error reply for it.
bool isReplyTo(const Report& reply, const Report& request);

}  // namespace mouse::hidpp
