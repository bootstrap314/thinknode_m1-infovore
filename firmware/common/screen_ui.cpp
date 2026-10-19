#include "screen_ui.h"
#include "display.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ScreenUi {
namespace {
// 12 characters at 16 pixels each fit between the 4-pixel side margins.
void row(unsigned index, const char *format, ...) __attribute__((format(printf, 2, 3)));
void row(unsigned index, const char *format, ...) {
  char text[48];
  va_list ap;
  va_start(ap, format);
  vsnprintf(text, sizeof(text), format, ap);
  va_end(ap);
  Screen::text(4, 4 + int(index) * 22, text, 2);
}
const char *gps(const Output::Status &s) {
  if (!strcmp(s.gpsPower, "switch_off"))
    return "GPS: off";
  if (s.fix)
    return "GPS: fix";
  return !strcmp(s.gpsPower, "awake") ? "GPS: awake" : "GPS: standby";
}
} // namespace
void draw(const Output::Status &s, double altitude, int joinResult, int txResult,
          const char *passkey) {
  Screen::clear();
  if (*passkey) {
    row(0, "Pair phone");
    row(2, "Enter code:");
    Screen::text(28, 82, passkey, 3);
    row(5, "On iPhone");
    row(7, "Bluefruit");
    row(8, "Connect");
  } else if (s.page == 0) {
    row(0, "Mode: %s", s.dongle ? "Dongle" : "Track");
    if (s.battery < 0) {
      row(1, "Batt: --");
      row(2, "Charge: --");
    } else {
      row(1, "Batt: %.2fV", s.battery);
      row(2, "Charge: %d%%", s.batteryPercent);
    }
    row(3, "%s", gps(s));
    row(4, "Join: %s", s.joined ? "yes" : "no");
    row(5, "BLE: %s", s.bleConnected ? "paired" : "ready");
    row(6, "Light: %s", s.light ? "on" : "off");
    row(7, "Storage: %s", s.storageOk ? "ok" : "err");
  } else if (s.page == 1) {
    row(0, "%s", gps(s));
    row(1, "Fix: %s", s.fix ? "current" : (s.fixAge >= 0 ? "stale" : "none"));
    row(2, "%s", s.fixAge >= 0 && !s.fix ? "Last lat:" : "Latitude:");
    if (s.fixAge >= 0)
      row(3, "%.6f", s.lat);
    else
      row(3, "--");
    row(4, "%s", s.fixAge >= 0 && !s.fix ? "Last lon:" : "Longitude:");
    if (s.fixAge >= 0) {
      row(5, "%.6f", s.lon);
      row(6, "Alt: %.0fm", altitude);
    } else {
      row(5, "--");
      row(6, "Alt: --");
    }
    row(7, "Sats: %u", s.satellites);
    if (s.hdop <= 0)
      row(8, "HDOP: --");
    else if (s.hdop >= 100)
      row(8, "HDOP: >99");
    else
      row(8, "HDOP: %.1f", s.hdop);
  } else {
    row(0, "Mode: %s", s.dongle ? "Dongle" : "Track");
    row(1, "Band: AU915");
    row(2, "FSB: 2");
    row(3, "Keys: %s", s.provisioned ? "set" : "none");
    row(4, "Join: %d", joinResult);
    row(5, "TX: %d", txResult);
    row(6, "Uplinks:");
    row(7, "%lu", (unsigned long)s.uplinks);
  }
}
} // namespace ScreenUi
