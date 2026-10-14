#include "clipboard_private.h"
#include <dmsdk/sdk.h>

#if defined(DM_PLATFORM_LINUX)

// Inspired by https://github.com/exebook/x11clipboard

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <dmsdk/dlib/thread.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

// How long the event thread sleeps waiting for X11 traffic before re-checking
// the queue and the exit flag. Bounds paste latency for other apps and shutdown time.
static const int EVENT_POLL_TIMEOUT_MS = 50;
// How long a paste may take as a whole, large ones included, so the game never freezes longer;
// and how long an app reading a large copy from us may stall before we drop the transfer.
static const int TIMEOUT_MS = 5000;
// Large copies served to other apps at the same time.
static const int MAX_TRANSFERS = 8;

struct Buffer {
    char *data;
    size_t len;
};

// An INCR transfer of a large copy to another app, sent in chunks as the app asks for them.
struct Transfer {
    Window requestor;  // None if the slot is free
    Atom property;
    char *text;
    size_t len, offset;
    uint64_t deadline_us;
};

static Display *display;
static Window window;
static Atom CLIPBOARD, UTF8_STRING, TARGETS, MULTIPLE, XSEL_DATA, INCR, CLIPBOARD_MANAGER, SAVE_TARGETS;
// Largest property value written in one request; larger copies are sent with INCR.
static size_t max_chunk;
static dmThread::Thread loop_thread;
// Guards all Xlib calls on `display` (shared between the main thread and the
// event thread), `cur_text` and `transfers`.
static dmMutex::HMutex mutex;
static char *cur_text;
static Transfer transfers[MAX_TRANSFERS];
static char *last_pasted;
static volatile bool exit_flag;
static XErrorHandler prev_error_handler;

static void SetCurText(char *text) {
    free(cur_text);
    cur_text = text;
}

static uint64_t Deadline() {
    return dmTime::GetMonotonicTime() + TIMEOUT_MS * 1000;
}

// Appends `size` bytes, converting them from ISO-8859-1 (the ICCCM STRING encoding) to UTF-8 if `latin1`.
static bool Append(Buffer *b, const void *data, size_t size, bool latin1) {
    char *grown = (char *)realloc(b->data, b->len + (latin1 ? size * 2 : size) + 1);
    if (!grown) return false;
    b->data = grown;
    const unsigned char *s = (const unsigned char *)data;
    if (!latin1) {
        memcpy(b->data + b->len, s, size);
        b->len += size;
    } else {
        for (size_t i = 0; i < size; i++) {
            if (s[i] < 0x80) {
                b->data[b->len++] = s[i];
            } else {
                b->data[b->len++] = 0xC0 | s[i] >> 6;
                b->data[b->len++] = 0x80 | (s[i] & 0x3F);
            }
        }
    }
    b->data[b->len] = 0;
    return true;
}

// The error handler is process-wide, so only errors on our own connection are handled here.
static int HandleXError(Display *d, XErrorEvent *error) {
    static __thread bool forwarding;
    if (d != display) {
        // A previous handler that chains back to us would otherwise recurse forever.
        if (forwarding || !prev_error_handler) return 0;
        forwarding = true;
        int result = prev_error_handler(d, error);
        forwarding = false;
        return result;
    }
    // A requestor may destroy its window before our reply arrives (our own paste does
    // this on timeout), so BadWindow is expected. The default handler would exit().
    if (error->error_code != BadWindow)
        dmLogWarning("clipboard: X error %d (request %d)", error->error_code, error->request_code);
    return 0;
}

// Other code may have replaced our error handler since, e.g. GLFW 3.3 resets it to the default
// one. Xlib only reports errors while reading our connection, so this must run under the lock
// before any Xlib call that may read it.
static void InstallErrorHandler() {
    XErrorHandler current = XSetErrorHandler(HandleXError);
    if (current != HandleXError) prev_error_handler = current;
}

// Locks the display for Xlib calls.
struct DisplayLock {
    DisplayLock() {
        dmMutex::Lock(mutex);
        InstallErrorHandler();
    }
    ~DisplayLock() { dmMutex::Unlock(mutex); }
};

// Finds the transfer to `requestor` through `property`; (None, None) finds a free slot.
static Transfer *FindTransfer(Window requestor, Atom property) {
    for (Transfer &t : transfers)
        if (t.requestor == requestor && t.property == property) return &t;
    return 0;
}

static void EndTransfer(Transfer *t) {
    Window requestor = t->requestor;
    free(t->text);
    *t = Transfer();
    for (Transfer &other : transfers)
        if (other.requestor == requestor) return;
    XSelectInput(display, requestor, NoEventMask);
}

