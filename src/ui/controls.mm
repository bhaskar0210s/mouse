// Drawing helpers and the small custom controls of the settings window.
#import "ui/ui.hpp"

#include <algorithm>
#include <cmath>

namespace ui {

CGFloat gForcedScale = 0;

static NSColor* gray(CGFloat white, CGFloat alpha) {
    return [NSColor colorWithSRGBRed:white green:white blue:white alpha:alpha];
}

NSColor* dynamicColor(NSColor* light, NSColor* dark) {
    return [NSColor colorWithName:nil
                  dynamicProvider:^NSColor*(NSAppearance* appearance) {
                      NSAppearanceName n = [appearance
                          bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
                      return [n isEqualToString:NSAppearanceNameDarkAqua] ? dark : light;
                  }];
}

#define UI_COLOR(name, light, dark)                         \
    NSColor* name() {                                       \
        static NSColor* c = dynamicColor((light), (dark)); \
        return c;                                           \
    }

UI_COLOR(groupFill, gray(0, 0.032), gray(1, 0.045))
UI_COLOR(groupStroke, gray(0, 0.055), gray(1, 0.06))
UI_COLOR(rowHover, gray(0, 0.03), gray(1, 0.035))
UI_COLOR(hoverFill, gray(0, 0.055), gray(1, 0.08))
UI_COLOR(pressFill, gray(0, 0.1), gray(1, 0.14))
UI_COLOR(trackFill, gray(0, 0.09), gray(1, 0.13))
UI_COLOR(knobFill, gray(1, 1), gray(1, 0.26))
UI_COLOR(bezelFill, gray(1, 1), gray(1, 0.16))
UI_COLOR(fieldFill, gray(1, 1), gray(0, 0.18))
UI_COLOR(fieldStroke, gray(0, 0.12), gray(1, 0.1))

NSFont* font(CGFloat size, NSFontWeight weight) { return [NSFont systemFontOfSize:size weight:weight]; }
NSFont* digits(CGFloat size, NSFontWeight weight) { return [NSFont monospacedDigitSystemFontOfSize:size weight:weight]; }

CGFloat scaleOf(NSView* v) {
    if (gForcedScale > 0) return gForcedScale;
    CGFloat s = v.window ? v.window.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
    return s > 0 ? s : 2;
}

CGFloat pixel(NSView* v) { return 1.0 / scaleOf(v); }

CGFloat snap(NSView* v, CGFloat value) {
    CGFloat s = scaleOf(v);
    return std::round(value * s) / s;
}

CGFloat textWidth(NSString* s, NSFont* f) {
    if (!s.length) return 0;
    return std::ceil([s sizeWithAttributes:@{NSFontAttributeName : f}].width);
}

static void drawBaseline(NSString* s, NSFont* f, NSColor* c, CGFloat x, CGFloat baseline) {
    if (!s.length) return;
    NSDictionary* a = @{NSFontAttributeName : f, NSForegroundColorAttributeName : c};
    [s drawWithRect:NSMakeRect(x, baseline, 10000, 0) options:0 attributes:a context:nil];
}

static CGFloat baselineFor(NSView* v, NSFont* f, CGFloat midY) { return snap(v, midY + f.capHeight / 2); }

void drawText(NSView* v, NSString* s, NSFont* f, NSColor* c, CGFloat x, CGFloat midY) {
    drawBaseline(s, f, c, x, baselineFor(v, f, midY));
}

void drawTextRight(NSView* v, NSString* s, NSFont* f, NSColor* c, CGFloat right, CGFloat midY) {
    drawBaseline(s, f, c, right - textWidth(s, f), baselineFor(v, f, midY));
}

void drawTextCentered(NSView* v, NSString* s, NSFont* f, NSColor* c, CGFloat midX, CGFloat midY) {
    drawBaseline(s, f, c, snap(v, midX - textWidth(s, f) / 2), baselineFor(v, f, midY));
}

NSImage* symbol(NSString* name, CGFloat pointSize, NSFontWeight weight) {
    NSImage* img = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
    if (!img) return nil;
    return [img imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:pointSize
                                                                                             weight:weight]];
}

void drawSymbol(NSView* v, NSImage* img, NSPoint center, NSColor* tint) {
    if (!img) return;
    NSSize sz = img.size;
    NSRect r = NSMakeRect(snap(v, center.x - sz.width / 2), snap(v, center.y - sz.height / 2), sz.width, sz.height);
    CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
    CGContextSaveGState(ctx);
    CGContextBeginTransparencyLayerWithRect(ctx, NSRectToCGRect(r), nullptr);
    [img drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    [tint set];
    NSRectFillUsingOperation(r, NSCompositingOperationSourceIn);
    CGContextEndTransparencyLayer(ctx);
    CGContextRestoreGState(ctx);
}

void fillRounded(NSRect r, CGFloat radius, NSColor* c) {
    [c setFill];
    [[NSBezierPath bezierPathWithRoundedRect:r xRadius:radius yRadius:radius] fill];
}

void strokeRounded(NSView* v, NSRect r, CGFloat radius, NSColor* c) {
    CGFloat p = pixel(v);
    NSRect in = NSInsetRect(r, p / 2, p / 2);
    NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:in xRadius:radius - p / 2 yRadius:radius - p / 2];
    path.lineWidth = p;
    [c setStroke];
    [path stroke];
}

void drawKnobShadowed(NSRect r, CGFloat radius, NSColor* fill, NSColor* edge) {
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [NSShadow new];
    sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.28];
    sh.shadowOffset = NSMakeSize(0, -0.75);
    sh.shadowBlurRadius = 2;
    [sh set];
    fillRounded(r, radius, fill);
    [NSGraphicsContext restoreGraphicsState];
    if (edge) {
        NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 0.25, 0.25)
                                                          xRadius:radius - 0.25
                                                          yRadius:radius - 0.25];
        p.lineWidth = 0.5;
        [edge setStroke];
        [p stroke];
    }
}

