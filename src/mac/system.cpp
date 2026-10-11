#include "system.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <dlfcn.h>

namespace mouse::mac {

void* hiServices(const char* symbol) {
    static void* lib = dlopen(
        "/System/Library/Frameworks/ApplicationServices.framework/Frameworks/HIServices.framework/HIServices",
        RTLD_LAZY | RTLD_LOCAL);
    return lib ? dlsym(lib, symbol) : nullptr;
}

bool canPostEvents() {
    if (IOHIDCheckAccess(kIOHIDRequestTypePostEvent) == kIOHIDAccessTypeGranted) return true;
    // Access granted under Accessibility may only show up here.
    static auto trusted = reinterpret_cast<Boolean (*)()>(hiServices("AXIsProcessTrusted"));
    return trusted && trusted();
}

bool canListen() { return IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted; }

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

void askForAccessibility() {
    auto prompt = reinterpret_cast<Boolean (*)(CFDictionaryRef)>(hiServices("AXIsProcessTrustedWithOptions"));
    auto key = static_cast<const CFStringRef*>(hiServices("kAXTrustedCheckOptionPrompt"));
    if (prompt && key && *key) {
        const void* keys[] = {*key};
        const void* vals[] = {kCFBooleanTrue};
        CFDictionaryRef opts = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
        prompt(opts);
        CFRelease(opts);
    }
    IOHIDRequestAccess(kIOHIDRequestTypePostEvent);
}

void askForInputMonitoring() { IOHIDRequestAccess(kIOHIDRequestTypeListenEvent); }

}  // namespace mouse::mac
