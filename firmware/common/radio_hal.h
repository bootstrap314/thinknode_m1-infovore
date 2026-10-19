#pragma once
#include <RadioLib.h>
#include "platform.h"

// RadioLib's generic HAL: neither OS build uses its optional wiring runtime.
class M1RadioHal final : public RadioLibHal {
public:
  M1RadioHal() : RadioLibHal(0, 1, 0, 1, 1, 0) {}
  void pinMode(uint32_t p, uint32_t m) override {
    Hw::pin(p, m ? Hw::Mode::Output : Hw::Mode::Input);
  }
  void digitalWrite(uint32_t p, uint32_t v) override { Hw::write(p, v); }
  uint32_t digitalRead(uint32_t p) override { return Hw::read(p); }
  void attachInterrupt(uint32_t p, void (*f)(), uint32_t m) override { Hw::interrupt(p, f, m); }
  void detachInterrupt(uint32_t p) override { Hw::detach(p); }
  void delay(RadioLibTime_t ms) override { Hw::sleep(ms); }
  void delayMicroseconds(RadioLibTime_t us) override { Hw::delayUs(us); }
  RadioLibTime_t millis() override { return Hw::now(); }
  RadioLibTime_t micros() override { return Hw::micros(); }
  long pulseIn(uint32_t p, uint32_t state, RadioLibTime_t timeout) override {
    uint32_t start = Hw::micros();
    while (Hw::read(p) != bool(state))
      if (uint32_t(Hw::micros() - start) >= timeout)
        return 0;
    uint32_t edge = Hw::micros();
    while (Hw::read(p) == bool(state))
      if (uint32_t(Hw::micros() - start) >= timeout)
        return 0;
    return uint32_t(Hw::micros() - edge);
  }
  void spiBegin() override {}
  void spiBeginTransaction() override {}
  void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override {
    Hw::spi(Hw::Bus::Radio, out, in, len);
  }
  void spiEndTransaction() override {}
  void spiEnd() override {}
  void yield() override { Hw::sleep(1); }
};
