// The background agent: keeps the MX Master 3S configured and turns its buttons into actions.
//
//   Mouse --agent                     normal operation (started by launchd)
//   Mouse --agent --simulate <file>   drive a simulated mouse from a script, with real output
//   Mouse --probe [seconds]           print what the connected mouse reports (diagnostics)
//   Mouse --update                    check for and install an update (spawned by the agent)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOMessage.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <dispatch/dispatch.h>
#include <libproc.h>
#include <notify.h>
#include <os/log.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

#include "core/device.hpp"
#include "core/store.hpp"
#include "mac/hid.hpp"
#include "mac/output.hpp"
#include "mac/updater.hpp"
#include "sim/mx3s.hpp"

extern char** environ;

namespace mouse::mac {
namespace {

bool systemNaturalScrolling() {
    CFPreferencesAppSynchronize(kCFPreferencesAnyApplication);
    CFPropertyListRef v = CFPreferencesCopyAppValue(CFSTR("com.apple.swipescrolldirection"), kCFPreferencesAnyApplication);
    bool natural = true;  // the macOS default
    if (v) {
        if (CFGetTypeID(v) == CFBooleanGetTypeID()) {
            natural = CFBooleanGetValue(static_cast<CFBooleanRef>(v));
        } else if (CFGetTypeID(v) == CFNumberGetTypeID()) {
            int n = 1;
            CFNumberGetValue(static_cast<CFNumberRef>(v), kCFNumberIntType, &n);
            natural = n != 0;
        }
        CFRelease(v);
    }
    return natural;
}

bool canPostEvents() {
    return IOHIDCheckAccess(kIOHIDRequestTypePostEvent) == kIOHIDAccessTypeGranted || AXIsProcessTrusted();
}

bool canListen() { return IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted; }

std::string displayName(const std::string& raw) {
    std::string n = raw.empty() ? "MX Master 3S" : raw;
    for (const char* suffix : {" For Mac", " for Mac", " For Business", " for Business"}) {
        size_t p = n.find(suffix);
        if (p != std::string::npos) n.erase(p);
    }
    return n;
}

// Runs fn once on the main run loop after `seconds`.
void after(double seconds, std::function<void()> fn) {
    auto* holder = new std::function<void()>(std::move(fn));
    CFRunLoopTimerContext ctx = {0, holder, nullptr, nullptr, nullptr};
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(
        kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + seconds, 0, 0, 0,
        [](CFRunLoopTimerRef timer, void* info) {
            auto* f = static_cast<std::function<void()>*>(info);
            CFRunLoopTimerInvalidate(timer);
            (*f)();
            delete f;
        },
        &ctx);
    CFRunLoopAddTimer(CFRunLoopGetMain(), t, kCFRunLoopDefaultMode);
    CFRelease(t);
}

// Runs fn every `interval` seconds, first after `first` seconds. Lives for the whole process.
void every(double first, double interval, std::function<void()> fn) {
    auto* holder = new std::function<void()>(std::move(fn));
    CFRunLoopTimerContext ctx = {0, holder, nullptr, nullptr, nullptr};
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(
        kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + first, interval, 0, 0,
        [](CFRunLoopTimerRef, void* info) { (*static_cast<std::function<void()>*>(info))(); }, &ctx);
    CFRunLoopAddTimer(CFRunLoopGetMain(), t, kCFRunLoopDefaultMode);
    CFRelease(t);
}

std::string selfPath() {
    char buf[PROC_PIDPATHINFO_MAXSIZE] = {};
    return proc_pidpath(getpid(), buf, sizeof buf) > 0 ? buf : "";
}

// ---------------------------------------------------------------- the agent

class Agent {
public:
    int run();
    void quit(int code);
    void startUpdate();

private:
    struct Session {
        std::shared_ptr<HidLink> link;
        std::unique_ptr<HidTransport> transport;
        std::unique_ptr<Device> device;
        int attempts = 0;
    };

