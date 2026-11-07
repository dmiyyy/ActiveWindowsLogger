#pragma once

#include <memory>
#include <string>

#include "Sources.h"

typedef struct _XDisplay Display;

namespace awl {

// Active window via EWMH (_NET_ACTIVE_WINDOW, _NET_WM_NAME, _NET_WM_PID).
// Works with any EWMH compliant X11 window manager (KWin, Mutter, Xfwm, ...).
class X11WindowSource : public WindowSource {
public:
	static std::unique_ptr<X11WindowSource> create(std::string& error);
	~X11WindowSource() override;

	const char* name() const override { return "X11"; }
	std::optional<WindowInfo> activeWindow() override;

private:
	explicit X11WindowSource(Display* display);
	Display* display_;
	unsigned long atomActiveWindow_, atomWmName_, atomUtf8String_, atomWmPid_;
};

// Idle time via the MIT-SCREEN-SAVER extension (keyboard and mouse).
class X11IdleDetector : public IdleDetector {
public:
	static std::unique_ptr<X11IdleDetector> create(std::string& error);
	~X11IdleDetector() override;

	const char* name() const override { return "X11 (XScreenSaver)"; }
	void setThreshold(std::chrono::milliseconds threshold) override { threshold_ = threshold; }
	bool isIdle() override;

private:
	explicit X11IdleDetector(Display* display) : display_(display) {}
	Display* display_;
	std::chrono::milliseconds threshold_{0};
};

// Called when the connection to the X server is lost (the session ended).
void setX11ConnectionLostHandler(void (*handler)());

} // namespace awl
