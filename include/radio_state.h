#pragma once
#include <cstddef>
#include <cstdint>

// RadioLib 7.8.1: little-endian counters, XOR of big-endian 16-bit words,
// signature stored little-endian. Keep this synchronized with the pinned version.
namespace RadioState {
inline bool reserve(uint8_t *buffer, size_t size, size_t offset, unsigned width, uint32_t advance) {
  if (size < 2 || (width != 2 && width != 4) || offset > size - 2 || width > size - 2 - offset)
    return false;
  uint32_t value = 0;
  for (unsigned i = 0; i < width; ++i)
    value |= uint32_t(buffer[offset + i]) << (8 * i);
  const uint32_t maximum = width == 2 ? 0xffffu : 0xffffffffu;
  if (advance > maximum || value > maximum - advance)
    return false;
  value += advance;
  for (unsigned i = 0; i < width; ++i)
    buffer[offset + i] = value >> (8 * i);
  uint16_t sum = 0;
  for (size_t i = 0; i < size - 2; i += 2) {
    uint16_t word = uint16_t(buffer[i]) << 8;
    if (i + 1 < size - 2)
      word |= buffer[i + 1];
    sum ^= word;
  }
  buffer[size - 2] = sum;
  buffer[size - 1] = sum >> 8;
  return true;
}
} // namespace RadioState