    void arrive(const std::shared_ptr<HidLink>& link);
    void tryStart(HidLink* key);
    void remove(const std::shared_ptr<HidLink>& link);
    void reports();
    void applyAll(bool force = false);
    void reloadConfig();
    void refreshPermissions();
    void publish();
    void watchSystem();
    void askPermissions();
    bool missingPermission() const { return !canPost_ || !canListen_ || openDenied_; }
    void updateFinished(int status);

    Config cfg_;
    State last_;
    bool canPost_ = false, canListen_ = false, natural_ = true, openDenied_ = false;
    std::string updateNote_;
    MacOutput out_;
    HidWatcher hid_;
    std::vector<std::unique_ptr<Session>> sessions_;
    pid_t updater_ = 0;
    dispatch_source_t updaterWatch_ = nullptr;
    io_connect_t power_ = 0;
    IONotificationPortRef powerPort_ = nullptr;
    io_object_t powerNotifier_ = 0;
    int notifyToken_ = 0, askToken_ = 0;
    CFRunLoopTimerRef permissionTimer_ = nullptr;
    std::vector<dispatch_source_t> signals_;
    bool quitting_ = false;
};

Agent* gAgent = nullptr;
os_log_t gLog = OS_LOG_DEFAULT;

void Agent::arrive(const std::shared_ptr<HidLink>& link) {
    auto s = std::make_unique<Session>();
    s->link = link;
    s->transport = std::make_unique<HidTransport>(link);
    s->device = std::make_unique<Device>(*s->transport, out_);
    s->device->onInfoChanged = [this] { publish(); };
    HidLink* key = link.get();
    sessions_.push_back(std::move(s));
    os_log(gLog, "mouse found: %{public}s", link->product.c_str());
    tryStart(key);
}

void Agent::tryStart(HidLink* key) {
    for (auto& s : sessions_) {
        if (s->link.get() != key) continue;
        if (s->device->started()) return;
        if (s->device->start()) {
            s->device->apply(cfg_, canPost_, natural_);
            s->device->processPending();
            os_log(gLog, "mouse ready: dpi %d, battery %d", s->device->info().dpi, s->device->info().battery);
            publish();
        } else if (++s->attempts < 4) {
            // Interfaces without HID++ never answer; a mouse that is waking up answers a later try.
            after(1.5 * s->attempts, [this, key] { tryStart(key); });
        }
        return;
    }
}

void Agent::remove(const std::shared_ptr<HidLink>& link) {
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if ((*it)->link == link) {
            sessions_.erase(it);
            os_log(gLog, "mouse disconnected");
            break;
        }
    }
    publish();
}

void Agent::reports() {
    for (auto& s : sessions_) {
        hidpp::Report r;
        while (s->transport->poll(r)) {
            s->device->handle(r);
            s->device->processPending();
        }
        if (s->device->started() && s->device->needsReapply()) {
            s->device->apply(cfg_, canPost_, natural_);
            s->device->processPending();
        }
    }
}

void Agent::applyAll(bool force) {
    for (auto& s : sessions_) {
        if (!s->device->started()) continue;
        if (force) s->device->invalidate();
        s->device->apply(cfg_, canPost_, natural_);
        s->device->processPending();
    }
    publish();
}

void Agent::reloadConfig() {
    Config c = loadConfig();
    bool updatesTurnedOn = c.autoUpdate && !cfg_.autoUpdate;
    cfg_ = c;
    applyAll();
    if (updatesTurnedOn) after(30, [this] { startUpdate(); });
}

void Agent::refreshPermissions() {
    bool post = canPostEvents(), listen = canListen(), natural = systemNaturalScrolling();
    bool changed = post != canPost_ || natural != natural_;
    bool listenGranted = listen && !canListen_;
    canPost_ = post;
    canListen_ = listen;
    natural_ = natural;
    if (changed) applyAll();
    if (listenGranted && openDenied_) {
        openDenied_ = false;  // Input Monitoring was just granted: open the mouse again
        hid_.stop();
        sessions_.clear();  // their links are closed now; the watcher reports the mouse afresh
        hid_.start();
    }
    if (missingPermission() && permissionTimer_)
        CFRunLoopTimerSetNextFireDate(permissionTimer_, CFAbsoluteTimeGetCurrent() + 3);
    publish();
}

void Agent::publish() {
    if (quitting_) return;
    State s;
    s.running = true;
    s.trusted = canPost_;
    s.inputAccess = canListen_ && !openDenied_;
    s.version = MOUSE_VERSION;
    s.update = updateNote_;
    for (auto& session : sessions_) {
        if (!session->device->started()) continue;
        const DeviceInfo& info = session->device->info();
        s.connected = true;
        s.name = displayName(info.name);
        s.battery = info.battery;
        s.charging = info.charging;
        s.dpiMin = info.dpiMin;
        s.dpiMax = info.dpiMax;
        s.dpiStep = info.dpiStep;
        break;
    }
    if (s == last_) return;
    last_ = s;
    saveState(s);
}

void Agent::startUpdate() {
    if (updater_ && waitpid(updater_, nullptr, WNOHANG) == updater_) {
        // The child finished before its exit could be observed: forget it.
        if (updaterWatch_) {
            dispatch_source_cancel(updaterWatch_);
            dispatch_release(updaterWatch_);
            updaterWatch_ = nullptr;
        }
        updater_ = 0;
    }
    if (updater_ || quitting_ || !cfg_.autoUpdate || !installedForUpdates()) return;
    std::string exe = selfPath();
    if (exe.empty()) return;
    char* argv[] = {const_cast<char*>(exe.c_str()), const_cast<char*>("--update"), nullptr};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, exe.c_str(), &fa, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return;
    updater_ = pid;
    updaterWatch_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, static_cast<uintptr_t>(pid), DISPATCH_PROC_EXIT,
                                           dispatch_get_main_queue());
    dispatch_source_set_event_handler(updaterWatch_, ^{
        int status = 0;
        pid_t p = updater_;
        waitpid(p, &status, 0);
        dispatch_source_cancel(updaterWatch_);
        dispatch_release(updaterWatch_);
        updaterWatch_ = nullptr;
        updater_ = 0;
        updateFinished(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    });
    dispatch_resume(updaterWatch_);
}

void Agent::updateFinished(int status) {
    if (status != kUpdateInstalled) return;
    std::string text;
    if (readFile(dataDir() + "/update-note", text)) updateNote_ = text;
    os_log(gLog, "updated, restarting: %{public}s", updateNote_.c_str());
    publish();
    quit(0);  // launchd starts the new version right away
}

void Agent::quit(int code) {
    if (quitting_) return;
    // Give every control back to the mouse so it keeps working while we are not running.
    for (auto& s : sessions_)
        if (s->device->started()) s->device->restore();
    State s = last_;
    s.running = false;
    s.connected = false;
    saveState(s);
    quitting_ = true;
    hid_.stop();
    std::exit(code);
}

void Agent::askPermissions() {
    bool post = canPostEvents(), listen = canListen();
    if (post && listen) return;
    // The prompts can block until answered, so never ask on the main thread.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (!post) {
            const void* keys[] = {kAXTrustedCheckOptionPrompt};
            const void* vals[] = {kCFBooleanTrue};
            CFDictionaryRef opts = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks);
            AXIsProcessTrustedWithOptions(opts);
            CFRelease(opts);
            IOHIDRequestAccess(kIOHIDRequestTypePostEvent);
        }
        if (!listen) IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);
    });
}

