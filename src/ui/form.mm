// Grouped form (inset rounded boxes with hairline separators, like System Settings) and the
// illustration of the mouse shown next to the button list.
#import "ui/ui.hpp"

#include <algorithm>
#include <cmath>

using namespace ui;

static const CGFloat kGroupRadius = 10;
static const CGFloat kPad = 14;        // horizontal padding inside a group
static const CGFloat kIconColumn = 28;
static const CGFloat kHeaderH = 26;
static const CGFloat kSectionGap = 16;
static const CGFloat kSpacing = 8;     // between trailing controls

// ============================================================================ MForm

@implementation MForm {
    std::vector<FormSection> _sections;
    struct Placed {
        NSRect rect;
        const FormRow* row;
        bool first;
        size_t group;
    };
    std::vector<Placed> _placed;
    std::vector<NSRect> _groups;
    std::vector<std::pair<NSString*, CGFloat>> _headers;  // title, mid y
    NSInteger _hoverRow;
    int _highlightPart;
    bool _icons;
    CGFloat _height;
    NSTrackingArea* _area;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _hoverRow = -1;
        _highlightPart = -1;
        _titleColumn = 170;
        _valueColumn = 76;
        _topInset = 4;
        _bottomInset = 10;
        _area = [[NSTrackingArea alloc]
            initWithRect:NSZeroRect
                 options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp |
                         NSTrackingInVisibleRect
                   owner:self
                userInfo:nil];
        [self addTrackingArea:_area];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (void)viewDidChangeEffectiveAppearance { self.needsDisplay = YES; }
- (CGFloat)contentHeight { return _height; }

- (void)setSections:(std::vector<FormSection>)sections {
    for (const FormSection& s : _sections)
        for (const FormRow& r : s.rows) {
            [r.fill removeFromSuperview];
            for (NSView* v : r.trailing) [v removeFromSuperview];
        }
    _sections = std::move(sections);
    _icons = false;
    for (const FormSection& s : _sections)
        for (const FormRow& r : s.rows) {
            if (r.icon && r.level == 0) _icons = true;
            if (r.fill) [self addSubview:r.fill];
            for (NSView* v : r.trailing) [self addSubview:v];
        }
    _hoverRow = -1;
    [self relayout];
}

- (CGFloat)titleX:(int)level {
    CGFloat x = _groupX + kPad + (_icons ? kIconColumn : 0);
    return level == 0 ? x : x + 22;
}

- (void)relayout {
    _placed.clear();
    _groups.clear();
    _headers.clear();
    CGFloat y = _topInset;
    CGFloat right = _groupX + _groupWidth - kPad + 6;  // borderless popups carry their own padding
    for (size_t si = 0; si < _sections.size(); si++) {
        const FormSection& s = _sections[si];
        if (si > 0) y += kSectionGap;
        if (s.header) {
            _headers.push_back({s.header, y + 10});
            y += kHeaderH;
        }
        CGFloat top = y;
        for (size_t ri = 0; ri < s.rows.size(); ri++) {
            const FormRow& r = s.rows[ri];
            NSRect rect = NSMakeRect(_groupX, y, _groupWidth, r.height);
            _placed.push_back({rect, &r, ri == 0, _groups.size()});
            CGFloat mid = y + r.height / 2;
            CGFloat x = right;
            for (auto it = r.trailing.rbegin(); it != r.trailing.rend(); ++it) {
                NSView* v = *it;
                NSSize sz = v.frame.size;
                CGFloat pad = [v isKindOfClass:[MPopup class]] ? 0 : 6;  // non-popups align to the padding
                x -= sz.width + pad;
                v.frame = NSMakeRect(snap(self, x), snap(self, mid - sz.height / 2), sz.width, sz.height);
                x -= kSpacing - pad;
            }
            if (r.fill) {
                CGFloat x0 = _groupX + kPad + _titleColumn;
                CGFloat x1 = _groupX + _groupWidth - kPad - (r.value ? _valueColumn + 12 : 0);
                CGFloat h = r.fill.frame.size.height;
                r.fill.frame = NSMakeRect(x0, snap(self, mid - h / 2), x1 - x0, h);
            }
            y += r.height;
        }
        _groups.push_back(NSMakeRect(_groupX, top, _groupWidth, y - top));
    }
    _height = y + _bottomInset;
    self.needsDisplay = YES;
}

- (void)setValue:(NSString*)value forFill:(NSView*)fill {
    for (FormSection& s : _sections)
        for (FormRow& r : s.rows)
            if (r.fill == fill) r.value = value;
    for (const Placed& p : _placed)
        if (p.row->fill == fill) [self setNeedsDisplayInRect:p.rect];
}

- (void)setHighlightPart:(int)part {
    if (_highlightPart == part) return;
    _highlightPart = part;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirty {
    CGFloat px = pixel(self);
    for (NSRect g : _groups) {
        fillRounded(g, kGroupRadius, groupFill());
        strokeRounded(self, g, kGroupRadius, groupStroke());
    }
    for (const auto& h : _headers)
        drawText(self, h.first, font(13, NSFontWeightSemibold), [NSColor labelColor], _groupX + 2, h.second);

    NSFont* titleFont = font(13);
    NSFont* valueFont = digits(13);
    for (size_t i = 0; i < _placed.size(); i++) {
        const Placed& p = _placed[i];
        const FormRow& r = *p.row;
        NSRect rect = p.rect;
        if (!NSIntersectsRect(rect, dirty)) continue;
        BOOL hot = r.part >= 0 && ((NSInteger)i == _hoverRow || (r.level == 0 && r.part == _highlightPart));
        if (hot) {
            [NSGraphicsContext saveGraphicsState];
            NSRect g = _groups[p.group];
            [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(g, px, px)
                                             xRadius:kGroupRadius - px
                                             yRadius:kGroupRadius - px] addClip];
            [rowHover() setFill];
            NSRectFillUsingOperation(rect, NSCompositingOperationSourceOver);
            [NSGraphicsContext restoreGraphicsState];
        }
        if (!p.first && r.separator) {
            [[NSColor separatorColor] setFill];
            NSRectFillUsingOperation(NSMakeRect(_groupX + kPad, snap(self, rect.origin.y), _groupWidth - 2 * kPad, px),
                                     NSCompositingOperationSourceOver);
        }
        CGFloat mid = NSMidY(rect);
        CGFloat tx = [self titleX:r.level];
        if (r.icon) {
            CGFloat cx = r.level == 0 ? _groupX + kPad + 10 : tx - 12;
            drawSymbol(self, r.icon, NSMakePoint(cx, mid),
                       r.level == 0 ? [NSColor secondaryLabelColor] : [NSColor tertiaryLabelColor]);
        }
        drawText(self, r.title, titleFont, r.level == 0 ? [NSColor labelColor] : [NSColor secondaryLabelColor],
                 tx, mid);
        if (r.value)
            drawTextRight(self, r.value, valueFont, [NSColor secondaryLabelColor], _groupX + _groupWidth - kPad, mid);
    }
}

- (void)hoverAt:(NSPoint)p {
    NSInteger hit = -1;
    for (size_t i = 0; i < _placed.size(); i++)
        if (NSPointInRect(p, _placed[i].rect)) hit = (NSInteger)i;
    if (hit == _hoverRow) return;
    _hoverRow = hit;
    self.needsDisplay = YES;
    if (_onHoverPart) _onHoverPart(hit >= 0 ? _placed[(size_t)hit].row->part : -1);
}

- (void)mouseMoved:(NSEvent*)event { [self hoverAt:[self convertPoint:event.locationInWindow fromView:nil]]; }
- (void)mouseEntered:(NSEvent*)event { [self mouseMoved:event]; }
- (void)mouseExited:(NSEvent*)event { [self hoverAt:NSMakePoint(-1, -1)]; }

@end

// ============================================================================ MArt

// The drawing is defined on a 200 x 300 canvas (y down, front of the mouse at the top) and scaled
// to fit. It is an original, deliberately simple shape: an ergonomic right-handed mouse seen from
// above with a thumb rest flaring out on the left.
namespace {

struct Part {
    NSPoint marker;
};

const Part kParts[6] = {
    {{113, 59}},   // middle (wheel click)
    {{44.5, 162}}, // back
    {{45.5, 144}}, // forward
    {{29, 206}},   // thumb button
    {{113, 104}},  // top button
    {{46.5, 118}}, // thumb wheel
};

NSBezierPath* bodyPath() {
    NSBezierPath* p = [NSBezierPath bezierPath];
    [p moveToPoint:NSMakePoint(112, 14)];
    [p curveToPoint:NSMakePoint(172, 82) controlPoint1:NSMakePoint(146, 14) controlPoint2:NSMakePoint(170, 40)];
    [p curveToPoint:NSMakePoint(176, 206) controlPoint1:NSMakePoint(174, 124) controlPoint2:NSMakePoint(178, 168)];
    [p curveToPoint:NSMakePoint(118, 292) controlPoint1:NSMakePoint(174, 252) controlPoint2:NSMakePoint(156, 290)];
    [p curveToPoint:NSMakePoint(58, 268) controlPoint1:NSMakePoint(92, 293) controlPoint2:NSMakePoint(70, 284)];
    [p curveToPoint:NSMakePoint(26, 240) controlPoint1:NSMakePoint(48, 256) controlPoint2:NSMakePoint(36, 252)];
    [p curveToPoint:NSMakePoint(16, 196) controlPoint1:NSMakePoint(14, 226) controlPoint2:NSMakePoint(10, 208)];
    [p curveToPoint:NSMakePoint(46, 168) controlPoint1:NSMakePoint(22, 184) controlPoint2:NSMakePoint(36, 178)];
    [p curveToPoint:NSMakePoint(52, 92) controlPoint1:NSMakePoint(52, 150) controlPoint2:NSMakePoint(50, 120)];
    [p curveToPoint:NSMakePoint(112, 14) controlPoint1:NSMakePoint(54, 46) controlPoint2:NSMakePoint(78, 14)];
    [p closePath];
    return p;
}

NSBezierPath* partPath(int part) {
    switch (part) {
        case 0: return [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(105, 34, 16, 50) xRadius:8 yRadius:8];
        case 1: return [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(40, 154, 9, 16) xRadius:4.5 yRadius:4.5];
        case 2: return [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(41, 136, 9, 16) xRadius:4.5 yRadius:4.5];
        case 3: {
            NSBezierPath* p = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(17, 199.5, 24, 13)];
            NSAffineTransform* t = [NSAffineTransform transform];
            [t translateXBy:29 yBy:206];
            [t rotateByDegrees:-38];
            [t translateXBy:-29 yBy:-206];
            [p transformUsingAffineTransform:t];
            return p;
        }
        case 4: return [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(107.5, 96, 11, 16) xRadius:4 yRadius:4];
        case 5: return [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(40.5, 104, 12, 28) xRadius:6 yRadius:6];
    }
    return nil;
}

}  // namespace

