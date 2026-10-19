#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "phone_commands.h"
#include "config.h"
namespace Gps {
struct Fix {
  double lat = 0, lon = 0, alt = 0, hdop = 0;
  unsigned sats = 0;
  bool valid = false;
};
inline bool numeric(const char *s, double &value) {
  if (!*s)
    return false;
  char *end = nullptr;
  value = std::strtod(s, &end);
  return !*end && std::isfinite(value);
}
inline bool coordinate(const char *s, const char *hemi, bool latitude, double &value) {
  double raw;
  if (!numeric(s, raw) || raw < 0 || std::strlen(hemi) != 1)
    return false;
  if (raw > (latitude ? 9000 : 18000))
    return false;
  int degrees = int(raw / 100);
  double minutes = raw - degrees * 100;
  if (minutes >= 60 || degrees > (latitude ? 90 : 180))
    return false;
  if (latitude ? (*hemi != 'N' && *hemi != 'S') : (*hemi != 'E' && *hemi != 'W'))
    return false;
  value = degrees + minutes / 60;
  if (value > (latitude ? 90 : 180))
    return false;
  if (*hemi == 'S' || *hemi == 'W')
    value = -value;
  return true;
}
inline bool parse(char *sentence, Fix &fix) {
  char *star = std::strchr(sentence, '*');
  if (sentence[0] != '$' || !star || std::strlen(star + 1) != 2)
    return false;
  const int a = PhoneCommands::hex(star[1]), b = PhoneCommands::hex(star[2]);
  uint8_t sum = 0;
  for (char *p = sentence + 1; p < star; ++p)
    sum ^= uint8_t(*p);
  if (a < 0 || b < 0 || sum != a * 16 + b)
    return false;
  *star = 0;
  char *f[20]{};
  unsigned count = 0;
  f[count++] = sentence;
  for (char *p = sentence; *p; ++p)
    if (*p == ',') {
      *p = 0;
      if (count == 20)
        return false;
      f[count++] = p + 1;
    }
  if (count < 10 || (std::strcmp(f[0], "$GNGGA") && std::strcmp(f[0], "$GPGGA")))
    return false;
  fix.valid = false;
  double quality, sats, hdop, alt, lat, lon;
  if (!numeric(f[6], quality) || !numeric(f[7], sats) || !numeric(f[8], hdop) ||
      !numeric(f[9], alt))
    return true;
  if (quality < 1 || sats < 0 || sats > 255 || hdop <= 0 || alt < -32768 || alt > 32767 ||
      !coordinate(f[2], f[3], true, lat) || !coordinate(f[4], f[5], false, lon))
    return true;
  fix = {lat, lon, alt, hdop, unsigned(sats), sats >= MIN_SATS && hdop <= MIN_HDOP};
  return true;
}
inline double distance(const Fix &a, const Fix &b) {
  constexpr double radians = 3.14159265358979323846 / 180;
  double dlat = (b.lat - a.lat) * radians, dlon = (b.lon - a.lon) * radians;
  double s = std::sin(dlat / 2), t = std::sin(dlon / 2);
  double h = s * s + std::cos(a.lat * radians) * std::cos(b.lat * radians) * t * t;
  h = h < 0 ? 0 : (h > 1 ? 1 : h);
  return 6371000 * 2 * std::asin(std::sqrt(h));
}
} // namespace Gps
