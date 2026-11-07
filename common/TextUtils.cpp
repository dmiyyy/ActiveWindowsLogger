#include "TextUtils.h"

#include <cctype>
#include <sstream>

namespace awl {

std::tm localTime(std::time_t t)
{
	std::tm bt{};
#if defined(_WIN32)
	localtime_s(&bt, &t);
#else
	localtime_r(&t, &bt);
#endif
	return bt;
}

std::string timeStamp(const char* fmt)
{
	std::tm bt = localTime(std::time(nullptr));
	char buf[128];
	size_t len = std::strftime(buf, sizeof(buf), fmt, &bt);
	return std::string(buf, len);
}

std::vector<std::string> split(const std::string& s, char delim)
{
	std::stringstream ss(s);
	std::string item;
	std::vector<std::string> elems;
	while (std::getline(ss, item, delim)) {
		elems.push_back(item);
	}
	return elems;
}

std::string trim(const std::string& s)
{
	const char* ws = " \t\r\n";
	size_t begin = s.find_first_not_of(ws);
	if (begin == std::string::npos)
		return std::string();
	size_t end = s.find_last_not_of(ws);
	return s.substr(begin, end - begin + 1);
}

std::string toLowerAscii(std::string s)
{
	for (auto& c : s) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return s;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
	if (from.empty())
		return;
	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos) {
		str.replace(pos, from.length(), to);
		pos += to.length(); // in case 'to' contains 'from'
	}
}

std::string csvQuote(const std::string& s)
{
	std::string escaped = s;
	replaceAll(escaped, "\"", "\"\"");
	return "\"" + escaped + "\"";
}

} // namespace awl
