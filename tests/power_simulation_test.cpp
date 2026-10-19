#include "power_policy.h"
#include <cassert>
#include <iostream>
using namespace PowerPolicy;
// Virtual time, not a hardware or current-consumption model. It runs the same
// scheduling code as the firmware, with synthetic GPS completion events.
static unsigned simulate(unsigned fixAfterSeconds, unsigned duration) {
  GpsSchedule gps;
  gps.set(true, 0);
  unsigned awakeSeconds = 0;
  for (unsigned second = 0; second < duration; ++second) {
    const uint32_t now = second * 1000UL;
    const bool sample = fixAfterSeconds && now - gps.changedAt >= fixAfterSeconds * 1000UL;
    const bool wanted = gps.wanted(now, true, GpsMode::Auto, sample);
    if (wanted != gps.awake) gps.set(wanted, now);
    if (gps.awake) ++awakeSeconds;
  }
  return awakeSeconds;
}
int main() {
  const unsigned good = simulate(20, 7200);
  const unsigned noFix = simulate(0, 7200);
  assert(good > 0 && good < 360); // <5% awake with a synthetic 20-second acquisition
  assert(noFix >= 1080 && noFix <= 1260); // 3-minute attempts, 15-minute sleeps
  std::cout << "PASS: 2-hour virtual GPS schedule: " << good
            << "s awake with 20s fixes; " << noFix << "s awake with no fix\n";
}
