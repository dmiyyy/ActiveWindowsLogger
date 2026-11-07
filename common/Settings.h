#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "WindowInfo.h"

namespace awl {

// Contents of settings.ini:
//
//   [Filters]
//   ProgramsFilter = chrome,foobar2000,firefox
//   [Tracking]
//   StopLoggingwhenInactiveInterval = 5
//   [LogsFormat]
//   Separator = ,
struct Settings {
	// Program names (or window classes) excluded from logging, lower-case.
	std::vector<std::string> programsFilter;
	// Suspend logging after this many minutes of user inactivity. 0 - never.
	int stopLoggingWhenInactiveInterval = 5;
	// CSV columns separator.
	std::string separator = ",";

	bool isFiltered(const WindowInfo& window) const;
};

// Loads settings from the file. If the file doesn't exist, `settings` is
// reset to defaults and false is returned.
bool loadSettings(const std::filesystem::path& file, Settings& settings);

// Creates a settings file with default values and comments.
bool writeDefaultSettings(const std::filesystem::path& file);

} // namespace awl
