// Link stubs for the Netchan_Process production test target (ILP32 Win32).
//
// The production reassembly tests link qcommon/net_chan_mp.cpp directly,
// the same way the engine links it. That translation unit references the
// engine's environment (console printing, dvar registration, filesystem,
// system sockets, zone memory, server profiling callbacks). None of that
// environment participates in fragment reassembly, so it is provided here
// as inert stubs: the production code under test is NOT stubbed, only its
// unused surroundings.
//
// This file is only compiled on the ILP32 Win32 leg (see tests/CMakeLists.txt);
// it includes the engine headers directly so any signature drift between the
// stubs and the real declarations fails the build instead of linking wrong.

#include <server_mp/server_mp.h>

#include <bgame/bg_local.h>
#include <qcommon/cmd.h>
#include <qcommon/com_error.h>
#include <qcommon/sys_time.h>
#include <universal/assertive.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include <universal/platform_compat.h>
#include <universal/q_shared.h>
#include <win32/win_local.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
dvar_t MakeStubDvar(const char *name)
{
    dvar_t dvar{};
    dvar.name = name;
    dvar.description = "net-chan reassembly test stub dvar";
    dvar.current.integer = 0;
    return dvar;
}

dvar_t g_stubDvar = MakeStubDvar("stub");
dvar_t g_svRunningDvar = MakeStubDvar("sv_running");
} // namespace

// Extern data referenced by the engine headers but not owned by any linked
// production translation unit.
const dvar_t *com_sv_running = &g_svRunningDvar;

// --- console / error surface -------------------------------------------------

void Com_Printf(int, const char *, ...)
{
}

void Com_DPrintf(int, const char *, ...)
{
}

void Com_PrintError(int, const char *, ...)
{
}

void Com_Error(errorParm_t, const char *, ...)
{
    // The tests keep every path that can raise this inert; if production code
    // reaches it anyway, fail loudly instead of continuing in a bad state.
    std::abort();
}

int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    // Keep rendering the caller's format: this shim is production sprintf,
    // and paths such as NET_AdrToString would emit garbage diagnostics
    // otherwise. Dispositioned like script_runtime_pointer_test.cpp.
    // Flawfinder: ignore -- passthrough shim; production callers own the literal format and buffer size.
    const int written = std::vsnprintf(dest, size, fmt, args);
    va_end(args);
    return written;
}

void Com_Memcpy(void *dest, const void *src, const size_t count)
{
    // std::copy instead of memcpy (Codacy CWE-120): byte-wise identical for
    // memcpy's non-overlapping contract, without the analyzer-untrackable
    // raw length copy.
    std::copy(static_cast<const uint8_t *>(src),
              static_cast<const uint8_t *>(src) + count,
              static_cast<uint8_t *>(dest));
}

void MyAssertHandler(const char *filename, int line, int, const char *fmt, ...)
{
    // Constant format spec (Codacy CWE-134): the stub never renders the
    // variadic payload as a format string. It prints the assert site and the
    // expression text as data, then aborts -- the loud failure path the
    // production assert macros expect.
    std::fprintf(stderr, "assert failed at %s:%d: %s\n",
                 filename != nullptr ? filename : "(unknown)", line,
                 fmt != nullptr ? fmt : "");
    std::abort();
}

// --- string helpers (unexercised by reassembly; kept honest) ------------------

uint8_t I_CleanChar(uint8_t character)
{
    return character;
}

void I_strncpyz(char *dest, const char *src, int destsize)
{
    if (dest == nullptr || src == nullptr || destsize <= 0)
        return;
    int i = 0;
    for (; i < destsize - 1 && src[i] != '\0'; ++i)
        dest[i] = src[i];
    dest[i] = '\0';
}

// --- dvars -------------------------------------------------------------------

const dvar_t *Dvar_RegisterBool(const char *, bool, uint16_t, const char *)
{
    return &g_stubDvar;
}

const dvar_t *Dvar_RegisterInt(const char *, int, DvarLimits, uint16_t,
                               const char *)
{
    return &g_stubDvar;
}

const dvar_t *Dvar_RegisterFloat(const char *, float, DvarLimits, uint16_t,
                                 const char *)
{
    return &g_stubDvar;
}

void Dvar_SetInt(dvar_t *dvar, int value)
{
    if (dvar != nullptr)
        dvar->current.integer = value;
}

// --- filesystem (net profile dumps only) -------------------------------------

int FS_FOpenFileWrite(const char *)
{
    return 0; // invalid handle: FS_Write below discards everything
}

uint32_t FS_Write(const char *, uint32_t len, int)
{
    return len;
}

void FS_FCloseFile(int)
{
}

// --- server profiling callbacks (net_profile == 0 keeps these unexercised) ----

void SV_Netchan_AddOOBProfilePacket(int)
{
}

void SV_Netchan_PrintProfileStats(int)
{
}

// --- system clock / sockets (fakelag and send paths stay inert) ---------------

std::uint32_t Sys_Milliseconds()
{
    return 0;
}

void Sys_Sleep(std::uint32_t)
{
}

qboolean Sys_GetPacket(netadr_t *, msg_t *)
{
    return 0;
}

char Sys_SendPacket(int, unsigned __int8 *, netadr_t)
{
    return 0;
}

qboolean Sys_StringToAdr(const char *, netadr_t *)
{
    return 0;
}

// --- zone memory (fakelag buffering only) --------------------------------------

char *Z_VirtualAlloc(int size, const char *, int)
{
    return static_cast<char *>(std::malloc(static_cast<size_t>(size)));
}

void Z_VirtualFree(void *ptr)
{
    std::free(ptr);
}

// --- command buffer (unexercised by reassembly) --------------------------------

void Cmd_AddCommandInternal(const char *, void(__cdecl *)(), cmd_function_s *)
{
}

int Cmd_Argc()
{
    return 0;
}

const char *Cmd_Argv(int)
{
    return "";
}

// --- byte swaps (the q_shared.h inline helpers call these on Win32) -------------

int ShortSwap(__int16 l)
{
    const uint16_t value = static_cast<uint16_t>(l);
    return static_cast<int>(static_cast<uint16_t>((value << 8) | (value >> 8)));
}

int LongSwap(int l)
{
    const uint32_t value = static_cast<uint32_t>(l);
    return static_cast<int>(static_cast<uint32_t>((value << 24)
        | ((value << 8) & 0x00FF0000u)
        | ((value >> 8) & 0x0000FF00u)
        | (value >> 24)));
}

// --- game type names (entity dump prints only) ----------------------------------

char *BG_GetEntityTypeName(int32_t)
{
    return const_cast<char *>("entity");
}