// Starts an INCR transfer of the clipboard. Returns false if no slot is free.
static bool StartTransfer(Window requestor, Atom property) {
    // A requestor asking again through the same property has given up on the previous transfer.
    if (Transfer *old = FindTransfer(requestor, property)) EndTransfer(old);
    Transfer *t = FindTransfer(None, None);
    if (!t || !(t->text = strdup(cur_text))) {
        dmLogWarning("clipboard: unable to send a large copy, too many are being pasted at once");
        return false;
    }
    // The requestor deleting the property asks for the next chunk, destroying its window abandons the transfer.
    XSelectInput(display, requestor, PropertyChangeMask | StructureNotifyMask);
    t->requestor = requestor, t->property = property, t->len = strlen(t->text);
    t->deadline_us = Deadline();
    long size = t->len;  // format 32 data is passed as longs
    XChangeProperty(display, requestor, property, INCR, 32, PropModeReplace, (unsigned char *)&size, 1);
    return true;
}

static void SendNextChunk(Transfer *t) {
    size_t size = t->len - t->offset < max_chunk ? t->len - t->offset : max_chunk;
    XChangeProperty(display, t->requestor, t->property, UTF8_STRING, 8, PropModeReplace,
                    (unsigned char *)t->text + t->offset, size);
    t->offset += size;
    t->deadline_us = Deadline();
    // The zero-length chunk ends the transfer.
    if (size == 0) EndTransfer(t);
}

// Puts the clipboard converted to `target` into `property` on `requestor`. Returns false if refused.
static bool ConvertClipboard(Window requestor, Atom target, Atom property) {
    if (target == TARGETS) {
        Atom targets[] = { TARGETS, MULTIPLE, UTF8_STRING };
        XChangeProperty(display, requestor, property, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)targets, sizeof(targets) / sizeof(targets[0]));
        return true;
    }
    if (target != UTF8_STRING) return false;
    size_t len = strlen(cur_text);
    if (len > max_chunk) return StartTransfer(requestor, property);
    XChangeProperty(display, requestor, property, UTF8_STRING, 8, PropModeReplace, (unsigned char *)cur_text, len);
    return true;
}

// Converts each (target, property) pair listed in `property` on `requestor`, replacing the property
// of refused pairs with None. Clipboard managers use this to save the clipboard.
static bool ConvertMultiple(Window requestor, Atom property) {
    Atom type;
    int format;
    unsigned long count, bytes_after;
    unsigned char *data = 0;
    bool ok = XGetWindowProperty(display, requestor, property, 0L, (~0L), False, AnyPropertyType, &type,
                                 &format, &count, &bytes_after, &data) == Success && format == 32;
    if (ok) {
        Atom *pairs = (Atom *)data;  // format 32 data is returned as longs
        for (unsigned long i = 0; i + 1 < count; i += 2)
            if (pairs[i] == MULTIPLE || pairs[i + 1] == None || !ConvertClipboard(requestor, pairs[i], pairs[i + 1]))
                pairs[i + 1] = None;
        XChangeProperty(display, requestor, property, type, 32, PropModeReplace, data, count);
    }
    if (data) XFree(data);
    return ok;
}

static void HandleSelectionRequest(XSelectionRequestEvent *req) {
    // Obsolete clients pass None and expect the target to be used as the property.
    Atom property = req->property != None ? req->property : req->target;
    bool ok = req->selection == CLIPBOARD && cur_text != 0 &&
              (req->target == MULTIPLE ? ConvertMultiple(req->requestor, property)
                                       : ConvertClipboard(req->requestor, req->target, property));
    // Always reply, a refusal (property None) included, so the requestor doesn't wait for a timeout.
    XSelectionEvent reply = {};
    reply.type = SelectionNotify;
    reply.requestor = req->requestor;
    reply.selection = req->selection;
    reply.target = req->target;
    reply.property = ok ? property : None;
    reply.time = req->time;
    XSendEvent(display, req->requestor, False, NoEventMask, (XEvent *)&reply);
}

// Handles any event that isn't a reply the caller is waiting for. Leftover SelectionNotify
// and PropertyNotify events belong to finished paste requests and are dropped.
static void HandleEvent(XEvent *event) {
    if (event->type == SelectionRequest) {
        HandleSelectionRequest(&event->xselectionrequest);
    } else if (event->type == SelectionClear && event->xselectionclear.selection == CLIPBOARD) {
        // The clear may predate a newer copy that made us the owner again; only drop the text if we lost it.
        if (XGetSelectionOwner(display, CLIPBOARD) != window)
            SetCurText(0);
    } else if (event->type == PropertyNotify && event->xproperty.state == PropertyDelete) {
        Transfer *t = FindTransfer(event->xproperty.window, event->xproperty.atom);
        if (t) SendNextChunk(t);
    } else if (event->type == DestroyNotify) {
        for (Transfer &t : transfers)
            if (t.requestor == event->xdestroywindow.window) EndTransfer(&t);
    }
}

