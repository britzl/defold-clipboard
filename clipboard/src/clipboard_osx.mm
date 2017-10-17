#include <dmsdk/sdk.h>
#include "clipboard_private.h"

#if defined(DM_PLATFORM_OSX)

#include <AppKit/AppKit.h>

void clipboard_to_clipboard(const char* text) {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    NSMutableArray *array = [[NSMutableArray alloc] init];
    [array addObject:[NSString stringWithUTF8String:text]];
    [pasteboard writeObjects:array];
}

const char* clipboard_from_clipboard() {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSArray *classArray = [NSArray arrayWithObject:[NSString class]];
    NSDictionary *options = [NSDictionary dictionary];
    BOOL ok = [pasteboard canReadObjectForClasses:classArray options:options];
    if (ok) {
        NSArray *objectsToPaste = [pasteboard readObjectsForClasses:classArray options:options];
        NSString *text = [objectsToPaste objectAtIndex:0];
        return [text UTF8String];
    }
    return "";
}

#endif