NSString* str(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
NSString* str(const char* s) { return s ? ([NSString stringWithUTF8String:s] ?: @"") : @""; }

}  // namespace ui

using namespace ui;

static NSColor* knobEdge() {
    static NSColor* c = dynamicColor([NSColor colorWithWhite:0 alpha:0.06], [NSColor colorWithWhite:1 alpha:0.06]);
    return c;
}

NSRect ui::drawBezel(NSView* v, BOOL pressed) {
    NSRect b = NSInsetRect(v.bounds, 0.5, 0.5);
    b.size.height -= 0.5;
    CGFloat r = b.size.height / 2;
    drawKnobShadowed(b, r, bezelFill(), knobEdge());
    if (pressed) fillRounded(b, r, pressFill());
    return b;
}

// ============================================================================ MControl

@implementation MControl {
    NSTrackingArea* _area;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                             options:[self trackingOptions]
                                               owner:self
                                            userInfo:nil];
        [self addTrackingArea:_area];
    }
    return self;
}

- (NSTrackingAreaOptions)trackingOptions {
    return NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (BOOL)mouseDownCanMoveWindow { return NO; }
- (void)mouseEntered:(NSEvent*)event { self.hovered = YES; }
- (void)mouseExited:(NSEvent*)event { self.hovered = NO; }
- (void)viewDidChangeEffectiveAppearance { self.needsDisplay = YES; }

- (void)setHovered:(BOOL)hovered {
    if (_hovered == hovered) return;
    _hovered = hovered;
    self.needsDisplay = YES;
}

- (void)setPressed:(BOOL)pressed {
    if (_pressed == pressed) return;
    _pressed = pressed;
    self.needsDisplay = YES;
}

- (BOOL)isAccessibilityElement { return YES; }
- (NSString*)accessibilityLabel { return _label; }

@end

// ============================================================================ MToggle

@implementation MToggle

- (instancetype)initWithSize:(NSSize)size {
    return [super initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
}

- (void)setOn:(BOOL)on {
    if (_on == on) return;
    _on = on;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    CGFloat h = b.size.height;
    fillRounded(b, h / 2, _on ? [NSColor controlAccentColor] : trackFill());
    if (self.hovered && !_on) fillRounded(b, h / 2, [NSColor colorWithWhite:0 alpha:0.03]);
    CGFloat inset = std::max<CGFloat>(1.5, std::round(h * 0.09 * 2) / 2);
    CGFloat d = h - 2 * inset;
    CGFloat w = d * 1.0;
    NSRect knob = NSMakeRect(_on ? b.size.width - inset - w : inset, inset, w, d);
    drawKnobShadowed(knob, d / 2, [NSColor whiteColor], knobEdge());
}

- (void)toggle {
    self.on = !_on;
    if (_onChange) _onChange(_on);
}

- (void)mouseDown:(NSEvent*)event { [self toggle]; }

- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityCheckBoxRole; }
- (NSAccessibilitySubrole)accessibilitySubrole { return NSAccessibilitySwitchSubrole; }
- (id)accessibilityValue { return @(_on); }
- (BOOL)accessibilityPerformPress {
    [self toggle];
    return YES;
}

@end

// ============================================================================ MSegmented

@interface MSegmentElement : NSAccessibilityElement
@property(nonatomic, weak) MSegmented* owner;
@property(nonatomic) NSInteger index;
@end

@implementation MSegmented {
    NSArray<NSString*>* _titles;
    NSFont* _font;
    CGFloat _segW;
    NSInteger _hover;
    NSArray* _elements;
}

static const CGFloat kSegInset = 2;

- (instancetype)initWithTitles:(NSArray<NSString*>*)titles height:(CGFloat)height fontSize:(CGFloat)size {
    NSFont* f = font(size);
    CGFloat maxW = 0;
    for (NSString* t in titles) maxW = std::max(maxW, textWidth(t, f));
    CGFloat segW = std::ceil(maxW + (height >= 26 ? 36 : 26));
    if ((self = [super initWithFrame:NSMakeRect(0, 0, segW * titles.count + 2 * kSegInset, height)])) {
        _titles = [titles copy];
        _font = f;
        _segW = segW;
        _hover = -1;
    }
    return self;
}

- (NSTrackingAreaOptions)trackingOptions { return [super trackingOptions] | NSTrackingMouseMoved; }

- (NSString*)titleAt:(NSInteger)i { return _titles[(NSUInteger)i]; }

- (NSRect)segmentRect:(NSInteger)i {
    return NSMakeRect(kSegInset + _segW * i, kSegInset, _segW, self.bounds.size.height - 2 * kSegInset);
}

- (void)setSelected:(NSInteger)selected {
    if (_selected == selected) return;
    _selected = selected;
    self.needsDisplay = YES;
}

- (NSInteger)indexAt:(NSPoint)p {
    if (!NSPointInRect(p, self.bounds)) return -1;
    NSInteger i = (NSInteger)std::floor((p.x - kSegInset) / _segW);
    return std::clamp<NSInteger>(i, 0, (NSInteger)_titles.count - 1);
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    CGFloat h = b.size.height;
    fillRounded(b, h / 2, trackFill());
    CGFloat r = (h - 2 * kSegInset) / 2;
    NSInteger n = (NSInteger)_titles.count;
    // Faint dividers between two unselected segments.
    for (NSInteger i = 1; i < n; i++) {
        if (i == _selected || i - 1 == _selected || i == _hover || i - 1 == _hover) continue;
        CGFloat x = snap(self, kSegInset + _segW * i);
        [[NSColor colorWithName:nil
                dynamicProvider:^NSColor*(NSAppearance* a) {
                    BOOL dark = [[a bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]]
                        isEqualToString:NSAppearanceNameDarkAqua];
                    return [NSColor colorWithWhite:dark ? 1 : 0 alpha:dark ? 0.12 : 0.1];
                }] setFill];
        NSRectFillUsingOperation(NSMakeRect(x, std::round(h * 0.28), pixel(self), h - 2 * std::round(h * 0.28)),
                                 NSCompositingOperationSourceOver);
    }
    for (NSInteger i = 0; i < n; i++) {
        NSRect s = [self segmentRect:i];
        if (i == _selected) {
            drawKnobShadowed(s, r, knobFill(), knobEdge());
        } else if (i == _hover) {
            fillRounded(s, r, self.pressed ? pressFill() : hoverFill());
        }
        NSColor* c = i == _selected ? [NSColor labelColor] : [NSColor secondaryLabelColor];
        drawTextCentered(self, _titles[(NSUInteger)i], _font, c, NSMidX(s), NSMidY(s));
    }
}

