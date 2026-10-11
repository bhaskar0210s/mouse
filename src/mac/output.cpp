// Real input for macOS.
//
// Keys go through the IOKit HID event system (IOHIDPostEvent): on current macOS, keyboard
// events posted with CGEventPost by apps that are not notarised never reach other apps,
// while HID-system events behave exactly like a physical key press (system shortcuts such as
// ⌘⇥, ⌃← and the Dictation key included). Mouse buttons and scrolling use Quartz events.
#include "output.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <IOKit/hidsystem/ev_keymap.h>
#include <dlfcn.h>
#include <mach/mach_time.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace mouse::mac {
namespace {

constexpr uint16_t kKeyDictation = 0xB0;

struct Modifier {
    uint32_t bit;      // our Shortcut bit
    uint16_t key;      // left-hand key code
    uint32_t nx;       // device independent flag
    uint32_t device;   // device dependent (left key) flag
    CGEventFlags cg;
};

constexpr Modifier kModifiers[] = {
    {kCtrl, 0x3B, NX_CONTROLMASK, NX_DEVICELCTLKEYMASK, kCGEventFlagMaskControl},
    {kOpt, 0x3A, NX_ALTERNATEMASK, NX_DEVICELALTKEYMASK, kCGEventFlagMaskAlternate},
    {kShift, 0x38, NX_SHIFTMASK, NX_DEVICELSHIFTKEYMASK, kCGEventFlagMaskShift},
    {kCmd, 0x37, NX_COMMANDMASK, NX_DEVICELCMDKEYMASK, kCGEventFlagMaskCommand},
};

// Keys that macOS reports with the Fn flag (and arrows also with the keypad flag).
uint32_t extraFlags(uint16_t key) {
    switch (key) {
        case 0x7B: case 0x7C: case 0x7D: case 0x7E:
            return NX_SECONDARYFNMASK | NX_NUMERICPADMASK;
        case 0x73: case 0x74: case 0x75: case 0x77: case 0x79:
        case 0x7A: case 0x78: case 0x63: case 0x76: case 0x60: case 0x61: case 0x62: case 0x64:
        case 0x65: case 0x6D: case 0x67: case 0x6F: case 0x69: case 0x6B: case 0x71: case 0x6A:
        case 0x40: case 0x4F: case 0x50: case 0x5A:
            return NX_SECONDARYFNMASK;
        default:
            return 0;
    }
}

using CoreDockSendNotificationFn = void (*)(CFStringRef, void*);

}  // namespace

MacOutput::MacOutput() {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    tick_ = double(tb.numer) / tb.denom / 1e9;
}

MacOutput::~MacOutput() {
    if (hid_) IOServiceClose(hid_);
}

double MacOutput::now() { return mach_absolute_time() * tick_; }

bool MacOutput::connect() {
    if (hid_) return true;
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching(kIOHIDSystemClass));
    if (!svc) return false;
    io_connect_t c = 0;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), kIOHIDParamConnectType, &c);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) return false;
    hid_ = c;
    return true;
}

void MacOutput::post(uint32_t type, uint16_t key, uint32_t flags) {
    if (!connect()) return;
    NXEventData ev = {};
    ev.key.keyCode = key;
    ev.key.origCharSet = ev.key.charSet = NX_ASCIISET;
    IOGPoint loc = {0, 0};
    IOOptionBits options = kIOHIDSetGlobalEventFlags;
    if (IOHIDPostEvent(hid_, type, loc, &ev, kNXEventDataVersion, flags, options) != KERN_SUCCESS) {
        // The connection can go stale (e.g. after the HID system restarts); retry once.
        IOServiceClose(hid_);
        hid_ = 0;
        if (connect()) IOHIDPostEvent(hid_, type, loc, &ev, kNXEventDataVersion, flags, options);
    }
}

