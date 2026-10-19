#pragma once
#include <cstddef>
#include <cstdint>

namespace Hw {
enum Pin : unsigned {
  Button1 = 42,
  Button2 = 39,
  Light = 43,
  Power = 12,
  RadioPower = 21,
  Buzzer = 6,
  Led = 13,
  Battery = 4,
  RadioCs = 24,
  RadioReset = 25,
  RadioIrq = 20,
  RadioBusy = 17,
  DisplayCs = 30,
  DisplayBusy = 3,
  DisplayDc = 28,
  DisplayReset = 2,
  GpsRx = 41,
  GpsTx = 40,
  GpsWake = 34,
  GpsReset = 37,
  GpsSwitch = 33
};
enum class Mode { Input, PullUp, Output };
enum class Bus { Radio, Display };
enum class Link : uint8_t { Usb, Ble };
enum class EventKind : uint8_t { Data, Connected, Disconnected, Passkey, Overflow };
struct Event {
  EventKind kind{};
  Link link{};
  uint16_t size = 0;
  char data[96]{};
};
void init();
uint32_t now();
uint32_t micros();
void sleep(uint32_t ms);
void delayUs(uint32_t us);
void pin(unsigned pin, Mode mode);
void write(unsigned pin, bool high);
bool read(unsigned pin);
void interrupt(unsigned pin, void (*callback)(), bool rising);
void detach(unsigned pin);
void spi(Bus bus, const uint8_t *tx, uint8_t *rx, size_t count);
void gps(bool awake);
int gpsRead();
float batteryVolts();
bool event(Event &event);
bool send(Link link, const char *data, size_t size);
bool bleReady();
bool usbReady();
bool flashRead(uint32_t address, void *bytes, size_t size);
bool flashErase(uint32_t address);
bool flashWrite(uint32_t address, const void *bytes, size_t size);
[[noreturn]] void rebootToBootloader();
[[noreturn]] void fatal(const char *reason);
} // namespace Hw

namespace App {
[[noreturn]] void run();
}
