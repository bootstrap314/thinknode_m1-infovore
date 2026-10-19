#pragma once
#include <stdint.h>
namespace PowerPolicy {
enum class GpsMode { Auto, On, Off };
struct GpsSchedule {
  bool awake = true;
  uint32_t changedAt = 0;
  // Wait 15 minutes after a sample/timeout, then acquire for up to 3 minutes.
  bool wanted(uint32_t now, bool switchOn, GpsMode mode, bool sampleComplete) const {
    if (!switchOn || mode == GpsMode::Off) return false;
    if (mode == GpsMode::On) return true;
    const uint32_t elapsed = now - changedAt;
    if (!awake) return elapsed >= 900000UL;
    return elapsed < 180000UL && !(sampleComplete && elapsed >= 10000UL);
  }
  void set(bool value, uint32_t now) { awake = value; changedAt = now; }
};
inline bool lightExpired(bool on, uint32_t now, uint32_t enabledAt) {
  return on && uint32_t(now - enabledAt) >= 60000UL;
}
}
