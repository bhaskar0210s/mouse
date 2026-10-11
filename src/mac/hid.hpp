// Finds the MX Master 3S and moves HID++ reports between it and the agent.
//
// IOKit callbacks run on a small dedicated thread; the agent's logic stays on the main thread,
// where requests simply wait for their reply. Nothing else runs while a request waits, so the
// device logic never re-enters itself.
#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/device.hpp"

namespace mouse::mac {

// One opened HID interface of the mouse, shared by the HID thread and the main thread.
struct HidLink {
    IOHIDDeviceRef dev = nullptr;  // retained
    std::string product;
    int productId = 0;
    std::mutex m;
    std::condition_variable cv;
    std::deque<hidpp::Report> queue;
    bool gone = false;
    uint8_t buf[64] = {};
    ~HidLink();
};

class HidTransport : public Transport {
public:
    explicit HidTransport(std::shared_ptr<HidLink> link) : link_(std::move(link)) {}
    bool write(const hidpp::Report& r) override;
    bool read(hidpp::Report& out, int timeoutMs) override;
    bool poll(hidpp::Report& out);  // non-blocking
    const std::shared_ptr<HidLink>& link() const { return link_; }

private:
    std::shared_ptr<HidLink> link_;
};

class HidWatcher {
public:
    // All callbacks are delivered on the main thread.
    std::function<void(std::shared_ptr<HidLink>)> onArrive;
    std::function<void(std::shared_ptr<HidLink>)> onRemove;
    std::function<void()> onReports;
    std::function<void(int)> onOpenFailed;  // IOReturn, e.g. kIOReturnNotPermitted

    bool start();
    void stop();
    ~HidWatcher() { stop(); }

private:
    void run();
    void post(std::function<void()> fn);
    static void matched(void* ctx, IOReturn, void*, IOHIDDeviceRef dev);
    static void removed(void* ctx, IOReturn, void*, IOHIDDeviceRef dev);
    static void input(void* ctx, IOReturn, void*, IOHIDReportType, uint32_t, uint8_t* report, CFIndex len);

    std::thread thread_;
    CFRunLoopRef loop_ = nullptr;
    CFRunLoopRef main_ = nullptr;
    IOHIDManagerRef manager_ = nullptr;
    std::mutex startM_;
    std::condition_variable startCv_;
    bool ready_ = false;
    std::deque<std::shared_ptr<HidLink>> links_;  // HID thread only
};

}  // namespace mouse::mac
