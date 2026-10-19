#pragma once
#include "platform.h"

namespace Output {
constexpr size_t phoneColumns = 32;
struct Status {
  const char *rtos, *version;
  bool dongle;
  double battery;
  int batteryPercent;
  bool fix;
  double lat, lon;
  unsigned satellites;
  double hdop;
  bool joined;
  uint32_t uplinks;
  bool light;
  unsigned page;
  uint32_t uptime;
  bool bleConnected;
  const char *gpsPower;
  long fixAge;
  unsigned provisioned;
  bool storageOk;
};
// USB remains one CRLF-terminated protocol record. BLE wraps at word boundaries.
bool reply(Hw::Link link, const char *format, ...) __attribute__((format(printf, 2, 3)));
bool status(Hw::Link link, const Status &value);
} // namespace Output
