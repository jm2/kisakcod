// posix_win32_window.cpp: the Win32 window and monitor calls the D3D9
// renderer makes, answered through SDL3 for a POSIX client on dxvk-native
// (see posix_win32_window.h). An HWND is an SDL_Window * and an HMONITOR an
// SDL_DisplayID, the encoding dxvk's SDL3 WSI uses.

#include "posix_win32_window.h"

#include <cstdint>

#include <SDL3/SDL.h>

namespace
{
HMONITOR ToMonitor(SDL_DisplayID display)
{
    return reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(display));
}

SDL_DisplayID ToDisplay(HMONITOR monitor)
{
    return static_cast<SDL_DisplayID>(reinterpret_cast<uintptr_t>(monitor));
}

RECT ToRect(const SDL_Rect &r)
{
    return RECT{ r.x, r.y, r.x + r.w, r.y + r.h };
}
} // namespace

HWND GetActiveWindow()
{
    return SDL_GetKeyboardFocus();
}

int MessageBoxA(HWND window, const char *text, const char *caption, UINT type)
{
    // MB_ICONHAND (0x10) is the renderer's fatal-error box; everything else
    // is shown as a warning.
    const SDL_MessageBoxFlags flags = (type & 0x10u) ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_WARNING;
    SDL_ShowSimpleMessageBox(flags, caption, text, static_cast<SDL_Window *>(window));
    return 1; // IDOK
}

HINSTANCE ShellExecuteA(HWND, const char *, const char *, const char *, const char *, int)
{
    // The DirectX help page is a Windows document: there is nothing to open.
    return nullptr;
}

BOOL EnumDisplayMonitors(HDC dc, const RECT *, MONITORENUMPROC proc, LPARAM data)
{
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (!displays)
        return 0;
    for (int i = 0; i < count; ++i)
    {
        const SDL_DisplayID display = displays[i];
        SDL_Rect bounds{};
        SDL_GetDisplayBounds(display, &bounds);
        RECT rect = ToRect(bounds);
        if (!proc(ToMonitor(display), dc, &rect, data))
            break;
    }
    SDL_free(displays);
    return 1;
}

HMONITOR MonitorFromPoint(POINT pt, DWORD)
{
    const SDL_Point point{ pt.x, pt.y };
    return ToMonitor(SDL_GetDisplayForPoint(&point));
}

HMONITOR MonitorFromWindow(HWND window, DWORD)
{
    return ToMonitor(SDL_GetDisplayForWindow(static_cast<SDL_Window *>(window)));
}

BOOL GetMonitorInfoA(HMONITOR monitor, tagMONITORINFO *info)
{
    SDL_Rect bounds;
    SDL_Rect usable;
    const SDL_DisplayID display = ToDisplay(monitor);
    if (!SDL_GetDisplayBounds(display, &bounds))
        return 0;
    if (!SDL_GetDisplayUsableBounds(display, &usable))
        usable = bounds;
    info->rcMonitor = ToRect(bounds);
    info->rcWork = ToRect(usable);
    info->dwFlags = display == SDL_GetPrimaryDisplay() ? 1u : 0u; // MONITORINFOF_PRIMARY
    return 1;
}

int GetSystemMetrics(int index)
{
    // SM_CXSCREEN (0) and SM_CYSCREEN (1): the primary display's size.
    SDL_Rect bounds;
    if ((index != 0 && index != 1) || !SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds))
        return 0;
    return index == 0 ? bounds.w : bounds.h;
}

BOOL ClientToScreen(HWND window, POINT *pt)
{
    // SDL_GetWindowPosition is the client area's screen position.
    int x = 0;
    int y = 0;
    if (!SDL_GetWindowPosition(static_cast<SDL_Window *>(window), &x, &y))
        return 0;
    pt->x += x;
    pt->y += y;
    return 1;
}

BOOL ShowWindow(HWND window, int cmd)
{
    SDL_Window *const sdlWindow = static_cast<SDL_Window *>(window);
    return cmd == 0 ? SDL_HideWindow(sdlWindow) : SDL_ShowWindow(sdlWindow); // SW_HIDE
}

BOOL DestroyWindow(HWND window)
{
    SDL_DestroyWindow(static_cast<SDL_Window *>(window));
    return 1;
}

BOOL AdjustWindowRectEx(RECT *, DWORD, BOOL, DWORD)
{
    return 1;
}

HMODULE GetModuleHandleA(const char *)
{
    return nullptr;
}

HWND CreateWindowExA(DWORD, const char *, const char *, DWORD, int, int, int, int, HWND, void *, HINSTANCE, void *)
{
    // The SDL3 client's windows come from CL_SdlCreateWindow.
    return nullptr;
}
