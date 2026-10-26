// sv_map_hint_guard_tests.cpp: SV_Map_f in the production sv_ccmds_mp.cpp.
// Loading a map queues "selectStringTableEntryInDvar mp/didyouknow.csv" so a
// client can show a did-you-know tip. The command is client-only, so every
// dedicated-server map load in the G3 captures answered it with
// `Unknown command "selectStringTableEntryInDvar"` (#835). The fix guards the
// queue with !Com_IsDedicatedServer().
//
// The TU is compiled with the headless dedicated server's defines, the ones
// the G3 server build uses, so this is the shipped SV_Map_f and not a copy.
// Com_IsDedicatedServer() is an inline read of com_dedicated, so one binary
// drives both branches. Assertion 1 is the fix; assertion 2 keeps the hint for
// clients (guard it, do not delete it); assertion 3 proves the guard is not an
// early return, because the missing-map path still runs.

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <qcommon/qcommon.h>

void SV_Map_f();

namespace
{
int g_failures = 0;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

const char *const kHint = "selectStringTableEntryInDvar mp/didyouknow.csv 0 didyouknow";

struct Run
{
    int cbufCalls = 0;
    std::string cbufText;
    int spawnCalls = 0;
    std::string lastError;
};

Run g_run;

// com_dedicated carries the dedicated flag. fs_gameDirVar stays a separate
// dvar: SV_Map_f reads its current.string as the base-game marker, and a dvar
// holding the dedicated integer is not the same value.
dvar_s g_dedicated{};
dvar_s g_gameDir{};

// -1 is "the map file is present", 0 is "it is not".
int g_dbFileSizeResult = -1;

void StartRun() { g_run = Run{}; }

const char *QueuedText() { return g_run.cbufText.empty() ? "(none)" : g_run.cbufText.c_str(); }

bool NotFoundPrinted() { return g_run.lastError.rfind("Can't find map", 0) == 0; }
} // namespace

uint32_t com_errorPrintsCount = 0;
const dvar_t *com_dedicated = &g_dedicated;
const dvar_s *fs_gameDirVar = &g_gameDir;
const dvar_t *sv_cheats = &g_dedicated;

const char *SV_Cmd_Argv(int argIndex) { return argIndex == 0 ? "map" : "mp_zzz_nonexistent"; }

int FS_ReadFile(const char *, void **) { return -1; }
void FS_ConvertPath(char *) {}
char *FS_GetMapBaseName(char *) { return const_cast<char *>("mp_zzz_nonexistent"); }
void Com_GetBspFilename(char *expanded, unsigned int size, const char *map)
{
    std::snprintf(expanded, size, "maps/mp/%s.d3dbsp", map);
}
void Cbuf_ExecuteBuffer(int, int, const char *text)
{
    ++g_run.cbufCalls;
    g_run.cbufText = text ? text : "";
}
void Cbuf_InsertText(int, const char *) {}
void SV_SpawnServer(char *) { ++g_run.spawnCalls; }
int DB_FileSize(const char *, int) { return g_dbFileSizeResult; }
void Dvar_SetBool(dvar_s *, bool) {}
bool Com_HasPlayerProfile() { return true; }
void I_strncpyz(char *dest, const char *src, int size) { std::snprintf(dest, size, "%s", src); }
char *I_strlwr(char *text)
{
    for (char *ch = text; *ch; ++ch)
        *ch = static_cast<char>(std::tolower(static_cast<unsigned char>(*ch)));
    return text;
}
int I_stricmp(const char *, const char *) { return 1; }
char *va(const char *format, ...)
{
    static char buffer[256];
    std::string out;
    va_list args;
    va_start(args, format);
    for (const char *ch = format; *ch; ++ch)
    {
        if (*ch != '%')
        {
            out.push_back(*ch);
            continue;
        }
        ++ch;
        switch (*ch)
        {
        case 's':
            out += va_arg(args, const char *);
            break;
        case 'c':
            out.push_back(static_cast<char>(va_arg(args, int)));
            break;
        case 'd':
            out += std::to_string(va_arg(args, int));
            break;
        case '%':
            out.push_back('%');
            break;
        default:
            std::abort();
        }
    }
    va_end(args);
    std::snprintf(buffer, sizeof(buffer), "%s", out.c_str());
    return buffer;
}
void Com_PrintError(int, const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    g_run.lastError = text;
    ++com_errorPrintsCount;
}
void Com_Error(errorParm_t, const char *format, ...)
{
    std::fprintf(stderr, "unexpected Com_Error: %s\n", format ? format : "");
    std::exit(2);
}
void MyAssertHandler(const char *file, int line, int, const char *format, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file ? file : "?", line, format ? format : "");
    std::exit(3);
}

int main()
{
    // SV_Map_f reads this as the base-game marker; a null here is a null read,
    // so give it an empty string, which is what "no fs_game override" means.
    g_gameDir.current.string = "";

    // 1. A dedicated server has no client UI and no string table: it must not
    //    queue the hint. This is the case that failed before the guard.
    g_dedicated.current.integer = 1;
    g_dedicated.latched.integer = 1;
    g_dbFileSizeResult = -1;
    StartRun();
    SV_Map_f();
    std::printf("dedicated=1 calls=%d queued=%s\n", g_run.cbufCalls, QueuedText());
    Check(g_run.cbufCalls == 0, "a dedicated server queues no did-you-know hint");

    // 2. A client must still get it, exactly once and unchanged: the fix
    //    guards the hint, it does not delete it.
    g_dedicated.current.integer = 0;
    g_dedicated.latched.integer = 0;
    g_dbFileSizeResult = -1;
    StartRun();
    SV_Map_f();
    std::printf("dedicated=0 calls=%d queued=%s\n", g_run.cbufCalls, QueuedText());
    Check(g_run.cbufCalls == 1, "a client still queues the hint once");
    Check(g_run.cbufText == kHint, "the queued text is the did-you-know hint");

    // 3. The guard must not become an early return: with a missing map a
    //    dedicated server still reaches the not-found path and spawns nothing.
    g_dedicated.current.integer = 1;
    g_dedicated.latched.integer = 1;
    g_dbFileSizeResult = 0;
    StartRun();
    SV_Map_f();
    std::printf("dedicated=1 missing-map calls=%d not-found=%d spawned=%d\n", g_run.cbufCalls,
                NotFoundPrinted() ? 1 : 0, g_run.spawnCalls);
    Check(g_run.cbufCalls == 0, "a dedicated server queues nothing on a missing map");
    Check(NotFoundPrinted(), "the missing-map path still runs on a dedicated server");
    Check(g_run.spawnCalls == 0, "a missing map spawns no server");

    if (!g_failures)
        std::printf("sv map hint guard: all checks passed\n");
    else
        std::fprintf(stderr, "sv map hint guard: %d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
