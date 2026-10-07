// dvar_pointer_tests.cpp: 64-bit round trips through the production dvar
// system (universal/dvar.cpp), NOW row 11.
//
// The decompiled dvar code stored string pointers in DvarValue::integer and
// the enum string list in DvarLimits::integer.max. On a 64-bit target that
// truncates every pointer to 32 bits: registering "+set fs_basepath ..." or
// setting the "dedicated" enum crashed the Linux headless server at startup.
// These checks drive the real registration, set and lookup paths with strings
// that live above 4 GiB, so a truncated pointer is always a wrong pointer.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/mman.h>

#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <universal/q_parse.h>

// ---------------------------------------------------------------------------
// Engine boundary: everything dvar.cpp calls outside itself. String helpers
// keep the engine's semantics; logging, commands and parsing are inert.
// ---------------------------------------------------------------------------
char info1[1024];
char info2[8192];
const dvar_t *com_dedicated; // common.cpp's; registered below

// The failure reports print the engine's format string verbatim rather than
// formatting with it: a test stub has no reason to trust a caller's format.
void MyAssertHandler(const char *filename, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d: %s\n", filename ? filename : "?", line, fmt ? fmt : "");
    std::exit(3);
}

void __cdecl Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "unexpected Com_Error: %s\n", fmt ? fmt : "");
    std::exit(2);
}

void Com_Printf(int, const char *, ...) {}
void Com_PrintWarning(int, const char *, ...) {}
void Com_PrintError(int, const char *, ...) {}
// What the dvar callbacks handed across the boundary.
int g_printedHandle = 0;
int g_printedCount = 0;
std::string g_infoKeys;
void FS_Printf(int f, const char *, ...)
{
    g_printedHandle = f;
    ++g_printedCount;
}
bool __cdecl Com_LogFileOpen() { return false; }
void __cdecl Dvar_AddCommands() {}
void __cdecl Sys_Sleep(uint32_t) {}
void __cdecl Sys_LockRead(FastCriticalSection *) {}
void __cdecl Sys_UnlockRead(FastCriticalSection *) {}
void __cdecl Sys_LockWrite(FastCriticalSection *) {}
void __cdecl Sys_UnlockWrite(FastCriticalSection *) {}
int __cdecl Cmd_Argc() { return 0; }
const char *__cdecl Cmd_Argv(int) { return ""; }
void __cdecl Com_BeginParseSession(const char *) {}
void __cdecl Com_EndParseSession() {}
void __cdecl Com_SkipRestOfLine(const char **) {}
parseInfo_t *__cdecl Com_Parse(const char **) { static parseInfo_t empty{}; return &empty; }
parseInfo_t *__cdecl Com_ParseOnLine(const char **) { static parseInfo_t empty{}; return &empty; }
void __cdecl Info_SetValueForKey(char *, const char *key, const char *)
{
    g_infoKeys += key;
    g_infoKeys += ' ';
}
bool __cdecl Info_SetValueForKey_Big(char *, const char *, const char *) { return true; }
// Every name here is a literal, which the engine may keep by pointer.
bool __cdecl CanKeepStringPointer(const char *) { return true; }
const char *__cdecl CL_GetUsernameForLocalClient() { return "player"; }
const char *CopyString(const char *in) { return strdup(in ? in : ""); }
void __cdecl FreeString(const char *str) { std::free(const_cast<char *>(str)); }

