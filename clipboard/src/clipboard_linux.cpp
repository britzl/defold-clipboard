#include "clipboard_private.h"
#include <dmsdk/sdk.h>

#if defined(DM_PLATFORM_LINUX)

// Inspired by https://github.com/exebook/x11clipboard

#include <X11/Xlib.h>
#include <dmsdk/dlib/thread.h>

Display *display;
Window window;
Atom UTF8, XA_STRING = 31, selection;
dmThread::Thread loop_thread;
dmMutex::HMutex mutex;
const char *cur_text;
bool exit_flag;

void EventLoop(void *user_data) {
    XEvent event;
    while (!exit_flag) {
        if (XCheckTypedEvent(display, SelectionClear, &event)) {
            dmMutex::Lock(mutex);
            cur_text = 0;
            dmMutex::Unlock(mutex);
        }
        if (XCheckTypedEvent(display, SelectionRequest, &event)) {
            if (cur_text == 0) continue;
            int size = strlen(cur_text);
            if (event.xselectionrequest.selection != selection) continue;
            UTF8 = XInternAtom(display, "UTF8_STRING", 1);
            if (UTF8 == None) UTF8 = XA_STRING;
            Atom targets_atom = XInternAtom(display, "TARGETS", 0),
                 text_atom = XInternAtom(display, "TEXT", 0),
                 XA_ATOM = 4;
            XSelectionRequestEvent *xsr = &event.xselectionrequest;
            XSelectionEvent ev = {0};
            int R = 0;
            ev.type = SelectionNotify, ev.display = xsr->display, ev.requestor = xsr->requestor,
            ev.selection = xsr->selection, ev.time = xsr->time, ev.target = xsr->target, ev.property = xsr->property;
            if (ev.target == targets_atom)
                R = XChangeProperty(ev.display, ev.requestor, ev.property, XA_ATOM, 32,
                                    PropModeReplace, (unsigned char *)&UTF8, 1);
            else if (ev.target == XA_STRING || ev.target == text_atom)
                R = XChangeProperty(ev.display, ev.requestor, ev.property, XA_STRING, 8, PropModeReplace, (unsigned char *)cur_text, size);
            else if (ev.target == UTF8)
                R = XChangeProperty(ev.display, ev.requestor, ev.property, UTF8, 8, PropModeReplace, (unsigned char *)cur_text, size);
            else
                ev.property = None;
            if ((R & 2) == 0)
                XSendEvent(display, ev.requestor, 0, 0, (XEvent *)&ev);
        }
    }
}

void InitializeLinuxClipboard() {
    display = XOpenDisplay(0);
    int N = DefaultScreen(display);
    window = XCreateSimpleWindow(display, RootWindow(display, N), 0, 0, 1, 1, 0,
                                 BlackPixel(display, N), WhitePixel(display, N));
    exit_flag = 0;
    mutex = dmMutex::New();
    loop_thread = dmThread::New(EventLoop, 0x80000, 0, "defclip");
}

void FinalizeLinuxClipboard() {
    exit_flag = 1;
    dmThread::Join(loop_thread);
    dmMutex::Delete(mutex);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
}

void clipboard_to_clipboard(const char *text) {
    selection = XInternAtom(display, "CLIPBOARD", 0);
    XSetSelectionOwner(display, selection, window, 0);
    if (XGetSelectionOwner(display, selection) != window) return;
    dmMutex::Lock(mutex);
    cur_text = text;
    dmMutex::Unlock(mutex);
}

char *XPasteType(Atom atom) {
    XEvent event;
    int format;
    unsigned long N, size;
    char *data, *s = 0;
    Atom target,
        CLIPBOARD = XInternAtom(display, "CLIPBOARD", 0),
        XSEL_DATA = XInternAtom(display, "XSEL_DATA", 0);
    XConvertSelection(display, CLIPBOARD, atom, XSEL_DATA, window, CurrentTime);
    XSync(display, 0);
    XNextEvent(display, &event);

    switch (event.type) {
    case SelectionNotify:
        if (event.xselection.selection != CLIPBOARD) break;
        if (event.xselection.property) {
            XGetWindowProperty(event.xselection.display, event.xselection.requestor,
                               event.xselection.property, 0L, (~0L), 0, AnyPropertyType, &target,
                               &format, &size, &N, (unsigned char **)&data);
            if (target == UTF8 || target == XA_STRING) {
                s = strndup(data, size);
                XFree(data);
            }
            XDeleteProperty(event.xselection.display, event.xselection.requestor, event.xselection.property);
        }
    }
    return s;
}

const char *clipboard_from_clipboard() {
    if (cur_text != 0) {
        char *new_str = new char[strlen(cur_text)];
        strcpy(new_str, cur_text);
        return new_str;
    }
    UTF8 = XInternAtom(display, "UTF8_STRING", True);
    char *c = 0;
    if (UTF8 != None) c = XPasteType(UTF8);
    if (!c) c = XPasteType(XA_STRING);
    return c;
}

#endif