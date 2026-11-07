#include "Sources.h"

#include <climits>
#include <fstream>
#include <string>
#include <unistd.h>

#include "../common/TextUtils.h"

namespace awl {

namespace {

std::string readExeName(long pid)
{
	std::string link = "/proc/" + std::to_string(pid) + "/exe";
	char buf[PATH_MAX];
	ssize_t len = readlink(link.c_str(), buf, sizeof(buf) - 1);
	if (len <= 0)
		return std::string();
	std::string path(buf, static_cast<size_t>(len));

	const std::string deleted = " (deleted)";
	if (path.size() > deleted.size() && path.compare(path.size() - deleted.size(), deleted.size(), deleted) == 0)
		path.resize(path.size() - deleted.size());

	auto slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string readComm(long pid)
{
	std::ifstream in("/proc/" + std::to_string(pid) + "/comm");
	std::string comm;
	std::getline(in, comm);
	return trim(comm);
}

bool isWrapper(const std::string& exe)
{
	static const char* prefixes[] = {
		"python", "java", "node", "electron", "bwrap", "perl", "ruby", "mono", "dotnet",
		"wine", "qemu", "bash", "sh", "ld-linux", "flatpak", "steam",
	};
	std::string lower = toLowerAscii(exe);
	for (const char* p : prefixes) {
		std::string prefix(p);
		if (lower.compare(0, prefix.size(), prefix) == 0) {
			// "sh" must match exactly, otherwise "shotwell" would be treated as a wrapper.
			if ((prefix == "sh" || prefix == "bash" || prefix == "node" || prefix == "java") && lower != prefix)
				continue;
			return true;
		}
	}
	return false;
}

} // namespace

std::string programNameForPid(long pid, const std::string& windowClass)
{
	std::string exe;
	if (pid > 0) {
		exe = readExeName(pid);
		if (exe.empty())
			exe = readComm(pid);
	}
	if (exe.empty() || (isWrapper(exe) && !windowClass.empty()))
		return windowClass;
	return exe;
}

} // namespace awl