void Agent::watchSystem() {
    // Settings saved by the window.
    notify_register_dispatch(kConfigChanged, &notifyToken_, dispatch_get_main_queue(), ^(int) {
        reloadConfig();
    });
    // "Allow…" in the window: the system prompts have to come from the agent, which needs them.
    notify_register_dispatch(kAskPermission, &askToken_, dispatch_get_main_queue(), ^(int) {
        askPermissions();
    });
    // Natural scrolling and Accessibility changes.
    CFNotificationCenterRef dist = CFNotificationCenterGetDistributedCenter();
    // TCC can take a moment to reflect a new grant: look shortly after, and again a bit later.
    CFNotificationCenterAddObserver(
        dist, this,
        [](CFNotificationCenterRef, void* self, CFNotificationName, const void*, CFDictionaryRef) {
            auto* a = static_cast<Agent*>(self);
            after(0.3, [a] { a->refreshPermissions(); });
            after(2.0, [a] { a->refreshPermissions(); });
        },
        CFSTR("com.apple.accessibility.api"), nullptr, CFNotificationSuspensionBehaviorDeliverImmediately);
    CFNotificationCenterAddObserver(
        dist, this,
        [](CFNotificationCenterRef, void* self, CFNotificationName, const void*, CFDictionaryRef) {
            auto* a = static_cast<Agent*>(self);
            after(0.2, [a] { a->refreshPermissions(); });
        },
        CFSTR("SwipeScrollDirectionDidChangeNotification"), nullptr, CFNotificationSuspensionBehaviorDeliverImmediately);

    // Permissions are re-checked every few seconds while something is missing, hourly otherwise
    // (a change also arrives as a notification, see above).
    CFRunLoopTimerContext ctx = {0, this, nullptr, nullptr, nullptr};
    permissionTimer_ = CFRunLoopTimerCreate(
        kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 3, 3, 0, 0,
        [](CFRunLoopTimerRef t, void* self) {
            auto* a = static_cast<Agent*>(self);
            if (a->missingPermission()) a->refreshPermissions();
            if (!a->missingPermission()) CFRunLoopTimerSetNextFireDate(t, CFAbsoluteTimeGetCurrent() + 3600);
        },
        &ctx);
    CFRunLoopAddTimer(CFRunLoopGetMain(), permissionTimer_, kCFRunLoopDefaultMode);

    // After sleep the mouse may have forgotten its settings.
    powerPort_ = nullptr;
    power_ = IORegisterForSystemPower(
        this, &powerPort_,
        [](void* self, io_service_t, natural_t type, void* arg) {
            auto* a = static_cast<Agent*>(self);
            if (type == kIOMessageCanSystemSleep || type == kIOMessageSystemWillSleep)
                IOAllowPowerChange(a->power_, reinterpret_cast<intptr_t>(arg));
            else if (type == kIOMessageSystemHasPoweredOn)
                after(3, [a] { a->applyAll(true); });
        },
        &powerNotifier_);
    if (power_ && powerPort_) IONotificationPortSetDispatchQueue(powerPort_, dispatch_get_main_queue());

    // Stop cleanly when launchd (or anyone) asks.
    for (int sig : {SIGTERM, SIGINT, SIGHUP}) {
        signal(sig, SIG_IGN);
        dispatch_source_t src = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, static_cast<uintptr_t>(sig), 0,
                                                       dispatch_get_main_queue());
        dispatch_source_set_event_handler(src, ^{
            quit(0);
        });
        dispatch_resume(src);
        signals_.push_back(src);  // lives as long as the agent
    }
}

