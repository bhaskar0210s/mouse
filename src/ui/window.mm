// The settings window and its two pages.
#import "ui/settings.hpp"
#import "ui/ui.hpp"

#include <algorithm>
#include <cmath>

using namespace ui;

static const CGFloat kWidth = 720;
static const CGFloat kHeight = 560;
static const CGFloat kMargin = 24;
static const CGFloat kFooterH = 44;
static const CGFloat kBannerH = 40;
static const CGFloat kArtWidth = 216;
static const CGFloat kRowH = 40;      // buttons page
static const CGFloat kSubRowH = 30;
static const CGFloat kFormRowH = 36;  // point & scroll page

// The agent reports its own permissions; the window process needs none.
static BOOL permissionMissing(const mouse::State& s) { return s.running && (!s.trusted || !s.inputAccess); }

// ============================================================================ root, banner, footer

@interface MRootView : NSView
@end

@implementation MRootView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirty {
    [[NSColor windowBackgroundColor] setFill];
    NSRectFill(dirty);
}
@end

static NSString* const kBannerText = @"Allow Mouse Agent in Accessibility and Input Monitoring so your buttons work.";

@interface MBanner : NSView
@property(nonatomic, readonly) MButton* button;
@end

@implementation MBanner

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _button = [[MButton alloc] initWithTitle:@"Allow…"];
        _button.label = @"Allow access";
        [self addSubview:_button];
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = kBannerText;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)layout {
    [super layout];
    NSSize b = _button.frame.size;
    _button.frame = NSMakeRect(self.bounds.size.width - 10 - b.width, snap(self, (self.bounds.size.height - b.height) / 2),
                               b.width, b.height);
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    fillRounded(b, 10, groupFill());
    strokeRounded(self, b, 10, groupStroke());
    static NSImage* icon = symbol(@"hand.raised.fill", 14, NSFontWeightRegular);
    drawSymbol(self, icon, NSMakePoint(14 + 10, NSMidY(b)), [NSColor systemOrangeColor]);
    drawText(self, kBannerText, font(13), [NSColor labelColor], 14 + 28, NSMidY(b));
}

@end

@interface MFooter : NSView
@property(nonatomic, readonly) MToggle* updates;
@property(nonatomic, readonly) MButton* loginButton;
@property(nonatomic) mouse::State state;
@property(nonatomic, copy) NSString* version;
@property(nonatomic) BOOL loginNote;
@end

@implementation MFooter

static NSString* const kLoginNote = @"Allow Mouse in System Settings → General → Login Items";

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _updates = [[MToggle alloc] initWithSize:NSMakeSize(26, 15)];
        _updates.label = @"Automatic updates";
        [self addSubview:_updates];
        _loginButton = [[MButton alloc] initWithTitle:@"Open…"];
        _loginButton.label = @"Open Login Items";
        _loginButton.hidden = YES;
        [self addSubview:_loginButton];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)setState:(mouse::State)state {
    _state = state;
    self.needsDisplay = YES;
}

- (void)setLoginNote:(BOOL)loginNote {
    _loginNote = loginNote;
    _loginButton.hidden = !loginNote;
    self.needsLayout = YES;
    self.needsDisplay = YES;
}

- (NSString*)versionText {
    NSString* v = _state.version.empty() ? _version : str(_state.version);
    return v.length ? [@"v" stringByAppendingString:v] : @"";
}

- (CGFloat)updatesLabelRight {
    return _updates.frame.origin.x - 8;
}

