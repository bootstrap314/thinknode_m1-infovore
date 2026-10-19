#pragma once
#include <cstdint>

namespace ButtonPolicy {
constexpr uint32_t debounceMs = 35, modeHoldMs = 1000, joinHoldMs = 1200, blankHoldMs = 2000;
enum class Action { None, Pressed, Tap, Hold };
class Button {
public:
  explicit Button(uint32_t holdMs) : holdMs_(holdMs) {}
  // Pass true for a pressed button. Act on release for taps so a hold cannot
  // also trigger the tap action. Timestamps use unsigned rollover arithmetic.
  Action update(bool pressed, uint32_t now) {
    if (pressed != raw_) {
      raw_ = pressed;
      rawAt_ = now;
    }
    if (stable_ != raw_ && uint32_t(now - rawAt_) >= debounceMs) {
      stable_ = raw_;
      if (stable_) {
        downAt_ = rawAt_; // Count the accepted press from its edge, not after debounce.
        held_ = false;
        return Action::Pressed;
      }
      if (!held_) {
        held_ = true;
        // A delayed poll after release must still recognize a completed hold,
        // without counting release debounce time as time spent pressing.
        return uint32_t(rawAt_ - downAt_) >= holdMs_ ? Action::Hold : Action::Tap;
      }
    }
    if (stable_ && raw_ && !held_ && uint32_t(now - downAt_) >= holdMs_) {
      held_ = true;
      return Action::Hold;
    }
    return Action::None;
  }

private:
  const uint32_t holdMs_;
  bool raw_ = false, stable_ = false, held_ = false;
  uint32_t rawAt_ = 0, downAt_ = 0;
};

enum class Gesture { None, Light, Mode, Page, Join, Activity, Blank, Wake };
class Controls {
public:
  Gesture update(bool first, bool second, uint32_t now, bool screenBlank = false) {
    const auto a = first_.update(first, now);
    const auto b = second_.update(second, now);
    const auto chord = both_.update(first && second, now);
    // Give an overlapping press priority before dispatching either single hold,
    // including when the second button arrives at the first button's threshold.
    if (first && second)
      suppressed_ = true;
    if (suppressed_) {
      if (chord == Action::Hold && !chordFired_) {
        chordFired_ = true;
        return Gesture::Blank;
      }
      if (!first && !second) {
        if (!releasing_) {
          releasedAt_ = now;
          releasing_ = true;
        }
        if (uint32_t(now - releasedAt_) >= debounceMs) {
          suppressed_ = false;
          chordFired_ = false;
        }
      } else
        releasing_ = false;
      return Gesture::None;
    }
    if (screenBlank && (a == Action::Pressed || b == Action::Pressed)) {
      // Consume the entire wake press, even if the user keeps it held down.
      suppressed_ = true;
      chordFired_ = true;
      releasing_ = false;
      return Gesture::Wake;
    }
    if (a == Action::Tap)
      return Gesture::Light;
    if (a == Action::Hold)
      return Gesture::Mode;
    if (b == Action::Tap)
      return Gesture::Page;
    if (b == Action::Hold)
      return Gesture::Join;
    return b == Action::Pressed ? Gesture::Activity : Gesture::None;
  }

private:
  Button first_{modeHoldMs}, second_{joinHoldMs}, both_{blankHoldMs};
  bool suppressed_ = false, chordFired_ = false, releasing_ = false;
  uint32_t releasedAt_ = 0;
};
} // namespace ButtonPolicy
