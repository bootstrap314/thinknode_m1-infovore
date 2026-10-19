#include "credentials.h"
#include "gps_fix.h"
#include "journal.h"
#include "session_policy.h"
#include "radio_state.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
struct Data {
  uint32_t nonce, count;
  uint8_t key[16];
};
struct Flash {
  std::array<uint8_t, 8192> bytes{};
  int writes = -1;
  Flash() { bytes.fill(0xff); }
  bool read(uint32_t a, void *p, size_t n) {
    assert(a >= Journal::first && a + n <= Journal::first + 8192);
    memcpy(p, bytes.data() + a - Journal::first, n);
    return true;
  }
  bool erase(uint32_t a) {
    if (writes == 0)
      return false;
    if (writes > 0)
      --writes;
    memset(bytes.data() + a - Journal::first, 0xff, 4096);
    return true;
  }
  bool write(uint32_t a, const void *p, size_t n) {
    // Inject a power loss at every individual word in a transaction.
    assert(n % 4 == 0);
    for (size_t i = 0; i < n; i += 4) {
      if (writes == 0)
        return false;
      if (writes > 0)
        --writes;
      for (unsigned j = 0; j < 4; ++j)
        bytes[a - Journal::first + i + j] &= static_cast<const uint8_t *>(p)[i + j];
    }
    return true;
  }
};
int main() {
  Credentials::Value keys{};
  assert(Credentials::parse("0000000000000000 0102030405060708 00112233445566778899aabbccddeeff",
                            keys));
  assert(keys.devEui == 0x0102030405060708ULL && keys.appKey[15] == 0xff);
  const auto before = keys;
  for (const char *bad :
       {"", "0", "0000000000000000 0000000000000000 00112233445566778899aabbccddeeff",
        "0000000000000000 0102030405060708 00000000000000000000000000000000",
        "0000000000000000 0102030405060708 00112233445566778899aabbccddeefg"}) {
    assert(!Credentials::parse(bad, keys));
    assert(!memcmp(&keys, &before, sizeof(keys)));
  }
  Gps::Fix fix;
  char nmea[] = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
  assert(Gps::parse(nmea, fix) && fix.valid && fix.sats == 8);
  assert(std::abs(fix.lat - 48.1173) < 0.000001 && std::abs(fix.lon - 11.5166667) < 0.000001);
  char corrupt[] = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00";
  assert(!Gps::parse(corrupt, fix));
  double coord;
  assert(!Gps::coordinate("9060.0", "N", true, coord));
  assert(!Gps::coordinate("nan", "N", true, coord));
  assert(!Gps::coordinate("12345", "E", true, coord));
  assert(!Gps::coordinate("4807.0x", "N", true, coord));
  assert(!Gps::coordinate("1e99", "N", true, coord));
  Gps::Fix a{}, b{};
  b.lon = 0.001;
  assert(Gps::distance(a, a) == 0);
  assert(Gps::distance(a, b) > 111 && Gps::distance(a, b) < 112);
  SessionPolicy::Stream usb, ble;
  usb.start(2, 0xfffffff0u);
  ble.start(5, 0xfffffff0u);
  assert(!usb.due(1000, true));
  assert(usb.due(2000, true));
  assert(!ble.due(2000, true));
  assert(!usb.due(5000, false) && usb.seconds == 0);
  assert(ble.due(5000, true));
  assert(!usb.due(7000, true)); // Reconnect does not restart a subscription.
  ble.stop();
  assert(!ble.due(10000, true));
  // Golden format vector, carry propagation and exhaustion must not wrap.
  uint8_t counters[8] = {0xff, 0, 0, 0, 0x12, 0x34, 0, 0};
  assert(RadioState::reserve(counters, sizeof(counters), 0, 4, 32));
  assert(counters[0] == 0x1f && counters[1] == 1);
  assert(counters[6] == 0x35 && counters[7] == 0x0d);
  uint8_t nonce[4] = {0xfe, 0xff, 0, 0};
  assert(RadioState::reserve(nonce, sizeof(nonce), 0, 2, 1));
  assert(nonce[0] == 0xff && nonce[1] == 0xff);
  auto exhausted = std::array<uint8_t, 4>{nonce[0], nonce[1], nonce[2], nonce[3]};
  assert(!RadioState::reserve(nonce, sizeof(nonce), 0, 2, 1));
  assert(!memcmp(nonce, exhausted.data(), sizeof(nonce)));
  assert(!RadioState::reserve(nonce, sizeof(nonce), 1, 2, 1));
  Flash initial;
  Journal::Store<Data, Flash> store(initial);
  Data old{7, 32, {1}}, next{8, 64, {2}}, read{};
  assert(!store.load(read));
  assert(store.save(old));
  for (int cut = 0; cut < 15; ++cut) {
    Flash f = initial;
    Journal::Store<Data, Flash> transaction(f);
    assert(transaction.load(read));
    f.writes = cut;
    bool committed = transaction.save(next);
    Journal::Store<Data, Flash> reboot(f);
    assert(reboot.load(read));
    assert(read.nonce == (committed ? 8u : 7u));
    assert(read.count == (committed ? 64u : 32u));
  }
  for (unsigned i = 0; i < 1000; ++i) {
    next.nonce = 8 + i;
    assert(store.save(next));
    Journal::Store<Data, Flash> reboot(initial);
    assert(reboot.load(read));
    assert(read.nonce == next.nonce);
  }
  // Corrupt the newest page and recover the older, still valid transaction.
  Flash damaged;
  Journal::Store<Data, Flash> snapshots(damaged);
  assert(snapshots.save(old));
  assert(snapshots.save(next));
  damaged.bytes[4096 + sizeof(Journal::Header)] ^= 1;
  Journal::Store<Data, Flash> recover(damaged);
  assert(recover.load(read) && read.nonce == old.nonce);
  puts("PASS: provisioning validation, GPS parsing, and journal power-cut recovery");
}
