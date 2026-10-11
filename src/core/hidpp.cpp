#include "hidpp.hpp"

namespace mouse::hidpp {

bool parseInput(const uint8_t* data, size_t size, Report& out) {
    if (!data || size < 4) return false;
    if (data[0] == kReportShort) {
        if (size < 7) return false;
    } else if (data[0] == kReportLong) {
        if (size < kLongSize) return false;
    } else {
        return false;  // mouse movement, keyboard, consumer reports...
    }
    out = Report{};
    std::memcpy(out.b, data, size < kLongSize ? size : kLongSize);
    return true;
}

static bool sameDevice(uint8_t reply, uint8_t request) {
    if (reply == request) return true;
    // Some Bluetooth devices answer requests sent to 0xFF with device index 0.
    return request == kDeviceDirect && reply == 0x00;
}

bool isReplyTo(const Report& reply, const Report& request) {
    if (!sameDevice(reply.device(), request.device())) return false;
    if (reply.b[2] == kErrorFeature)
        return reply.errorFeature() == request.feature() && reply.errorFunction() == request.function() &&
               reply.errorSoftware() == request.software();
    if (reply.b[2] == kError10) return reply.b[3] == request.feature();
    return reply.feature() == request.feature() && reply.function() == request.function() &&
           reply.software() == request.software();
}

}  // namespace mouse::hidpp