- (void)layout {
    [super layout];
    CGFloat mid = kFooterH / 2;
    CGFloat right = self.bounds.size.width - kMargin;
    NSString* v = [self versionText];
    if (v.length) right -= textWidth(v, digits(12)) + 16;
    NSSize t = _updates.frame.size;
    _updates.frame = NSMakeRect(right - t.width, snap(self, mid - t.height / 2), t.width, t.height);
    NSSize lb = _loginButton.frame.size;
    _loginButton.frame = NSMakeRect(kMargin + textWidth(kLoginNote, font(12)) + 10, snap(self, mid - lb.height / 2),
                                    lb.width, lb.height);
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    CGFloat mid = kFooterH / 2;
    [[NSColor separatorColor] setFill];
    NSRectFillUsingOperation(NSMakeRect(0, 0, b.size.width, pixel(self)), NSCompositingOperationSourceOver);

    // Left: device status, or the Login Items note.
    CGFloat x = kMargin;
    NSFont* f = font(12);
    if (_loginNote) {
        drawText(self, kLoginNote, f, [NSColor secondaryLabelColor], x, mid);
    } else {
        NSColor* dot = _state.running && _state.connected ? [NSColor systemGreenColor] : [NSColor tertiaryLabelColor];
        [dot setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x, snap(self, mid - 3.5), 7, 7)] fill];
        x += 7 + 8;
        if (!_state.running) {
            drawText(self, @"Starting…", f, [NSColor secondaryLabelColor], x, mid);
        } else if (!_state.connected) {
            drawText(self, @"Not connected — turn on your mouse or move it to wake it", f, [NSColor secondaryLabelColor],
                     x, mid);
        } else {
            NSString* name = _state.name.empty() ? @"MX Master 3S" : str(_state.name);
            NSFont* nf = font(12, NSFontWeightMedium);
            drawText(self, name, nf, [NSColor labelColor], x, mid);
            x += textWidth(name, nf) + 10;
            if (_state.battery >= 0) {
                int pct = std::clamp(_state.battery, 0, 100);
                NSString* sym = _state.charging ? @"battery.100.bolt"
                                : pct >= 88     ? @"battery.100"
                                : pct >= 63     ? @"battery.75"
                                : pct >= 38     ? @"battery.50"
                                : pct >= 13     ? @"battery.25"
                                                : @"battery.0";
                NSImage* img = symbol(sym, 13, NSFontWeightRegular);
                drawSymbol(self, img, NSMakePoint(x + img.size.width / 2, mid), [NSColor secondaryLabelColor]);
                x += img.size.width + 5;
                drawText(self, [NSString stringWithFormat:@"%d%%", pct], digits(12), [NSColor secondaryLabelColor], x, mid);
            }
        }
    }

    // Right: automatic updates, last update note and version.
    drawTextRight(self, @"Automatic updates", f, [NSColor secondaryLabelColor], [self updatesLabelRight], mid);
    CGFloat right = b.size.width - kMargin;
    NSString* v = [self versionText];
    if (v.length) drawTextRight(self, v, digits(12), [NSColor tertiaryLabelColor], right, mid);
    if (!_state.update.empty() && !_loginNote) {
        CGFloat r = [self updatesLabelRight] - textWidth(@"Automatic updates", f) - 16;
        drawTextRight(self, str(_state.update), f, [NSColor tertiaryLabelColor], r, mid);
    }
}

@end

// ============================================================================ MSettings

@implementation MSettings {
    mouse::Config _cfg;
    mouse::State _state;
    BOOL _snapshot;
    NSInteger _page;

    MRootView* _root;
    MSegmented* _pages;
    MBanner* _banner;
    MFooter* _footer;
    MArt* _art;
    NSScrollView* _buttonsScroll;
    NSScrollView* _scrollScroll;
    MForm* _buttons;
    MForm* _scroll;
    MSlider* _dpiSlider;
}

- (instancetype)initWithConfig:(const mouse::Config&)config state:(const mouse::State&)state snapshot:(BOOL)snapshot {
    if ((self = [super init])) {
        _cfg = config;
        _state = state;
        _snapshot = snapshot;
        _needsPermission = permissionMissing(state);
        [self buildWindow];
        [self rebuildButtons];
        [self rebuildScroll];
        [self layout];
    }
    return self;
}

- (NSView*)rootView { return _root; }

- (void)save {
    if (_snapshot) return;
    mouse::saveConfig(_cfg);
}

// ---------------------------------------------------------------------------- window

static NSScrollView* makeScroll(NSView* document) {
    NSScrollView* sv = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    sv.drawsBackground = NO;
    sv.borderType = NSNoBorder;
    sv.hasVerticalScroller = YES;
    sv.hasHorizontalScroller = NO;
    sv.autohidesScrollers = YES;
    sv.scrollerStyle = NSScrollerStyleOverlay;
    sv.verticalScrollElasticity = NSScrollElasticityNone;
    sv.horizontalScrollElasticity = NSScrollElasticityNone;
    sv.contentView.drawsBackground = NO;
    sv.automaticallyAdjustsContentInsets = NO;  // the page starts below the title bar already
    sv.documentView = document;
    return sv;
}