@implementation MArt {
    NSTrackingArea* _area;
    int _hover;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _highlight = -1;
        _hover = -1;
        _area = [[NSTrackingArea alloc]
            initWithRect:NSZeroRect
                 options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp |
                         NSTrackingInVisibleRect
                   owner:self
                userInfo:nil];
        [self addTrackingArea:_area];
        self.accessibilityElement = NO;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (void)viewDidChangeEffectiveAppearance { self.needsDisplay = YES; }

- (void)setHighlight:(int)highlight {
    if (_highlight == highlight) return;
    _highlight = highlight;
    self.needsDisplay = YES;
}

- (NSAffineTransform*)canvasTransform {
    NSRect b = self.bounds;
    CGFloat s = std::min((b.size.width - 24) / 200.0, (b.size.height - 40) / 300.0);
    NSAffineTransform* t = [NSAffineTransform transform];
    [t translateXBy:NSMidX(b) - 100 * s yBy:NSMidY(b) - 150 * s];
    [t scaleBy:s];
    return t;
}

static BOOL isDark(NSView* v) {
    return [[v.effectiveAppearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]]
        isEqualToString:NSAppearanceNameDarkAqua];
}

- (void)drawRect:(NSRect)dirty {
    BOOL dark = isDark(self);
    NSAffineTransform* t = [self canvasTransform];
    CGFloat s = t.transformStruct.m11;
    [NSGraphicsContext saveGraphicsState];
    [t concat];

    NSBezierPath* body = bodyPath();
    // Soft contact shadow.
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [NSShadow new];
    sh.shadowColor = [NSColor colorWithWhite:0 alpha:dark ? 0.5 : 0.16];
    sh.shadowOffset = NSMakeSize(0, -8 * s);
    sh.shadowBlurRadius = 22 * s;
    [sh set];
    [(dark ? [NSColor colorWithSRGBRed:0.27 green:0.27 blue:0.29 alpha:1] : [NSColor whiteColor]) setFill];
    [body fill];
    [NSGraphicsContext restoreGraphicsState];

    NSGradient* g = dark ? [[NSGradient alloc] initWithStartingColor:[NSColor colorWithSRGBRed:0.31 green:0.31 blue:0.33 alpha:1]
                                                         endingColor:[NSColor colorWithSRGBRed:0.22 green:0.22 blue:0.24 alpha:1]]
                         : [[NSGradient alloc] initWithStartingColor:[NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:1]
                                                         endingColor:[NSColor colorWithSRGBRed:0.91 green:0.91 blue:0.93 alpha:1]];
    [g drawInBezierPath:body angle:90];

    // Split between the two main buttons.
    NSBezierPath* split = [NSBezierPath bezierPath];
    [split moveToPoint:NSMakePoint(112.5, 15)];
    [split curveToPoint:NSMakePoint(114, 118) controlPoint1:NSMakePoint(113, 50) controlPoint2:NSMakePoint(114, 85)];
    split.lineWidth = 1.0 / s;
    [[NSColor colorWithWhite:0 alpha:dark ? 0.45 : 0.1] setStroke];
    [split stroke];

    // Wheel well.
    NSColor* well = [NSColor colorWithWhite:0 alpha:dark ? 0.35 : 0.09];
    [well setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(102, 30, 22, 58) xRadius:11 yRadius:11] fill];

    NSColor* partFill = dark ? [NSColor colorWithSRGBRed:0.17 green:0.17 blue:0.19 alpha:1]
                             : [NSColor colorWithSRGBRed:0.84 green:0.84 blue:0.86 alpha:1];
    NSColor* ridge = [NSColor colorWithWhite:dark ? 1 : 0 alpha:dark ? 0.07 : 0.1];
    NSColor* accent = [NSColor controlAccentColor];
    int hot = _highlight;
    for (int i = 0; i < 6; i++) {
        NSBezierPath* p = partPath(i);
        [partFill setFill];
        [p fill];
        if (i == 0 || i == 5) {  // ridges on the wheels
            [NSGraphicsContext saveGraphicsState];
            [p addClip];
            [ridge setFill];
            NSRect r = p.bounds;
            for (CGFloat y = r.origin.y + 3; y < NSMaxY(r) - 1; y += 3.2)
                NSRectFillUsingOperation(NSMakeRect(r.origin.x, y, r.size.width, 1.0 / s), NSCompositingOperationSourceOver);
            [NSGraphicsContext restoreGraphicsState];
        }
        if (i == hot) {
            [[accent colorWithAlphaComponent:0.35] setFill];
            [p fill];
        }
        p.lineWidth = 1.0 / s;
        [[NSColor colorWithWhite:dark ? 1 : 0 alpha:dark ? 0.06 : 0.1] setStroke];
        [p stroke];
    }

    body.lineWidth = 1.0 / s;
    [[NSColor colorWithWhite:dark ? 1 : 0 alpha:dark ? 0.09 : 0.1] setStroke];
    [body stroke];
    [NSGraphicsContext restoreGraphicsState];

    // Markers (drawn in view space so they stay crisp and round).
    for (int i = 0; i < 6; i++) {
        NSPoint c = [t transformPoint:kParts[i].marker];
        if (i == hot) {
            [[accent colorWithAlphaComponent:0.22] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 9, c.y - 9, 18, 18)] fill];
            [accent setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 4, c.y - 4, 8, 8)] fill];
            [[NSColor whiteColor] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 1.5, c.y - 1.5, 3, 3)] fill];
        } else {
            [[NSColor colorWithWhite:dark ? 1 : 0 alpha:dark ? 0.55 : 0.38] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 2.5, c.y - 2.5, 5, 5)] fill];
        }
    }
}

- (int)partAt:(NSPoint)p {
    NSAffineTransform* t = [self canvasTransform];
    int best = -1;
    CGFloat bestD = 16;
    for (int i = 0; i < 6; i++) {
        NSPoint c = [t transformPoint:kParts[i].marker];
        CGFloat d = std::hypot(c.x - p.x, c.y - p.y);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

- (void)mouseMoved:(NSEvent*)event {
    int part = [self partAt:[self convertPoint:event.locationInWindow fromView:nil]];
    if (part == _hover) return;
    _hover = part;
    self.highlight = part;
    if (_onHoverPart) _onHoverPart(part);
}

- (void)mouseEntered:(NSEvent*)event { [self mouseMoved:event]; }

- (void)mouseExited:(NSEvent*)event {
    if (_hover < 0) return;
    _hover = -1;
    self.highlight = -1;
    if (_onHoverPart) _onHoverPart(-1);
}

@end
