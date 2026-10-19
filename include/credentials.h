#pragma once
#include <cstring>
#include "phone_commands.h"
namespace Credentials {
struct Value {
  uint64_t joinEui = 0, devEui = 0;
  uint8_t appKey[16]{};
};
inline bool parse(const char *s, Value &out) {
  // provision JOIN_EUI DEV_EUI APP_KEY, canonical big-endian hex strings.
  if (std::strlen(s) != 66 || s[16] != ' ' || s[33] != ' ')
    return false;
  Value v{};
  for (unsigned i = 0; i < 16; ++i) {
    int a = PhoneCommands::hex(s[i]), b = PhoneCommands::hex(s[17 + i]);
    if (a < 0 || b < 0)
      return false;
    v.joinEui = (v.joinEui << 4) | unsigned(a);
    v.devEui = (v.devEui << 4) | unsigned(b);
  }
  bool nonzero = false;
  for (unsigned i = 0; i < 16; ++i) {
    int a = PhoneCommands::hex(s[34 + 2 * i]), b = PhoneCommands::hex(s[35 + 2 * i]);
    if (a < 0 || b < 0)
      return false;
    v.appKey[i] = uint8_t(a * 16 + b);
    nonzero |= v.appKey[i] != 0;
  }
  if (!v.devEui || !nonzero)
    return false;
  out = v;
  return true;
}
} // namespace Credentials
