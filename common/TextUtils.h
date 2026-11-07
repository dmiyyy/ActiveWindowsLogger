#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace awl {

// Thread-safe localtime().
std::tm localTime(std::time_t t);

// Current local time formatted with strftime().
// Default format is "YYYY-MM-DD HH:MM:SS" (%F = 2001-08-23, %T = 14:55:02).
std::string timeStamp(const char* fmt = "%F %T");

std::vector<std::string> split(const std::string& s, char delim);
std::string trim(const std::string& s);
std::string toLowerAscii(std::string s);
void replaceAll(std::string& str, const std::string& from, const std::string& to);

// Wraps the value in double quotes and doubles the inner quotes (CSV escaping).
std::string csvQuote(const std::string& s);

} // namespace awl
