#pragma once
#include <stdint.h>

// SSD1681 RAM windows are byte-aligned horizontally. Keep separate changed
// text lines separate, while joining tiny gaps inside a glyph into one window.
namespace DisplayDiff {
constexpr int16_t width = 200;
constexpr int16_t height = 200;
constexpr int16_t stride = width / 8;
constexpr int16_t bytes = stride * height;

struct Rect { int16_t x, y, w, h; };

inline bool rowBounds(const uint8_t *before, const uint8_t *after,
                      int16_t y, int16_t &left, int16_t &right) {
  left = stride;
  right = -1;
  for (int16_t x = 0; x < stride; ++x) {
    const int16_t i = y * stride + x;
    if (before[i] != after[i]) {
      if (x < left) left = x;
      right = x;
    }
  }
  return right >= 0;
}

inline bool next(const uint8_t *before, const uint8_t *after,
                 int16_t &cursor, Rect &rect) {
  int16_t left = stride, right = -1;
  while (cursor < height && !rowBounds(before, after, cursor, left, right)) ++cursor;
  if (cursor >= height) return false;

  const int16_t top = cursor;
  int16_t bottom = cursor;
  for (int16_t y = cursor + 1; y < height && y <= bottom + 3; ++y) {
    int16_t rowLeft, rowRight;
    if (rowBounds(before, after, y, rowLeft, rowRight)) {
      if (rowLeft < left) left = rowLeft;
      if (rowRight > right) right = rowRight;
      bottom = y;
    }
  }
  rect = {int16_t(left * 8), top, int16_t((right - left + 1) * 8),
          int16_t(bottom - top + 1)};
  cursor = bottom + 1;
  return true;
}
}