- (void)mouseMoved:(NSEvent*)event {
    NSInteger i = [self indexAt:[self convertPoint:event.locationInWindow fromView:nil]];
    if (i != _hover) {
        _hover = i;
        self.needsDisplay = YES;
    }
}

- (void)mouseExited:(NSEvent*)event {
    [super mouseExited:event];
    _hover = -1;
    self.needsDisplay = YES;
}

- (void)mouseDown:(NSEvent*)event {
    NSInteger i = [self indexAt:[self convertPoint:event.locationInWindow fromView:nil]];
    [self selectIndex:i];
}

- (void)selectIndex:(NSInteger)i {
    if (i < 0 || i == _selected) return;
    self.selected = i;
    if (_onChange) _onChange(i);
}

- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityRadioGroupRole; }

- (NSArray*)accessibilityChildren {
    if (!_elements) {
        NSMutableArray* a = [NSMutableArray array];
        for (NSUInteger i = 0; i < _titles.count; i++) {
            MSegmentElement* e = [MSegmentElement new];
            e.owner = self;
            e.index = (NSInteger)i;
            [e setAccessibilityParent:self];
            [e setAccessibilityRole:NSAccessibilityRadioButtonRole];
            [e setAccessibilityLabel:_titles[i]];
            [a addObject:e];
        }
        _elements = a;
    }
    return _elements;
}

