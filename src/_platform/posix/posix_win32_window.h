#pragma once

// posix_win32_window.h: the Win32 window, monitor and message-box calls the
// D3D9 renderer makes (gfx_d3d/r_init.cpp, r_screenshot.cpp), for a POSIX
// client on dxvk-native. dxvk's SDL3 WSI encodes an HWND as an SDL_Window *
// and an HMONITOR as an SDL_DisplayID, so posix_win32_window.cpp answers these
// calls through SDL3. They are declarations here, not in-file branches, so the
// renderer's sources keep their Windows lines (__LINE__ feeds iassert).

#include <d3d9.h> // dxvk-native's windows_base.h: HWND, HMONITOR, RECT, POINT

// windows.h's STRICT handle and struct tags, as the decompiled code spells
// them. Every handle is a HANDLE (void *) in dxvk-native.
typedef void HWND__;
typedef void HMONITOR__;
typedef void HDC__;
typedef void HINSTANCE__;
typedef RECT tagRECT;
typedef POINT tagPOINT;
typedef LONG_PTR LPARAM;

struct tagMONITORINFO
{
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
};
typedef BOOL(__stdcall *MONITORENUMPROC)(HMONITOR, HDC, RECT *, LPARAM);

// Window styles R_CreateWindow names. The SDL3 client creates its window
// through CL_SdlCreateWindow and returns before it uses them.
#define WS_EX_TOPMOST 0x00000008L
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L

// The splash window is Windows-only: off Windows it never exists.
inline HWND g_splashWnd{};

HWND GetActiveWindow();
int MessageBoxA(HWND window, const char *text, const char *caption, UINT type);
HINSTANCE ShellExecuteA(HWND window, const char *op, const char *file, const char *params, const char *dir, int show);

BOOL EnumDisplayMonitors(HDC dc, const RECT *clip, MONITORENUMPROC proc, LPARAM data);
HMONITOR MonitorFromPoint(POINT pt, DWORD flags);
HMONITOR MonitorFromWindow(HWND window, DWORD flags);
BOOL GetMonitorInfoA(HMONITOR monitor, tagMONITORINFO *info);
int GetSystemMetrics(int index);
BOOL ClientToScreen(HWND window, POINT *pt);

BOOL ShowWindow(HWND window, int cmd);
BOOL DestroyWindow(HWND window);
BOOL AdjustWindowRectEx(RECT *rect, DWORD style, BOOL menu, DWORD exStyle);
HMODULE GetModuleHandleA(const char *name);
HWND CreateWindowExA(DWORD exStyle, const char *className, const char *title, DWORD style, int x, int y,
    int width, int height, HWND parent, void *menu, HINSTANCE instance, void *param);
