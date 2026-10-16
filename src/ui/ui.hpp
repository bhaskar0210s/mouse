// Settings window: shared drawing helpers and the custom controls.
//
// Every control here is drawn in code and changes state the moment it is clicked: no animations,
// no fades, no delays. Views are flipped (y grows downwards) to keep the layout code simple.
#pragma once

#import <AppKit/AppKit.h>

#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/store.hpp"

namespace ui {

// ------------------------------------------------------------------------------------- style

NSColor* dynamicColor(NSColor* light, NSColor* dark);
NSColor* groupFill();     // inset group boxes
NSColor* groupStroke();
NSColor* rowHover();      // a row under the pointer
NSColor* hoverFill();     // borderless buttons under the pointer
NSColor* pressFill();     // borderless buttons while pressed / open
NSColor* trackFill();     // switch (off), slider track, segmented control background
NSColor* knobFill();      // selected segment
NSColor* bezelFill();     // push buttons
NSColor* fieldFill();     // shortcut recorder
NSColor* fieldStroke();

NSFont* font(CGFloat size, NSFontWeight weight = NSFontWeightRegular);
NSFont* digits(CGFloat size, NSFontWeight weight = NSFontWeightRegular);  // tabular figures

// One device pixel in points (snapshots force 2x).
extern CGFloat gForcedScale;
CGFloat scaleOf(NSView* v);
CGFloat pixel(NSView* v);
CGFloat snap(NSView* v, CGFloat value);

CGFloat textWidth(NSString* s, NSFont* f);
// One line of text whose cap height is centred on midY (flipped views).
void drawText(NSView* v, NSString* s, NSFont* f, NSColor* c, CGFloat x, CGFloat midY);
void drawTextRight(NSView* v, NSString* s, NSFont* f, NSColor* c, CGFloat right, CGFloat midY);
void drawTextCentered(NSView* v, NSString* s, NSFont* f, NSColor* c, CGFloat midX, CGFloat midY);

NSImage* symbol(NSString* name, CGFloat pointSize, NSFontWeight weight = NSFontWeightRegular);
void drawSymbol(NSView* v, NSImage* img, NSPoint center, NSColor* tint);

void fillRounded(NSRect r, CGFloat radius, NSColor* c);
void strokeRounded(NSView* v, NSRect r, CGFloat radius, NSColor* c);  // 1 px, inside r
void drawKnobShadowed(NSRect r, CGFloat radius, NSColor* fill, NSColor* edge);

NSString* str(const std::string& s);
NSString* str(const char* s);

}  // namespace ui

// ------------------------------------------------------------------------------------- controls

// Base class: flipped, tracks hover, reacts on the first click even when the window is inactive.
@interface MControl : NSView
@property(nonatomic) BOOL hovered;
@property(nonatomic) BOOL pressed;
@property(nonatomic, copy) NSString* label;  // accessibility label
@end

// Toggle switch.
@interface MToggle : MControl
- (instancetype)initWithSize:(NSSize)size;
@property(nonatomic) BOOL on;
@property(nonatomic, copy) void (^onChange)(BOOL on);
@end

// Segmented control with equal segments. The selected segment is a raised knob.
@interface MSegmented : MControl
- (instancetype)initWithTitles:(NSArray<NSString*>*)titles height:(CGFloat)height fontSize:(CGFloat)size;
@property(nonatomic) NSInteger selected;
@property(nonatomic, copy) void (^onChange)(NSInteger index);
- (NSString*)titleAt:(NSInteger)i;
- (NSRect)segmentRect:(NSInteger)i;
@end

// Horizontal slider, value 0...1.
@interface MSlider : MControl
@property(nonatomic) double value;
@property(nonatomic) double step;  // accessibility increment
@property(nonatomic, copy) void (^onChange)(double value);
@property(nonatomic, copy) NSString* (^valueText)(void);  // accessibility value
@end

// Push button with a light bezel.
@interface MButton : MControl
- (instancetype)initWithTitle:(NSString*)title;
@property(nonatomic, copy) void (^onPress)(void);
@end

// One entry of a popup list. tag < 0 is a separator.
struct MenuItem {
    NSString* title;
    int tag;
    NSImage* image = nil;  // shown before the title (16 pt), e.g. an app icon
};

// Borderless popup button: "Middle click ⌃⌄". Opens an instant list (see popup.mm).
@interface MPopup : MControl
- (void)setItems:(const std::vector<MenuItem>&)items selected:(int)tag;
@property(nonatomic, readonly) int selectedTag;
@property(nonatomic, readonly) const std::vector<MenuItem>& items;
@property(nonatomic, copy) void (^onSelect)(int tag);
- (CGFloat)textInset;  // x of the title inside the button
- (void)choose:(int)tag;
@end

// Inline keyboard shortcut recorder.
@interface MRecorder : MControl
@property(nonatomic) mouse::Shortcut shortcut;
@property(nonatomic, copy) void (^onChange)(mouse::Shortcut shortcut);
@property(nonatomic, readonly) BOOL recording;
- (void)startRecording;
@end

// ------------------------------------------------------------------------------------- form

// A row of a form: title on the left, controls on the right, optionally a stretched control
// (slider) between the title column and a value column.
struct FormRow {
    int level = 0;  // 0 = row, 1 = indented sub row
    NSString* title = nil;
    NSImage* icon = nil;
    int part = -1;  // the mouse control this row is about (illustration marker), -1 none
    CGFloat height = 40;
    NSView* fill = nil;
    NSString* value = nil;
    std::vector<NSView*> trailing;  // laid out right to left from the row's right edge
    bool separator = true;          // hairline above this row (ignored for the first row)
};

struct FormSection {
    NSString* header = nil;
    std::vector<FormRow> rows;
};

@interface MForm : NSView
@property(nonatomic) CGFloat groupX, groupWidth;
@property(nonatomic) CGFloat titleColumn;  // width of the title column for rows with a fill control
@property(nonatomic) CGFloat valueColumn;  // width of the value column
@property(nonatomic) CGFloat topInset, bottomInset;
@property(nonatomic, copy) void (^onHoverPart)(int part);
- (void)setSections:(std::vector<FormSection>)sections;
- (void)setValue:(NSString*)value forFill:(NSView*)fill;
- (void)setHighlightPart:(int)part;
- (CGFloat)contentHeight;
@end

// Top-down illustration of the mouse with markers on the customisable controls.
// Parts: 0 middle, 1 back, 2 forward, 3 thumb button, 4 top button, 5 thumb wheel.
@interface MArt : NSView
@property(nonatomic) int highlight;
@property(nonatomic, copy) void (^onHoverPart)(int part);
@end
