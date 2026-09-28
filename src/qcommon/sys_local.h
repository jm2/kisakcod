// sys_local.h: portable engine system-layer surface (events, console, paths,
// input entry points, voice). Split out of win32/win_local.h so shared and
// headless translation units no longer pull Win32-only declarations (and
// windows.h reach through winsock/dinput) into every composition; see
// docs/design/PLATFORM_POSIX.md. Win32-only pieces (WinVars_t, MainWndProc,
// the DirectInput-era input calls, HWND/HMODULE entry points and the winsock
// includes) stay in win32/win_local.h.
#pragma once

#include <universal/q_shared.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#ifdef KISAK_MP
#include <qcommon/net_chan_mp.h>
#elif KISAK_SP
#include <qcommon/net_chan.h>
#endif

void	IN_MouseEvent (int mstate);

void	Sys_DestroyConsole( void );
void __cdecl Sys_ShowConsole();

char	*Sys_ConsoleInput (void);

void Sys_ShowIP();
bool Sys_IsLANAddress(netadr_t adr);
bool Sys_IsLANAddress_IgnoreSubnet(netadr_t adr);

struct netadr_t;
struct msg_t;

qboolean	Sys_GetPacket ( netadr_t *net_from, msg_t *net_message );
qboolean	Sys_GetBroadcastPacket( msg_t *net_message );

// Input subsystem

void	IN_Init (void);
void	IN_Shutdown (void);

// KISAKTODO void	IN_Move (usercmd_s *cmd); // usercmd_t -> usercmd_s
// add additional non keyboard / non mouse movement on top of the keyboard move cmd

void	IN_Activate (qboolean active);
void	IN_Frame (void);

bool IN_IsTalkKeyHeld();

void Conbuf_AppendText( const char *msg );
void Conbuf_AppendTextInMainThread(const char* msg);

// The original MSVC __declspec(align(8)) pins the ILP32 layout's 8-byte
// alignment. C++ forbids requesting LESS than the members' natural alignment,
// and on LP64 the long double members already force 16 bytes, so an alignas(8)
// spelling is rejected there; the pin is therefore spelled only on MSVC (where
// it is the original attribute) and every other target takes the members'
// natural (>= 8) alignment.
#if defined(_MSC_VER)
struct __declspec(align(8)) SysInfo // sizeof=0x260
#else
struct SysInfo
#endif
{                                       // ...
	long double cpuGHz;                 // ...
	long double configureGHz;           // ...
	int logicalCpuCount;                // ...
	int physicalCpuCount;               // ...
	int sysMB;                          // ...
	char gpuDescription[512];           // ...
	bool SSE;                           // ...
	char cpuVendor[13];                 // ...
	char cpuName[49];                   // ...
	// padding byte
	// padding byte
	// padding byte
	// padding byte
	// padding byte
};

#define	MAX_QUED_EVENTS		256
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

extern int client_state; // LWSS ADD. This looks similar to signonstate

// cppcheck's unusedStructMember pairs header members per translation unit,
// so it reports the five suppressed members below as unused even though
// Sys_QueEvent (win32/win_main.cpp) writes them and Com_EventLoop and
// Debug_EventLoop (qcommon/common.cpp) read them; the same blind spot is
// documented for the net_capture fixtures in .codacy.yaml. Each member keeps
// its own suppression carrying the call-site evidence, and none is removed:
// evPtrLength is additionally the retail sizeof=0x18 layout slot. The
// cross-TU record still round-trips exactly as before the header split.
struct sysEvent_t // sizeof=0x18
{                                       // ...
	// cppcheck-suppress unusedStructMember -- cross-TU use: written by Sys_QueEvent (win32/win_main.cpp), read by Com_HandleKeyEvent from Com_EventLoop/Debug_EventLoop (qcommon/common.cpp).
	int evTime;                         // ...
	sysEventType_t evType;              // ...
	// cppcheck-suppress unusedStructMember -- cross-TU use: written by Sys_QueEvent (win32/win_main.cpp), read by Com_HandleCharEvent/Com_HandleKeyEvent (qcommon/common.cpp).
	int evValue;                        // ...
	// cppcheck-suppress unusedStructMember -- cross-TU use: written by Sys_QueEvent (win32/win_main.cpp), read by Com_HandleKeyEvent (qcommon/common.cpp).
	int evValue2;                       // ...
	// cppcheck-suppress unusedStructMember -- retail layout member of the sizeof=0x18 event record; Sys_QueEvent (win32/win_main.cpp) stores the payload length.
	int evPtrLength;                    // ...
	// cppcheck-suppress unusedStructMember -- cross-TU use: payload consumed and freed by Com_EventLoop (qcommon/common.cpp) and Sys_ShutdownEvents (win32/win_main.cpp).
	void *evPtr;                        // ...
};

void Sys_SetErrorText(const char* buf);
[[noreturn]] void Sys_Error(const char *error, ...);
void __cdecl Sys_OutOfMemErrorInternal(const char *filename, int line);
void __cdecl Sys_NormalExit();

void __cdecl Sys_OpenURL(const char *url, int doexit);
void __cdecl  Sys_Quit();
void __cdecl Sys_Print(const char *msg);
char *__cdecl Sys_GetClipboardData();
int __cdecl Sys_SetClipboardData(const char *text);
void __cdecl Sys_QueEvent(uint32_t timeMs, sysEventType_t type, int value, int value2, int ptrLength, void *ptr);
void Sys_ShutdownEvents();
void __cdecl Sys_LoadingKeepAlive();
sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result);
void __cdecl Sys_Init();

void Sys_In_Restart_f();
#ifdef KISAK_MP
void Sys_Net_Restart_f();
void __cdecl Sys_Listen_f();
#endif

void __cdecl Sys_Mkdir(const char *path);
bool __cdecl Sys_RemoveDirTree(const char *path);
int __cdecl Sys_CountFileList(char **list);
char **__cdecl Sys_ListFiles(
	const char *directory,
	const char *extension,
	const char *filter,
	int *numfiles,
	int wantsubs);
char *__cdecl Sys_Cwd();
const char *__cdecl Sys_DefaultCDPath();
char *__cdecl Sys_DefaultInstallPath();
void __cdecl Sys_QuitAndStartProcess(const char *exeName, const char *parameters);


// win_voice
bool __cdecl Voice_SendVoiceData();
bool __cdecl Voice_Init();
void __cdecl Voice_Shutdown();
double __cdecl Voice_GetVoiceLevel();
void __cdecl Voice_Playback();
int __cdecl Voice_GetLocalVoiceData();
void __cdecl Voice_IncomingVoiceData(unsigned __int8 talker, unsigned __int8 *data, int packetDataSize);
bool __cdecl Voice_IsClientTalking(uint32_t clientNum);
char __cdecl Voice_StartRecording();
char __cdecl Voice_StopRecording();

extern SysInfo sys_info;
