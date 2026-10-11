// A simulated MX Master 3S on Bluetooth: answers HID++ 2.0 requests like the real mouse
// (feature table, controls and ranges recorded from real hardware) and produces the
// notifications its buttons, wheels and battery would send. Used by the tests and by
// `Mouse --agent --simulate`.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/device.hpp"
#include "core/hidpp.hpp"

namespace mouse::sim {

class MXMaster3S {
public:
    MXMaster3S();

    // Host -> mouse. Replies (and errors) are appended to the outbox.
    void receive(const hidpp::Report& request);
    std::deque<hidpp::Report> outbox;

    // ---- physical interaction (what a hand on the mouse does)
    void press(uint16_t cid);
    void release(uint16_t cid);
    void move(int dx, int dy);        // sensor counts
    void turnWheel(int notches);      // + = away from you
    void turnWheelHiRes(int units);   // + = away from you, hi-res units
    void turnThumbwheel(int units);   // + = up
    void setBattery(int percent, bool charging);
    void powerCycle();                // firmware forgets volatile settings, then reconnects

    // ---- state the tests inspect
    int dpi = 1000;
    uint8_t wheelMode = 0;            // HiResWheel flags
    uint8_t ratchetMode = 2;          // 1 free spin, 2 ratchet
    uint8_t threshold = 10;
    bool thumbDivert = false, thumbInvert = false;
    struct Ctl {
        uint16_t cid, task;
        uint8_t flags, add;
        bool divert = false, rawXY = false;
    };
    std::vector<Ctl> controls;
    Ctl* control(uint16_t cid);
    int battery = 85;
    bool charging = false;

    // What the firmware did on its own because a control was not diverted.
    std::vector<std::string> nativeLog;

    // ---- fault injection
    bool silent = false;          // never answer (asleep / out of range)
    int failNextRequests = 0;     // answer the next N requests with HID++ "busy"
    uint16_t brokenFeature = 0;   // requests to this feature id answer with "hardware error"
    int delayReplyOf = 0;         // the reply to this request number (1-based) arrives one request late
    uint16_t loseReplyFeature = 0;  // the next request to this feature/function is carried out, reply lost
    int loseReplyFunction = -1;
    int requestCount = 0;

private:
    void handleRequest(const hidpp::Report& req);
    void reply(const hidpp::Report& req, std::initializer_list<uint8_t> params);
    void error(const hidpp::Report& req, uint8_t code);
    void event(uint8_t featureIndex, uint8_t ev, std::initializer_list<uint8_t> params);
    void sendHeld();
    uint8_t indexOf(uint16_t featureId) const;
    uint16_t featureAt(uint8_t index) const;

    std::vector<uint16_t> held_;
    uint16_t ts_ = 0;
    std::deque<hidpp::Report> late_;
};

// Transport that talks to a simulated mouse in-process.
class SimTransport : public Transport {
public:
    explicit SimTransport(MXMaster3S& m) : mouse(m) {}
    bool write(const hidpp::Report& r) override;
    bool read(hidpp::Report& out, int timeoutMs) override;
    // Deliver everything the mouse sent outside requests (button events etc.).
    void pump(Device& d);
    MXMaster3S& mouse;
    int writes = 0;
};

}  // namespace mouse::sim
