#include "screen_ui.h"
#include "display.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace {
struct Label {
  int x, y, w, h;
  std::string text;
};
std::vector<Label> labels;
bool has(const char *text) {
  for (const auto &label : labels)
    if (label.text == text)
      return true;
  return false;
}
} // namespace
namespace Screen {
void clear() {
  labels.clear();
}
void text(int x, int y, const char *value, unsigned scale) {
  assert(scale >= 2); // All on-device labels and values must be readable.
  const int w = int(strlen(value) * 8 * scale), h = int(8 * scale);
  assert(x >= 0 && y >= 0 && x + w <= 200 && y + h <= 200);
  for (const auto &other : labels)
    assert(x + w <= other.x || other.x + other.w <= x || y + h <= other.y ||
           other.y + other.h <= y);
  labels.push_back({x, y, w, h, value});
}
} // namespace Screen
int main() {
  Output::Status s{"zephyr", "dev",       true,  4.22, 100, false, -90,       -180, 255, 99.9,
                   false,    0xffffffffu, false, 0,    321, true,  "standby", -1,   0,   true};
  for (bool dongle : {false, true}) {
    s.dongle = dongle;
    for (unsigned page = 0; page < 3; ++page) {
      s.page = page;
      ScreenUi::draw(s, -32768, -32768, 32767, "");
      assert(!has("ThinkNode M1") && !has("zephyr") && !has("freertos"));
      if (page == 0)
        assert(has("Batt: 4.22V") && has("Charge: 100%"));
      if (page != 1) {
        assert(labels.front().text == (dongle ? "Mode: Dongle" : "Mode: Track"));
        assert(labels.front().y == 4 && !has("Mode:") && !has("TRACKER") && !has("USB DONGLE"));
      } else
        assert(has("Fix: none") && !has("-90.000000"));
      s.fixAge = 3600;
      ScreenUi::draw(s, 32767, 32767, -32768, "");
      if (page == 1)
        assert(has("Last lat:") && has("-180.000000"));
      s.fixAge = -1;
    }
  }
  s.rtos = "freertos";
  s.page = 0;
  s.battery = -1;
  s.storageOk = false;
  s.gpsPower = "switch_off";
  ScreenUi::draw(s, 0, 0, 0, "");
  assert(has("Storage: err") && has("Batt: --") && has("GPS: off"));
  s.page = 1;
  s.fix = true;
  s.fixAge = 0;
  s.gpsPower = "awake";
  s.lat = 90;
  s.lon = 180;
  s.hdop = 1e100;
  ScreenUi::draw(s, 32767, 0, 0, "");
  assert(has("Latitude:") && has("90.000000") && has("HDOP: >99"));
  ScreenUi::draw(s, 0, 0, 0, "123456");
  assert(has("123456") && has("Pair phone"));
  puts("PASS: all screen labels >=16px, within bounds, no overlaps; modes, stale GPS and pairing");
}