- (void)accessibilitySelect:(NSInteger)i { [self selectIndex:i]; }

@end

@implementation MSegmentElement
- (NSRect)accessibilityFrame {
    MSegmented* o = _owner;
    if (!o.window) return NSZeroRect;
    NSRect r = [o convertRect:[o segmentRect:_index] toView:nil];
    return [o.window convertRectToScreen:r];
}
- (id)accessibilityValue { return @(_owner.selected == _index); }
- (BOOL)accessibilityPerformPress {
    [_owner accessibilitySelect:_index];
    return YES;
}
@end

// ============================================================================ MSlider

@implementation MSlider {
    CGFloat _grab;
}

static const CGFloat kKnob = 18;

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) _step = 0.05;
    return self;
}

- (void)setValue:(double)value {
    value = std::clamp(value, 0.0, 1.0);
    if (_value == value) return;
    _value = value;
    self.needsDisplay = YES;
}

- (CGFloat)knobX {
    CGFloat w = self.bounds.size.width - kKnob;
    return snap(self, kKnob / 2 + w * _value);
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    CGFloat midY = NSMidY(b);
    CGFloat th = 4;
    NSRect track = NSMakeRect(kKnob / 2 - 2, snap(self, midY - th / 2), b.size.width - kKnob + 4, th);
    fillRounded(track, th / 2, trackFill());
    CGFloat kx = [self knobX];
    NSRect done = track;
    done.size.width = kx - track.origin.x;
    if (done.size.width > 0) fillRounded(done, th / 2, [NSColor controlAccentColor]);
    NSRect knob = NSMakeRect(kx - kKnob / 2, snap(self, midY - kKnob / 2), kKnob, kKnob);
    drawKnobShadowed(knob, kKnob / 2, self.pressed ? [NSColor colorWithWhite:0.96 alpha:1] : [NSColor whiteColor],
                     knobEdge());
}

- (void)trackTo:(NSPoint)p {
    CGFloat w = self.bounds.size.width - kKnob;
    double v = w > 0 ? (p.x - _grab - kKnob / 2) / w : 0;
    v = std::clamp(v, 0.0, 1.0);
    if (v == _value) return;
    self.value = v;
    if (_onChange) _onChange(_value);
}

- (void)mouseDown:(NSEvent*)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    CGFloat kx = [self knobX];
    _grab = std::fabs(p.x - kx) <= kKnob / 2 + 2 ? p.x - kx : 0;
    self.pressed = YES;
    [self trackTo:p];
}

- (void)mouseDragged:(NSEvent*)event { [self trackTo:[self convertPoint:event.locationInWindow fromView:nil]]; }

- (void)mouseUp:(NSEvent*)event {
    self.pressed = NO;
    _grab = 0;
}

- (NSAccessibilityRole)accessibilityRole { return NSAccessibilitySliderRole; }
- (id)accessibilityValue { return _valueText ? _valueText() : @(_value); }
- (void)nudge:(double)d {
    double v = std::clamp(_value + d, 0.0, 1.0);
    if (v == _value) return;
    self.value = v;
    if (_onChange) _onChange(_value);
}
- (BOOL)accessibilityPerformIncrement {
    [self nudge:_step];
    return YES;
}
- (BOOL)accessibilityPerformDecrement {
    [self nudge:-_step];
    return YES;
}

@end

// ============================================================================ MButton

@implementation MButton {
    NSString* _title;
}

