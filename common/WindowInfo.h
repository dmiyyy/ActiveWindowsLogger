#pragma once

#include <string>

namespace awl {

// Information about the currently active (focused) window.
// All strings are UTF-8 encoded.
struct WindowInfo {
	std::string title;        // window title (caption)
	std::string program;      // program name as written to the log (e.g. "firefox")
	std::string windowClass;  // optional window class (WM_CLASS / KWin resourceClass), used by the filter

	bool empty() const { return title.empty() && program.empty(); }
};

} // namespace awl
