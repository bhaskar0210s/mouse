// The settings window: two pages ("Buttons", "Point & Scroll"), a permission banner and a footer
// with the device status. Every change is saved immediately.
#pragma once

#import <AppKit/AppKit.h>

#include "core/config.hpp"
#include "core/store.hpp"

@interface MSettings : NSObject <NSWindowDelegate>
// snapshot: never saves the configuration (used by --snapshot).
- (instancetype)initWithConfig:(const mouse::Config&)config
                         state:(const mouse::State&)state
                      snapshot:(BOOL)snapshot;
@property(nonatomic, readonly) NSWindow* window;
@property(nonatomic, readonly) NSView* rootView;

- (void)setState:(const mouse::State&)state;
- (void)setConfig:(const mouse::Config&)config;
- (void)showPage:(NSInteger)page;  // 0 Buttons, 1 Point & Scroll
- (void)setHoverPart:(int)part;    // snapshots: show the hover link between row and illustration

// Permission banner, shown while the running agent lacks Accessibility or Input Monitoring
// (taken from the agent's state; the window process itself needs no permission).
@property(nonatomic, readonly) BOOL needsPermission;
@property(nonatomic, copy) void (^onAllow)(void);

// Footer note shown while the background agent waits for approval in Login Items.
@property(nonatomic) BOOL needsLoginApproval;
@property(nonatomic, copy) void (^onOpenLoginItems)(void);
@end
