#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace PhoneCommands {
// UART writes may split a command anywhere. Only an explicit delimiter commits it.
class Line {
 public:
  enum Result { None, Complete, Invalid };
  Result feed(char c) {
    if (c == '\n' || c == '\r' || c == ';') {
      const Result result = bad_ ? Invalid : (length_ ? Complete : None);
      text_[length_] = '\0';
      length_ = 0;
      bad_ = false;
      return result;
    }
    if (c < 32 || c > 126 || length_ == sizeof(text_) - 1) bad_ = true;
    if (!bad_) text_[length_++] = c;
    return None;
  }
  const char* text() const { return text_; }
  void reset() { length_ = 0; bad_ = false; text_[0] = '\0'; }
  void discard() { reset(); bad_ = true; }
 private:
  char text_[96] = {};
  size_t length_ = 0;
  bool bad_ = false;
};

inline bool number(const char* text, unsigned low, unsigned high, unsigned& value) {
  if (!*text) return false;
  unsigned result = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    const unsigned digit = unsigned(*text - '0');
    if (digit > high || result > (high - digit) / 10) return false;
    result = result * 10 + digit;
  }
  if (result < low) return false;
  value = result;
  return true;
}

inline int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

struct Packet { uint8_t port = 0; uint8_t bytes[32] = {}; size_t size = 0; };
// Port 1 is reserved for this firmware's GPS payload; 224+ are LoRaWAN reserved.
inline bool packet(const char* text, Packet& result) {
  const char* space = strchr(text, ' ');
  if (!space || space == text || space - text > 3) return false;
  char portText[4] = {};
  memcpy(portText, text, size_t(space - text));
  unsigned port;
  if (!number(portText, 2, 223, port)) return false;
  const char* data = space + 1;
  const size_t length = strlen(data);
  if (!length || length % 2 || length > sizeof(result.bytes) * 2) return false;
  Packet parsed;
  parsed.port = uint8_t(port);
  parsed.size = length / 2;
  for (size_t i = 0; i < parsed.size; ++i) {
    const int a = hex(data[i * 2]), b = hex(data[i * 2 + 1]);
    if (a < 0 || b < 0) return false;
    parsed.bytes[i] = uint8_t(a * 16 + b);
  }
  result = parsed;
  return true;
}
}
