#include "display_diff.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <random>

using Frame = std::array<uint8_t, DisplayDiff::bytes>;

static int applyChanges(const Frame &before, const Frame &after, Frame &panel) {
  panel = before;
  int16_t cursor = 0;
  DisplayDiff::Rect r;
  int count = 0;
  while (DisplayDiff::next(before.data(), after.data(), cursor, r)) {
    assert(++count <= DisplayDiff::height);
    assert(r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0);
    assert(r.x % 8 == 0 && r.w % 8 == 0);
    assert(r.x + r.w <= 200 && r.y + r.h <= 200);
    bool changed = false;
    for (int y = r.y; y < r.y + r.h; ++y) {
      for (int x = r.x / 8; x < (r.x + r.w) / 8; ++x) {
        const int i = y * DisplayDiff::stride + x;
        changed |= before[i] != after[i];
        panel[i] = after[i];
      }
    }
    assert(changed);
  }
  assert(panel == after); // Applying only these windows must reproduce ALL pixels.
  return count;
}

int main() {
  Frame a, b, panel;
  a.fill(0xff);
  b = a;
  assert(applyChanges(a, b, panel) == 0);
  b[0] = 0x7f; // Top-left pixel.
  b.back() = 0xfe; // Bottom-right pixel and byte alignment at the far edge.
  assert(applyChanges(a, b, panel) == 2);
  assert(applyChanges(b, a, panel) == 2); // Erased text must be refreshed too.
  b = a;
  for (int y = 35; y < 49; ++y) b[y * 25 + 8] = 0;
  for (int y = 135; y < 149; ++y) b[y * 25 + 17] = 0;
  assert(applyChanges(a, b, panel) == 2); // Separate fields stay separate.

  std::mt19937 rng(0x1681);
  for (int trial = 0; trial < 2000; ++trial) {
    a = b;
    const int edits = trial % 201;
    for (int n = 0; n < edits; ++n) b[rng() % b.size()] = uint8_t(rng());
    applyChanges(a, b, panel);
  }
  std::puts("PASS: unchanged, edges, erasure, isolated fields, 2000 randomized frame transitions");
}