- (void)buildWindow {
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                              NSWindowStyleMaskFullSizeContentView;
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, kWidth, kHeight)
                                          styleMask:style
                                            backing:NSBackingStoreBuffered
                                              defer:YES];
    _window.title = @"Mouse";
    _window.titleVisibility = NSWindowTitleHidden;
    _window.titlebarAppearsTransparent = YES;
    _window.releasedWhenClosed = NO;
    _window.animationBehavior = NSWindowAnimationBehaviorNone;
    _window.restorable = NO;
    _window.tabbingMode = NSWindowTabbingModeDisallowed;
    NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"app.mouse.settings"];
    toolbar.allowsUserCustomization = NO;
    _window.toolbar = toolbar;
    _window.toolbarStyle = NSWindowToolbarStyleUnified;
    _window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    _window.delegate = self;

    _root = [[MRootView alloc] initWithFrame:NSMakeRect(0, 0, kWidth, kHeight)];
    _window.contentView = _root;

    __weak MSettings* weakSelf = self;
    _pages = [[MSegmented alloc] initWithTitles:@[ @"Buttons", @"Point & Scroll" ] height:28 fontSize:13];
    _pages.label = @"Page";
    _pages.onChange = ^(NSInteger i) {
        [weakSelf showPage:i];
    };
    [_root addSubview:_pages];

    _banner = [[MBanner alloc] initWithFrame:NSZeroRect];
    _banner.hidden = YES;
    _banner.button.onPress = ^{
        MSettings* s = weakSelf;
        if (s && s->_onAllow) s->_onAllow();
    };
    [_root addSubview:_banner];

    _art = [[MArt alloc] initWithFrame:NSZeroRect];
    [_root addSubview:_art];

    _buttons = [[MForm alloc] initWithFrame:NSZeroRect];
    _buttons.groupX = 8;
    _buttons.groupWidth = kWidth - kMargin - kArtWidth - kMargin - 8;
    _buttonsScroll = makeScroll(_buttons);
    [_root addSubview:_buttonsScroll];

    _scroll = [[MForm alloc] initWithFrame:NSZeroRect];
    _scroll.groupX = kMargin;
    _scroll.groupWidth = kWidth - 2 * kMargin;
    _scroll.titleColumn = 180;
    _scrollScroll = makeScroll(_scroll);
    _scrollScroll.hidden = YES;
    [_root addSubview:_scrollScroll];

    _art.onHoverPart = ^(int part) {
        MSettings* s = weakSelf;
        if (s) [s->_buttons setHighlightPart:part];
    };
    _buttons.onHoverPart = ^(int part) {
        MSettings* s = weakSelf;
        if (s) s->_art.highlight = part;
    };

    _footer = [[MFooter alloc] initWithFrame:NSZeroRect];
    _footer.state = _state;
    _footer.version = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @MOUSE_VERSION;
    _footer.updates.on = _cfg.autoUpdate;
    _footer.updates.onChange = ^(BOOL on) {
        MSettings* s = weakSelf;
        if (!s) return;
        s->_cfg.autoUpdate = on;
        [s save];
    };
    _footer.loginButton.onPress = ^{
        MSettings* s = weakSelf;
        if (s && s->_onOpenLoginItems) s->_onOpenLoginItems();
    };
    [_root addSubview:_footer];
    // Opens where it was last time, which also keeps it in place when it reopens after an update.
    if (_snapshot || ![_window setFrameUsingName:@"Settings"]) [_window center];
    if (!_snapshot) _window.frameAutosaveName = @"Settings";
}

// Vertical centre of the traffic lights: the page switcher sits on the same line.
- (CGFloat)titleCenter {
    NSButton* close = [_window standardWindowButton:NSWindowCloseButton];
    if (close.superview) {
        NSRect r = [close.superview convertRect:close.frame toView:nil];
        CGFloat c = NSHeight(_window.frame) - NSMidY(r);
        if (c >= 12 && c <= 40) return std::round(c);
    }
    return 26;
}

