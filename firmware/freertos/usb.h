#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void usbInit(void);
bool usbReady(void);
bool usbSend(const char *, size_t);
void nativeInput(unsigned kind, unsigned link, const void *, size_t);
#ifdef __cplusplus
}
#endif