bool __cdecl Vec4Compare(const float *a, const float *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

int Com_sprintf(char *dest, unsigned int size, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    const int n = std::vsnprintf(dest, size, fmt, va);
    va_end(va);
    return n;
}

char *__cdecl va(const char *fmt, ...)
{
    static char buffer[4096];
    va_list va_;
    va_start(va_, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, va_);
    va_end(va_);
    return buffer;
}

int I_strnicmp(const char *s0, const char *s1, int n)
{
    for (; n > 0; --n, ++s0, ++s1)
    {
        int c0 = static_cast<unsigned char>(*s0), c1 = static_cast<unsigned char>(*s1);
        if (c0 >= 'A' && c0 <= 'Z')
            c0 += 'a' - 'A';
        if (c1 >= 'A' && c1 <= 'Z')
            c1 += 'a' - 'A';
        if (c0 != c1)
            return c0 < c1 ? -1 : 1;
        if (!c0)
            return 0;
    }
    return 0;
}

int I_stricmp(const char *s0, const char *s1)
{
    return I_strnicmp(s0, s1, 0x7FFFFFFF);
}

void I_strncpyz(char *dest, const char *src, int destsize)
{
    std::snprintf(dest, static_cast<size_t>(destsize), "%s", src);
}

void I_strncat(char *dest, int size, const char *src)
{
    const size_t used = strnlen(dest, static_cast<size_t>(size));
    if (used + 1 < static_cast<size_t>(size))
        std::snprintf(dest + used, static_cast<size_t>(size) - used, "%s", src);
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------
namespace
{
int g_failures = 0;

void Check(const bool condition, const char *stage)
{
    if (!condition)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", stage);
    }
}

// A page the kernel maps high (above 4 GiB on 64-bit Linux and macOS), so
// every string placed in it has a pointer that does not survive a 32-bit
// round trip.
char *HighPage()
{
    void *const page = mmap(nullptr, 1 << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return page == MAP_FAILED ? nullptr : static_cast<char *>(page);
}

// Copies a short test string into the high page (every caller passes a
// literal well under the 64-byte slot).
char *Place(char **cursor, const char *text)
{
    constexpr size_t kSlot = 64;
    char *const at = *cursor;
    std::snprintf(at, kSlot, "%s", text);
    *cursor += kSlot;
    return at;
}
} // namespace

int main()
{
    char *const page = HighPage();
    if (!page || reinterpret_cast<uintptr_t>(page) <= 0xFFFFFFFFu)
    {
        std::fprintf(stderr, "FAIL no mapping above 4 GiB; the check would prove nothing\n");
        return 1;
    }
    char *cursor = page;
    char *const basepath = Place(&cursor, "/srv/cod4/server");
    char *const renamed = Place(&cursor, "/srv/cod4/other");

    Dvar_Init();

    // String dvar: register, read back, set, read back.
    const dvar_s *const str = Dvar_RegisterString("kisak_test_basepath", basepath, 0, "test");
    Check(str != nullptr, "string dvar registers");
    Check(str && str->current.string && std::strcmp(str->current.string, basepath) == 0,
        "string dvar current value round-trips");
    Check(str && str->reset.string && std::strcmp(str->reset.string, basepath) == 0,
        "string dvar reset value round-trips");
    Dvar_SetString(const_cast<dvar_s *>(str), renamed);
    Check(std::strcmp(Dvar_GetString("kisak_test_basepath"), renamed) == 0,
        "Dvar_SetString round-trips a high pointer");

    // The +set path the server's command line takes: an unknown name creates
    // an external string dvar from the command-line string.
    Dvar_SetFromStringByName("kisak_test_external", basepath);
    Check(std::strcmp(Dvar_GetString("kisak_test_external"), basepath) == 0,
        "an external dvar created by name round-trips");

    // Enum dvar: the value list itself lives in the high page.
    const char **const names = reinterpret_cast<const char **>(page + 4096);
    names[0] = Place(&cursor, "alpha");
    names[1] = Place(&cursor, "beta");
    names[2] = Place(&cursor, "gamma");
    names[3] = nullptr;
    const dvar_s *const en = Dvar_RegisterEnum("kisak_test_enum", names, 1, 0, "test");
    Check(en && en->domain.enumeration.stringCount == 3, "enum dvar counts its names");
    Check(en && std::strcmp(Dvar_EnumToString(en), "beta") == 0, "enum default maps to its name");
    Dvar_SetFromStringByName("kisak_test_enum", "gamma");
    Check(en && en->current.integer == 2, "enum set by name");
    Dvar_SetFromStringByName("kisak_test_enum", "0");
    Check(en && en->current.integer == 0 && std::strcmp(Dvar_EnumToString(en), "alpha") == 0,
        "enum set by index");

    // The server's "dedicated" enum: a dedicated build registers it as 2
    // (internet server). Com_IsDedicatedServer must treat every non-zero
    // value as dedicated; reading it as a bool made 2 refuse "+map".
    static const char *dedicatedNames[] = {
        "listen server", "dedicated LAN server", "dedicated internet server", nullptr};
    com_dedicated = Dvar_RegisterEnum("dedicated", dedicatedNames, 2, 0, "test");
    Check(com_dedicated && com_dedicated->current.integer == 2, "dedicated registers as 2");
    Check(com_dedicated && Com_IsDedicatedServer(), "dedicated 2 is a dedicated server");
    Dvar_SetFromStringByName("dedicated", "1");
    Check(com_dedicated && Com_IsDedicatedServer(), "dedicated 1 is a dedicated server");
    Dvar_SetFromStringByName("dedicated", "0");
    Check(com_dedicated && !Com_IsDedicatedServer(), "dedicated 0 is a listen server");

    // Dvar_ForEach's callbacks, called through their own type: the info
    // string takes exactly the dvars carrying its flag (bit 0x4, server info),
    // and the config writer prints archived dvars to the handle it was given.
    Dvar_RegisterString("kisak_test_info", "a", DVAR_SERVERINFO, "test");
    Dvar_RegisterString("kisak_test_saved", "b", DVAR_ARCHIVE, "test");
    g_infoKeys.clear();
    Dvar_InfoString(0, DVAR_SERVERINFO);
    Check(g_infoKeys.find("kisak_test_info ") != std::string::npos
              && g_infoKeys.find("kisak_test_saved") == std::string::npos,
          "the info string takes exactly the flagged dvars");
    Dvar_WriteVariables(7);
    Check(g_printedCount >= 1 && g_printedHandle == 7, "archived dvars print to the given handle");

    if (g_failures == 0)
        std::printf("dvar pointers: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