- (instancetype)initWithTitle:(NSString*)title {
    CGFloat w = textWidth(title, font(13)) + 24;
    if ((self = [super initWithFrame:NSMakeRect(0, 0, std::max<CGFloat>(w, 64), 24)])) {
        _title = [title copy];
        self.label = title;
    }
    return self;
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = drawBezel(self, self.pressed);
    drawTextCentered(self, _title, font(13), [NSColor labelColor], NSMidX(b), NSMidY(b));
}

- (void)mouseDown:(NSEvent*)event {
    self.pressed = YES;
    while (true) {
        NSEvent* e = [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
        BOOL inside = NSPointInRect([self convertPoint:e.locationInWindow fromView:nil], self.bounds);
        if (e.type == NSEventTypeLeftMouseUp) {
            self.pressed = NO;
            if (inside && _onPress) _onPress();
            return;
        }
        self.pressed = inside;
    }
}

- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityButtonRole; }
- (BOOL)accessibilityPerformPress {
    if (_onPress) _onPress();
    return YES;
}

@end

// ============================================================================ MRecorder

@implementation MRecorder {
    NSEventModifierFlags _live;  // modifier flags held while recording (with left/right bits)
    mouse::ModifierTap _tap;
    id _clickMonitor;
    id _resignObserver;
}

- (instancetype)initWithFrame:(NSRect)frame {
    return [super initWithFrame:NSMakeRect(frame.origin.x, frame.origin.y, 112, 24)];
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)setShortcut:(mouse::Shortcut)shortcut {
    _shortcut = shortcut;
    self.needsDisplay = YES;
}

static NSString* modSymbols(NSEventModifierFlags f) {
    NSMutableString* s = [NSMutableString string];
    if (f & NSEventModifierFlagControl) [s appendString:@"⌃"];
    if (f & NSEventModifierFlagOption) [s appendString:@"⌥"];
    if (f & NSEventModifierFlagShift) [s appendString:@"⇧"];
    if (f & NSEventModifierFlagCommand) [s appendString:@"⌘"];
    return s;
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    CGFloat r = 6;
    fillRounded(b, r, fieldFill());
    if (_recording) {
        NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(b, 1, 1) xRadius:r - 1 yRadius:r - 1];
        p.lineWidth = 2;
        [[NSColor keyboardFocusIndicatorColor] setStroke];
        [p stroke];
        [[[NSColor controlAccentColor] colorWithAlphaComponent:0.9] setStroke];
        NSBezierPath* q = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(b, 0.75, 0.75) xRadius:r - 0.75 yRadius:r - 0.75];
        q.lineWidth = 1.5;
        [q stroke];
    } else {
        strokeRounded(self, b, r, fieldStroke());
        if (self.hovered) fillRounded(b, r, [NSColor colorWithWhite:0.5 alpha:0.05]);
    }
    NSString* text;
    NSColor* color;
    NSFont* f = font(12);
    if (_recording) {
        // One modifier held: name it (it is what letting go records); several: their symbols.
        uint16_t one = mouse::ModifierTap::single(_live);
        NSString* mods = one != mouse::ModifierTap::kNone ? str(mouse::shortcutDisplay({one, 0}))
                                                          : modSymbols(_live & NSEventModifierFlagDeviceIndependentFlagsMask);
        text = mods.length ? mods : @"Type shortcut";
        color = mods.length ? [NSColor labelColor] : [NSColor secondaryLabelColor];
        if (mods.length) f = font(13);
    } else if (_shortcut.valid()) {
        text = str(mouse::shortcutDisplay(_shortcut));
        color = [NSColor labelColor];
        f = font(13);
    } else {
        text = @"Record shortcut";
        color = [NSColor placeholderTextColor];
    }
    drawTextCentered(self, text, f, color, NSMidX(b), NSMidY(b));
}

- (void)startRecording {
    if (!self.window || _recording) return;
    _recording = YES;
    _live = 0;
    _tap = {};
    [self.window makeFirstResponder:self];
    self.needsDisplay = YES;
    // A click anywhere else, or leaving the window, ends recording.
    __weak MRecorder* weakSelf = self;
    NSEventMask clicks = NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown;
    _clickMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:clicks
                                                          handler:^NSEvent*(NSEvent* e) {
                                                              MRecorder* me = weakSelf;
                                                              if (me && (e.window != me.window ||
                                                                         !NSPointInRect([me convertPoint:e.locationInWindow fromView:nil],
                                                                                        me.bounds)))
                                                                  [me stopRecording];
                                                              return e;
                                                          }];
    _resignObserver = [NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidResignKeyNotification
                                                                      object:self.window
                                                                       queue:nil
                                                                  usingBlock:^(NSNotification*) {
                                                                      [weakSelf stopRecording];
                                                                  }];
}

