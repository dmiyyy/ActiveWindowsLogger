#include "X11Source.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/scrnsaver.h>

#include <cstdlib>
#include <cstring>

namespace awl {

namespace {

void (*g_connectionLostHandler)() = nullptr;

// Windows may be destroyed at any moment; ignore BadWindow & co.
int ignoreXErrors(Display*, XErrorEvent*)
{
	return 0;
}

int onIOError(Display*)
{
	if (g_connectionLostHandler)
		g_connectionLostHandler();
	std::exit(0);
}

void installHandlers()
{
	static bool installed = false;
	if (!installed) {
		XSetErrorHandler(ignoreXErrors);
		XSetIOErrorHandler(onIOError);
		installed = true;
	}
}

// Reads a window property. The caller frees the result with XFree().
unsigned char* getProperty(Display* d, Window w, Atom property, Atom type, unsigned long* count, int* format = nullptr)
{
	Atom actualType = None;
	int actualFormat = 0;
	unsigned long nItems = 0, bytesAfter = 0;
	unsigned char* data = nullptr;
	if (XGetWindowProperty(d, w, property, 0, 4096, False, type, &actualType, &actualFormat, &nItems,
			&bytesAfter, &data) != Success) {
		return nullptr;
	}
	if (actualType == None || nItems == 0) {
		if (data)
			XFree(data);
		return nullptr;
	}
	if (count)
		*count = nItems;
	if (format)
		*format = actualFormat;
	return data;
}

} // namespace

void setX11ConnectionLostHandler(void (*handler)())
{
	g_connectionLostHandler = handler;
}

std::unique_ptr<X11WindowSource> X11WindowSource::create(std::string& error)
{
	installHandlers();
	Display* display = XOpenDisplay(nullptr);
	if (!display) {
		error = "cannot open X display (is DISPLAY set?)";
		return nullptr;
	}
	return std::unique_ptr<X11WindowSource>(new X11WindowSource(display));
}

X11WindowSource::X11WindowSource(Display* display)
	: display_(display)
{
	atomActiveWindow_ = XInternAtom(display_, "_NET_ACTIVE_WINDOW", False);
	atomWmName_ = XInternAtom(display_, "_NET_WM_NAME", False);
	atomUtf8String_ = XInternAtom(display_, "UTF8_STRING", False);
	atomWmPid_ = XInternAtom(display_, "_NET_WM_PID", False);
}

X11WindowSource::~X11WindowSource()
{
	XCloseDisplay(display_);
}

std::optional<WindowInfo> X11WindowSource::activeWindow()
{
	Window root = DefaultRootWindow(display_);

	unsigned long count = 0;
	unsigned char* data = getProperty(display_, root, atomActiveWindow_, XA_WINDOW, &count);
	if (!data)
		return std::nullopt;
	Window window = reinterpret_cast<unsigned long*>(data)[0];
	XFree(data);
	if (window == None)
		return std::nullopt;

	WindowInfo info;

	// Title: _NET_WM_NAME (UTF-8), fallback to WM_NAME.
	data = getProperty(display_, window, atomWmName_, atomUtf8String_, &count);
	if (data) {
		info.title.assign(reinterpret_cast<char*>(data), count);
		XFree(data);
	}
	else {
		XTextProperty prop;
		if (XGetWMName(display_, window, &prop) && prop.value) {
			char** list = nullptr;
			int n = 0;
			if (Xutf8TextPropertyToTextList(display_, &prop, &list, &n) >= Success && n > 0 && list) {
				info.title = list[0];
				XFreeStringList(list);
			}
			XFree(prop.value);
		}
	}

	// Window class: WM_CLASS = "instance\0Class\0"
	XClassHint hint{};
	if (XGetClassHint(display_, window, &hint)) {
		if (hint.res_class)
			info.windowClass = hint.res_class;
		else if (hint.res_name)
			info.windowClass = hint.res_name;
		if (hint.res_name)
			XFree(hint.res_name);
		if (hint.res_class)
			XFree(hint.res_class);
	}

	long pid = 0;
	data = getProperty(display_, window, atomWmPid_, XA_CARDINAL, &count);
	if (data) {
		pid = static_cast<long>(reinterpret_cast<unsigned long*>(data)[0]);
		XFree(data);
	}
	info.program = programNameForPid(pid, info.windowClass);

	return info;
}

std::unique_ptr<X11IdleDetector> X11IdleDetector::create(std::string& error)
{
	installHandlers();
	Display* display = XOpenDisplay(nullptr);
	if (!display) {
		error = "cannot open X display";
		return nullptr;
	}
	int eventBase = 0, errorBase = 0;
	if (!XScreenSaverQueryExtension(display, &eventBase, &errorBase)) {
		XCloseDisplay(display);
		error = "X server doesn't support the MIT-SCREEN-SAVER extension";
		return nullptr;
	}
	return std::unique_ptr<X11IdleDetector>(new X11IdleDetector(display));
}

X11IdleDetector::~X11IdleDetector()
{
	XCloseDisplay(display_);
}

bool X11IdleDetector::isIdle()
{
	if (threshold_.count() <= 0)
		return false;
	XScreenSaverInfo* info = XScreenSaverAllocInfo();
	if (!info)
		return false;
	bool idle = false;
	if (XScreenSaverQueryInfo(display_, DefaultRootWindow(display_), info))
		idle = static_cast<long long>(info->idle) >= threshold_.count();
	XFree(info);
	return idle;
}

} // namespace awl
