#include "hid.hpp"

#include <IOKit/hid/IOHIDKeys.h>
#include <pthread.h>

#include <atomic>
#include <vector>

namespace mouse::mac {
namespace {

constexpr int kLogitech = 0x046D;
constexpr int kMXMaster3SBluetooth = 0xB034;

int intProperty(IOHIDDeviceRef dev, CFStringRef key) {
    int v = 0;
    if (auto n = static_cast<CFNumberRef>(IOHIDDeviceGetProperty(dev, key)); n && CFGetTypeID(n) == CFNumberGetTypeID())
        CFNumberGetValue(n, kCFNumberIntType, &v);
    return v;
}

std::string stringProperty(IOHIDDeviceRef dev, CFStringRef key) {
    auto s = static_cast<CFStringRef>(IOHIDDeviceGetProperty(dev, key));
    if (!s || CFGetTypeID(s) != CFStringGetTypeID()) return "";
    char buf[128] = {};
    CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8);
    return buf;
}

// Main-thread mailbox: functions posted from the HID thread, plus a coalesced "reports" flag.
struct Mailbox {
    std::mutex m;
    std::vector<std::function<void()>> fns;
    std::atomic<bool> reports{false};
    CFRunLoopSourceRef source = nullptr;
    CFRunLoopRef loop = nullptr;
    std::function<void()> onReports;
};

void mailboxPerform(void* info) {
    auto* mb = static_cast<Mailbox*>(info);
    std::vector<std::function<void()>> fns;
    {
        std::lock_guard<std::mutex> l(mb->m);
        fns.swap(mb->fns);
    }
    for (auto& f : fns) f();
    if (mb->reports.exchange(false) && mb->onReports) mb->onReports();
}

Mailbox* gMailbox = nullptr;  // one watcher per process

void signalMain() {
    if (!gMailbox || !gMailbox->source) return;
    CFRunLoopSourceSignal(gMailbox->source);
    CFRunLoopWakeUp(gMailbox->loop);
}

}  // namespace

HidLink::~HidLink() {
    if (dev) CFRelease(dev);
}

bool HidTransport::write(const hidpp::Report& r) {
    IOHIDDeviceRef dev;
    {
        std::lock_guard<std::mutex> l(link_->m);
        if (link_->gone || !link_->dev) return false;
        dev = link_->dev;
    }
    return IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, r.b[0], r.b, hidpp::kLongSize) == kIOReturnSuccess;
}

bool HidTransport::read(hidpp::Report& out, int timeoutMs) {
    std::unique_lock<std::mutex> l(link_->m);
    link_->cv.wait_for(l, std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs),
                       [this] { return !link_->queue.empty() || link_->gone; });
    if (link_->queue.empty()) return false;
    out = link_->queue.front();
    link_->queue.pop_front();
    return true;
}

bool HidTransport::poll(hidpp::Report& out) {
    std::lock_guard<std::mutex> l(link_->m);
    if (link_->queue.empty()) return false;
    out = link_->queue.front();
    link_->queue.pop_front();
    return true;
}

// ---------------------------------------------------------------- watcher

bool HidWatcher::start() {
    if (thread_.joinable()) return manager_ != nullptr;
    main_ = CFRunLoopGetMain();
    if (!gMailbox) gMailbox = new Mailbox();  // lives for the process
    gMailbox->loop = main_;
    gMailbox->onReports = [this] {
        if (onReports) onReports();
    };
    if (!gMailbox->source) {
        CFRunLoopSourceContext ctx = {};
        ctx.info = gMailbox;
        ctx.perform = mailboxPerform;
        gMailbox->source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
        CFRunLoopAddSource(main_, gMailbox->source, kCFRunLoopCommonModes);
    }
    ready_ = false;
    thread_ = std::thread([this] { run(); });
    std::unique_lock<std::mutex> l(startM_);
    startCv_.wait(l, [this] { return ready_; });
    return manager_ != nullptr;
}

void HidWatcher::stop() {
    if (!thread_.joinable()) return;
    CFRunLoopRef loop = loop_;
    if (loop) {
        CFRunLoopStop(loop);
        CFRunLoopWakeUp(loop);
    }
    thread_.join();
}

void HidWatcher::post(std::function<void()> fn) {
    if (!gMailbox) return;
    {
        std::lock_guard<std::mutex> l(gMailbox->m);
        gMailbox->fns.push_back(std::move(fn));
    }
    signalMain();
}