int Agent::run() {
    gAgent = this;
    gLog = os_log_create("app.mouse", "agent");
    cfg_ = loadConfig();
    canPost_ = canPostEvents();
    canListen_ = canListen();
    natural_ = systemNaturalScrolling();
    std::string note;
    if (readFile(dataDir() + "/update-note", note)) updateNote_ = note;

    hid_.onArrive = [this](std::shared_ptr<HidLink> link) { arrive(link); };
    hid_.onRemove = [this](std::shared_ptr<HidLink> link) { remove(link); };
    hid_.onReports = [this] { reports(); };
    hid_.onOpenFailed = [this](int code) {
        os_log(gLog, "cannot open the mouse: 0x%x", code);
        if (code == static_cast<int>(kIOReturnNotPermitted)) openDenied_ = true;
        publish();
    };
    watchSystem();
    if (!hid_.start()) os_log_error(gLog, "HID manager unavailable");
    publish();
    // First run of this version without permission: show the system prompts once by ourselves.
    std::string asked = dataDir() + "/asked-" MOUSE_VERSION;
    if ((!canPost_ || !canListen_) && access(asked.c_str(), F_OK) != 0) {
        writeFileAtomic(asked, "1");
        askPermissions();
    }
    // First look for updates ten minutes after start, then every six hours: updates land within a day.
    every(600, 6 * 3600, [this] { startUpdate(); });
    CFRunLoopRun();
    return 0;
}

