// Settings window process: opened from Finder or Spotlight to change settings, quits when the
// window closes. Also renders the documentation snapshots (--snapshot <dir>).
#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>

#include <notify.h>

#include <cstring>

#import "ui/settings.hpp"
#import "ui/ui.hpp"

int runSettingsWindow(int argc, char** argv);

// Asks the background agent to show the system permission prompts for itself (window -> agent).

// ============================================================================ app delegate

@interface MAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic) MSettings* settings;
@end

@implementation MAppDelegate {
    int _stateToken;
    BOOL _registeredToken;
    BOOL _agentRefused;
    id _activeObserver;
}

- (void)applicationWillFinishLaunching:(NSNotification*)note {
    [self buildMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    _settings = [[MSettings alloc] initWithConfig:mouse::loadConfig() state:mouse::loadState() snapshot:NO];
    __weak MAppDelegate* weakSelf = self;
    _settings.onAllow = ^{
        // The agent owns the permissions: it shows the system prompts, we open the settings pane.
        mouse::postNotification(mouse::kAskPermission);
        NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"];
        [NSWorkspace.sharedWorkspace openURL:url];
    };
    _settings.onOpenLoginItems = ^{
        [SMAppService openSystemSettingsLoginItems];
    };

    __weak MSettings* weakSettings = _settings;
    if (notify_register_dispatch(mouse::kStateChanged, &_stateToken, dispatch_get_main_queue(), ^(int) {
            [weakSettings setState:mouse::loadState()];
        }) == NOTIFY_STATUS_OK)
        _registeredToken = YES;
    [_settings setState:mouse::loadState()];  // in case it changed before we started listening

    [self registerAgent];
    _activeObserver = [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidBecomeActiveNotification
                                                                      object:nil
                                                                       queue:nil
                                                                  usingBlock:^(NSNotification*) {
                                                                      [weakSelf updateLoginNote];
                                                                  }];

    [_settings.window makeKeyAndOrderFront:nil];
    if (@available(macOS 14.0, *)) [NSApp activate];
    else [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { return YES; }

- (BOOL)applicationShouldHandleReopen:(NSApplication*)app hasVisibleWindows:(BOOL)visible {
    [_settings.window makeKeyAndOrderFront:nil];
    return NO;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app { return YES; }

- (void)applicationWillTerminate:(NSNotification*)note {
    if (_registeredToken) notify_cancel(_stateToken);
    _registeredToken = NO;
    if (_activeObserver) [NSNotificationCenter.defaultCenter removeObserver:_activeObserver];
    _activeObserver = nil;
}

// ---------------------------------------------------------------------------- background agent

static BOOL installedInApplications() {
    NSString* path = NSBundle.mainBundle.bundlePath.stringByStandardizingPath;
    if (![path.pathExtension isEqualToString:@"app"]) return NO;
    NSString* user = [NSHomeDirectory() stringByAppendingPathComponent:@"Applications/"];
    return [path hasPrefix:@"/Applications/"] || [path hasPrefix:user];
}

// The background agent runs as a classic per-user LaunchAgent pointing into this bundle. (A
// ServiceManagement registration would pin the agent to its exact code hash, because the app
// has no Team ID, and launchd would then refuse to start it after every automatic update.)
static NSString* agentPlistPath() {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents/app.mouse.agent.plist"];
}

static NSDictionary* agentPlist() {
    NSString* program = [NSBundle.mainBundle.bundlePath
        stringByAppendingPathComponent:@"Contents/Helpers/MouseAgent.app/Contents/MacOS/MouseAgent"];
    return @{
        @"Label" : @"app.mouse.agent",
        @"Program" : program,
        @"AssociatedBundleIdentifiers" : @[ @"app.mouse.Mouse" ],
        @"RunAtLoad" : @YES,
        @"KeepAlive" : @YES,
        @"ProcessType" : @"Interactive",
        @"LimitLoadToSessionType" : @"Aqua",
    };
}

static int launchctl(NSArray<NSString*>* args) {
    NSTask* task = [NSTask new];
    task.executableURL = [NSURL fileURLWithPath:@"/bin/launchctl"];
    task.arguments = args;
    task.standardOutput = NSFileHandle.fileHandleWithNullDevice;
    task.standardError = NSFileHandle.fileHandleWithNullDevice;
    NSError* error = nil;
    if (![task launchAndReturnError:&error]) return -1;
    [task waitUntilExit];
    return task.terminationStatus;
}

// Installs (or repairs) the LaunchAgent and makes sure it is loaded. Returns NO if macOS refused
// to load it, which happens when Mouse is switched off in Login Items.
static BOOL installAgent(BOOL restart) {
    NSString* domain = [NSString stringWithFormat:@"gui/%u", getuid()];
    NSString* service = [domain stringByAppendingString:@"/app.mouse.agent"];
    // Older builds registered through ServiceManagement: retire that registration.
    SMAppService* svc = [SMAppService agentServiceWithPlistName:@"app.mouse.agent.plist"];
    if (svc.status != SMAppServiceStatusNotRegistered) [svc unregisterAndReturnError:nil];

    NSString* path = agentPlistPath();
    NSDictionary* want = agentPlist();
    NSDictionary* have = [NSDictionary dictionaryWithContentsOfFile:path];
    BOOL changed = ![have isEqualToDictionary:want];
    if (changed) {
        [NSFileManager.defaultManager createDirectoryAtPath:path.stringByDeletingLastPathComponent
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:nil];
        if (![want writeToURL:[NSURL fileURLWithPath:path] error:nil]) return NO;
    }
    BOOL loaded = launchctl(@[ @"print", service ]) == 0;
    if (loaded && (changed || restart)) {
        launchctl(@[ @"bootout", service ]);
        loaded = NO;
    }
    if (!loaded && launchctl(@[ @"bootstrap", domain, path ]) != 0) return NO;
    return YES;
}

// `Mouse --register-agent [--force]`: installs the LaunchAgent without showing any UI;
// --force reloads it even if nothing changed.
int registerAgentHeadless(bool force) {
    @autoreleasepool {
        if (!installedInApplications()) {
            fprintf(stderr, "Mouse: not in Applications, not installing the agent\n");
            return 2;
        }
        return installAgent(force) ? 0 : 1;
    }
}

int unregisterAgentHeadless() {
    @autoreleasepool {
        launchctl(@[ @"bootout", [NSString stringWithFormat:@"gui/%u/app.mouse.agent", getuid()] ]);
        [NSFileManager.defaultManager removeItemAtPath:agentPlistPath() error:nil];
        SMAppService* svc = [SMAppService agentServiceWithPlistName:@"app.mouse.agent.plist"];
        if (svc.status != SMAppServiceStatusNotRegistered) [svc unregisterAndReturnError:nil];
        return 0;
    }
}

- (void)registerAgent {
    if (!installedInApplications()) return;  // never from a build directory
    _agentRefused = !installAgent(NO);
    _settings.needsLoginApproval = _agentRefused;
}

- (void)updateLoginNote {
    if (!installedInApplications()) return;
    if (_agentRefused) _agentRefused = !installAgent(NO);  // maybe switched on in Login Items meanwhile
    _settings.needsLoginApproval = _agentRefused;
}

// ---------------------------------------------------------------------------- menu

- (void)buildMenu {
    NSMenu* bar = [NSMenu new];
    NSMenuItem* appItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu* app = [[NSMenu alloc] initWithTitle:@"Mouse"];
    [app addItemWithTitle:@"About Mouse" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:@"Hide Mouse" action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* others = [app addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    others.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
    [app addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:@"Quit Mouse" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = app;

    NSMenuItem* winItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu* win = [[NSMenu alloc] initWithTitle:@"Window"];
    [win addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
    [win addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    winItem.submenu = win;
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = win;
}

@end

// ============================================================================ snapshots

static bool writeSnapshot(MSettings* s, NSString* dir, NSString* name, BOOL dark) {
    NSWindow* w = s.window;
    w.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    NSView* v = s.rootView;
    __block NSBitmapImageRep* rep = nil;
    [w.appearance performAsCurrentDrawingAppearance:^{
        [v layoutSubtreeIfNeeded];
        NSSize size = v.bounds.size;
        rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                      pixelsWide:(NSInteger)(size.width * 2)
                                                      pixelsHigh:(NSInteger)(size.height * 2)
                                                   bitsPerSample:8
                                                 samplesPerPixel:4
                                                        hasAlpha:YES
                                                        isPlanar:NO
                                                  colorSpaceName:NSCalibratedRGBColorSpace
                                                     bytesPerRow:0
                                                    bitsPerPixel:0];
        rep.size = size;
        [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
    }];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    NSString* path = [dir stringByAppendingPathComponent:name];
    if (![png writeToFile:path atomically:YES]) {
        fprintf(stderr, "Could not write %s\n", path.fileSystemRepresentation);
        return false;
    }
    printf("Wrote %s\n", path.fileSystemRepresentation);
    return true;
}

static int runSnapshots(NSString* dir) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    ui::gForcedScale = 2;

    mouse::State demo;
    demo.running = true;
    demo.connected = true;
    demo.name = "MX Master 3S";
    demo.battery = 85;
    demo.version = "1.0.0";
    demo.trusted = true;
    demo.inputAccess = true;
    mouse::Config cfg;  // defaults, never saved

    bool ok = true;
    MSettings* s = [[MSettings alloc] initWithConfig:cfg state:demo snapshot:YES];
    [s showPage:0];
    ok &= writeSnapshot(s, dir, @"buttons-light.png", NO);
    ok &= writeSnapshot(s, dir, @"buttons-dark.png", YES);
    [s showPage:1];
    ok &= writeSnapshot(s, dir, @"scroll-light.png", NO);
    ok &= writeSnapshot(s, dir, @"scroll-dark.png", YES);

    mouse::Config shortcut = cfg;
    shortcut.button(mouse::Control::Thumb) = {mouse::Act::Shortcut, {0x30, mouse::kCmd}};       // ⌘⇥
    shortcut.thumbWheel = mouse::WheelAct::Shortcuts;
    shortcut.thumbUp = {0x0D, mouse::kCmd};                                                     // ⌘W
    shortcut.thumbDown = {0x11, mouse::kShift | mouse::kCmd};                                   // ⇧⌘T
    s = [[MSettings alloc] initWithConfig:shortcut state:demo snapshot:YES];
    [s showPage:0];
    ok &= writeSnapshot(s, dir, @"buttons-shortcut-light.png", NO);

    s = [[MSettings alloc] initWithConfig:cfg state:demo snapshot:YES];
    [s showPage:0];
    [s setHoverPart:3];
    ok &= writeSnapshot(s, dir, @"buttons-gestures-light.png", NO);

    mouse::State untrusted = demo;
    untrusted.trusted = false;
    s = [[MSettings alloc] initWithConfig:cfg state:untrusted snapshot:YES];
    [s showPage:0];
    ok &= writeSnapshot(s, dir, @"permission-light.png", NO);

    mouse::State away = demo;
    away.connected = false;
    away.battery = -1;
    s = [[MSettings alloc] initWithConfig:cfg state:away snapshot:YES];
    [s showPage:0];
    ok &= writeSnapshot(s, dir, @"disconnected-light.png", NO);
    return ok ? 0 : 1;
}

// ============================================================================ entry point

int runSettingsWindow(int argc, char** argv) {
    @autoreleasepool {
        for (int i = 1; i < argc; i++) {
            if (std::strcmp(argv[i], "--snapshot") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "usage: Mouse --snapshot <directory>\n");
                    return 2;
                }
                return runSnapshots([NSString stringWithUTF8String:argv[i + 1]]);
            }
        }
        NSApplication* app = [NSApplication sharedApplication];
        MAppDelegate* delegate = [MAppDelegate new];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