// Sleeps until the X server sends something or the timeout expires. Returns true if it did.
static bool WaitForXInput(int timeout_ms) {
    struct pollfd pfd = { ConnectionNumber(display), POLLIN, 0 };
    return poll(&pfd, 1, timeout_ms) > 0;
}

static void EventLoop(void *) {
    XEvent event;
    bool input = true;
    while (!exit_flag) {
        dmMutex::Lock(mutex);
        // Only touch the process-wide error handler when there is work: X input, events the main
        // thread left in Xlib's queue (checking doesn't read the connection), or a transfer to expire.
        bool work = input || XEventsQueued(display, QueuedAlready);
        for (Transfer &t : transfers)
            work |= t.requestor != None;
        if (work) {
            InstallErrorHandler();
            while (XPending(display)) {
                XNextEvent(display, &event);
                HandleEvent(&event);
            }
            uint64_t now = dmTime::GetMonotonicTime();
            for (Transfer &t : transfers) {
                if (t.requestor != None && now >= t.deadline_us) {
                    dmLogWarning("clipboard: an app stopped reading a large copy, dropping it");
                    EndTransfer(&t);
                }
            }
            XFlush(display);
        }
        dmMutex::Unlock(mutex);
        input = WaitForXInput(EVENT_POLL_TIMEOUT_MS);
    }
}

// Waits for a SelectionNotify or a new property value (XSEL_DATA, the only property we use on
// our windows) on `requestor`, serving other events meanwhile: the event thread is locked out,
// and the request may even be our own. Returns false if none arrives before `deadline_us`.
static bool WaitForReply(Window requestor, int type, XEvent *event, uint64_t deadline_us) {
    for (;;) {
        while (XPending(display)) {
            XNextEvent(display, event);
            // xany.window is the requestor of a SelectionNotify and the window of a PropertyNotify.
            if (event->type == type && event->xany.window == requestor &&
                (type != PropertyNotify || event->xproperty.state == PropertyNewValue))
                return true;
            HandleEvent(event);
        }
        uint64_t now = dmTime::GetMonotonicTime();
        if (now >= deadline_us) {
            dmLogWarning("clipboard: no reply from the X server or the clipboard owner within %d ms", TIMEOUT_MS);
            return false;
        }
        WaitForXInput((int)((deadline_us - now + 999) / 1000));
    }
}

void clipboard_to_clipboard(const char *text) {
    if (!display) return;
    DisplayLock lock;
    XSetSelectionOwner(display, CLIPBOARD, window, CurrentTime);
    if (XGetSelectionOwner(display, CLIPBOARD) == window)
        SetCurText(strdup(text));
}

// Reads and deletes XSEL_DATA from `requestor`, appending text to `*text` as UTF-8.
// An INCR marker appends nothing. Returns the number of bytes read, or -1 on failure.
static long TakeProperty(Window requestor, Atom *type, Buffer *text) {
    int format;
    unsigned long size, bytes_after;
    unsigned char *data = 0;
    if (XGetWindowProperty(display, requestor, XSEL_DATA, 0L, (~0L), True, AnyPropertyType, type,
                           &format, &size, &bytes_after, &data) != Success)
        return -1;
    long result = -1;
    if (*type == INCR)
        result = 0;
    else if (format == 8 && (*type == UTF8_STRING || *type == XA_STRING) &&
             Append(text, data, size, *type == XA_STRING))
        result = size;
    if (data) XFree(data);
    return result;
}

// Asks the owner to put the clipboard converted to `target` into XSEL_DATA on `requestor`.
// Returns false if it refuses or doesn't reply before `deadline_us`.
static bool RequestSelection(Window requestor, Atom target, uint64_t deadline_us) {
    XEvent event;
    XConvertSelection(display, CLIPBOARD, target, XSEL_DATA, requestor, CurrentTime);
    return WaitForReply(requestor, SelectionNotify, &event, deadline_us) &&
           event.xselection.selection == CLIPBOARD && event.xselection.target == target &&
           event.xselection.property != None;
}

