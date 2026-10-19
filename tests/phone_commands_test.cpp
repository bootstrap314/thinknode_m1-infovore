#include "phone_commands.h"
#include <cassert>
#include <iostream>
#include <random>
#include <string>
using namespace PhoneCommands;
int main() {
  Line line;
  for (char c : std::string("light on")) assert(line.feed(c) == Line::None);
  assert(line.feed(';') == Line::Complete);
  assert(std::string(line.text()) == "light on");
  assert(line.feed('\r') == Line::None);
  assert(line.feed('\n') == Line::None);
  for (int i = 0; i < 140; ++i) line.feed('x');
  assert(line.feed(';') == Line::Invalid);
  line.feed('s'); line.feed('\0'); line.feed('t');
  assert(line.feed('\n') == Line::Invalid);
  line.feed('s'); line.discard(); line.feed('t');
  assert(line.feed(';') == Line::Invalid);
  line.feed('x'); line.reset();
  assert(line.feed(';') == Line::None);
  unsigned n = 999;
  assert(number("2", 2, 60, n) && n == 2);
  assert(number("60", 2, 60, n) && n == 60);
  for (auto s : {"", "-1", "1", "61", "2x", "2 ", "999999999999999999999"})
    assert(!number(s, 2, 60, n));
  Packet packet;
  assert(PhoneCommands::packet("10 00A1ff", packet));
  assert(packet.port == 10 && packet.size == 3 && packet.bytes[2] == 255);
  for (auto s : {"0 FF", "1 FF", "224 FF", "256 FF", "2 F", "2 FG", "2 ", "2 FF FF", "2  FF", "-1 FF", "999999 FF"})
    assert(!PhoneCommands::packet(s, packet));
  assert(PhoneCommands::packet(("223 " + std::string(64, 'f')).c_str(), packet));
  assert(!PhoneCommands::packet(("223 " + std::string(66, 'f')).c_str(), packet));
  // Framing fuzz: invalid/oversized input must never commit a truncated command.
  std::mt19937 random(5146);
  for (int i = 0; i < 10000; ++i) {
    line.reset();
    std::string expected;
    bool invalid = false;
    const unsigned length = random() % 160;
    for (unsigned j = 0; j < length; ++j) {
      char c = char(random() % 256);
      if (c == ';' || c == '\n' || c == '\r') c = 'a';
      invalid |= (c < 32 || c > 126 || j >= 95);
      expected += c;
      assert(line.feed(c) == Line::None);
    }
    auto result = line.feed(';');
    assert(result == (invalid ? Line::Invalid : (length ? Line::Complete : Line::None)));
    if (result == Line::Complete) assert(expected == line.text());
    Packet parsed;
    PhoneCommands::packet(expected.c_str(), parsed);
  }
  std::cout << "Phone command framing, bounds and 10000 fuzz cases passed\n";
}
