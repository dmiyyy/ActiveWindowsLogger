#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>

#include "Settings.h"
#include "WindowInfo.h"

namespace awl {

// Platform independent part of the logger: tracks how long each window was
// active and writes the result to daily CSV files:
//
//   Timestamp,Program,WindowTitle,Time
//   2024-01-31 10:00:00,"firefox","Some page - Mozilla Firefox",15000
//
// The platform layer calls update() periodically with the active window and
// suspend() while the user is idle, the screen is locked, the computer goes
// to sleep or logging is paused.
class ActivityLogger {
public:
	using Clock = std::chrono::steady_clock;

	explicit ActivityLogger(std::filesystem::path logsDir);
	~ActivityLogger();

	ActivityLogger(const ActivityLogger&) = delete;
	ActivityLogger& operator=(const ActivityLogger&) = delete;

	// Opens (creates) today's log file. Returns false on failure.
	bool open();

	void setSettings(const Settings& settings) { settings_ = settings; }
	const Settings& settings() const { return settings_; }

	// Reports the currently active window. std::nullopt - no active window.
	void update(const std::optional<WindowInfo>& window, Clock::time_point now = Clock::now());

	// Stops counting time: the current record is written, the time until the
	// next update() is not counted.
	void suspend(Clock::time_point now = Clock::now());
	bool suspended() const { return suspended_; }

	// Writes the pending record and closes the file.
	void close(Clock::time_point now = Clock::now());

	const std::filesystem::path& currentFile() const { return currentFile_; }
	const WindowInfo& currentWindow() const { return current_; }

private:
	void writeRecord(Clock::time_point end);
	bool ensureFile();
	void flushIfNeeded(Clock::time_point now, bool force = false);

	std::filesystem::path logsDir_;
	std::filesystem::path currentFile_;
	std::ofstream file_;
	Settings settings_;

	WindowInfo current_;
	bool hasCurrent_ = false;
	bool suspended_ = false;
	Clock::time_point start_;
	Clock::time_point lastFlush_;
};

} // namespace awl