// ---------------------------------------------------------------- simulation

// `--simulate <script>`: a simulated mouse whose events produce real macOS input. Script lines:
//   press c3 | release c3 | move dx dy | wheel n | hires n | thumb n | battery p | sleep s | apply
int simulate(const char* path, bool dryRun) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", path);
        return 2;
    }
    sim::MXMaster3S mouse;
    sim::SimTransport link(mouse);
    struct Printer : Output {
        Output* real;
        bool dry;
        double now() override { return real->now(); }
        void key(const Shortcut& s) override { std::printf("key %s\n", shortcutDisplay(s).c_str()); if (!dry) real->key(s); }
        void button(int b, bool d, uint32_t m) override { std::printf("button %d %s %u\n", b, d ? "down" : "up", m); if (!dry) real->button(b, d, m); }
        void dock(Dock d) override { std::printf("dock %d\n", static_cast<int>(d)); if (!dry) real->dock(d); }
        void media(int k) override { std::printf("media %d\n", k); if (!dry) real->media(k); }
        void dictation() override { std::printf("dictation\n"); if (!dry) real->dictation(); }
        void navigate(bool f) override { std::printf("navigate %s\n", f ? "forward" : "back"); if (!dry) real->navigate(f); }
        void scroll(int dy, int dx, bool c) override { std::printf("scroll %d %d %s\n", dy, dx, c ? "px" : "lines"); if (!dry) real->scroll(dy, dx, c); }
    };
    MacOutput real;
    Printer out;
    out.real = &real;
    out.dry = dryRun;
    Device dev(link, out);
    if (!dev.start()) return 3;
    Config cfg = loadConfig();
    dev.apply(cfg, true, systemNaturalScrolling());
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd.empty() || cmd[0] == '#') continue;
        if (cmd == "sleep") {
            double s = 0;
            ss >> s;
            usleep(static_cast<useconds_t>(s * 1e6));
            continue;
        }
        if (cmd == "apply") {
            dev.apply(loadConfig(), true, systemNaturalScrolling());
            continue;
        }
        long a = 0, b = 0;
        std::string first;
        ss >> first;
        a = std::strtol(first.c_str(), nullptr, cmd == "press" || cmd == "release" ? 16 : 10);
        ss >> b;
        if (cmd == "press") mouse.press(static_cast<uint16_t>(a));
        else if (cmd == "release") mouse.release(static_cast<uint16_t>(a));
        else if (cmd == "move") mouse.move(static_cast<int>(a), static_cast<int>(b));
        else if (cmd == "wheel") mouse.turnWheel(static_cast<int>(a));
        else if (cmd == "hires") mouse.turnWheelHiRes(static_cast<int>(a));
        else if (cmd == "thumb") mouse.turnThumbwheel(static_cast<int>(a));
        else if (cmd == "battery") mouse.setBattery(static_cast<int>(a), b != 0);
        link.pump(dev);
        std::fflush(stdout);
    }
    dev.restore();
    return 0;
}

// ---------------------------------------------------------------- probe