// Reads the owner's reply from XSEL_DATA on `requestor`, receiving INCR transfers in full.
static char *ReceiveText(Window requestor, uint64_t deadline_us) {
    Buffer text = {};
    Atom type;
    XEvent event;
    long result = TakeProperty(requestor, &type, &text);
    if (result == 0 && type == INCR) {
        // Taking the INCR marker deleted it, which tells the owner to start. Each chunk is a new
        // XSEL_DATA value, and a zero-length one ends the transfer.
        // Running out of time fails the paste rather than returning the chunks received so far.
        while ((result = WaitForReply(requestor, PropertyNotify, &event, deadline_us)
                             ? TakeProperty(requestor, &type, &text) : -1) > 0) {}
    }
    if (result < 0) {
        free(text.data);
        return 0;
    }
    return text.data;
}

// The returned string stays valid until the next call. It is empty if there is no text, as on other platforms.
const char *clipboard_from_clipboard() {
    if (!display) return "";
    free(last_pasted);
    last_pasted = 0;
    DisplayLock lock;
    // Ask the server instead of trusting cur_text alone: a SelectionClear may not have been handled yet.
    if (XGetSelectionOwner(display, CLIPBOARD) == window) {
        if (cur_text != 0) last_pasted = strdup(cur_text);
    } else {
        SetCurText(0);
        // A fresh requestor window per paste gives replies an identity: a late reply to a timed-out
        // paste is addressed to that paste's window, which is destroyed by then.
        // PropertyChangeMask is needed for INCR transfers and must be set before the request.
        Window requestor = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
        XSelectInput(display, requestor, PropertyChangeMask);
        // One deadline for the whole paste. Fall back to STRING only if the owner refused UTF8_STRING:
        // a refused request is finished, so the window can be reused without mixing up replies.
        uint64_t deadline_us = Deadline();
        if (RequestSelection(requestor, UTF8_STRING, deadline_us) ||
            (dmTime::GetMonotonicTime() < deadline_us && RequestSelection(requestor, XA_STRING, deadline_us)))
            last_pasted = ReceiveText(requestor, deadline_us);
        XDestroyWindow(display, requestor);
        XFlush(display);
    }
    return last_pasted ? last_pasted : "";
}

// Hands the copied text to the clipboard manager, if one runs, so that it stays available after
// the app exits, as on other platforms. The manager reads the text from us before replying.
static void SaveToClipboardManager() {
    if (cur_text == 0 || XGetSelectionOwner(display, CLIPBOARD) != window ||
        XGetSelectionOwner(display, CLIPBOARD_MANAGER) == None)
        return;
    XEvent event;
    XConvertSelection(display, CLIPBOARD_MANAGER, SAVE_TARGETS, None, window, CurrentTime);
    WaitForReply(window, SelectionNotify, &event, Deadline());
}

void InitializeLinuxClipboard() {
    // Called for every engine context (e.g. again after sys.reboot), but finalized only once at app exit.
    if (display) return;
    display = XOpenDisplay(0);
    if (!display) {
        dmLogWarning("clipboard: unable to open X display, clipboard is disabled");
        return;
    }
    prev_error_handler = XSetErrorHandler(HandleXError);
    const char *names[] = { "CLIPBOARD", "UTF8_STRING", "TARGETS", "MULTIPLE", "XSEL_DATA", "INCR",
                            "CLIPBOARD_MANAGER", "SAVE_TARGETS" };
    Atom atoms[sizeof(names) / sizeof(names[0])];
    XInternAtoms(display, (char **)names, sizeof(names) / sizeof(names[0]), False, atoms);
    CLIPBOARD = atoms[0], UTF8_STRING = atoms[1], TARGETS = atoms[2], MULTIPLE = atoms[3],
    XSEL_DATA = atoms[4], INCR = atoms[5], CLIPBOARD_MANAGER = atoms[6], SAVE_TARGETS = atoms[7];
    // In 4-byte units, including the ChangeProperty header (at most 28 bytes).
    long max_request = XExtendedMaxRequestSize(display);
    if (max_request == 0) max_request = XMaxRequestSize(display);
    max_chunk = max_request * 4 - 28;
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    exit_flag = 0;
    mutex = dmMutex::New();
    loop_thread = dmThread::New(EventLoop, 0x80000, 0, "defclip");
}

void FinalizeLinuxClipboard() {
    if (!display) return;
    exit_flag = 1;
    dmThread::Join(loop_thread);
    dmMutex::Delete(mutex);
    // The event thread is gone, so no lock is needed; Xlib calls still read the connection.
    InstallErrorHandler();
    SaveToClipboardManager();
    for (Transfer &t : transfers)
        if (t.requestor != None) EndTransfer(&t);
    SetCurText(0);
    free(last_pasted);
    last_pasted = 0;
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    // Restore the previous handler unless someone installed their own after us.
    XErrorHandler current = XSetErrorHandler(prev_error_handler);
    if (current != HandleXError) XSetErrorHandler(current);
    display = 0;
}

#endif
