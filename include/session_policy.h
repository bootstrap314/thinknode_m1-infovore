#pragma once
#include <cstdint>
namespace SessionPolicy {
// A subscription belongs to one local transport; radio scheduling is separate.
struct Stream {
  unsigned seconds = 0;
  uint32_t last = 0;
  void start(unsigned interval, uint32_t now) {
    seconds = interval;
    last = now;
  }
  void stop() { seconds = 0; }
  bool due(uint32_t now, bool connected) {
    if (!connected) {
      stop();
      return false;
    }
    if (!seconds || uint32_t(now - last) < seconds * 1000u)
      return false;
    last = now;
    return true;
  }
};
} // namespace SessionPolicy
