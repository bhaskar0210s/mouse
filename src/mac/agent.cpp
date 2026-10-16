// The background agent: keeps the MX Master 3S configured and turns its buttons into actions.
//
//   MouseAgent                   normal operation (started by launchd)
//   MouseAgent --info            read the connected mouse without changing anything
//   MouseAgent --probe [secs]    print what the connected mouse reports (diagnostics)
//   MouseAgent --update          check for and install an update (spawned by the agent)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOMessage.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <dispatch/dispatch.h>
#include <libproc.h>
#include <malloc/malloc.h>
#include <notify.h>
#include <os/log.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <vector>

#include "core/device.hpp"
#include "core/store.hpp"
#include "mac/hid.hpp"
#include "mac/output.hpp"
#include "mac/system.hpp"
#include "mac/updater.hpp"

extern char** environ;

namespace mouse::mac {
namespace {

constexpr const char* kLabel = "app.mouse.agent";  // the launchd job (written by src/ui/app.mm)

// Started by launchd as our job, not from a terminal, a test or LaunchServices.
bool isLaunchdJob() {
    const char* name = getenv("XPC_SERVICE_NAME");
    return name && std::strcmp(name, kLabel) == 0;
}

// Seconds since the file was last written; very large if it does not exist.
double fileAge(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 1e12;
    return std::difftime(std::time(nullptr), st.st_mtime);
}

bool usesDock(const Config& c) {
    auto dock = [](Act a) { return a == Act::MissionControl || a == Act::AppExpose || a == Act::ShowDesktop; };
    for (const auto& b : c.buttons)
        if (dock(b.act)) return true;
    if (c.button(Control::Thumb).act == Act::Gestures)
        for (Act a : c.gestures)
            if (dock(a)) return true;
    return false;
}

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
    void quit(int code, bool restarting = false);
    void startUpdate(bool force = false);

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
    void refreshScrolling();
    void restartIfInputMissing(double minUptime);
    void restart(const char* why);
    void publish();
    void watchSystem();
    void askPermissions();
    bool missingPermission() const { return !canPost_ || !canListen_ || openDenied_; }
    void updateFinished(int status);
    void trimSoon();
    void trimWhenIdle();

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
    int notifyToken_ = 0, askToken_ = 0, reapplyToken_ = 0, recheckToken_ = 0;
    CFRunLoopTimerRef permissionTimer_ = nullptr;
    std::vector<dispatch_source_t> signals_;
    std::atomic<bool> asking_{false};  // system prompts on screen
    double started_ = 0, lastActivity_ = 0;
    bool trimPending_ = false, restartAfterUpdate_ = false, quitting_ = false;
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

// Handle what arrived during requests; if that asked for it (e.g. the mouse said it reconnected),
// configure again right away so no later press or wheel turn is lost.
static void settle(Device& d, const Config& cfg, bool canPost, bool natural) {
    for (int round = 0; round < 3; round++) {
        d.processPending();
        if (!d.needsReapply()) return;
        d.apply(cfg, canPost, natural);
    }
}

void Agent::tryStart(HidLink* key) {
    for (auto& s : sessions_) {
        if (s->link.get() != key) continue;
        if (s->device->started()) return;
        if (s->device->start()) {
            s->device->apply(cfg_, canPost_, natural_);
            settle(*s->device, cfg_, canPost_, natural_);
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
        if (s->device->started()) settle(*s->device, cfg_, canPost_, natural_);
    }
    trimSoon();
}

void Agent::applyAll(bool force) {
    for (auto& s : sessions_) {
        if (!s->device->started()) continue;
        if (force) s->device->invalidate();
        s->device->apply(cfg_, canPost_, natural_);
        settle(*s->device, cfg_, canPost_, natural_);
    }
    publish();
}

void Agent::reloadConfig() {
    Config c = loadConfig();
    bool updatesTurnedOn = c.autoUpdate && !cfg_.autoUpdate;
    cfg_ = c;
    if (usesDock(cfg_)) hiServices("CoreDockSendNotification");  // load it now, not on the first press
    applyAll();
    if (updatesTurnedOn) after(30, [this] { startUpdate(true); });
}

void Agent::refreshPermissions() {
    bool post = canPostEvents(), listen = canListen();
    // A new process sees a new permission everywhere (opening the mouse, posting events), so the
    // launchd job simply restarts; launchd starts it again right away.
    if (((post && !canPost_) || (listen && !canListen_)) && isLaunchdJob()) return restart("permission granted");
    bool changed = post != canPost_;
    bool listenGranted = listen && !canListen_;
    canPost_ = post;
    canListen_ = listen;
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

void Agent::refreshScrolling() {
    bool natural = systemNaturalScrolling();
    if (natural == natural_) return;
    natural_ = natural;
    applyAll();
}

// macOS keeps answering a running process's Input Monitoring check with what was true when the
// process started: access granted later in System Settings only reaches a new process. So while
// access is missing, the launchd job restarts itself now and then (see watchSystem), never while
// the system prompts are on screen.
void Agent::restartIfInputMissing(double minUptime) {
    if (!isLaunchdJob() || (canListen_ && !openDenied_) || asking_) return;
    if (CFAbsoluteTimeGetCurrent() - started_ < minUptime) return;
    restart("looking for Input Monitoring access");
}

void Agent::restart(const char* why) {
    if (updater_) {  // an update may be replacing the app right now: restart after it
        restartAfterUpdate_ = true;
        return;
    }
    os_log(gLog, "restarting: %{public}s", why);
    quit(0, true);
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
    trimSoon();
}

// The allocator keeps memory that was freed (after startup, a reconnect or a burst of events)
// for later; once things are quiet it goes back to the system.
void Agent::trimSoon() {
    lastActivity_ = CFAbsoluteTimeGetCurrent();
    if (trimPending_) return;
    trimPending_ = true;
    after(5, [this] { trimWhenIdle(); });
}

void Agent::trimWhenIdle() {
    double idle = CFAbsoluteTimeGetCurrent() - lastActivity_;
    if (idle < 4) {
        after(5 - idle, [this] { trimWhenIdle(); });
        return;
    }
    trimPending_ = false;
    malloc_zone_pressure_relief(nullptr, 0);
}

void Agent::startUpdate(bool force) {
    int status = 0;
    if (updater_ && waitpid(updater_, &status, WNOHANG) == updater_) {
        // The child finished before its exit could be observed: handle it now.
        if (updaterWatch_) {
            dispatch_source_cancel(updaterWatch_);
            dispatch_release(updaterWatch_);
            updaterWatch_ = nullptr;
        }
        updater_ = 0;
        updateFinished(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }
    if (updater_ || quitting_ || !cfg_.autoUpdate || !installedForUpdates()) return;
    // At most one check every six hours, also across restarts.
    std::string checked = dataDir() + "/update-checked";
    if (!force && fileAge(checked) < 6 * 3600 - 120) return;
    writeFileAtomic(checked, "");
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
        int exitStatus = 0;
        pid_t p = updater_;
        if (p == 0 || waitpid(p, &exitStatus, 0) != p) return;  // already handled in startUpdate()
        dispatch_source_cancel(updaterWatch_);
        dispatch_release(updaterWatch_);
        updaterWatch_ = nullptr;
        updater_ = 0;
        updateFinished(WIFEXITED(exitStatus) ? WEXITSTATUS(exitStatus) : -1);
    });
    dispatch_resume(updaterWatch_);
}

void Agent::updateFinished(int status) {
    if (status != kUpdateInstalled) {
        if (restartAfterUpdate_) restart("permission granted");
        trimSoon();
        return;
    }
    std::string text;
    if (readFile(dataDir() + "/update-note", text)) updateNote_ = text;
    os_log(gLog, "updated, restarting: %{public}s", updateNote_.c_str());
    publish();
    quit(0, true);  // launchd starts the new version right away
}

void Agent::quit(int code, bool restarting) {
    if (quitting_) return;
    // Give every control back to the mouse so it keeps working while we are not running.
    for (auto& s : sessions_)
        if (s->device->started()) s->device->restore();
    if (!restarting) {
        State s = last_;
        s.running = false;
        s.connected = false;
        saveState(s);
    }
    quitting_ = true;
    hid_.stop();
    std::exit(code);
}

void Agent::askPermissions() {
    bool post = canPostEvents(), listen = canListen();
    if (post && listen) return;
    writeFileAtomic(dataDir() + "/prompted", "");  // look for the grant often for a while
    if (asking_.exchange(true)) return;            // the prompts are on screen already
    // The prompts can block until answered, so never ask on the main thread.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (!post) askForAccessibility();
        if (!listen) askForInputMonitoring();
        asking_ = false;
    });
}

void Agent::watchSystem() {
    // Settings saved by the window.
    notify_register_dispatch(kConfigChanged, &notifyToken_, dispatch_get_main_queue(), ^(int) {
        reloadConfig();
    });
    // `MouseAgent --probe` reconfigured the mouse behind our back.
    notify_register_dispatch(kReapply, &reapplyToken_, dispatch_get_main_queue(), ^(int) {
        applyAll(true);
    });
    // "Allow…" in the window: the system prompts have to come from the agent, which needs them.
    notify_register_dispatch(kAskPermission, &askToken_, dispatch_get_main_queue(), ^(int) {
        askPermissions();
    });
    // The window became active while it shows a missing permission (perhaps back from System
    // Settings): look again, in a new process if Input Monitoring is the one missing.
    notify_register_dispatch(kRecheck, &recheckToken_, dispatch_get_main_queue(), ^(int) {
        refreshPermissions();
        restartIfInputMissing(3);
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
            after(0.2, [a] { a->refreshScrolling(); });
        },
        CFSTR("SwipeScrollDirectionDidChangeNotification"), nullptr, CFNotificationSuspensionBehaviorDeliverImmediately);

    // Permissions are re-checked every few seconds while something is missing, hourly otherwise
    // (a change also arrives as a notification, see above). While Input Monitoring is missing the
    // agent restarts to see a grant: every 10 seconds during the ten minutes after the system
    // prompt or "Allow…", every 15 minutes otherwise.
    CFRunLoopTimerContext ctx = {0, this, nullptr, nullptr, nullptr};
    permissionTimer_ = CFRunLoopTimerCreate(
        kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 3, 3, 0, 0,
        [](CFRunLoopTimerRef t, void* self) {
            auto* a = static_cast<Agent*>(self);
            if (a->missingPermission()) a->refreshPermissions();
            if (a->missingPermission()) a->restartIfInputMissing(fileAge(dataDir() + "/prompted") < 600 ? 10 : 900);
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
                after(3, [a] {
                    a->natural_ = systemNaturalScrolling();
                    a->applyAll(true);
                });
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
    started_ = CFAbsoluteTimeGetCurrent();
    last_ = loadState();  // the state file is written only when something changes
    cfg_ = loadConfig();
    if (usesDock(cfg_)) hiServices("CoreDockSendNotification");
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
    // Look for updates ten minutes after start and then hourly; a check runs at most every six
    // hours (see startUpdate), so updates land within a day.
    every(600, 3600, [this] { startUpdate(); });
    trimSoon();
    CFRunLoopRun();
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
        void modifier(uint16_t k, bool d) override { std::printf("  -> modifier 0x%x %s\n", k, d ? "down" : "up"); }
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
            postNotification(kReapply);  // the background agent sets everything up again
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

// Opened by LaunchServices (Finder, or "Quit & Reopen" in System Settings) instead of launchd:
// restart the launchd job, which then sees any permission granted meanwhile, and leave.
int handOverToLaunchd() {
    std::string target = "gui/" + std::to_string(getuid()) + "/" + kLabel;
    char* argv[] = {const_cast<char*>("/bin/launchctl"), const_cast<char*>("kickstart"), const_cast<char*>("-k"),
                    const_cast<char*>(target.c_str()), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv, environ) == 0) {
        int status = 0;
        waitpid(pid, &status, 0);
    }
    return 0;
}

}  // namespace
}  // namespace mouse::mac

int runAgent(int, char** argv) {
    if (const char* n = getenv("XPC_SERVICE_NAME"); n && std::strncmp(n, "application.app.mouse.agent.", 28) == 0)
        return mouse::mac::handOverToLaunchd();
    // The agent's heap is tiny (about 150 KB), but by default macOS spreads a process's heap over
    // pages per CPU, which here costs half a megabyte. Malloc reads its settings when a process
    // starts, so the agent asks for its space-efficient mode with a single magazine and starts
    // itself once more (execv keeps the process: launchd sees nothing).
    if (!getenv("MallocMaxMagazines") && setenv("MallocSpaceEfficient", "1", 1) == 0 &&
        setenv("MallocMaxMagazines", "1", 1) == 0) {
        std::string exe = mouse::mac::selfPath();
        if (!exe.empty()) execv(exe.c_str(), argv);  // returns only on failure: carry on as we are
    }
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
