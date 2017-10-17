#include <dmsdk/sdk.h>
#include "clipboard_private.h"

#if defined(DM_PLATFORM_WINDOWS)

#include <atlbase.h>
#include <atlconv.h>
#include <atlstr.h>

void clipboard_to_clipboard(const char* text) {
	// https://stackoverflow.com/a/15378096/1266551
	if (OpenClipboard(NULL)) {
		EmptyClipboard();
		size_t size = strlen(text) + 1;
		HGLOBAL hClipboardData = GlobalAlloc(NULL, size);
		if (hClipboardData) {
			CHAR* pchData = (CHAR*) GlobalLock(hClipboardData);
			if (pchData) {
				memcpy(pchData, text, size);
				GlobalUnlock(hClipboardData);
				SetClipboardData(CF_TEXT, hClipboardData);
			}
		}
		CloseClipboard();
	}
}

const char* clipboard_from_clipboard() {
	CStringA strData;

	if (OpenClipboard(NULL)) {
		HANDLE hClipboardData = GetClipboardData(CF_TEXT);
		if (hClipboardData) {
			CHAR *pchData = (CHAR*) GlobalLock(hClipboardData);
			if (pchData) {
				strData = pchData;
				GlobalUnlock(hClipboardData);
			}
		}
		CloseClipboard();
	}
	return (const char*)strData;
}

#endif
