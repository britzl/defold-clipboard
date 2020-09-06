#pragma once

#include <dmsdk/sdk.h>

extern void clipboard_to_clipboard(const char* text);
extern const char* clipboard_from_clipboard();

#if defined(DM_PLATFORM_LINUX)
extern void InitializeLinuxClipboard();
extern void FinalizeLinuxClipboard();
#endif
