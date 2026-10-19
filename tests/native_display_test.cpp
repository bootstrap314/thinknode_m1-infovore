#include "display.h"
#include "platform.h"
#include <cassert>
#include <vector>
#include <cstdio>
namespace {
bool dataMode = false;
bool busy = false;
uint32_t timeMs = 0;
struct Tx {
  uint8_t command;
  std::vector<uint8_t> bytes;
};
std::vector<Tx> transfers;
} // namespace
namespace Hw {
uint32_t now() {
  return timeMs;
}
void sleep(uint32_t ms) {
  timeMs += ms;
}
void pin(unsigned, Mode) {}
void write(unsigned p, bool value) {
  if (p == DisplayDc)
    dataMode = value;
}
bool read(unsigned) {
  return busy;
}
void spi(Bus bus, const uint8_t *tx, uint8_t *, size_t n) {
  assert(bus == Bus::Display);
  if (!dataMode) {
    assert(n == 1);
    transfers.push_back({*tx, {}});
  } else
    transfers.back().bytes.insert(transfers.back().bytes.end(), tx, tx + n);
}
} // namespace Hw
unsigned updates(uint8_t mode) {
  unsigned n = 0;
  for (auto &tx : transfers)
    if (tx.command == 0x22 && tx.bytes == std::vector<uint8_t>{mode})
      ++n;
  return n;
}
int main() {
  Screen::init();
  Screen::clear();
  Screen::text(4, 20, "M1");
  assert(Screen::refresh());
  assert(updates(0xf4) == 1);
  transfers.clear();
  assert(Screen::refresh());
  assert(transfers.empty());
  Screen::clear();
  Screen::text(4, 20, "M2");
  assert(Screen::refresh());
  assert(updates(0xf4) == 0);
  assert(updates(0xfc) > 0);
  for (auto &tx : transfers) {
    if (tx.command == 0x24)
      assert(tx.bytes.size() < 5000);
    if (tx.command == 0x44)
      assert(tx.bytes[0] <= tx.bytes[1] && tx.bytes[1] < 25);
  }
  transfers.clear();
  Screen::clear();
  assert(Screen::refresh());
  assert(updates(0xfc) > 0);
  assert(!updates(0xf4));
  transfers.clear();
  assert(Screen::refresh(true));
  assert(updates(0xf4) == 1);
  Screen::text(0, 0, "Mode: Track", 2);
  Screen::text(0, 64, "Bat: 4.20V", 2);
  assert(Screen::refresh());
  transfers.clear();
  assert(Screen::blank());
  assert(Screen::isBlank());
  assert(updates(0xfc) > 0);
  assert(!updates(0xf4));
  for (const auto &tx : transfers)
    if (tx.command == 0x24)
      for (auto byte : tx.bytes)
        assert(byte == 0xff); // White pixels replace every changed text region.
  transfers.clear();
  // Periodic redraws, connection events and clean requests cannot unblank it.
  Screen::text(0, 0, "background redraw", 2);
  assert(Screen::refresh());
  assert(Screen::refresh(true));
  assert(Screen::blank());
  assert(transfers.empty());
  Screen::wake();
  assert(!Screen::isBlank());
  Screen::clear();
  Screen::text(0, 0, "Mode: Track", 2);
  assert(Screen::refresh());
  assert(updates(0xfc) > 0);
  assert(!updates(0xf4));
  transfers.clear();
  // A failed erase reports failure and stays suppressed; another hold can retry.
  busy = true;
  assert(!Screen::blank());
  assert(Screen::isBlank());
  transfers.clear();
  assert(Screen::refresh());
  assert(transfers.empty());
  busy = false;
  assert(Screen::blank());
  assert(updates(0xfc) > 0);
  puts("PASS: native SSD1681 full/partial/unchanged/erase, persistent blank, wake and timeout retry");
}