void MacOutput::key(const Shortcut& s) {
    if (!s.valid()) return;
    // Modifiers that are down right now (keys the user is holding, Caps Lock) are restored after
    // the shortcut, which itself is sent with exactly its own modifiers.
    constexpr uint32_t kHeld = NX_ALPHASHIFTMASK | NX_SHIFTMASK | NX_CONTROLMASK | NX_ALTERNATEMASK | NX_COMMANDMASK |
                               NX_SECONDARYFNMASK | NX_DEVICELCTLKEYMASK | NX_DEVICELSHIFTKEYMASK |
                               NX_DEVICERSHIFTKEYMASK | NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK |
                               NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK | NX_DEVICERCTLKEYMASK;
    uint32_t before = static_cast<uint32_t>(CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState)) & kHeld;
    uint32_t flags = before & NX_ALPHASHIFTMASK;
    uint16_t lastModifier = 0;
    for (const auto& m : kModifiers) {
        if (!(s.mods & m.bit)) continue;
        flags |= m.nx | m.device;
        post(NX_FLAGSCHANGED, m.key, flags);
        lastModifier = m.key;
    }
    uint32_t keyFlags = flags | extraFlags(s.key) | ((s.mods & kFn) ? NX_SECONDARYFNMASK : 0);
    post(NX_KEYDOWN, s.key, keyFlags);
    post(NX_KEYUP, s.key, keyFlags);
    uint32_t now = keyFlags;
    for (int i = 3; i >= 0; i--) {
        const auto& m = kModifiers[i];
        if (!(s.mods & m.bit)) continue;
        flags &= ~(m.nx | m.device);
        post(NX_FLAGSCHANGED, m.key, flags);
        now = flags;
    }
    // Leave the system exactly as it was (no Fn left over from an arrow key, held keys kept).
    if (now != before) post(NX_FLAGSCHANGED, lastModifier ? lastModifier : 0x3F, before);
}

void MacOutput::dictation() {
    post(NX_KEYDOWN, kKeyDictation, 0);
    post(NX_KEYUP, kKeyDictation, 0);
}

void MacOutput::media(int key) {
    if (!connect()) return;
    IOGPoint loc = {0, 0};
    for (int down = 1; down >= 0; down--) {
        NXEventData ev = {};
        ev.compound.subType = NX_SUBTYPE_AUX_CONTROL_BUTTONS;
        ev.compound.misc.L[0] = (key << 16) | ((down ? NX_KEYDOWN : NX_KEYUP) << 8);
        IOHIDPostEvent(hid_, NX_SYSDEFINED, loc, &ev, kNXEventDataVersion, 0, FALSE);
    }
}

void MacOutput::button(int b, bool down, uint32_t mods) {
    CGEventRef here = CGEventCreate(nullptr);
    CGPoint at = here ? CGEventGetLocation(here) : CGPointZero;
    if (here) CFRelease(here);
    CGEventType type;
    CGMouseButton cgb = static_cast<CGMouseButton>(b);
    if (b == kButtonLeft) type = down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
    else if (b == kButtonRight) type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp;
    else type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
    CGEventRef e = CGEventCreateMouseEvent(nullptr, type, at, cgb);
    if (!e) return;
    CGEventFlags flags = 0;
    for (const auto& m : kModifiers)
        if (mods & m.bit) flags |= m.cg;
    if (flags) CGEventSetFlags(e, flags);
    CGEventSetIntegerValueField(e, kCGMouseEventClickState, 1);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

void MacOutput::navigate(bool forward) {
    button(forward ? kButtonForward : kButtonBack, true, 0);
    button(forward ? kButtonForward : kButtonBack, false, 0);
}

void MacOutput::dock(Dock d) {
    static auto send = reinterpret_cast<CoreDockSendNotificationFn>(dlsym(RTLD_DEFAULT, "CoreDockSendNotification"));
    if (!send) return;
    CFStringRef name = d == Dock::MissionControl ? CFSTR("com.apple.expose.awake")
                       : d == Dock::AppExpose    ? CFSTR("com.apple.expose.front.awake")
                                                 : CFSTR("com.apple.showdesktop.awake");
    send(name, nullptr);
}

void MacOutput::scroll(int dy, int dx, bool continuous) {
    if (!dy && !dx) return;
    CGEventRef e = CGEventCreateScrollWheelEvent2(nullptr, continuous ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine,
                                                  2, dy, dx, 0);
    if (!e) return;
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

}  // namespace mouse::mac
