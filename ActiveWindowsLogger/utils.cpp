#include "stdafx.h"
#include <shlobj.h>
#include "utils.h"

std::string toUtf8(const std::wstring& s)
{
	std::string utf8;
	int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.length()), NULL, 0, NULL, NULL);
	if (len > 0) {
		utf8.resize(len);
		WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.length()), &utf8[0], len, NULL, NULL);
	}
	return utf8;
}

std::filesystem::path GetAppDataFolderPath()
{
	std::filesystem::path path;
	wchar_t* wcPath = NULL;
	if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, 0, &wcPath) == S_OK) {
		path.assign(wcPath);
	}
	CoTaskMemFree(wcPath);
	return path;
}

HWND GetRealParent(HWND hWnd)
{
	HWND hParent = GetAncestor(hWnd, GA_PARENT);
	if (!hParent || hParent == GetDesktopWindow())
		return NULL;
	return hParent;
}
