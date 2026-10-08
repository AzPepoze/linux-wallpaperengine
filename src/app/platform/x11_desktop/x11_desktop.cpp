// Built as the x11 plugin; the host reaches it through x11_desktop.h.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <string>
#include <vector>

#include "shared/core/plugin.h"

namespace {
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Opens its own connection, so the window is changed without touching sokol's.
class XConnection {
   public:
    XConnection() : display_(XOpenDisplay(nullptr)) {}
    ~XConnection() {
        if (display_) XCloseDisplay(display_);
    }
    XConnection(const XConnection&) = delete;
    XConnection& operator=(const XConnection&) = delete;

    Display* get() const {
        return display_;
    }

   private:
    Display* display_;
};

// Reads the window's UTF-8 name (_NET_WM_NAME), or an empty string when it has none.
std::string windowName(Display* dpy, Window window) {
    const Atom name_atom = XInternAtom(dpy, "_NET_WM_NAME", True);
    const Atom utf8 = XInternAtom(dpy, "UTF8_STRING", True);
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, window, name_atom, 0, 1024, False, utf8, &type, &format, &count, &remaining, &data) !=
            Success ||
        !data) {
        return "";
    }
    std::string name(reinterpret_cast<char*>(data), count);
    XFree(data);
    return name;
}

Window findWindow(Display* dpy, const std::string& title) {
    Window root = DefaultRootWindow(dpy);
    Window root_return = None, parent_return = None;
    Window* children = nullptr;
    unsigned int child_count = 0;
    if (!XQueryTree(dpy, root, &root_return, &parent_return, &children, &child_count)) return None;

    Window found = None;
    for (unsigned int i = 0; i < child_count && found == None; ++i) {
        if (windowName(dpy, children[i]) == title) found = children[i];
    }
    if (children) XFree(children);
    return found;
}

// Looks for the named output, then the primary one, then the first one.
bool outputRect(Display* dpy, const std::string& output, Rect& rect) {
    int count = 0;
    XRRMonitorInfo* monitors = XRRGetMonitors(dpy, DefaultRootWindow(dpy), True, &count);
    if (!monitors || count <= 0) {
        if (monitors) XRRFreeMonitors(monitors);
        return false;
    }

    const XRRMonitorInfo* chosen = &monitors[0];
    bool named = false;
    for (int i = 0; i < count && !named; ++i) {
        char* name = XGetAtomName(dpy, monitors[i].name);
        if (name && output == name) {
            chosen = &monitors[i];
            named = true;
        }
        if (name) XFree(name);
    }
    if (!named) {
        for (int i = 0; i < count; ++i) {
            if (monitors[i].primary) {
                chosen = &monitors[i];
                break;
            }
        }
    }

    rect.x = chosen->x;
    rect.y = chosen->y;
    rect.width = chosen->width;
    rect.height = chosen->height;
    XRRFreeMonitors(monitors);
    return true;
}

// Sets an EWMH property that holds a list of atom names.
void setAtomList(Display* dpy, Window window, const char* property, const std::vector<const char*>& names) {
    std::vector<Atom> atoms;
    for (const char* name : names) atoms.push_back(XInternAtom(dpy, name, False));
    XChangeProperty(dpy, window, XInternAtom(dpy, property, False), XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(atoms.data()), (int)atoms.size());
}
}  // namespace

bool placeDesktopWindow(const std::string& title, const std::string& output) {
    XConnection connection;
    Display* dpy = connection.get();
    if (!dpy) return false;

    Rect rect;
    if (!outputRect(dpy, output, rect)) return false;
    const Window window = findWindow(dpy, title);
    if (window == None) return false;

    setAtomList(dpy, window, "_NET_WM_WINDOW_TYPE", {"_NET_WM_WINDOW_TYPE_DESKTOP"});
    setAtomList(
        dpy, window, "_NET_WM_STATE",
        {"_NET_WM_STATE_BELOW", "_NET_WM_STATE_STICKY", "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER"});
    // Remapping makes a window manager read the new type and states.
    XUnmapWindow(dpy, window);
    XMoveResizeWindow(dpy, window, rect.x, rect.y, (unsigned int)rect.width, (unsigned int)rect.height);
    XMapWindow(dpy, window);
    XFlush(dpy);
    return true;
}

// One connection kept open for the whole run, since the pointer is asked for every frame.
Display* pointerDisplay() {
    static Display* display = XOpenDisplay(nullptr);
    return display;
}

// Reports the real pointer position on `output`, as fractions of it. False when it is not on that output.
bool queryPointer(const std::string& output, float& x, float& y) {
    Display* dpy = pointerDisplay();
    if (!dpy) return false;

    Rect rect;
    if (!outputRect(dpy, output, rect) || rect.width <= 0 || rect.height <= 0) return false;

    Window root = DefaultRootWindow(dpy);
    Window root_return = None, child_return = None;
    int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(dpy, root, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask)) return false;

    x = (float)(root_x - rect.x) / (float)rect.width;
    y = (float)(root_y - rect.y) / (float)rect.height;
    return x >= 0.0f && x < 1.0f && y >= 0.0f && y < 1.0f;
}

// Plugin entry points, resolved by x11_desktop_loader.cpp in the host binary.
extern "C" {
int lwe_plugin_abi() {
    return kPluginAbi;
}

bool lwe_x11_place_desktop_window(const char* title, const char* output) {
    return placeDesktopWindow(title ? title : "", output ? output : "");
}

bool lwe_x11_query_pointer(const char* output, float* x, float* y) {
    return queryPointer(output ? output : "", *x, *y);
}
}
