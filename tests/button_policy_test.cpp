#include "button_policy.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
using ButtonPolicy::Action;
using ButtonPolicy::Button;
using ButtonPolicy::Controls;
using ButtonPolicy::Gesture;

void controlsTests() {
  // Existing single-button actions retain their timing and release behavior.
  for (bool first : {false, true}) {
    Controls c;
    assert(c.update(first, !first, 0) == Gesture::None);
    assert(c.update(first, !first, 35) == (first ? Gesture::None : Gesture::Activity));
    assert(c.update(false, false, 100) == Gesture::None);
    assert(c.update(false, false, 135) == (first ? Gesture::Light : Gesture::Page));
    const auto hold = first ? ButtonPolicy::modeHoldMs : ButtonPolicy::joinHoldMs;
    c.update(first, !first, 1000);
    c.update(first, !first, 1035);
    assert(c.update(first, !first, 1000 + hold) == (first ? Gesture::Mode : Gesture::Join));
    assert(c.update(first, !first, 5000) == Gesture::None);
    assert(c.update(false, false, 5100) == Gesture::None);
    assert(c.update(false, false, 5135) == Gesture::None);
  }
  // Both buttons, with either arriving first, cancel mode/join/tap actions.
  // The second can arrive exactly as the first single-button hold becomes due.
  for (bool first : {false, true}) {
    for (uint32_t delay : {0u, 200u, first ? ButtonPolicy::modeHoldMs : ButtonPolicy::joinHoldMs}) {
      Controls c;
      c.update(first, !first, 0);
      if (delay)
        c.update(first, !first, 35);
      assert(c.update(true, true, delay) == Gesture::None);
      assert(c.update(true, true, delay + 35) == Gesture::None);
      assert(c.update(true, true, delay + 1999) == Gesture::None);
      assert(c.update(true, true, delay + 2000) == Gesture::Blank);
      assert(c.update(true, true, delay + 9000, true) == Gesture::None);
      // Releasing only one button cannot wake, join, change mode or change pages.
      assert(c.update(first, !first, delay + 9010, true) == Gesture::None);
      assert(c.update(first, !first, delay + 9045, true) == Gesture::None);
      assert(c.update(first, !first, delay + 12000, true) == Gesture::None);
      assert(c.update(true, true, delay + 13000, true) == Gesture::None);
      assert(c.update(true, true, delay + 13035, true) == Gesture::None);
      assert(c.update(true, true, delay + 15000, true) == Gesture::None);
      assert(c.update(false, false, delay + 16000, true) == Gesture::None);
      assert(c.update(false, false, delay + 16035, true) == Gesture::None);
      // Either button wakes; even a long wake press cannot run a normal action.
      assert(c.update(first, !first, delay + 17000, true) == Gesture::None);
      assert(c.update(first, !first, delay + 17035, true) == Gesture::Wake);
      assert(c.update(first, !first, delay + 20000, false) == Gesture::None);
      assert(c.update(false, false, delay + 21000) == Gesture::None);
      assert(c.update(false, false, delay + 21035) == Gesture::None);
      c.update(first, !first, delay + 22000);
      c.update(first, !first, delay + 22035);
      c.update(false, false, delay + 22100);
      assert(c.update(false, false, delay + 22135) == (first ? Gesture::Light : Gesture::Page));
    }
  }
  // Short chords do nothing; bounce and release debounce add no hold time.
  for (uint32_t duration : {100u, 1999u, 2000u, 2100u}) {
    Controls c;
    c.update(true, true, 0);
    c.update(false, false, 10);
    c.update(true, true, 20);
    assert(c.update(true, true, 55) == Gesture::None);
    assert(c.update(false, false, 20 + duration) == Gesture::None);
    assert(c.update(false, false, 55 + duration) ==
           (duration >= 2000 ? Gesture::Blank : Gesture::None));
    assert(c.update(false, false, 90 + duration) == Gesture::None);
    assert(c.update(false, false, 125 + duration) == Gesture::None);
    // Re-arm after complete release, including after a hold detected on release.
    c.update(true, true, 10000);
    c.update(true, true, 10035);
    assert(c.update(true, true, 12000) == Gesture::Blank);
  }
  Controls rollover;
  const uint32_t start = UINT32_MAX - 100;
  rollover.update(true, true, start);
  assert(rollover.update(true, true, start + 35) == Gesture::None);
  assert(rollover.update(true, true, start + 1999) == Gesture::None);
  assert(rollover.update(true, true, start + 2000) == Gesture::Blank);
  rollover.update(false, false, start + 2010);
  rollover.update(false, false, start + 2045);
  rollover.update(true, false, start + 2100, true);
  assert(rollover.update(true, false, start + 2135, true) == Gesture::Wake);
}

