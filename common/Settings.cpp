#include "Settings.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

// Only CSimpleIniA with UTF-8 data is used: no wide-char converters needed.
#define SI_NO_CONVERSION
#include "../third_party/SimpleIni/SimpleIni.h"
#include "TextUtils.h"

namespace fs = std::filesystem;

namespace awl {

bool Settings::isFiltered(const WindowInfo& window) const
{
	if (programsFilter.empty())
		return false;
	const std::string program = toLowerAscii(window.program);
	const std::string windowClass = toLowerAscii(window.windowClass);
	for (const auto& f : programsFilter) {
		if (f == program || (!windowClass.empty() && f == windowClass))
			return true;
	}
	return false;
}

bool loadSettings(const fs::path& file, Settings& settings)
{
	settings = Settings();

	std::error_code ec;
	if (!fs::exists(file, ec))
		return false;

	// Read the file ourselves: std::ifstream understands wide paths on Windows.
	std::ifstream in(file, std::ios::binary);
	if (!in)
		return false;
	std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

	CSimpleIniA ini;
	ini.SetUnicode(true); // UTF-8
	if (ini.LoadData(data) < 0)
		return false;

	for (const auto& item : split(ini.GetValue("Filters", "ProgramsFilter", ""), ',')) {
		std::string name = toLowerAscii(trim(item));
		if (!name.empty())
			settings.programsFilter.push_back(name);
	}

	settings.stopLoggingWhenInactiveInterval =
		static_cast<int>(ini.GetLongValue("Tracking", "StopLoggingwhenInactiveInterval", 5));
	if (settings.stopLoggingWhenInactiveInterval < 0)
		settings.stopLoggingWhenInactiveInterval = 0;

	settings.separator = ini.GetValue("LogsFormat", "Separator", ",");
	if (settings.separator.empty())
		settings.separator = ",";

	return true;
}

bool writeDefaultSettings(const fs::path& file)
{
	CSimpleIniA ini;
	ini.SetUnicode(true);
	ini.SetValue("Filters", "#ProgramsFilter", "chrome,foobar2000,firefox",
		"#Comma separated string with process names that should be excluded from monitoring");
	ini.SetValue("Tracking", "StopLoggingwhenInactiveInterval", "5",
		"#Suspend monitoring after the interval of inactivity (in minutes). Default - 5 minutes, don't suspend - 0");
	ini.SetValue("LogsFormat", "Separator", ",", "#CSV columns separator");

	std::string data;
	if (ini.Save(data) < 0)
		return false;

	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << data;
	return static_cast<bool>(out);
}

} // namespace awl
