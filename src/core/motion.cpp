#include "motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mouse {

double speedFactor(int speed) { return std::pow(2.0, (std::clamp(speed, 0, 100) - 50) / 30.0); }

// ---------------------------------------------------------------- ScrollEngine

namespace {
constexpr double kWindow = 0.2;  // seconds of history used to measure wheel speed
}

double ScrollEngine::gain(double v) {
    if (v <= 8) return 1.0;
    return std::min(1.0 + (v - 8) * 0.12, 10.0);
}

int ScrollEngine::feed(double notches, double now, double speed, bool pixels) {
    if (notches == 0 || !std::isfinite(notches)) return 0;
    int sign = notches > 0 ? 1 : -1;
    // A pause or a change of direction starts a fresh, unaccelerated movement.
    if (sign != lastSign_ || last_ < 0 || now - last_ > kWindow || now < last_) {
        velocity_ = 0;
        carry_ = 0;
    } else {
        // Exponential moving average of notches per second (time constant 0.1 s), so the
        // quick snap through a single detent never counts as a fast turn.
        double dt = std::max(now - last_, 0.004);
        double inst = std::fabs(notches) / dt;
        double alpha = 1.0 - std::exp(-dt / 0.1);
        velocity_ += alpha * (inst - velocity_);
    }
    last_ = now;
    lastSign_ = sign;
    double amount = notches * speedFactor(static_cast<int>(speed)) * gain(velocity_) * (pixels ? pixelsPerNotch_ : 1.0);
    carry_ += amount;
    double whole = std::trunc(carry_);
    carry_ -= whole;
    return static_cast<int>(std::clamp(whole, -100000.0, 100000.0));
}

void ScrollEngine::reset() {
    velocity_ = 0;
    last_ = -1;
    lastSign_ = 0;
    carry_ = 0;
}

// ---------------------------------------------------------------- GestureTracker

void GestureTracker::begin(int threshold) {
    active_ = true;
    fired_ = false;
    threshold_ = std::max(threshold, 10);
    dx_ = dy_ = 0;
}

GestureTracker::Result GestureTracker::move(int dx, int dy) {
    if (!active_ || fired_) return kNone;
    dx_ += dx;
    dy_ += dy;
    long ax = std::labs(dx_), ay = std::labs(dy_);
    if (std::max(ax, ay) < threshold_) return kNone;
    fired_ = true;
    if (ax > ay) return dx_ > 0 ? kRight : kLeft;
    return dy_ > 0 ? kDown : kUp;  // sensor y grows towards the user
}

GestureTracker::Result GestureTracker::end() {
    if (!active_) return kNone;
    active_ = false;
    return fired_ ? kNone : kClick;
}

// ---------------------------------------------------------------- Stepper

int Stepper::feed(int units, double unitsPerStep, double now, double minInterval) {
    if (units == 0) return 0;
    unitsPerStep = std::max(unitsPerStep, 1.0);
    if (lastInput_ >= 0 && (now - lastInput_ > 0.4 || now < lastInput_)) acc_ = 0;  // idle: drop partial turns
    lastInput_ = now;
    if ((acc_ > 0 && units < 0) || (acc_ < 0 && units > 0)) acc_ = 0;  // direction change
    acc_ += units;
    if (std::fabs(acc_) < unitsPerStep) return 0;
    int dir = acc_ > 0 ? 1 : -1;
    if (lastStep_ >= 0 && now >= lastStep_ && now - lastStep_ < minInterval) {
        acc_ = dir * unitsPerStep;  // ready to fire, but never bank extra steps
        return 0;
    }
    acc_ -= dir * unitsPerStep;
    if (std::fabs(acc_) >= unitsPerStep) acc_ = dir * (unitsPerStep - 1);
    lastStep_ = now;
    return dir;
}

void Stepper::reset() {
    acc_ = 0;
    lastStep_ = -1;
    lastInput_ = -1;
}

}  // namespace mouse