const char* featureName(const Device::Indexes& i, uint8_t f) {
    if (f == i.reprog) return "buttons";
    if (f == i.hiRes) return "wheel";
    if (f == i.thumb) return "thumbwheel";
    if (f == i.battery) return "battery";
    if (f == i.wireless) return "connection";
    return "other";
}

// `--probe [seconds]`: diverts every control for a few seconds and prints the raw events, so the
// directions and controls of a real mouse can be checked. Restores everything afterwards.
// `--info`: reads the connected mouse (name, battery, features, ranges) without changing anything.
int info() {
    static HidWatcher hid;
    static MacOutput real;
    static std::shared_ptr<HidLink> link;
    static std::unique_ptr<HidTransport> transport;
    static std::unique_ptr<Device> dev;
    std::printf("Input Monitoring: %s, Accessibility: %s\n", canListen() ? "yes" : "no", canPostEvents() ? "yes" : "no");
    hid.onOpenFailed = [](int code) {
        std::printf("cannot open the mouse: 0x%x\n", code);
        std::exit(2);
    };
    hid.onArrive = [](std::shared_ptr<HidLink> l) {
        if (dev) return;
        link = l;
        transport = std::make_unique<HidTransport>(l);
        dev = std::make_unique<Device>(*transport, real);
        if (!dev->start()) {
            std::printf("%s (0x%04x): no HID++ answer\n", l->product.c_str(), l->productId);
            dev.reset();
            return;
        }
        const auto& i = dev->info();
        const auto& x = dev->indexes();
        std::printf("%s (0x%04x)\nname: %s\nbattery: %d%%%s\ndpi: %d (%d..%d step %d)\n"
                    "wheel multiplier: %d\nthumb wheel resolution: %d%s\n"
                    "feature indexes: name %d battery %d buttons %d connection %d wheelmode %d%s wheel %d thumbwheel %d dpi %d\n",
                    l->product.c_str(), l->productId, i.name.c_str(), i.battery, i.charging ? " (charging)" : "", i.dpi,
                    i.dpiMin, i.dpiMax, i.dpiStep, i.wheelMultiplier, i.thumbResolution,
                    i.thumbReversedByDefault ? " (reversed by default)" : "", x.name, x.battery, x.reprog, x.wireless,
                    x.smartShift, x.smartShiftEnhanced ? " (enhanced)" : "", x.hiRes, x.thumb, x.dpi);
        std::exit(0);
    };
    if (!hid.start()) return 1;
    after(8, [] {
        std::printf("no MX Master 3S answered (move it to wake it up)\n");
        std::exit(1);
    });
    CFRunLoopRun();
    return 0;
}