- (void)endRecordingState {
    _recording = NO;
    _live = 0;
    if (_clickMonitor) [NSEvent removeMonitor:_clickMonitor];
    if (_resignObserver) [NSNotificationCenter.defaultCenter removeObserver:_resignObserver];
    _clickMonitor = _resignObserver = nil;
    self.needsDisplay = YES;
}

- (void)stopRecording {
    if (!_recording) return;
    [self endRecordingState];
    if (self.window.firstResponder == self) [self.window makeFirstResponder:nil];
}

- (BOOL)resignFirstResponder {
    [self endRecordingState];
    return YES;
}

- (void)viewWillMoveToWindow:(NSWindow*)window {
    if (!window) [self endRecordingState];
    [super viewWillMoveToWindow:window];
}

- (void)mouseDown:(NSEvent*)event {
    if (_recording) [self stopRecording];
    else [self startRecording];
}

- (void)dealloc {
    if (_clickMonitor) [NSEvent removeMonitor:_clickMonitor];
    if (_resignObserver) [NSNotificationCenter.defaultCenter removeObserver:_resignObserver];
}

// Keys for which macOS sets the Fn flag by itself.
static bool autoFnKey(uint16_t code) {
    std::string n = mouse::keyName(code);
    if (n.size() >= 2 && n[0] == 'f' && n[1] >= '0' && n[1] <= '9') return true;
    return n == "left" || n == "right" || n == "up" || n == "down" || n == "home" || n == "end" ||
           n == "page-up" || n == "page-down" || n == "forward-delete";
}

- (void)record:(NSEvent*)event {
    _tap.otherKey();
    uint16_t code = event.keyCode;
    NSEventModifierFlags f = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    bool plain = !(f & (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption |
                        NSEventModifierFlagShift));
    if (code == 0x35 && plain) {  // Esc cancels
        [self stopRecording];
        return;
    }
    if (code == 0x33 && plain) {  // Delete clears
        [self stopRecording];
        self.shortcut = mouse::Shortcut{};
        if (_onChange) _onChange(_shortcut);
        return;
    }
    if (mouse::keyName(code).empty()) return;  // not a key we can send
    mouse::Shortcut s;
    s.key = code;
    if (f & NSEventModifierFlagControl) s.mods |= mouse::kCtrl;
    if (f & NSEventModifierFlagOption) s.mods |= mouse::kOpt;
    if (f & NSEventModifierFlagShift) s.mods |= mouse::kShift;
    if (f & NSEventModifierFlagCommand) s.mods |= mouse::kCmd;
    if ((f & NSEventModifierFlagFunction) && !autoFnKey(code)) s.mods |= mouse::kFn;
    [self stopRecording];
    self.shortcut = s;
    if (_onChange) _onChange(_shortcut);
}

- (void)keyDown:(NSEvent*)event {
    if (_recording) {
        [self record:event];
    } else if (event.keyCode == 0x31 || event.keyCode == 0x24) {
        [self startRecording];
    } else {
        [super keyDown:event];
    }
}

- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if (!_recording || event.type != NSEventTypeKeyDown) return NO;
    [self record:event];
    return YES;
}

// A modifier key pressed and let go on its own records that key alone, left or right.
- (void)flagsChanged:(NSEvent*)event {
    if (!_recording) return [super flagsChanged:event];
    _live = event.modifierFlags;
    uint16_t lone = _tap.changed(event.keyCode, event.modifierFlags);
    if (lone != mouse::ModifierTap::kNone) {
        [self stopRecording];
        self.shortcut = mouse::Shortcut{lone, 0};
        if (_onChange) _onChange(_shortcut);
        return;
    }
    self.needsDisplay = YES;
}

- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityButtonRole; }
- (id)accessibilityValue {
    return _shortcut.valid() ? str(mouse::shortcutDisplay(_shortcut)) : @"None";
}
- (BOOL)accessibilityPerformPress {
    [self startRecording];
    return YES;
}

@end