int main() {
  for (uint32_t threshold : {ButtonPolicy::modeHoldMs, ButtonPolicy::joinHoldMs}) {
    Button b(threshold);
    assert(b.update(false, 0) == Action::None);
    // Debounce contact chatter and toggle only after a released tap.
    assert(b.update(true, 10) == Action::None);
    assert(b.update(false, 20) == Action::None);
    assert(b.update(true, 30) == Action::None);
    assert(b.update(true, 64) == Action::None);
    assert(b.update(true, 65) == Action::Pressed);
    assert(b.update(false, 100) == Action::None);
    assert(b.update(true, 110) == Action::None);
    assert(b.update(false, 120) == Action::None);
    assert(b.update(false, 155) == Action::Tap);
    assert(b.update(false, 3000) == Action::None);
    // The hold interval starts at the accepted press edge, without adding debounce.
    assert(b.update(true, 4000) == Action::None);
    assert(b.update(true, 4035) == Action::Pressed);
    assert(b.update(true, 4000 + threshold - 1) == Action::None);
    assert(b.update(true, 4000 + threshold) == Action::Hold);
    assert(b.update(true, 12000) == Action::None);
    assert(b.update(false, 12020) == Action::None);
    assert(b.update(false, 12055) == Action::None);
    // A second hold works after release, with no repeat or release tap.
    assert(b.update(true, 13000) == Action::None);
    assert(b.update(true, 13035) == Action::Pressed);
    assert(b.update(true, 13000 + threshold) == Action::Hold);
    Button early(threshold);
    early.update(true, 0);
    assert(early.update(true, 35) == Action::Pressed);
    assert(early.update(false, threshold - 1) == Action::None);
    assert(early.update(false, threshold + 34) == Action::Tap);
    // Releasing at the advertised threshold still counts as a hold.
    Button exact(threshold);
    exact.update(true, 0);
    assert(exact.update(true, 35) == Action::Pressed);
    assert(exact.update(false, threshold) == Action::None);
    assert(exact.update(false, threshold + 35) == Action::Hold);
    assert(exact.update(false, threshold + 100) == Action::None);
    // Delayed polls do not add time to the gesture or cause a release tap.
    Button delayed(threshold);
    delayed.update(true, 0);
    assert(delayed.update(true, 350) == Action::Pressed);
    assert(delayed.update(false, threshold + 10) == Action::None);
    assert(delayed.update(false, threshold + 45) == Action::Hold);
    assert(delayed.update(false, 5000) == Action::None);
    Button rollover(threshold);
    const uint32_t start = UINT32_MAX - 100;
    rollover.update(true, start);
    assert(rollover.update(true, start + 35) == Action::Pressed);
    assert(rollover.update(true, start + threshold - 1) == Action::None);
    assert(rollover.update(true, start + threshold) == Action::Hold);
    assert(rollover.update(false, start + threshold + 1) == Action::None);
    assert(rollover.update(false, start + threshold + 36) == Action::None);
  }
  controlsTests();
  puts("PASS: button thresholds, debounce, taps, delayed polls, rollover, two-button blanking, "
       "single-action suppression and wake/re-arm");
}
