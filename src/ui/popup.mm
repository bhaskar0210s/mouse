// Popup button, pull-down button and their list. The list is a borderless child panel with menu
// material that shows and hides instantly (NSMenu fades out), highlights on hover, selects on click
// or press-drag-release, and handles arrow keys, Return and Esc. Clicking anywhere else closes it.
#import "ui/ui.hpp"

#include <algorithm>
#include <cmath>

using namespace ui;

static const CGFloat kPadV = 5;       // above the first and below the last item
static const CGFloat kItemH = 24;
static const CGFloat kSepH = 11;
static const CGFloat kTextX = 28;     // title x inside the list
static const CGFloat kRightPad = 20;
static const CGFloat kRadius = 10;
static const CGFloat kPopupPadL = 8;  // title x inside the popup button
static const CGFloat kChevronW = 16;
static const CGFloat kImage = 16;      // item images
static const CGFloat kImageGap = 6;
static const CGFloat kPullGap = 4;     // between a pull-down button and its list
static const CGFloat kPullPadL = 12;   // title x inside a pull-down button
static const CGFloat kPullPadR = 10;   // right of its chevron

@interface MPopup ()
- (BOOL)pullsDown;
@end

static bool hasImages(const std::vector<MenuItem>& items) {
    for (const MenuItem& it : items)
        if (it.image) return true;
    return false;
}

@class MMenu;
static MMenu* gMenu;  // the open list, if any

