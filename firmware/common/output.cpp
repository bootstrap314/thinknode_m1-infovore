#include "output.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Output {
namespace {
bool phoneText(const char *text, size_t length) {
  char line[phoneColumns + 2];
  // Explicit newlines and blank lines are preserved; long tokens (such as
  // downlink hex) are split without dropping characters.
  do {
    size_t end = 0;
    while (end < length && text[end] != '\r' && text[end] != '\n')
      ++end;
    size_t used = 0;
    do {
      size_t count = std::min(phoneColumns, end - used);
      if (used + count < end && text[used + count] != ' ') {
        size_t space = count;
        while (space && text[used + space] != ' ')
          --space;
        if (space)
          count = space;
      }
      memcpy(line, text + used, count);
      line[count] = '\r';
      line[count + 1] = '\n';
      if (!Hw::send(Hw::Link::Ble, line, count + 2))
        return false;
      used += count;
      while (used < end && text[used] == ' ')
        ++used;
    } while (used < end);
    size_t next = end;
    if (next < length && text[next] == '\r')
      ++next;
    if (next < length && text[next] == '\n')
      ++next;
    text += next;
    length -= next;
  } while (length);
  return true;
}
} // namespace

bool reply(Hw::Link link, const char *format, ...) {
  char text[640];
  va_list ap;
  va_start(ap, format);
  int n = vsnprintf(text, sizeof(text) - 2, format, ap);
  va_end(ap);
  if (n < 0)
    return false;
  size_t length = std::min(size_t(n), sizeof(text) - 3);
  if (link == Hw::Link::Ble)
    return phoneText(text, length);
  text[length++] = '\r';
  text[length++] = '\n';
  return Hw::send(link, text, length);
}

bool status(Hw::Link link, const Status &s) {
  if (link == Hw::Link::Usb)
    return reply(
        link,
        "STATUS protocol=1 mode=%s rtos=%s version=%s battery=%.2fV battery_pct=%d gps_fix=%u "
        "lat=%.6f lon=%.6f "
        "sats=%u hdop=%.1f lora=%s fcnt=%lu light=%s page=%u uptime_s=%lu ble=%s gps_power=%s "
        "fix_age_s=%ld provisioned=%u storage=%s",
        s.dongle ? "dongle" : "tracker", s.rtos, s.version, s.battery, s.batteryPercent,
        unsigned(s.fix), s.lat, s.lon, s.satellites, s.hdop, s.joined ? "joined" : "not_joined",
        (unsigned long)s.uplinks, s.light ? "on" : "off", s.page, (unsigned long)s.uptime,
        s.bleConnected ? "connected" : "advertising", s.gpsPower, s.fixAge, s.provisioned,
        s.storageOk ? "ok" : "error");

  if (!reply(link, "\nThinkNode M1\nFirmware: %s %s", s.rtos, s.version))
    return false;
  if (s.battery < 0) {
    if (!reply(link, "Battery: unavailable"))
      return false;
  } else if (!reply(link, "Battery: %.2f V (%d%%)", s.battery, s.batteryPercent))
    return false;
  const char *gps = !strcmp(s.gpsPower, "switch_off") ? "switch off" : s.gpsPower;
  if (!reply(link, "Mode: %s\nGPS: %s\nFix: %s", s.dongle ? "USB dongle" : "tracker", gps,
             s.fix ? "current" : (s.fixAge >= 0 ? "last known" : "none")))
    return false;
  if (s.fixAge >= 0) {
    if (!reply(link, "Latitude: %.6f\nLongitude: %.6f\nFix age: %ld s", s.lat, s.lon, s.fixAge))
      return false;
  } else if (!reply(link, "Position: no fix"))
    return false;
  if (!reply(link, "Satellites: %u", s.satellites))
    return false;
  if (s.hdop > 0) {
    if (!reply(link, "HDOP: %.1f", s.hdop))
      return false;
  } else if (!reply(link, "HDOP: unavailable"))
    return false;
  return reply(link,
               "LoRaWAN: %s\nUplinks: %lu\nBluetooth: %s\nLight: %s\nPage: %u\n"
               "Uptime: %lu s\nKeys: %s\nStorage: %s\n\n",
               s.joined ? "joined" : "not joined", (unsigned long)s.uplinks,
               s.bleConnected ? "connected" : "advertising", s.light ? "on" : "off", s.page,
               (unsigned long)s.uptime, s.provisioned ? "configured" : "not configured",
               s.storageOk ? "ok" : "error");
}
} // namespace Output
