#include "output.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Packet {
  Hw::Link link;
  std::string text;
};
std::vector<Packet> sent;
int failAfter = -1;
void reset() {
  sent.clear();
  failAfter = -1;
}
std::string captured(Hw::Link link) {
  std::string result;
  for (const auto &packet : sent) {
    assert(packet.link == link);
    result += packet.text;
  }
  return result;
}
void shortLines(const std::string &text) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find("\r\n", start);
    assert(end != std::string::npos && end - start <= 32);
    for (size_t i = start; i < end; ++i)
      assert(text[i] >= 32 && text[i] <= 126);
    start = end + 2;
  }
}
std::string withoutWhitespace(const std::string &text) {
  std::string result;
  for (char c : text)
    if (c != ' ' && c != '\r' && c != '\n')
      result += c;
  return result;
}
} // namespace
namespace Hw {
bool send(Link link, const char *data, size_t size) {
  if (failAfter == 0)
    return false;
  if (failAfter > 0)
    --failAfter;
  sent.push_back({link, std::string(data, size)});
  return true;
}
} // namespace Hw

int main() {
  const auto usb = Hw::Link::Usb, ble = Hw::Link::Ble;
  Output::Status s{"zephyr", "dev", true,  4.22, 100, false, 0,         0,  0, 0,
                   false,    0,     false, 2,    321, true,  "standby", -1, 0, true};
  // Byte-for-byte compatibility with the previously flashed USB protocol.
  assert(Output::status(usb, s));
  assert(sent.size() == 1);
  assert(captured(usb) ==
         "STATUS protocol=1 mode=dongle rtos=zephyr version=dev battery=4.22V battery_pct=100 "
         "gps_fix=0 lat=0.000000 lon=0.000000 sats=0 hdop=0.0 lora=not_joined fcnt=0 "
         "light=off page=2 uptime_s=321 ble=connected gps_power=standby fix_age_s=-1 "
         "provisioned=0 storage=ok\r\n");
  reset();
  assert(Output::status(ble, s));
  auto phone = captured(ble);
  shortLines(phone);
  for (const char *field :
       {"Battery: 4.22 V (100%)\r\n", "Mode: USB dongle\r\n", "GPS: standby\r\n",
        "LoRaWAN: not joined\r\n", "Bluetooth: connected\r\n", "Position: no fix\r\n",
        "Keys: not configured\r\n", "HDOP: unavailable\r\n"})
    assert(phone.find(field) != std::string::npos);
  assert(phone.find("STATUS protocol=") == std::string::npos);
  assert(phone.find("0.000000") == std::string::npos); // Unknown location is not a real zero fix.
  assert(phone.substr(phone.size() - 4) == "\r\n\r\n");
  reset();
  s.dongle = false;
  s.fixAge = 4294967;
  s.lat = -90;
  s.lon = -180;
  s.hdop = 1.5;
  s.satellites = 12;
  s.uplinks = 0xffffffff;
  s.uptime = 4294967;
  s.battery = -1;
  s.batteryPercent = -1;
  s.gpsPower = "switch_off";
  s.storageOk = false;
  s.provisioned = 1;
  s.joined = true;
  assert(Output::status(ble, s));
  phone = captured(ble);
  shortLines(phone);
  for (const char *field :
       {"Fix: last known\r\n", "Latitude: -90.000000\r\n", "Longitude: -180.000000\r\n",
        "Fix age: 4294967 s\r\n", "Uplinks: 4294967295\r\n", "Battery: unavailable\r\n",
        "GPS: switch off\r\n", "Storage: error\r\n"})
    assert(phone.find(field) != std::string::npos);
  reset();
  s.fix = true;
  assert(Output::status(ble, s));
  assert(captured(ble).find("Fix: current\r\n") != std::string::npos);

  for (const char *record :
       {"OK mode=dongle", "ERR not joined; provision keys and connect your gateway/server",
        "DOWNLINK port=10 bytes=3 hex=010203", "TX result=-7", "JOIN joined=0 result=-1"}) {
    reset();
    assert(Output::reply(usb, "%s", record));
    assert(sent.size() == 1 && captured(usb) == std::string(record) + "\r\n");
  }
  // Boundaries, long payloads and multiple explicit newline styles must neither
  // lose characters nor leak phone formatting into the USB stream.
  for (unsigned length = 0; length <= 700; ++length) {
    for (bool spaces : {false, true}) {
      std::string input(length, 'a');
      for (unsigned i = 0; i < length; ++i)
        input[i] = spaces && i % 11 == 10 ? ' ' : "0123456789abcdef"[i % 16];
      reset();
      assert(Output::reply(ble, "%s", input.c_str()));
      auto wrapped = captured(ble);
      shortLines(wrapped);
      assert(withoutWhitespace(wrapped) == withoutWhitespace(input.substr(0, 637)));
      reset();
      assert(Output::reply(usb, "%s", input.c_str()));
      assert(captured(usb) == input.substr(0, 637) + "\r\n");
    }
  }
  reset();
  assert(Output::reply(ble, "first\r\n\r\nsecond\nthird\rfourth"));
  assert(captured(ble) == "first\r\n\r\nsecond\r\nthird\r\nfourth\r\n");
  reset();
  const std::string word(32, 'x');
  assert(Output::reply(ble, "%s next", word.c_str()));
  assert(captured(ble) == word + "\r\nnext\r\n");
  reset();
  failAfter = 1;
  assert(!Output::status(ble, s));
  assert(sent.size() == 1); // Stop the report when disconnected; do not keep sending fragments.
  puts(
      "PASS: readable BLE lines, unavailable/stale fixes, wrapping boundaries, USB golden records");
}
