#include "platform.h"
#include "output.h"
#include "radio_hal.h"
#include "display.h"
#include "screen_ui.h"
#include "phone_commands.h"
#include "power_policy.h"
#include "credentials.h"
#include "journal.h"
#include "gps_fix.h"
#include "config.h"
#include "session_policy.h"
#include "button_policy.h"
#include "radio_state.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>

#ifndef M1_VERSION
#define M1_VERSION "dev"
#endif
#ifndef M1_RTOS
#error Select an RTOS build
#endif
namespace App {
namespace {
M1RadioHal hal;
Module module(&hal, Hw::RadioCs, Hw::RadioIrq, Hw::RadioReset, Hw::RadioBusy);
SX1262 radio(&module);
LoRaWANNode node(&radio, &AU915, LORAWAN_SUB_BAND);
struct Flash {
  bool read(uint32_t a, void *b, size_t n) { return Hw::flashRead(a, b, n); }
  bool erase(uint32_t a) { return Hw::flashErase(a); }
  bool write(uint32_t a, const void *b, size_t n) { return Hw::flashWrite(a, b, n); }
} flash;
struct alignas(4) Saved {
  uint32_t version = 1, provisioned = 0;
  Credentials::Value keys{};
  uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE]{};
  uint8_t session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE]{};
} saved;
Journal::Store<Saved, Flash> store(flash);
Gps::Fix fix;
PowerPolicy::GpsSchedule gpsSchedule;
PowerPolicy::GpsMode gpsMode = PowerPolicy::GpsMode::Auto;
bool joined = false, radioOk = false, storageOk = true, light = false, havePosition = false,
     sentPosition = false;
uint32_t lightAt = 0, lastFix = 0, lastJoin = 0, lastTx = 0, lastLocation = 0, lastDisplay = 0;
bool txAttempted = false;
unsigned page = 0;
bool dongle = false;
SessionPolicy::Stream streams[2];
Gps::Fix sentFix;
int16_t joinResult = 0, txResult = 0;
float battery = -1;
char passkey[7]{};
PhoneCommands::Line bleLine, usbLine;