- (void)layout {
    CGFloat center = [self titleCenter];
    CGFloat tb = 2 * center;
    NSSize ps = _pages.frame.size;
    _pages.frame = NSMakeRect(std::round((kWidth - ps.width) / 2), std::round(center - ps.height / 2), ps.width, ps.height);

    CGFloat top = tb;
    _banner.hidden = !_needsPermission;
    if (_needsPermission) {
        _banner.frame = NSMakeRect(kMargin, top + 4, kWidth - 2 * kMargin, kBannerH);
        top += 4 + kBannerH + 4;
    }
    CGFloat bottom = kHeight - kFooterH;
    _footer.frame = NSMakeRect(0, bottom, kWidth, kFooterH);

    CGFloat x0 = kMargin + kArtWidth;
    _art.frame = NSMakeRect(kMargin, top, kArtWidth, bottom - top);
    _buttonsScroll.frame = NSMakeRect(x0, top, kWidth - x0, bottom - top);
    _scrollScroll.frame = NSMakeRect(0, top, kWidth, bottom - top);
    [self sizeDocument:_buttons in:_buttonsScroll];
    [self sizeDocument:_scroll in:_scrollScroll];
    _art.hidden = _page != 0;
    _buttonsScroll.hidden = _page != 0;
    _scrollScroll.hidden = _page != 1;
}

- (void)sizeDocument:(MForm*)form in:(NSScrollView*)sv {
    NSSize cs = sv.contentSize;
    form.frame = NSMakeRect(0, 0, cs.width, std::max(cs.height, [form contentHeight]));
}

- (void)showPage:(NSInteger)page {
    _page = page;
    _pages.selected = page;
    [self layout];
}


- (void)setNeedsLoginApproval:(BOOL)needsLoginApproval {
    _needsLoginApproval = needsLoginApproval;
    _footer.loginNote = needsLoginApproval;
}

- (void)setState:(const mouse::State&)state {
    BOOL rangeChanged = state.dpiMin != _state.dpiMin || state.dpiMax != _state.dpiMax || state.dpiStep != _state.dpiStep;
    _state = state;
    _footer.state = state;
    _footer.needsLayout = YES;
    if (rangeChanged) [self rebuildScroll];
    BOOL missing = permissionMissing(state);
    if (missing != _needsPermission) {
        _needsPermission = missing;
        [self layout];
    }
}

- (void)setConfig:(const mouse::Config&)config {
    _cfg = config;
    _footer.updates.on = _cfg.autoUpdate;
    [self rebuildButtons];
    [self rebuildScroll];
    [self layout];
}

- (void)setHoverPart:(int)part {
    _art.highlight = part;
    [_buttons setHighlightPart:part];
}

// ---------------------------------------------------------------------------- buttons page

static std::vector<MenuItem> actItems(const std::vector<mouse::Act>& acts) {
    std::vector<MenuItem> items;
    for (mouse::Act a : acts) {
        if (a == mouse::Act::None) items.push_back({nil, -1});
        else items.push_back({str(mouse::actName(a)), static_cast<int>(a)});
    }
    items.push_back({nil, -1});
    items.push_back({str(mouse::actName(mouse::Act::None)), static_cast<int>(mouse::Act::None)});
    return items;
}

static std::vector<MenuItem> wheelItems() {
    std::vector<MenuItem> items;
    for (mouse::WheelAct a : mouse::wheelMenu()) items.push_back({str(mouse::wheelActName(a)), static_cast<int>(a)});
    items.push_back({nil, -1});
    items.push_back({str(mouse::wheelActName(mouse::WheelAct::None)), static_cast<int>(mouse::WheelAct::None)});
    return items;
}

static NSImage* rowIcon(NSString* name) { return symbol(name, 14, NSFontWeightRegular); }
static NSImage* subIcon(NSString* name) { return symbol(name, 10, NSFontWeightSemibold); }

- (MPopup*)popupWithItems:(const std::vector<MenuItem>&)items selected:(int)tag label:(NSString*)label {
    MPopup* p = [[MPopup alloc] initWithFrame:NSZeroRect];
    [p setItems:items selected:tag];
    p.label = label;
    return p;
}

