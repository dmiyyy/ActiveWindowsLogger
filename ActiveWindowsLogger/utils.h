#pragma once

#include <filesystem>
#include <string>

#include <windows.h>

// Windows specific helpers.

std::string toUtf8(const std::wstring& s);

// %LOCALAPPDATA%
std::filesystem::path GetAppDataFolderPath();

// Same as GetParent(), but doesn't return the owner window.
HWND GetRealParent(HWND hWnd);
