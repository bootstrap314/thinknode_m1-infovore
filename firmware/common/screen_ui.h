#pragma once
#include "output.h"
namespace ScreenUi {
// Draw only; the caller keeps control of partial/full refresh scheduling.
void draw(const Output::Status &status, double altitude, int joinResult, int txResult,
          const char *passkey);
} // namespace ScreenUi