// An item's image: app icons as they are, symbols (templates) in the text colour.
static void drawItemImage(NSView* v, NSImage* image, NSRect r, BOOL highlighted) {
    if (image.isTemplate) {
        NSColor* c = highlighted ? [NSColor selectedMenuItemTextColor] : [NSColor secondaryLabelColor];
        drawSymbol(v, image, NSMakePoint(NSMidX(r), NSMidY(r)), c);
    } else {
        [image drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    }
}

// ============================================================================ list view

@interface MMenuList : NSView
@property(nonatomic) std::vector<MenuItem> items;
@property(nonatomic) int selectedTag;
@property(nonatomic) NSInteger highlight;
@property(nonatomic, weak) MMenu* owner;
- (NSInteger)indexAt:(NSPoint)p;
- (CGFloat)offsetOf:(NSInteger)index;
- (CGFloat)heightOf:(NSInteger)index;
@end

@interface MMenu : NSObject
- (instancetype)initWithPopup:(MPopup*)popup;
- (void)show;
- (void)close;
- (void)chooseIndex:(NSInteger)i;
- (void)trackScreenPoint:(NSPoint)p;
- (BOOL)isOverItemAtScreenPoint:(NSPoint)p;
- (void)chooseAtScreenPoint:(NSPoint)p;
@end

@interface MMenuPanel : NSPanel
@end

@implementation MMenuPanel
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@implementation MMenuList {
    NSTrackingArea* _area;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _highlight = -1;
        _area = [[NSTrackingArea alloc]
            initWithRect:NSZeroRect
                 options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways |
                         NSTrackingInVisibleRect
                   owner:self
                userInfo:nil];
        [self addTrackingArea:_area];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (CGFloat)heightOf:(NSInteger)i { return _items[(size_t)i].tag < 0 ? kSepH : kItemH; }

- (CGFloat)offsetOf:(NSInteger)index {
    CGFloat y = kPadV;
    for (NSInteger i = 0; i < index; i++) y += [self heightOf:i];
    return y;
}

- (NSInteger)indexAt:(NSPoint)p {
    if (p.x < 0 || p.x > self.bounds.size.width) return -1;
    CGFloat y = kPadV;
    for (NSInteger i = 0; i < (NSInteger)_items.size(); i++) {
        CGFloat h = [self heightOf:i];
        if (p.y >= y && p.y < y + h) return _items[(size_t)i].tag < 0 ? -1 : i;
        y += h;
    }
    return -1;
}

- (void)setHighlight:(NSInteger)highlight {
    if (_highlight == highlight) return;
    _highlight = highlight;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    NSFont* f = font(13);
    NSImage* check = symbol(@"checkmark", 11, NSFontWeightSemibold);
    CGFloat textX = kTextX + (hasImages(_items) ? kImage + kImageGap : 0);
    CGFloat y = kPadV;
    for (NSInteger i = 0; i < (NSInteger)_items.size(); i++) {
        const MenuItem& it = _items[(size_t)i];
        CGFloat h = [self heightOf:i];
        if (it.tag < 0) {
            [[NSColor separatorColor] setFill];
            NSRectFillUsingOperation(NSMakeRect(kTextX - 8, snap(self, y + h / 2), b.size.width - (kTextX - 8) - 14, pixel(self)),
                                     NSCompositingOperationSourceOver);
        } else {
            BOOL hi = i == _highlight;
            if (hi) fillRounded(NSMakeRect(5, y, b.size.width - 10, h), 6, [NSColor controlAccentColor]);
            NSColor* c = hi ? [NSColor selectedMenuItemTextColor] : [NSColor labelColor];
            if (it.tag == _selectedTag) drawSymbol(self, check, NSMakePoint(17, y + h / 2), c);
            if (it.image) drawItemImage(self, it.image, NSMakeRect(kTextX, y + (h - kImage) / 2, kImage, kImage), hi);
            drawText(self, it.title, f, c, textX, y + h / 2);
        }
        y += h;
    }
    // Hairline edge, like system menus.
    NSColor* edge = dynamicColor([NSColor colorWithWhite:0 alpha:0.12], [NSColor colorWithWhite:1 alpha:0.16]);
    strokeRounded(self, b, kRadius, edge);
}

- (void)mouseMoved:(NSEvent*)event {
    self.highlight = [self indexAt:[self convertPoint:event.locationInWindow fromView:nil]];
}
- (void)mouseEntered:(NSEvent*)event { [self mouseMoved:event]; }
- (void)mouseExited:(NSEvent*)event { self.highlight = -1; }
- (void)mouseDown:(NSEvent*)event { [self mouseMoved:event]; }
- (void)mouseDragged:(NSEvent*)event { [self mouseMoved:event]; }

- (void)mouseUp:(NSEvent*)event {
    NSInteger i = [self indexAt:[self convertPoint:event.locationInWindow fromView:nil]];
    if (i >= 0) [_owner chooseIndex:i];
}

@end

// ============================================================================ menu controller

@implementation MMenu {
    __weak MPopup* _popup;
    MMenuPanel* _panel;
    MMenuList* _list;
    id _localMouse, _globalMouse, _keys;
    NSMutableArray* _observers;
}

static NSImage* roundedMask(CGFloat r) {
    NSImage* img = [NSImage imageWithSize:NSMakeSize(2 * r + 1, 2 * r + 1)
                                  flipped:NO
                           drawingHandler:^BOOL(NSRect rect) {
                               [[NSColor blackColor] setFill];
                               [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:r yRadius:r] fill];
                               return YES;
                           }];
    img.capInsets = NSEdgeInsetsMake(r, r, r, r);
    img.resizingMode = NSImageResizingModeStretch;
    return img;
}

- (instancetype)initWithPopup:(MPopup*)popup {
    if ((self = [super init])) {
        _popup = popup;
        _observers = [NSMutableArray array];
    }
    return self;
}

- (void)show {
    MPopup* popup = _popup;
    NSWindow* parent = popup.window;
    if (!popup || !parent) return;

    _list = [[MMenuList alloc] initWithFrame:NSZeroRect];
    _list.items = popup.items;
    _list.selectedTag = popup.selectedTag;
    _list.owner = self;

    NSFont* f = font(13);
    CGFloat w = 0, h = 2 * kPadV;
    NSInteger selIndex = -1;
    for (size_t i = 0; i < popup.items.size(); i++) {
        const MenuItem& it = popup.items[i];
        if (it.tag >= 0) w = std::max(w, textWidth(it.title, f));
        if (it.tag >= 0 && it.tag == popup.selectedTag && selIndex < 0) selIndex = (NSInteger)i;
        h += it.tag < 0 ? kSepH : kItemH;
    }
    CGFloat imageColumn = hasImages(popup.items) ? kImage + kImageGap : 0;
    BOOL pull = popup.pullsDown;
    CGFloat minW = pull ? popup.bounds.size.width : popup.bounds.size.width + kTextX - [popup textInset] + 8;
    w = std::ceil(std::max(kTextX + imageColumn + w + kRightPad, minW));

    NSRect btn = [parent convertRectToScreen:[popup convertRect:popup.bounds toView:nil]];
    NSRect frame;
    if (pull) {
        // Right below the button, like a native pull-down menu.
        frame = NSMakeRect(NSMinX(btn), NSMinY(btn) - kPullGap - h, w, h);
    } else {
        // Line the current item up with the button's title, like a native popup menu.
        CGFloat x = NSMinX(btn) + [popup textInset] - kTextX;
        CGFloat itemMid = selIndex >= 0 ? [_list offsetOf:selIndex] + kItemH / 2 : kPadV + kItemH / 2;
        CGFloat top = NSMidY(btn) + itemMid;  // screen coordinates grow upwards
        frame = NSMakeRect(x, top - h, w, h);
    }
    NSScreen* screen = parent.screen ?: NSScreen.mainScreen;
    if (screen) {
        NSRect vf = NSInsetRect(screen.visibleFrame, 4, 4);
        if (pull && NSMinY(frame) < NSMinY(vf)) frame.origin.y = NSMaxY(btn) + kPullGap;  // no room below: above
        if (NSMinY(frame) < NSMinY(vf)) frame.origin.y = NSMinY(vf);
        if (NSMaxY(frame) > NSMaxY(vf)) frame.origin.y = NSMaxY(vf) - h;
        if (NSMaxX(frame) > NSMaxX(vf)) frame.origin.x = NSMaxX(vf) - w;
        if (NSMinX(frame) < NSMinX(vf)) frame.origin.x = NSMinX(vf);
    }

    _panel = [[MMenuPanel alloc] initWithContentRect:frame
                                           styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    _panel.releasedWhenClosed = NO;
    _panel.opaque = NO;
    _panel.backgroundColor = NSColor.clearColor;
    _panel.hasShadow = YES;
    _panel.level = NSPopUpMenuWindowLevel;
    _panel.animationBehavior = NSWindowAnimationBehaviorNone;
    _panel.becomesKeyOnlyIfNeeded = YES;
    _panel.appearance = parent.effectiveAppearance;

    NSVisualEffectView* fx = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    fx.material = NSVisualEffectMaterialMenu;
    fx.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    fx.state = NSVisualEffectStateActive;
    fx.maskImage = roundedMask(kRadius);
    _panel.contentView = fx;
    _list.frame = fx.bounds;
    _list.highlight = selIndex;
    [fx addSubview:_list];

    [parent addChildWindow:_panel ordered:NSWindowAbove];
    [_panel orderFront:nil];
    gMenu = self;
    popup.pressed = YES;

    __weak MMenu* weakSelf = self;
    NSEventMask clicks = NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown;
    _localMouse = [NSEvent addLocalMonitorForEventsMatchingMask:clicks
                                                        handler:^NSEvent*(NSEvent* e) {
                                                            MMenu* m = weakSelf;
                                                            if (!m || e.window == m->_panel) return e;
                                                            [m close];
                                                            return nil;  // the click only closes the list
                                                        }];
    _globalMouse = [NSEvent addGlobalMonitorForEventsMatchingMask:clicks
                                                          handler:^(NSEvent*) {
                                                              [weakSelf close];
                                                          }];
    _keys = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                  handler:^NSEvent*(NSEvent* e) {
                                                      [weakSelf handleKey:e];
                                                      return nil;
                                                  }];
    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    void (^closer)(NSNotification*) = ^(NSNotification*) {
        [weakSelf close];
    };
    [_observers addObject:[nc addObserverForName:NSApplicationDidResignActiveNotification
                                          object:nil
                                           queue:nil
                                      usingBlock:closer]];
    [_observers addObject:[nc addObserverForName:NSWindowDidResignKeyNotification
                                          object:parent
                                           queue:nil
                                      usingBlock:closer]];
    [_observers addObject:[nc addObserverForName:NSWindowWillCloseNotification
                                          object:parent
                                           queue:nil
                                      usingBlock:closer]];
}

- (void)handleKey:(NSEvent*)e {
    NSInteger n = (NSInteger)_list.items.size();
    auto step = [&](NSInteger dir) {
        NSInteger i = _list.highlight;
        for (NSInteger k = 0; k < n; k++) {
            i = i < 0 ? (dir > 0 ? 0 : n - 1) : i + dir;
            if (i < 0 || i >= n) return;
            if (_list.items[(size_t)i].tag >= 0) {
                _list.highlight = i;
                return;
            }
        }
    };
    switch (e.keyCode) {
        case 0x35: [self close]; break;                              // Esc
        case 0x7E: step(-1); break;                                  // up
        case 0x7D: step(1); break;                                   // down
        case 0x24: case 0x4C: case 0x31:                             // Return, Enter, Space
            if (_list.highlight >= 0) [self chooseIndex:_list.highlight];
            else [self close];
            break;
        default: break;
    }
}

- (void)close {
    if (!_panel) return;
    NSWindow* parent = _panel.parentWindow;
    [parent removeChildWindow:_panel];
    [_panel orderOut:nil];
    _panel = nil;
    if (_localMouse) [NSEvent removeMonitor:_localMouse];
    if (_globalMouse) [NSEvent removeMonitor:_globalMouse];
    if (_keys) [NSEvent removeMonitor:_keys];
    _localMouse = _globalMouse = _keys = nil;
    for (id o in _observers) [NSNotificationCenter.defaultCenter removeObserver:o];
    [_observers removeAllObjects];
    MPopup* popup = _popup;
    popup.pressed = NO;
    if (gMenu == self) gMenu = nil;
}

- (void)chooseIndex:(NSInteger)i {
    NS_VALID_UNTIL_END_OF_SCOPE MMenu* keep = self;
    MPopup* popup = _popup;
    int tag = (i >= 0 && i < (NSInteger)_list.items.size()) ? _list.items[(size_t)i].tag : -1;
    [self close];
    if (popup && tag >= 0) [popup choose:tag];
}

- (NSPoint)listPoint:(NSPoint)screen {
    if (!_panel) return NSMakePoint(-1, -1);
    NSPoint w = [_panel convertPointFromScreen:screen];
    return [_list convertPoint:w fromView:nil];
}

- (void)trackScreenPoint:(NSPoint)p { _list.highlight = [_list indexAt:[self listPoint:p]]; }
- (BOOL)isOverItemAtScreenPoint:(NSPoint)p { return [_list indexAt:[self listPoint:p]] >= 0; }
- (void)chooseAtScreenPoint:(NSPoint)p { [self chooseIndex:[_list indexAt:[self listPoint:p]]]; }

@end

// ============================================================================ MPopup

@implementation MPopup {
    std::vector<MenuItem> _items;
    NSString* _title;
    NSImage* _image;
}

- (const std::vector<MenuItem>&)items { return _items; }
- (CGFloat)textInset { return kPopupPadL; }
- (BOOL)pullsDown { return NO; }

- (void)setItems:(const std::vector<MenuItem>&)items selected:(int)tag {
    _items = items;
    _selectedTag = tag;
    if (self.pullsDown) return;  // keeps its own title and size
    _title = @"";
    _image = nil;
    for (const MenuItem& it : _items) {
        if (it.tag != tag || it.tag < 0) continue;
        _title = it.title;
        _image = it.image;
    }
    CGFloat imageW = _image ? kImage + kImageGap : 0;
    NSRect f = self.frame;
    f.size = NSMakeSize(std::ceil(kPopupPadL + imageW + textWidth(_title, font(13)) + 6 + kChevronW), 24);
    self.frame = f;
    self.needsDisplay = YES;
}

- (void)choose:(int)tag {
    NS_VALID_UNTIL_END_OF_SCOPE MPopup* keep = self;
    if (self.pullsDown) {  // a command: nothing to select
        if (_onSelect) _onSelect(tag);
        return;
    }
    if (tag == _selectedTag) return;
    CGFloat right = NSMaxX(self.frame);
    [self setItems:_items selected:tag];
    NSRect f = self.frame;
    f.origin.x = right - f.size.width;  // stay right aligned
    self.frame = f;
    if (_onSelect) _onSelect(tag);
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    if (self.pressed) fillRounded(b, 6, pressFill());
    else if (self.hovered) fillRounded(b, 6, hoverFill());
    CGFloat x = kPopupPadL;
    if (_image) {
        drawItemImage(self, _image, NSMakeRect(x, std::round(NSMidY(b) - kImage / 2), kImage, kImage), NO);
        x += kImage + kImageGap;
    }
    drawText(self, _title, font(13), [NSColor labelColor], x, NSMidY(b));
    static NSImage* chevron = symbol(@"chevron.up.chevron.down", 9, NSFontWeightSemibold);
    drawSymbol(self, chevron, NSMakePoint(b.size.width - kChevronW / 2 - 2, NSMidY(b)),
               [NSColor secondaryLabelColor]);
}

- (void)mouseDown:(NSEvent*)event {
    NS_VALID_UNTIL_END_OF_SCOPE MPopup* keep = self;
    if (gMenu) {
        [gMenu close];
        return;
    }
    MMenu* menu = [[MMenu alloc] initWithPopup:self];
    [menu show];
    // Press, drag to an item and release also selects (like a native popup).
    NSWindow* win = self.window;
    NSPoint start = [win convertPointToScreen:event.locationInWindow];
    BOOL moved = NO;
    while (gMenu == menu) {
        NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp
                                        untilDate:NSDate.distantFuture
                                           inMode:NSEventTrackingRunLoopMode
                                          dequeue:YES];
        NSPoint p = e.window ? [e.window convertPointToScreen:e.locationInWindow] : NSEvent.mouseLocation;
        if (std::hypot(p.x - start.x, p.y - start.y) > 4) moved = YES;
        if (e.type == NSEventTypeLeftMouseUp) {
            if (moved && [menu isOverItemAtScreenPoint:p]) [menu chooseAtScreenPoint:p];
            else if (moved && !NSPointInRect([self convertPoint:[win convertPointFromScreen:p] fromView:nil], self.bounds))
                [menu close];
            break;
        }
        [menu trackScreenPoint:p];
    }
}

- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityPopUpButtonRole; }
- (id)accessibilityValue { return _title; }
- (BOOL)accessibilityPerformPress {
    if (gMenu) [gMenu close];
    [[[MMenu alloc] initWithPopup:self] show];
    return YES;
}
- (BOOL)accessibilityPerformShowMenu { return [self accessibilityPerformPress]; }

@end

// ============================================================================ MPullDown

static NSImage* pullChevron() {
    static NSImage* img = symbol(@"chevron.down", 9, NSFontWeightSemibold);
    return img;
}

@implementation MPullDown {
    NSString* _text;
}

- (instancetype)initWithTitle:(NSString*)title {
    CGFloat w = std::ceil(kPullPadL + textWidth(title, font(13)) + 6 + pullChevron().size.width + kPullPadR);
    if ((self = [super initWithFrame:NSMakeRect(0, 0, w, 24)])) {
        _text = [title copy];
        self.label = title;
    }
    return self;
}

- (BOOL)pullsDown { return YES; }
- (CGFloat)textInset { return kPullPadL; }

- (void)drawRect:(NSRect)dirty {
    NSRect b = drawBezel(self, self.pressed);
    drawText(self, _text, font(13), [NSColor labelColor], kPullPadL, NSMidY(b));
    NSImage* chevron = pullChevron();
    drawSymbol(self, chevron, NSMakePoint(NSMaxX(self.bounds) - kPullPadR - chevron.size.width / 2, NSMidY(b)),
               [NSColor secondaryLabelColor]);
}

- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityMenuButtonRole; }
- (id)accessibilityValue { return nil; }

@end