- (MRecorder*)recorderFor:(mouse::Shortcut)s label:(NSString*)label onChange:(void (^)(mouse::Shortcut))onChange {
    MRecorder* r = [[MRecorder alloc] initWithFrame:NSZeroRect];
    r.shortcut = s;
    r.label = label;
    r.onChange = onChange;
    return r;
}

- (void)rebuildButtons {
    [self rebuildButtonsRecording:nil];
}

// recordFirst: start recording in the recorder of this row after the rebuild.
- (void)rebuildButtonsRecording:(NSString*)recordFirst {
    __weak MSettings* weakSelf = self;
    static NSString* const icons[] = {@"computermouse", @"chevron.backward", @"chevron.forward", @"hand.draw",
                                      @"capsule.portrait"};
    FormSection sec;
    MRecorder* toRecord = nil;
    for (int i = 0; i < mouse::kControlCount; i++) {
        auto c = static_cast<mouse::Control>(i);
        const mouse::Binding& b = _cfg.button(c);
        NSString* name = str(mouse::controlName(c));
        FormRow row;
        row.title = name;
        row.icon = rowIcon(icons[i]);
        row.part = i;
        row.height = kRowH;
        MPopup* popup = [self popupWithItems:actItems(mouse::buttonMenu(c)) selected:static_cast<int>(b.act) label:name];
        popup.onSelect = ^(int tag) {
            [weakSelf setAction:static_cast<mouse::Act>(tag) forControl:c];
        };
        if (b.act == mouse::Act::Shortcut) {
            MRecorder* rec = [self recorderFor:b.keys
                                         label:[name stringByAppendingString:@" shortcut"]
                                      onChange:^(mouse::Shortcut s) {
                                          MSettings* me = weakSelf;
                                          if (!me) return;
                                          me->_cfg.button(c).keys = s;
                                          [me save];
                                      }];
            row.trailing.push_back(rec);
            if ([recordFirst isEqualToString:name]) toRecord = rec;
        }
        row.trailing.push_back(popup);
        sec.rows.push_back(row);

        if (c == mouse::Control::Thumb && b.act == mouse::Act::Gestures) {
            static NSString* const dirIcons[] = {@"smallcircle.filled.circle", @"arrow.up", @"arrow.down", @"arrow.left",
                                                 @"arrow.right"};
            for (int d = 0; d < mouse::kDirCount; d++) {
                FormRow sub;
                sub.level = 1;
                sub.title = str(mouse::dirName(static_cast<mouse::Dir>(d)));
                sub.icon = subIcon(dirIcons[d]);
                sub.part = i;
                sub.height = kSubRowH;
                sub.separator = false;
                NSString* label = [NSString stringWithFormat:@"Gesture %@", sub.title];
                MPopup* gp = [self popupWithItems:actItems(mouse::gestureMenu())
                                         selected:static_cast<int>(_cfg.gestures[d])
                                            label:label];
                gp.onSelect = ^(int tag) {
                    MSettings* me = weakSelf;
                    if (!me) return;
                    me->_cfg.gestures[d] = static_cast<mouse::Act>(tag);
                    [me save];
                };
                sub.trailing.push_back(gp);
                sec.rows.push_back(sub);
            }
        }
    }

    // Thumb wheel.
    FormRow wheel;
    wheel.title = @"Thumb wheel";
    wheel.icon = rowIcon(@"arrow.left.and.right");
    wheel.part = 5;
    wheel.height = kRowH;
    MPopup* wp = [self popupWithItems:wheelItems() selected:static_cast<int>(_cfg.thumbWheel) label:@"Thumb wheel"];
    wp.onSelect = ^(int tag) {
        [weakSelf setWheelAction:static_cast<mouse::WheelAct>(tag)];
    };
    wheel.trailing.push_back(wp);
    sec.rows.push_back(wheel);
    if (_cfg.thumbWheel == mouse::WheelAct::Shortcuts) {
        for (int up = 1; up >= 0; up--) {
            FormRow sub;
            sub.level = 1;
            sub.title = up ? @"Roll up" : @"Roll down";
            sub.icon = subIcon(up ? @"arrow.up" : @"arrow.down");
            sub.part = 5;
            sub.height = kSubRowH;
            sub.separator = false;
            MRecorder* rec = [self recorderFor:up ? _cfg.thumbUp : _cfg.thumbDown
                                         label:[@"Thumb wheel " stringByAppendingString:sub.title]
                                      onChange:^(mouse::Shortcut s) {
                                          MSettings* me = weakSelf;
                                          if (!me) return;
                                          (up ? me->_cfg.thumbUp : me->_cfg.thumbDown) = s;
                                          [me save];
                                      }];
            sub.trailing.push_back(rec);
            sec.rows.push_back(sub);
            if (up && [recordFirst isEqualToString:@"Thumb wheel"]) toRecord = rec;
        }
    }

    std::vector<FormSection> sections;
    sections.push_back(std::move(sec));
    [_buttons setSections:std::move(sections)];
    if (_buttonsScroll) [self sizeDocument:_buttons in:_buttonsScroll];
    if (toRecord) [toRecord startRecording];
}

