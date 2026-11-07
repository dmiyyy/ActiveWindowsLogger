#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "../common/WindowInfo.h"

namespace awl {

// Provides the currently active window.
class WindowSource {
public:
	virtual ~WindowSource() = default;
	virtual const char* name() const = 0;
	virtual std::optional<WindowInfo> activeWindow() = 0;
	// Called on every tick of the main loop (reconnects, retries, ...).
	virtual void maintain() {}
};

// Tells whether the user has been inactive (no keyboard/mouse input) for at
// least the configured threshold.
class IdleDetector {
public:
	virtual ~IdleDetector() = default;
	virtual const char* name() const = 0;
	virtual void setThreshold(std::chrono::milliseconds threshold) = 0;
	virtual bool isIdle() = 0;
};

// Program name for the process: basename of /proc/<pid>/exe (or comm).
// For interpreters and wrappers (python, java, electron, bwrap, ...) the window
// class is more meaningful, so it is returned instead when available.
std::string programNameForPid(long pid, const std::string& windowClass);

} // namespace awl
