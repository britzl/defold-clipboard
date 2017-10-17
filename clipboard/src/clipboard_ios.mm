#include <dmsdk/sdk.h>
#include "clipboard_private.h"

#if defined(DM_PLATFORM_IOS)

#include <UIKit/UIKit.h>

void clipboard_to_clipboard(const char* text) {
	dmLogError("clipboard_to_clipboard %s", text);
	UIPasteboard *pasteboard = [UIPasteboard generalPasteboard];
	[pasteboard setPersistent:TRUE];
	[pasteboard setString:[NSString stringWithUTF8String:text]];
}

const char* clipboard_from_clipboard() {
	dmLogError("clipboard_from_clipboard");
	UIPasteboard *pasteboard = [UIPasteboard generalPasteboard];
	return [[pasteboard string] UTF8String];
}

#endif