- (void)setAction:(mouse::Act)act forControl:(mouse::Control)c {
    mouse::Binding& b = _cfg.button(c);
    mouse::Act old = b.act;
    b.act = act;
    [self save];
    auto structural = [](mouse::Act a) { return a == mouse::Act::Shortcut || a == mouse::Act::Gestures; };
    if (structural(old) || structural(act))
        [self rebuildButtonsRecording:act == mouse::Act::Shortcut && !b.keys.valid() ? str(mouse::controlName(c)) : nil];
}

- (void)setWheelAction:(mouse::WheelAct)act {
    mouse::WheelAct old = _cfg.thumbWheel;
    _cfg.thumbWheel = act;
    [self save];
    if (old == mouse::WheelAct::Shortcuts || act == mouse::WheelAct::Shortcuts)
        [self rebuildButtonsRecording:act == mouse::WheelAct::Shortcuts && !_cfg.thumbUp.valid() ? @"Thumb wheel" : nil];
}

// ---------------------------------------------------------------------------- point & scroll page

static NSString* percent(int v) { return [NSString stringWithFormat:@"%d%%", v]; }

static NSString* dpiText(int dpi) {
    return [[NSNumberFormatter localizedStringFromNumber:@(dpi) numberStyle:NSNumberFormatterDecimalStyle]
        stringByAppendingString:@" DPI"];
}

- (MSlider*)slider:(double)value label:(NSString*)label {
    MSlider* s = [[MSlider alloc] initWithFrame:NSMakeRect(0, 0, 100, 22)];
    s.value = value;
    s.label = label;
    return s;
}

// A 0...100 setting shown as a percentage.
- (FormRow)percentRow:(NSString*)title value:(int mouse::Config::*)field {
    __weak MSettings* weakSelf = self;
    FormRow row;
    row.title = title;
    row.height = kFormRowH;
    MSlider* s = [self slider:(_cfg.*field) / 100.0 label:title];
    s.step = 0.05;
    __weak MSlider* weakSlider = s;
    s.valueText = ^NSString* {
        MSettings* me = weakSelf;
        return me ? percent(me->_cfg.*field) : @"";
    };
    s.onChange = ^(double v) {
        MSettings* me = weakSelf;
        if (!me) return;
        int n = static_cast<int>(std::lround(v * 100));
        if (n == me->_cfg.*field) return;
        me->_cfg.*field = n;
        [me->_scroll setValue:percent(n) forFill:weakSlider];
        [me save];
    };
    row.fill = s;
    row.value = percent(_cfg.*field);
    return row;
}

- (FormRow)segmentRow:(NSString*)title
               titles:(NSArray<NSString*>*)titles
             selected:(NSInteger)selected
             onChange:(void (^)(NSInteger))onChange {
    FormRow row;
    row.title = title;
    row.height = kFormRowH;
    MSegmented* seg = [[MSegmented alloc] initWithTitles:titles height:24 fontSize:12];
    seg.selected = selected;
    seg.label = title;
    seg.onChange = onChange;
    row.trailing.push_back(seg);
    return row;
}

