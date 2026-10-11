// Pure motion maths: wheel acceleration, thumb-button gestures and thumb-wheel steps.
// No I/O here, so it is fully unit tested.
#pragma once

#include <cstdint>

namespace mouse {

// Speed slider (0..100, 50 = normal) to a multiplier (about 0.3x .. 3.2x).
double speedFactor(int speed);

// Turns wheel movement into scroll amounts. Slow, careful turns scroll a fixed amount per notch;
// fast turns and free-spin flicks scroll progressively further, like macOS does for wheels.
class ScrollEngine {
public:
    // pixelsPerNotch: distance of one slow notch when emitting pixels.
    explicit ScrollEngine(double pixelsPerNotch = 36.0) : pixelsPerNotch_(pixelsPerNotch) {}

    // notches: signed wheel movement in notches (fractions allowed). Returns the signed amount to
    // scroll now: pixels when `pixels`, otherwise lines. Remainders carry over between calls.
    int feed(double notches, double now, double speed, bool pixels);
    void reset();

    double velocity() const { return velocity_; }  // notches per second (smoothed)
    static double gain(double notchesPerSecond);

private:
    double pixelsPerNotch_;
    double velocity_ = 0;
    double last_ = -1;
    int lastSign_ = 0;
    double carry_ = 0;
};

// Thumb button gestures: hold the button and move the mouse up/down/left/right, or just click.
class GestureTracker {
public:
    enum Result : uint8_t { kNone, kClick, kUp, kDown, kLeft, kRight };

    void begin(int threshold);  // threshold in sensor counts
    Result move(int dx, int dy);  // returns a direction once per press when moved far enough
    Result end();               // kClick if no direction fired during the press
    bool active() const { return active_; }

private:
    bool active_ = false;
    bool fired_ = false;
    int threshold_ = 80;
    long dx_ = 0, dy_ = 0;
};

// Thumb wheel to discrete steps (volume, zoom, tabs, shortcuts).
class Stepper {
public:
    // units: signed rotation; unitsPerStep: rotation needed for one step; minInterval: seconds.
    // Returns +1 / -1 for a step, 0 otherwise.
    int feed(int units, double unitsPerStep, double now, double minInterval);
    void reset();

private:
    double acc_ = 0;
    double lastStep_ = -1;
    double lastInput_ = -1;
};

}  // namespace mouse
