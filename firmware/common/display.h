#pragma once
#include <cstdint>
#include "display_diff.h"
namespace Screen {
void init();
void clear();
void text(int x, int y, const char *value, unsigned scale = 1);
bool refresh(bool clean = false);
// Erase once, then suppress updates until explicitly woken. This is not shutdown.
bool blank();
bool isBlank();
void wake();
} // namespace Screen