- (void)rebuildScroll {
    __weak MSettings* weakSelf = self;
    std::vector<FormSection> sections;
    int lo = _state.dpiMin, hi = _state.dpiMax, step = _state.dpiStep;
    if (lo <= 0 || hi <= lo) lo = 200, hi = 8000, step = 50;

    // Pointer
    FormSection pointer;
    pointer.header = @"Pointer";
    FormRow speed;
    speed.title = @"Pointer speed";
    speed.height = kFormRowH;
    MSlider* dpi = [self slider:mouse::dpiToPosition(_cfg.dpi, lo, hi) label:@"Pointer speed"];
    dpi.step = 0.04;
    __weak MSlider* weakDpi = dpi;
    dpi.valueText = ^NSString* {
        MSettings* me = weakSelf;
        return me ? dpiText(me->_cfg.dpi) : @"";
    };
    dpi.onChange = ^(double v) {
        MSettings* me = weakSelf;
        if (!me) return;
        int d = mouse::positionToDpi(v, lo, hi, step);
        if (d == me->_cfg.dpi) return;
        me->_cfg.dpi = d;
        [me->_scroll setValue:dpiText(d) forFill:weakDpi];
        [me save];
    };
    speed.fill = dpi;
    speed.value = dpiText(_cfg.dpi);
    pointer.rows.push_back(speed);
    sections.push_back(pointer);
    _dpiSlider = dpi;

    // Scroll wheel
    FormSection wheel;
    wheel.header = @"Scroll wheel";
    wheel.rows.push_back([self percentRow:@"Scrolling speed" value:&mouse::Config::scrollSpeed]);
    wheel.rows.push_back([self segmentRow:@"Direction"
                                   titles:@[ @"Natural", @"Standard" ]
                                 selected:_cfg.naturalScroll ? 0 : 1
                                 onChange:^(NSInteger i) {
                                     MSettings* me = weakSelf;
                                     if (!me) return;
                                     me->_cfg.naturalScroll = i == 0;
                                     [me save];
                                 }]);
    FormRow smooth;
    smooth.title = @"Smooth scrolling";
    smooth.height = kFormRowH;
    MToggle* t = [[MToggle alloc] initWithSize:NSMakeSize(32, 18)];
    t.on = _cfg.smoothScroll;
    t.label = @"Smooth scrolling";
    t.onChange = ^(BOOL on) {
        MSettings* me = weakSelf;
        if (!me) return;
        me->_cfg.smoothScroll = on;
        [me save];
    };
    smooth.trailing.push_back(t);
    wheel.rows.push_back(smooth);
    wheel.rows.push_back([self segmentRow:@"Wheel mode"
                                   titles:@[ @"Auto", @"Ratchet", @"Free spin" ]
                                 selected:static_cast<NSInteger>(_cfg.wheelMode)
                                 onChange:^(NSInteger i) {
                                     MSettings* me = weakSelf;
                                     if (!me) return;
                                     bool wasAuto = me->_cfg.wheelMode == mouse::WheelMode::Auto;
                                     me->_cfg.wheelMode = static_cast<mouse::WheelMode>(i);
                                     [me save];
                                     if (wasAuto != (i == 0)) [me rebuildScroll];
                                 }]);
    if (_cfg.wheelMode == mouse::WheelMode::Auto)
        wheel.rows.push_back([self percentRow:@"Auto-shift sensitivity" value:&mouse::Config::sensitivity]);
    sections.push_back(wheel);

    // Thumb wheel
    FormSection thumb;
    thumb.header = @"Thumb wheel";
    thumb.rows.push_back([self percentRow:@"Speed" value:&mouse::Config::thumbSpeed]);
    thumb.rows.push_back([self segmentRow:@"Direction"
                                   titles:@[ @"Default", @"Inverted" ]
                                 selected:_cfg.thumbInverted ? 1 : 0
                                 onChange:^(NSInteger i) {
                                     MSettings* me = weakSelf;
                                     if (!me) return;
                                     me->_cfg.thumbInverted = i == 1;
                                     [me save];
                                 }]);
    sections.push_back(thumb);

    [_scroll setSections:std::move(sections)];
    if (_scrollScroll) [self sizeDocument:_scroll in:_scrollScroll];
}

// ---------------------------------------------------------------------------- window delegate

- (void)windowWillClose:(NSNotification*)note {
    [_window makeFirstResponder:nil];
}

@end
