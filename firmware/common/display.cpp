// SPDX-License-Identifier: GPL-3.0-or-later
// SSD1681 command sequence adapted from GxEPD2_154_D67, Jean-Marc Zingg.
// Native SPI/GPIO implementation; see licenses/GxEPD2.txt.
#include "display.h"
#include "platform.h"
#include "font8x8.h"
#include <cstring>
#include <initializer_list>
namespace Screen {
static uint8_t frame[DisplayDiff::bytes], previous[DisplayDiff::bytes];
static bool initialized = false, powered = false, blanked = false;
static bool wait() {
  Hw::sleep(2);
  const uint32_t start = Hw::now();
  while (Hw::read(Hw::DisplayBusy)) {
    if (uint32_t(Hw::now() - start) > 5000)
      return false;
    Hw::sleep(2);
  }
  return true;
}
static void bytes(bool data, const uint8_t *p, size_t n) {
  Hw::write(Hw::DisplayDc, data);
  Hw::write(Hw::DisplayCs, false);
  Hw::spi(Hw::Bus::Display, p, nullptr, n);
  Hw::write(Hw::DisplayCs, true);
}
static void command(uint8_t c) {
  bytes(false, &c, 1);
}
static void data(uint8_t d) {
  bytes(true, &d, 1);
}
static void window(const DisplayDiff::Rect &r) {
  command(0x11);
  data(3);
  command(0x44);
  data(r.x / 8);
  data((r.x + r.w - 1) / 8);
  command(0x45);
  data(r.y);
  data(r.y >> 8);
  data(r.y + r.h - 1);
  data((r.y + r.h - 1) >> 8);
  command(0x4e);
  data(r.x / 8);
  command(0x4f);
  data(r.y);
  data(r.y >> 8);
}
static bool powerOn() {
  if (powered)
    return true;
  command(0x12);
  Hw::sleep(10);
  if (!wait())
    return false;
  command(0x01);
  data(199);
  data(0);
  data(0);
  command(0x3c);
  data(5);
  command(0x18);
  data(0x80);
  command(0x22);
  data(0xf8);
  command(0x20);
  powered = wait();
  return powered;
}
static void writeFrame(uint8_t cmd, const DisplayDiff::Rect &r) {
  window(r);
  command(cmd);
  for (int y = r.y; y < r.y + r.h; ++y)
    bytes(true, frame + y * DisplayDiff::stride + r.x / 8, r.w / 8);
}
void init() {
  Hw::pin(Hw::DisplayBusy, Hw::Mode::Input);
  for (unsigned p :
       {unsigned(Hw::DisplayCs), unsigned(Hw::DisplayDc), unsigned(Hw::DisplayReset)}) {
    Hw::pin(p, Hw::Mode::Output);
    Hw::write(p, true);
  }
  Hw::write(Hw::DisplayReset, false);
  Hw::sleep(2);
  Hw::write(Hw::DisplayReset, true);
  Hw::sleep(10);
}
void clear() {
  std::memset(frame, 0xff, sizeof(frame));
}
void text(int x, int y, const char *value, unsigned scale) {
  if (scale < 1 || scale > 3)
    return;
  for (; *value; ++value, x += 8 * scale) {
    const unsigned ch = uint8_t(*value);
    if (ch >= 128)
      continue;
    for (unsigned row = 0; row < 8; ++row)
      for (unsigned col = 0; col < 8; ++col)
        if (font8x8_basic[ch][row] & (1 << col))
          for (unsigned dy = 0; dy < scale; ++dy)
            for (unsigned dx = 0; dx < scale; ++dx) {
              int px = x + int(col * scale + dx), py = y + int(row * scale + dy);
              if (px >= 0 && px < 200 && py >= 0 && py < 200)
                frame[py * 25 + px / 8] &= uint8_t(~(0x80 >> (px % 8)));
            }
  }
}
bool refresh(bool clean) {
  if (blanked)
    return true;
  int16_t cursor = 0;
  DisplayDiff::Rect r{};
  const bool full = !initialized || clean;
  if (!full && !DisplayDiff::next(previous, frame, cursor, r))
    return true;
  if (!powerOn())
    return false;
  cursor = 0;
  if (full) {
    r = {0, 0, 200, 200};
    writeFrame(0x26, r);
    writeFrame(0x24, r);
    command(0x22);
    data(0xf4);
    command(0x20);
    if (!wait())
      return false;
    writeFrame(0x24, r);
  } else
    while (DisplayDiff::next(previous, frame, cursor, r)) {
      writeFrame(0x24, r);
      window(r);
      command(0x22);
      data(0xfc);
      command(0x20);
      if (!wait())
        return false;
      writeFrame(0x24, r); // Synchronize the controller's differential baseline.
    }
  command(0x22);
  data(0x83);
  command(0x20);
  if (!wait())
    return false;
  powered = false;
  initialized = true;
  std::memcpy(previous, frame, sizeof(frame));
  return true;
}
bool blank() {
  blanked = false; // Allow a retry if the previous erase timed out.
  clear();
  const bool ok = refresh(); // Erase only changed regions, without a full flash.
  blanked = true;
  return ok;
}
bool isBlank() {
  return blanked;
}
void wake() {
  blanked = false;
}
} // namespace Screen
