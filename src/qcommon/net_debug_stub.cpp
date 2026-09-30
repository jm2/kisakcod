// net_debug_stub.cpp: headless stand-in for win32/win_net_debug.cpp.
//
// The remote debug socket is a development-time channel between the engine and
// the script debugger. The retail implementation speaks a Winsock stream to a
// listening IDE; the headless dedicated server has no debugger attached and no
// win32 source group to carry it, so every entry point is a no-op that reports
// "no debugger" (docs/design/PLATFORM_POSIX.md, NOW row 13).
//
// The stub keeps the call sites honest rather than deleting them: the engine
// polls Sys_UpdateDebugSocket and Sys_IsRemoteDebugClient from the frame loop
// and from the script VM, and those branches must keep compiling and behaving
// as "debugger absent" on every headless platform. Only the headless source
// composition links this file; Windows client and non-headless server builds
// keep the real implementation.

#include <win32/win_net_debug.h>

#include <cstring>

int g_debugClient = 0;

unsigned __int8 g_debugPacket[1][8192];

int __cdecl Sys_IsRemoteDebugClient()
{
    return 0;
}

void NET_InitDebug()
{
    g_debugClient = 0;
}

void __cdecl NET_ShutdownDebug()
{
    g_debugClient = 0;
}

void NET_RestartDebug()
{
    g_debugClient = 0;
}

void Sys_DebugSocketError(const char *message)
{
    (void)message;
}

void __cdecl Sys_Listen_f()
{
    // No listener on a headless server: the command is accepted and does
    // nothing, matching a debugger-less retail run.
}

int __cdecl Sys_UpdateDebugSocket()
{
    return 0;
}

int __cdecl Sys_ReadDebugSocketInt()
{
    return 0;
}

void __cdecl Sys_WriteDebugSocketInt(int value)
{
    (void)value;
}

void __cdecl Sys_WriteDebugSocketString(char *text)
{
    (void)text;
}

int __cdecl Sys_ReadDebugSocketMessageType(unsigned __int8 *type, int blocking)
{
    (void)blocking;
    if (type)
        *type = 0;
    return 0;
}

int __cdecl Sys_ReadDebugSocketData(char *buffer, int len, int blocking)
{
    (void)buffer;
    (void)len;
    (void)blocking;
    return 0;
}

void __cdecl Sys_ReadDebugSocketStringBuffer(char *buffer, int len)
{
    if (buffer && len > 0)
        buffer[0] = 0;
}

void __cdecl Sys_FlushDebugSocketData()
{
}

void __cdecl Sys_AckDebugSocket()
{
}

char *__cdecl Sys_ReadDebugSocketString()
{
    static char empty[] = "";
    return empty;
}

void __cdecl Sys_WriteDebugSocketData(unsigned __int8 *buffer, int len)
{
    (void)buffer;
    (void)len;
}

void __cdecl Sys_WriteDebugSocketMessageType(unsigned __int8 type)
{
    (void)type;
}

void __cdecl Sys_EndWriteDebugSocket()
{
}
