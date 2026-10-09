// win_local.h: Win32-specific Quake3 header file
#pragma once // addition

#if defined (_MSC_VER) && (_MSC_VER >= 1200)
#pragma warning(disable : 4201)
#pragma warning( push )
#endif
//#include <windows.h>
//#include "../qcommon/platform.h"
#if defined (_MSC_VER) && (_MSC_VER >= 1200)
#pragma warning( pop )
#endif

// KisakCOD ABI port: the portable system-layer surface (sysEvent_t, SysInfo,
// the Sys_*/Conbuf_*/Voice_* entry points) lives in qcommon/sys_local.h so
// shared and headless TUs stop failing inside this header at 64-bit
// (docs/design/PLATFORM_POSIX.md). What remains here is Win32-only: the
// DirectInput/WinSocket headers and the Win32 engine surface (WinVars_t,
// MainWndProc, the DirectInput-era input calls, HWND/HMODULE entry points).
#include <qcommon/sys_local.h>

// DirectInput/WinSocket are Win32-only headers; the OS includes are guarded
// for the Windows target instead of being reachable from every composition.
#if defined(_WIN32) && !defined(_XBOX)
#define DIRECTINPUT_VERSION 0x0800  //[ 0x0300 | 0x0500 | 0x0700 | 0x0800 ]
#include <dinput.h>
//#include <dsound.h>
#include <winsock.h>
#include <wsipx.h>
#endif

// The Win32 engine surface below exists only on Windows; a POSIX client gets
// the portable part through qcommon/sys_local.h above.
#if defined(_WIN32)
void __cdecl Sys_CreateConsole(HMODULE hInstance);

// Input subsystem (Win32/DirectInput era)

void	IN_JoystickCommands (void);

void __cdecl IN_ShowSystemCursor(BOOL show);

void	IN_DeactivateWin32Mouse( void);

// window procedure
#ifndef _XBOX
LRESULT WINAPI MainWndProc (
    HWND    hWnd,
    UINT    uMsg,
    WPARAM  wParam,
    LPARAM  lParam);
#endif

#ifndef _XBOX
// LWSS: Accurate to cod4
typedef struct
{
	HINSTANCE		reflib_library;		// Handle to refresh DLL 
	qboolean		reflib_active;

	HWND			hWnd;
	HINSTANCE		hInstance;
	qboolean		activeApp;
	qboolean		isMinimized;
	qboolean		recenterMouse;

	OSVERSIONINFO	osversion;

	// when we get a windows message, we store the time off so keyboard processing
	// can know the exact time of an event
	unsigned		sysMsgTime;
} WinVars_t;

extern WinVars_t	g_wv;
#endif

extern HWND g_splashWnd;
#endif // _WIN32
