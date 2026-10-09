#pragma once

// Inert Win32 directory calls for code that maintains Windows-only on-disk
// caches (the loose-shader cache in gfx_d3d/r_material_load_obj.cpp, which a
// dxvk-native client never fills: its D3DX compiles always fail). Listings
// find nothing and nothing is deleted. Types are the Win32 layouts.

#include <cstring>

#include <d3d9.h>

struct _FILETIME
{
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};

struct _SYSTEMTIME
{
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};

struct _WIN32_FIND_DATAA
{
    DWORD dwFileAttributes;
    _FILETIME ftCreationTime;
    _FILETIME ftLastAccessTime;
    _FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    char cFileName[MAX_PATH];
    char cAlternateFileName[14];
};

inline HANDLE FindFirstFileA(const char *, _WIN32_FIND_DATAA *findData)
{
    std::memset(findData, 0, sizeof(*findData));
    return INVALID_HANDLE_VALUE;
}
inline BOOL FindNextFileA(HANDLE, _WIN32_FIND_DATAA *) { return FALSE; }
inline BOOL FindClose(HANDLE) { return TRUE; }
inline BOOL DeleteFileA(const char *) { return FALSE; }
inline BOOL RemoveDirectoryA(const char *) { return FALSE; }
inline DWORD GetLastError() { return 0; }
inline void GetSystemTime(_SYSTEMTIME *time) { std::memset(time, 0, sizeof(*time)); }
inline BOOL SystemTimeToFileTime(const _SYSTEMTIME *, _FILETIME *fileTime)
{
    std::memset(fileTime, 0, sizeof(*fileTime));
    return TRUE;
}
inline LONG CompareFileTime(const _FILETIME *, const _FILETIME *) { return 0; }