void HidWatcher::run() {
    pthread_setname_np("mouse.hid");
    loop_ = CFRunLoopGetCurrent();
    manager_ = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (manager_) {
        int vid = kLogitech, pid = kMXMaster3SBluetooth;
        if (const char* v = getenv("MOUSE_VID")) vid = static_cast<int>(strtol(v, nullptr, 0));  // diagnostics only
        if (const char* p = getenv("MOUSE_PID")) pid = static_cast<int>(strtol(p, nullptr, 0));
        CFNumberRef v = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
        CFNumberRef p = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
        const void* keys[] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey)};
        const void* vals[] = {v, p};
        CFDictionaryRef match = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
                                                   &kCFTypeDictionaryValueCallBacks);
        IOHIDManagerSetDeviceMatching(manager_, match);
        CFRelease(match);
        CFRelease(v);
        CFRelease(p);
        IOHIDManagerRegisterDeviceMatchingCallback(manager_, matched, this);
        IOHIDManagerRegisterDeviceRemovalCallback(manager_, removed, this);
        IOHIDManagerScheduleWithRunLoop(manager_, loop_, kCFRunLoopDefaultMode);
    }
    {
        std::lock_guard<std::mutex> l(startM_);
        ready_ = true;
    }
    startCv_.notify_all();
    if (!manager_) return;

    // A timer far in the future keeps the run loop alive when no mouse is around.
    CFRunLoopTimerRef keepAlive = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 1e9, 1e9, 0, 0,
                                                       [](CFRunLoopTimerRef, void*) {}, nullptr);
    CFRunLoopAddTimer(loop_, keepAlive, kCFRunLoopDefaultMode);
    CFRunLoopRun();
    CFRunLoopTimerInvalidate(keepAlive);
    CFRelease(keepAlive);

    for (auto& link : links_) {
        {
            std::lock_guard<std::mutex> l(link->m);
            link->gone = true;
        }
        link->cv.notify_all();
        IOHIDDeviceRegisterInputReportCallback(link->dev, link->buf, sizeof link->buf, nullptr, nullptr);
        IOHIDDeviceClose(link->dev, kIOHIDOptionsTypeNone);
    }
    links_.clear();
    IOHIDManagerUnscheduleFromRunLoop(manager_, loop_, kCFRunLoopDefaultMode);
    CFRelease(manager_);
    manager_ = nullptr;
    loop_ = nullptr;
}

void HidWatcher::matched(void* ctx, IOReturn, void*, IOHIDDeviceRef dev) {
    auto* self = static_cast<HidWatcher*>(ctx);
    // Skip interfaces that cannot take even a short HID++ report. (The size may or may not count
    // the report id byte, so anything that could carry HID++ is tried; a ping decides.)
    int maxOut = intProperty(dev, CFSTR(kIOHIDMaxOutputReportSizeKey));
    if (maxOut > 0 && maxOut < 6) return;
    IOReturn r = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
    if (r != kIOReturnSuccess) {
        int code = static_cast<int>(r);
        self->post([self, code] {
            if (self->onOpenFailed) self->onOpenFailed(code);
        });
        return;
    }
    auto link = std::make_shared<HidLink>();
    link->dev = static_cast<IOHIDDeviceRef>(const_cast<void*>(CFRetain(dev)));
    link->product = stringProperty(dev, CFSTR(kIOHIDProductKey));
    link->productId = intProperty(dev, CFSTR(kIOHIDProductIDKey));
    IOHIDDeviceRegisterInputReportCallback(dev, link->buf, sizeof link->buf, input, link.get());
    self->links_.push_back(link);
    self->post([self, link] {
        if (self->onArrive) self->onArrive(link);
    });
}

void HidWatcher::removed(void* ctx, IOReturn, void*, IOHIDDeviceRef dev) {
    auto* self = static_cast<HidWatcher*>(ctx);
    for (auto it = self->links_.begin(); it != self->links_.end(); ++it) {
        if ((*it)->dev != dev) continue;
        std::shared_ptr<HidLink> link = *it;
        self->links_.erase(it);
        {
            std::lock_guard<std::mutex> l(link->m);
            link->gone = true;
        }
        link->cv.notify_all();
        IOHIDDeviceRegisterInputReportCallback(dev, link->buf, sizeof link->buf, nullptr, nullptr);
        IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
        self->post([self, link] {
            if (self->onRemove) self->onRemove(link);
        });
        return;
    }
}

void HidWatcher::input(void* ctx, IOReturn result, void*, IOHIDReportType, uint32_t, uint8_t* report, CFIndex len) {
    if (result != kIOReturnSuccess) return;
    auto* link = static_cast<HidLink*>(ctx);
    hidpp::Report r;
    if (!hidpp::parseInput(report, static_cast<size_t>(len), r)) return;  // pointer motion etc.
    {
        std::lock_guard<std::mutex> l(link->m);
        if (link->queue.size() < 1024) link->queue.push_back(r);
    }
    link->cv.notify_all();
    if (gMailbox) {
        gMailbox->reports = true;
        signalMain();
    }
}

}  // namespace mouse::mac
