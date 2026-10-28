// dvar_archive_quote_tests.cpp: archived dvar values with quotes or line
// breaks survive config.cfg without ending their seta line early. The
// production Dvar_WriteSingleVariable (universal/dvar.cpp) writes the line and
// the production Dvar_SetA_f (universal/dvar_cmds.cpp) reads it back; the rest
// of the engine boundary is weak, and --gc-sections drops the code no check
// reaches.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>
#include <qcommon/threads.h>
#include <stringed/stringed_hooks.h>
#include <universal/q_shared.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

std::string g_written;
std::vector<std::string> g_args;
} // namespace

WEAK void MyAssertHandler(const char *, int, int, const char *, ...) {}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK void __cdecl Com_Error(errorParm_t, const char *, ...) {}
WEAK int FS_Printf(int, const char *fmt, ...)
{
    char line[16384];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    g_written += line;
    return 0;
}
WEAK void __cdecl Cmd_AddCommandInternal(const char *, void (*)(), cmd_function_s *) {}
WEAK char __cdecl Com_Filter(const char *, char *, int) { return 0; }
WEAK bool __cdecl Com_LogFileOpen() { return false; }
WEAK void __cdecl Com_PrintMessage(int, const char *, int) {}
WEAK const char *CopyString(const char *in) { return strdup(in ? in : ""); }
WEAK void __cdecl FreeString(const char *str) { std::free(const_cast<char *>(str)); }
WEAK char *__cdecl SEH_LocalizeTextMessage(const char *text, const char *, msgLocErrType_t)
{
    return const_cast<char *>(text);
}
WEAK void *__cdecl Sys_GetValue(int valueIndex)
{
    static va_info_t vaInfo;
    return valueIndex == 1 ? &vaInfo : nullptr;
}
WEAK void KISAK_CDECL Sys_LockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_LockWrite(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_Sleep(std::uint32_t) {}
WEAK std::uint32_t KISAK_CDECL Sys_Milliseconds() { return 0; }
WEAK bool __cdecl Vec4Compare(const float *a, const float *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}
WEAK int __cdecl Cmd_Argc() { return static_cast<int>(g_args.size()); }
WEAK const char *__cdecl Cmd_Argv(int arg)
{
    return arg < static_cast<int>(g_args.size()) ? g_args[arg].c_str() : "";
}

namespace
{
// Writes one archived dvar, checks the line can't end early, and runs it back
// through seta the way a config reload does.
void RoundTrips(const char *value)
{
    const dvar_s *dvar = Dvar_RegisterString("test_archived", "", DVAR_ARCHIVE, "test");
    Dvar_SetStringByName("test_archived", value);
    g_written.clear();
    int file = 1;
    Dvar_WriteSingleVariable(dvar, &file);

    // seta test_archived "..." and one line break, at the end.
    const std::string prefix = "seta test_archived \"";
    CHECK(g_written.rfind(prefix, 0) == 0);
    CHECK(g_written.size() >= prefix.size() + 2);
    CHECK(g_written.compare(g_written.size() - 2, 2, "\"\n") == 0);
    const std::string inner = g_written.substr(prefix.size(), g_written.size() - prefix.size() - 2);
    CHECK(inner.find_first_of("\"\n\r") == std::string::npos);

    Dvar_SetStringByName("test_archived", "");
    g_args = {"seta", "test_archived", inner};
    Dvar_SetA_f();
    CHECK(std::strcmp(Dvar_GetString("test_archived"), value) == 0);
}
} // namespace

int main()
{
    Dvar_Init();
    RoundTrips("plain value");                  // written as retail writes it
    RoundTrips("server\";quit;echo \"");        // a quote can't end the line
    RoundTrips("two\nlines");                   // nor can a line break
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("dvar archive quotes: ok");
    return 0;
}
