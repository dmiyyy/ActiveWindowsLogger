#include "stdafx.h"
#include "SystemTray.h"
#include "Resource.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment (lib, "comctl32")

// GUID of the tray icon (plain constant instead of __declspec(uuid) so that MinGW can build it too).
#ifdef _DEBUG
// {C805DD46-663F-426C-8C47-95EF8AB5BFE0}
static const GUID kSystemTrayIconGuid = { 0xc805dd46, 0x663f, 0x426c, { 0x8c, 0x47, 0x95, 0xef, 0x8a, 0xb5, 0xbf, 0xe0 } };
#else
// {7E17ADC0-2E82-4491-9B9B-4E9B99CCE5B2}
static const GUID kSystemTrayIconGuid = { 0x7e17adc0, 0x2e82, 0x4491, { 0x9b, 0x9b, 0x4e, 0x9b, 0x99, 0xcc, 0xe5, 0xb2 } };
#endif

BOOL AddNotificationIcon(HWND hwnd, HINSTANCE hInstance, bool bPaused, bool bUpdate)
{
	NOTIFYICONDATA nid = { sizeof(nid) };
	nid.hWnd = hwnd;
	// add the icon, setting the icon, tooltip, and callback message.
	// the icon will be identified with the GUID
	nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP | NIF_GUID;
	nid.guidItem = kSystemTrayIconGuid;
	nid.uCallbackMessage = WMAPP_NOTIFYCALLBACK;
	LoadIconMetric(hInstance, MAKEINTRESOURCE(bPaused ? IDI_SMALL_P : IDI_SMALL), LIM_SMALL, &nid.hIcon);
	LoadString(hInstance, IDS_APP_TITLE, nid.szTip, ARRAYSIZE(nid.szTip));
	Shell_NotifyIcon(bUpdate ? NIM_MODIFY : NIM_ADD, &nid);

	// NOTIFYICON_VERSION_4 is prefered
	nid.uVersion = NOTIFYICON_VERSION_4;
	bool res = Shell_NotifyIcon(NIM_SETVERSION, &nid);

	if (!res) {
		nid.guidItem = GUID();
		res = Shell_NotifyIcon(NIM_SETVERSION, &nid);
	}
	DestroyIcon(nid.hIcon);
	return res;
}

BOOL DeleteNotificationIcon()
{
	NOTIFYICONDATA nid = { sizeof(nid) };
	nid.uFlags = NIF_GUID;
	nid.guidItem = kSystemTrayIconGuid;
	return Shell_NotifyIcon(NIM_DELETE, &nid);
}

int ShowContextMenu(HWND hwnd, HINSTANCE hInstance, POINT pt, bool bPaused)
{
	HMENU hMenu;
	int res = 0;

	if (bPaused) 
		hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDC_CONTEXTMENU_P));
	else
		hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDC_CONTEXTMENU));
	
	if (hMenu)
	{
		HMENU hSubMenu = GetSubMenu(hMenu, 0);
		if (hSubMenu)
		{
			// our window must be foreground before calling TrackPopupMenu or the menu will not disappear when the user clicks away
			SetForegroundWindow(hwnd);

			// respect menu drop alignment
			UINT uFlags = TPM_RIGHTBUTTON | TPM_RETURNCMD;
			if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
			{
				uFlags |= TPM_RIGHTALIGN;
			}
			else
			{
				uFlags |= TPM_LEFTALIGN;
			}

			res = TrackPopupMenuEx(hSubMenu, uFlags, pt.x, pt.y, hwnd, NULL);
			DestroyMenu(hSubMenu);
		}
		DestroyMenu(hMenu);
	}

	return res;
}

BOOL ShowLowInkBalloon(HINSTANCE hInstance)
{
	// Display a low ink balloon message. This is a warning, so show the appropriate system icon.
	NOTIFYICONDATA nid = { sizeof(nid) };
	nid.uFlags = NIF_INFO | NIF_GUID;
	nid.guidItem = kSystemTrayIconGuid;
	// respect quiet time since this balloon did not come from a direct user action.
	nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
	LoadString(hInstance, IDS_APP_TITLE, nid.szInfo, ARRAYSIZE(nid.szInfo));
	return Shell_NotifyIcon(NIM_MODIFY, &nid);
}

