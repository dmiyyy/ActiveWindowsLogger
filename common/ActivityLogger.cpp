#include "ActivityLogger.h"

#include <string>
#include <system_error>

#include "TextUtils.h"

namespace fs = std::filesystem;
using namespace std::chrono;

namespace awl {

namespace {
// A long activity in the same window is split into records of this length,
// so that the data isn't lost if the program is killed.
constexpr auto kMaxRecordDuration = seconds(60);
// The file is flushed to disk at least this often.
constexpr auto kFlushInterval = seconds(5);
} // namespace

ActivityLogger::ActivityLogger(fs::path logsDir)
	: logsDir_(std::move(logsDir))
	, lastFlush_(Clock::now())
{
}

ActivityLogger::~ActivityLogger()
{
	close();
}

bool ActivityLogger::open()
{
	return ensureFile();
}

bool ActivityLogger::ensureFile()
{
	// A new file for every day.
	fs::path wanted = logsDir_ / fs::u8path(timeStamp("%F") + ".csv");

	if (wanted != currentFile_ || !file_.is_open() || !file_.good()) {
		if (file_.is_open())
			file_.close();
		file_.clear();
		currentFile_ = wanted;
		file_.open(currentFile_, std::ios::app);
		if (!file_.is_open())
			return false;

		std::error_code ec;
		auto size = fs::file_size(currentFile_, ec);
		if (!ec && size == 0) {
			const std::string& sep = settings_.separator;
			file_ << "Timestamp" << sep << "Program" << sep << "WindowTitle" << sep << "Time" << std::endl;
		}
	}
	return file_.good();
}

void ActivityLogger::writeRecord(Clock::time_point end)
{
	if (!hasCurrent_ || current_.empty())
		return;

	auto ms = duration_cast<milliseconds>(end - start_).count();
	if (ms <= 0)
		return;

	if (settings_.isFiltered(current_))
		return;

	const std::string& sep = settings_.separator;
	std::string line = timeStamp() + sep + csvQuote(current_.program) + sep + csvQuote(current_.title) + sep +
		std::to_string(ms) + "\n";

	if (!ensureFile())
		return;
	file_ << line;
	if (!file_.good()) {
		// Try to reopen the file once (e.g. it was deleted or the disk was remounted).
		file_.close();
		if (ensureFile())
			file_ << line;
	}
}

void ActivityLogger::flushIfNeeded(Clock::time_point now, bool force)
{
	if (force || now - lastFlush_ >= kFlushInterval) {
		if (file_.is_open())
			file_.flush();
		lastFlush_ = now;
	}
}

void ActivityLogger::update(const std::optional<WindowInfo>& window, Clock::time_point now)
{
	if (suspended_) {
		suspended_ = false;
		hasCurrent_ = false;
	}

	WindowInfo next = window.value_or(WindowInfo());

	if (!hasCurrent_) {
		current_ = std::move(next);
		start_ = now;
		hasCurrent_ = true;
	}
	else {
		bool changed = next.title != current_.title || next.program != current_.program;
		if (changed || now - start_ >= kMaxRecordDuration) {
			writeRecord(now);
			current_ = std::move(next);
			start_ = now;
		}
	}

	flushIfNeeded(now);
}

void ActivityLogger::suspend(Clock::time_point now)
{
	if (!suspended_) {
		writeRecord(now);
		suspended_ = true;
		hasCurrent_ = false;
		flushIfNeeded(now, true);
	}
}

void ActivityLogger::close(Clock::time_point now)
{
	if (!suspended_)
		writeRecord(now);
	hasCurrent_ = false;
	suspended_ = true;
	if (file_.is_open()) {
		file_.flush();
		file_.close();
	}
}

} // namespace awl