int probe(double seconds) {
    static HidWatcher hid;
    static MacOutput real;
    struct Quiet : Output {
        Output* real;
        double now() override { return real->now(); }
        void key(const Shortcut& s) override { std::printf("  -> key %s\n", shortcutDisplay(s).c_str()); }
        void button(int b, bool d, uint32_t) override { std::printf("  -> button %d %s\n", b, d ? "down" : "up"); }
        void dock(Dock d) override { std::printf("  -> dock %d\n", static_cast<int>(d)); }
        void media(int k) override { std::printf("  -> media %d\n", k); }
        void dictation() override { std::printf("  -> dictation\n"); }
        void navigate(bool f) override { std::printf("  -> navigate %s\n", f ? "forward" : "back"); }
        void scroll(int dy, int dx, bool c) override { std::printf("  -> scroll dy=%d dx=%d %s\n", dy, dx, c ? "px" : "lines"); }
    };
    static Quiet out;
    out.real = &real;
    static std::shared_ptr<HidLink> link;
    static std::unique_ptr<HidTransport> transport;
    static std::unique_ptr<Device> dev;
    std::printf("Looking for an MX Master 3S (Bluetooth)...\n");
    std::printf("Input Monitoring: %s, Accessibility: %s\n", canListen() ? "yes" : "no", canPostEvents() ? "yes" : "no");
    hid.onOpenFailed = [](int code) { std::printf("cannot open the mouse: 0x%x\n", code); };
    hid.onArrive = [seconds](std::shared_ptr<HidLink> l) {
        if (dev) return;
        std::printf("found %s (pid 0x%04x)\n", l->product.c_str(), l->productId);
        link = l;
        transport = std::make_unique<HidTransport>(l);
        dev = std::make_unique<Device>(*transport, out);
        if (!dev->start()) {
            std::printf("  no HID++ answer on this interface\n");
            dev.reset();
            return;
        }
        const auto& i = dev->info();
        const auto& x = dev->indexes();
        std::printf("name: %s\nbattery: %d%%%s\ndpi: %d (%d..%d step %d)\nwheel multiplier: %d, thumb wheel resolution: %d%s\n",
                    i.name.c_str(), i.battery, i.charging ? " charging" : "", i.dpi, i.dpiMin, i.dpiMax, i.dpiStep,
                    i.wheelMultiplier, i.thumbResolution, i.thumbReversedByDefault ? " (reversed)" : "");
        std::printf("features: name %d battery %d buttons %d connection %d wheelmode %d%s wheel %d thumbwheel %d dpi %d\n",
                    x.name, x.battery, x.reprog, x.wireless, x.smartShift, x.smartShiftEnhanced ? "(enhanced)" : "", x.hiRes,
                    x.thumb, x.dpi);
        Config c;
        for (int k = 0; k < kControlCount; k++) c.buttons[k].act = Act::None;
        c.button(Control::Thumb).act = Act::Gestures;
        c.thumbWheel = WheelAct::Shortcuts;  // diverted, prints direction
        c.thumbUp = parseShortcut("f13");
        c.thumbDown = parseShortcut("f14");
        c.smoothScroll = true;
        c.naturalScroll = false;
        dev->apply(c, true, true);
        std::printf("\nFor %.0f seconds every control reports here instead of acting:\n"
                    "press each button, hold the thumb button and move, turn both wheels both ways.\n\n",
                    seconds);
        std::fflush(stdout);
        after(seconds, [] {
            dev->restore();
            std::printf("\nrestored the mouse\n");
            std::exit(0);
        });
    };
    hid.onReports = [] {
        if (!dev) return;
        hidpp::Report r;
        while (transport->poll(r)) {
            if (r.software() == 0 && !r.isError()) {
                std::printf("%-10s event %d:", featureName(dev->indexes(), r.feature()), r.function());
                for (int k = 0; k < 8; k++) std::printf(" %02x", r.p(k));
                std::printf("\n");
            }
            dev->handle(r);
            dev->processPending();
        }
        std::fflush(stdout);
    };
    hid.onRemove = [](std::shared_ptr<HidLink> l) {
        if (l == link) {
            std::printf("mouse disconnected\n");
            std::exit(1);
        }
    };
    if (!hid.start()) {
        std::printf("HID manager unavailable\n");
        return 1;
    }
    after(seconds + 20, [] {
        if (!dev) {
            std::printf("no MX Master 3S found (move it to wake it up)\n");
            std::exit(1);
        }
    });
    CFRunLoopRun();
    return 0;
}

}  // namespace
}  // namespace mouse::mac

int runAgent(int argc, char** argv) {
    const char* script = nullptr;
    bool dry = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--simulate") && i + 1 < argc) script = argv[++i];
        if (!std::strcmp(argv[i], "--dry")) dry = true;
    }
    if (script) return mouse::mac::simulate(script, dry);
    static mouse::mac::Agent agent;
    return agent.run();
}

int runProbe(int argc, char** argv) {
    double seconds = 15;
    for (int i = 1; i + 1 < argc; i++)
        if (!std::strcmp(argv[i], "--probe")) seconds = std::atof(argv[i + 1]) > 0 ? std::atof(argv[i + 1]) : 15;
    return mouse::mac::probe(seconds);
}

int runUpdateCheck() { return mouse::mac::runUpdate(); }

int runInfo() { return mouse::mac::info(); }