using Output::reply;
void setLight(bool on) {
  light = on;
  if (on)
    lightAt = Hw::now();
  Hw::write(Hw::Light, on);
}
const char *gpsState() {
  return !Hw::read(Hw::GpsSwitch) ? "switch_off" : (gpsSchedule.awake ? "awake" : "standby");
}
int batteryPercent() {
  return battery < 0 ? -1 : std::max(0, std::min(100, int((battery - 3.3f) * 100 / 0.9f + 0.5f)));
}
Output::Status snapshot() {
  const Output::Status value{M1_RTOS,
                             M1_VERSION,
                             dongle,
                             double(battery),
                             batteryPercent(),
                             fix.valid,
                             fix.lat,
                             fix.lon,
                             fix.sats,
                             fix.hdop,
                             joined,
                             node.getFCntUp(),
                             light,
                             page,
                             Hw::now() / 1000,
                             Hw::bleReady(),
                             gpsState(),
                             havePosition ? long(uint32_t(Hw::now() - lastFix) / 1000) : -1L,
                             unsigned(saved.provisioned),
                             storageOk};
  return value;
}
void status(Hw::Link link) {
  (void)Output::status(link, snapshot());
}
void display(bool clean = false) {
  lastDisplay = Hw::now();
  if (Screen::isBlank())
    return;
  ScreenUi::draw(snapshot(), fix.alt, joinResult, txResult, passkey);
  if (!Screen::refresh(clean))
    reply(Hw::Link::Usb, "ERR display timeout");
  lastDisplay = Hw::now();
}
void gpsPower(bool on) {
  if (gpsSchedule.awake == on)
    return;
  Hw::gps(on);
  gpsSchedule.set(on, Hw::now());
  if (!on)
    fix.valid = false;
}
void setMode(bool useDongle) {
  dongle = useDongle;
  gpsMode = dongle ? PowerPolicy::GpsMode::Off : PowerPolicy::GpsMode::Auto;
  gpsPower(!dongle && Hw::read(Hw::GpsSwitch));
  gpsSchedule.changedAt = Hw::now();
}
bool commit() {
  if (!storageOk)
    return false;
  storageOk = store.save(saved);
  if (!storageOk) {
    joined = false;
    reply(Hw::Link::Usb, "ERR persistence failed; radio disabled");
  }
  return storageOk;
}
void capture() {
  memcpy(saved.nonces, node.getBufferNonces(), sizeof(saved.nonces));
  memcpy(saved.session, node.getBufferSession(), sizeof(saved.session));
}
bool reserve(bool joining) {
  capture();
  if (joining) {
    if (!RadioState::reserve(saved.nonces, sizeof(saved.nonces), RADIOLIB_LORAWAN_NONCES_DEV_NONCE,
                             2, 1)) {
      storageOk = false;
      return false;
    }
    memset(saved.session, 0, sizeof(saved.session));
  } else {
    if (!RadioState::reserve(saved.session, sizeof(saved.session), RADIOLIB_LORAWAN_SESSION_FCNT_UP,
                             4, 32))
      return false;
  }
  return commit();
}
void join(bool force = false) {
  if (!radioOk || !saved.provisioned || !storageOk)
    return;
  lastJoin = Hw::now();
  if (force) {
    node.clearSession();
    joined = false;
  }
  if (!reserve(true))
    return;
  joinResult = node.activateOTAA();
  joined = joinResult == 0 || joinResult == RADIOLIB_LORAWAN_NEW_SESSION ||
           joinResult == RADIOLIB_LORAWAN_SESSION_RESTORED;
  if (joined) {
    capture();
    commit();
  } else {
    node.clearSession();
    node.setBufferNonces(saved.nonces);
  }
  if (joined)
    node.setDatarate(3);
  radio.sleep();
}
void configureRadio() {
  joined = false;
  if (!radioOk || !saved.provisioned)
    return;
  node.clearSession();
  int16_t result =
      node.beginOTAA(saved.keys.joinEui, saved.keys.devEui, nullptr, saved.keys.appKey);
  if (result != 0) {
    joinResult = result;
    return;
  }
  node.setBufferNonces(saved.nonces);
  node.setBufferSession(saved.session);
  node.setADR(false);
  node.setDutyCycle(true);
  node.setSleepFunction([](RadioLibTime_t ms) { Hw::sleep(ms); });
  join();
}
uint32_t radioWait() {
  uint32_t elapsed = Hw::now() - lastTx;
  return std::max(txAttempted && elapsed < 60000 ? 60000 - elapsed : 0u,
                  uint32_t(node.timeUntilUplink()));
}
int16_t transmit(const uint8_t *bytes, size_t size, uint8_t port) {
  if (!storageOk || !reserve(false))
    return RADIOLIB_ERR_UNKNOWN;
  txAttempted = true;
  lastTx = Hw::now();
  int percent = batteryPercent();
  node.setDeviceStatus(percent < 0 ? 255 : 1 + percent * 253 / 100);
  uint8_t down[255];
  size_t length = sizeof(down);
  LoRaWANEvent_t up{}, dn{};
  txResult = node.sendReceive(bytes, size, port, down, &length, false, &up, &dn);
  lastTx = Hw::now();
  // On errors the flash reservation is kept. A subsequent reservation starts
  // from the live counter; successful state is persisted after receive windows.
  if (txResult >= 0) {
    capture();
    commit();
  }
  if (txResult < 0) {
    node.clearSession();
    node.setBufferNonces(saved.nonces);
    joined = false;
  }
  if (txResult > 0 && length && length <= sizeof(down)) {
    char hex[511]{};
    const char *digits = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
      hex[2 * i] = digits[down[i] >> 4];
      hex[2 * i + 1] = digits[down[i] & 15];
    }
    reply(Hw::Link::Ble, "Downlink\nPort: %u\nBytes: %u\nHex:\n%s\n\n", unsigned(dn.fPort),
          unsigned(length), hex);
    reply(Hw::Link::Usb, "DOWNLINK port=%u bytes=%u hex=%s", unsigned(dn.fPort), unsigned(length),
          hex);
  }
  radio.sleep();
  return txResult;
}
void command(const char *text, Hw::Link link) {
  auto &stream = streams[unsigned(link)];
  if (!strcmp(text, "status"))
    status(link);
  else if (!strcmp(text, "help")) {
    if (link == Hw::Link::Ble) {
      reply(link, "Commands (end with ;)\nstatus;\nstream 5;  (2-60 seconds)\nstream off;\n"
                  "light on; / light off;\npage 0; / page 1; / page 2;\n"
                  "gps auto;\ngps on;\ngps off;\ngps now;\ndisplay clean;\n"
                  "mode tracker; / mode dongle;\nsend <port> <hex>;\n"
                  "Ports: 2-223; bytes: 1-32\npoll;\njoin;\nbootloader;");
      reply(link, "Provisioning:\nprovision <JoinEUI> <DevEUI>\n<AppKey>;\n"
                  "Send as ONE command.\nJoinEUI / DevEUI: 16 hex each\n"
                  "AppKey: 32 hex\nKeys are never echoed.");
    } else {
      reply(link, "status; stream 2..60; stream off; light on/off; page 0..2; gps auto/on/off/now; "
                  "display clean;");
      reply(link, "mode tracker/dongle;");
      reply(link, "send <port 2..223> <hex 1..32 bytes>; poll; join; bootloader;");
      reply(link,
            "provision <JoinEUI 16hex> <DevEUI 16hex> <AppKey 32hex>; (keys are never echoed)");
    }
  } else if (!strncmp(text, "stream ", 7)) {
    unsigned seconds;
    if (!strcmp(text + 7, "off")) {
      stream.stop();
      reply(link, "OK stream off");
    } else if (PhoneCommands::number(text + 7, 2, 60, seconds)) {
      stream.start(seconds, Hw::now());
      reply(link, "OK stream every %u seconds", seconds);
      status(link);
    } else
      reply(link, "ERR use stream 2..60; or stream off;");
  } else if (!strcmp(text, "mode dongle") || !strcmp(text, "mode tracker")) {
    setMode(!strcmp(text, "mode dongle"));
    reply(link, "OK mode=%s", dongle ? "dongle" : "tracker");
    display();
  } else if (!strcmp(text, "light on") || !strcmp(text, "light off")) {
    setLight(!strcmp(text, "light on"));
    reply(link, "OK light %s", light ? "on" : "off");
  } else if (strlen(text) == 6 && !strncmp(text, "page ", 5) && text[5] >= '0' && text[5] <= '2') {
    page = text[5] - '0';
    Screen::wake();
    display();
    reply(link, "OK page changed");
  } else if (!strcmp(text, "display clean")) {
    Screen::wake();
    display(true);
    reply(link, "OK display cleaned");
  } else if (!strcmp(text, "gps auto") || !strcmp(text, "gps on") || !strcmp(text, "gps off") ||
             !strcmp(text, "gps now")) {
    gpsMode = !strcmp(text, "gps on") ? PowerPolicy::GpsMode::On
                                      : (!strcmp(text, "gps off") ? PowerPolicy::GpsMode::Off
                                                                  : PowerPolicy::GpsMode::Auto);
    bool on = gpsMode != PowerPolicy::GpsMode::Off && Hw::read(Hw::GpsSwitch);
    gpsPower(on);
    gpsSchedule.changedAt = Hw::now();
    reply(link, "OK GPS %s", gpsState());
  } else if (!strncmp(text, "provision ", 10)) {
    Credentials::Value keys{};
    if (!Credentials::parse(text + 10, keys)) {
      reply(link, "ERR invalid credentials; see help;");
      return;
    }
    if (saved.provisioned && !memcmp(&saved.keys, &keys, sizeof(keys))) {
      reply(link, "OK credentials already stored; nonces retained");
      return;
    }
    saved = Saved{};
    saved.provisioned = 1;
    saved.keys = keys;
    if (!commit()) {
      reply(link, "ERR could not persist credentials");
      return;
    }
    configureRadio();
    reply(link, "OK credentials stored joined=%u result=%d", unsigned(joined), joinResult);
    display();
  } else if (!strcmp(text, "join")) {
    if (!radioOk || !saved.provisioned || !storageOk) {
      reply(link, "ERR join requires working radio, storage, and provisioned keys");
      return;
    }
    join(true);
    reply(link, "JOIN joined=%u result=%d", unsigned(joined), joinResult);
    display();
  } else if (!strcmp(text, "bootloader")) {
    reply(link, "OK rebooting to USB bootloader");
    Hw::sleep(150);
    Hw::rebootToBootloader();
  } else if (!strncmp(text, "send ", 5) || !strcmp(text, "poll")) {
    PhoneCommands::Packet packet;
    if (!strcmp(text, "poll")) {
      packet.port = 10;
      packet.size = 1;
      packet.bytes[0] = 0;
    } else if (!PhoneCommands::packet(text + 5, packet)) {
      reply(link, "ERR use send <port 2..223> <hex 1..32 bytes>;");
      return;
    }
    if (!joined) {
      reply(link, "ERR not joined; provision keys and connect your gateway/server");
      return;
    }
    uint32_t wait = radioWait();
    if (wait) {
      reply(link, "ERR radio busy; retry after seconds=%lu", (unsigned long)((wait + 999) / 1000));
      return;
    }
    if (packet.size > node.getMaxPayloadLen()) {
      reply(link, "ERR payload too large for current data rate");
      return;
    }
    reply(link, "TX starting; remote execution is not confirmed");
    reply(link, "TX result=%d", transmit(packet.bytes, packet.size, packet.port));
  } else
    reply(link, "ERR unknown command; try help;");
}
void inputs() {
  static bool usbWasOpen = false;
  bool usbOpen = Hw::usbReady();
  if (usbOpen != usbWasOpen) {
    usbLine.reset();
    streams[unsigned(Hw::Link::Usb)].stop();
    usbWasOpen = usbOpen;
  }
  Hw::Event e;
  for (unsigned n = 0; n < 32 && Hw::event(e); ++n) {
    auto &parser = e.link == Hw::Link::Ble ? bleLine : usbLine;
    if (e.kind == Hw::EventKind::Disconnected) {
      bleLine.reset();
      streams[unsigned(Hw::Link::Ble)].stop();
      passkey[0] = 0;
      display();
    } else if (e.kind == Hw::EventKind::Connected) {
      bleLine.reset();
      passkey[0] = 0;
      display();
    } else if (e.kind == Hw::EventKind::Passkey) {
      memcpy(passkey, e.data, 6);
      passkey[6] = 0;
      if (!Screen::isBlank())
        setLight(true);
      display();
    } else if (e.kind == Hw::EventKind::Overflow) {
      bleLine.discard();
      usbLine.discard(); // Platform overflow handling purges the shared input queue.
      reply(e.link, "ERR input overflow; resend command");
    } else if (e.kind == Hw::EventKind::Data && (e.link != Hw::Link::Usb || usbOpen))
      for (unsigned i = 0; i < e.size && i < sizeof(e.data); ++i) {
        auto result = parser.feed(e.data[i]);
        if (result == PhoneCommands::Line::Complete) {
          command(parser.text(), e.link);
          parser.reset();
        } else if (result == PhoneCommands::Line::Invalid)
          reply(e.link, "ERR invalid or oversized command");
      }
  }
}
void readGps() {
  static char sentence[160];
  static size_t count = 0;
  static bool overflow = false;
  if (!gpsSchedule.awake) {
    count = 0;
    overflow = false;
    return;
  }
  for (unsigned i = 0; i < 1024; ++i) {
    int value = Hw::gpsRead();
    if (value < 0)
      break;
    char ch = char(value);
    if (ch == '\n') {
      sentence[count] = 0;
      if (!overflow && Gps::parse(sentence, fix) && fix.valid) {
        lastFix = Hw::now();
        havePosition = true;
      }
      count = 0;
      overflow = false;
    } else if (ch != '\r') {
      if (count < sizeof(sentence) - 1)
        sentence[count++] = ch;
      else
        overflow = true;
    }
  }
  if (fix.valid && uint32_t(Hw::now() - lastFix) > 15000)
    fix.valid = false;
}
void buttons() {
  using ButtonPolicy::Gesture;
  static ButtonPolicy::Controls controls;
  const uint32_t now = Hw::now();
  switch (controls.update(!Hw::read(Hw::Button1), !Hw::read(Hw::Button2), now,
                          Screen::isBlank())) {
  case Gesture::Blank:
    setLight(false);
    if (!Screen::blank())
      reply(Hw::Link::Usb, "ERR display timeout");
    lastDisplay = Hw::now();
    break;
  case Gesture::Wake:
    Screen::wake();
    setLight(true);
    display();
    break;
  case Gesture::Light:
    setLight(!light);
    break;
  case Gesture::Mode:
    setMode(!dongle);
    page = 2; // Show the selected operating mode on the radio page.
    setLight(true);
    display();
    break;
  case Gesture::Activity:
    if (light)
      lightAt = now;
    break;
  case Gesture::Page:
    page = (page + 1) % 3;
    display();
    break;
  case Gesture::Join:
    join(true);
    display();
    break;
  case Gesture::None:
    break;
  }
}
void location() {
  uint8_t payload[13];
  auto be = [](uint8_t *p, uint32_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
      p[n - i - 1] = v >> (8 * i);
  };
  be(payload, uint32_t(int32_t(fix.lat * 1000000)), 4);
  be(payload + 4, uint32_t(int32_t(fix.lon * 1000000)), 4);
  be(payload + 8, uint16_t(int16_t(fix.alt)), 2);
  payload[10] = fix.sats;
  be(payload + 11, uint16_t(fix.hdop * 100), 2);
  if (transmit(payload, sizeof(payload), 1) >= 0) {
    lastLocation = Hw::now();
    sentPosition = true;
    sentFix = fix;
  }
}
} // namespace
[[noreturn]] void run() {
  Hw::init();
  for (unsigned p : {unsigned(Hw::Power), unsigned(Hw::RadioPower), unsigned(Hw::Light),
                     unsigned(Hw::GpsReset)}) {
    Hw::pin(p, Hw::Mode::Output);
    Hw::write(p, true);
  }
  Hw::pin(Hw::GpsSwitch, Hw::Mode::Input);
  Hw::pin(Hw::Button1, Hw::Mode::PullUp);
  Hw::pin(Hw::Button2, Hw::Mode::PullUp);
  setLight(true);
  Screen::init();
  Saved loaded{};
  if (store.load(loaded) && loaded.version == 1)
    saved = loaded;
  battery = Hw::batteryVolts();
  display();
  Hw::write(Hw::GpsReset, false);
  Hw::sleep(150);
  Hw::write(Hw::GpsReset, true);
  Hw::gps(true);
  gpsSchedule.set(true, Hw::now());
  if (!Hw::read(Hw::GpsSwitch))
    gpsPower(false);
  Hw::sleep(500);
  radioOk = radio.begin(916.8, 125.0, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 14, 8, 3.3) == 0;
  if (radioOk) {
    radio.setDio2AsRfSwitch(true);
    configureRadio();
    radio.sleep();
  }
  reply(Hw::Link::Usb, "BOOT ThinkNode-M1 %s %s AU915/FSB2", M1_RTOS, M1_VERSION);
  display();
  uint32_t batteryAt = Hw::now();
  bool previousSwitch = Hw::read(Hw::GpsSwitch);
  for (;;) {
    readGps();
    inputs();
    buttons();
    const uint32_t now = Hw::now();
    if (PowerPolicy::lightExpired(light, now, lightAt))
      setLight(false);
    if (!joined && uint32_t(now - lastJoin) >= JOIN_RETRY_SEC * 1000u)
      join();
    if (uint32_t(now - batteryAt) >= 60000) {
      batteryAt = now;
      float v = Hw::batteryVolts();
      if (v >= 2.5f && v <= 4.5f)
        battery = battery < 0 ? v : battery * 0.8f + v * 0.2f;
    }
    for (Hw::Link link : {Hw::Link::Usb, Hw::Link::Ble})
      if (streams[unsigned(link)].due(now, link == Hw::Link::Usb ? Hw::usbReady() : Hw::bleReady()))
        status(link);
    if (uint32_t(now - lastDisplay) >= 15000)
      display();
    const bool moved = sentPosition && Gps::distance(sentFix, fix) >= MOVE_THRESHOLD_M &&
                       uint32_t(now - lastLocation) >= MOVE_MIN_INTERVAL_SEC * 1000u;
    const bool due =
        !dongle && (!sentPosition ? now >= FIRST_FIX_SEND_DELAY_SEC * 1000u
                                  : moved || uint32_t(now - lastLocation) >= TX_PERIOD_SEC * 1000u);
    if (fix.valid && joined && due && !radioWait())
      location();
    const bool sw = Hw::read(Hw::GpsSwitch);
    if (sw && !previousSwitch && gpsMode != PowerPolicy::GpsMode::Off)
      gpsPower(true);
    previousSwitch = sw;
    gpsPower(gpsSchedule.wanted(Hw::now(), sw, gpsMode, fix.valid && !(joined && due)));
    Hw::sleep(20);
  }
}
} // namespace App
