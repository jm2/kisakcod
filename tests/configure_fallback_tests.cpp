// configure_fallback_tests.cpp: Com_SetRecommended (com_playerprofile.cpp)
// compiled as a client off Windows. configure_mp.csv's GPU rows match the
// video card's description; dxvk-native reports a card D3D9 knows for a
// vendor it never did (Apple's GPUs read as an AMD Radeon), so a POSIX client
// may match no row. It must keep its defaults and start, as the headless
// server does, instead of the Windows client's fatal "KISAK GPU" error.
//
// The engine boundary is weak: --gc-sections drops what the check never
// reaches. Com_Error fails the test, so the old code's fatal error does too.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

#include <qcommon/qcommon.h>
#include <qcommon/sys_local.h>

void __cdecl Com_SetRecommended(int localClientNum, int restart);

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// A CPU row that fits any host, and GPU rows for cards this host is not.
const char kCsv[] = "cpu ghz,sys mb,kisak_test_cpu\n"
                    "0.1,128,7\n"
                    "gpu,kisak_test_gpu\n"
                    "*GeForce 8800*,1\n"
                    "*Radeon X1900*,2\n";
std::string g_warnings;
std::vector<std::string> g_set;
dvar_s g_dvar{};
} // namespace

// Engine boundary.
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "unexpected Com_Error: %s\n", fmt ? fmt : "");
    std::exit(2);
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *fmt, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    g_warnings += text;
}
WEAK void MyAssertHandler(const char *file, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", file ? file : "?", line);
    std::exit(3);
}
WEAK int FS_ReadFile(const char *, void **buffer)
{
    *buffer = const_cast<char *>(kCsv);
    return static_cast<int>(sizeof(kCsv) - 1);
}
WEAK void FS_FreeFile(char *) {}
WEAK const dvar_s *Dvar_SetFromStringByNameFromSource(const char *name, const char *value, DvarSetSource)
{
    g_set.push_back(std::string(name) + "=" + value);
    return &g_dvar;
}
WEAK const dvar_s *Dvar_FindVar(const char *) { return &g_dvar; }
WEAK void Dvar_AddFlags(dvar_s *, int) {}
WEAK const dvar_s *Dvar_RegisterBool(const char *, bool, uint16_t, const char *) { return &g_dvar; }
WEAK const dvar_s *Dvar_RegisterFloat(const char *, float, DvarLimits, uint16_t, const char *) { return &g_dvar; }
WEAK const dvar_s *Dvar_RegisterInt(const char *, int, DvarLimits, uint16_t, const char *) { return &g_dvar; }
WEAK const dvar_s *Dvar_RegisterString(const char *, const char *, uint16_t, const char *) { return &g_dvar; }
WEAK void Dvar_SetFloat(dvar_s *, float) {}
WEAK void Dvar_SetInt(dvar_s *, int) {}
WEAK void Dvar_SetString(dvar_s *, char *) {}
WEAK char Dvar_AnyLatchedValues() { return 0; }
WEAK void Cbuf_AddText(int, const char *) {}
WEAK void Cbuf_Execute(int, int) {}
WEAK int I_stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
WEAK void I_strncpyz(char *dest, const char *src, int destsize)
{
    std::snprintf(dest, static_cast<std::size_t>(destsize), "%s", src);
}
WEAK SysInfo sys_info;
// The parser's per-thread state: this test is the main thread.
WEAK bool Sys_IsMainThread() { return true; }
WEAK bool Sys_IsDatabaseThread() { return false; }
WEAK bool Sys_IsRenderThread() { return false; }

int main()
{
    // What Posix_DetectCpu and Posix_DetectVideoCard fill (Sys_GetInfo copies it).
    sys_info.configureGHz = 3.0f;
    sys_info.sysMB = 1024;
    std::snprintf(sys_info.gpuDescription, sizeof(sys_info.gpuDescription), "AMD Radeon RX 6700 XT");
    Com_SetRecommended(0, 0);

    // The CPU row applied; no GPU row fit, which is a warning, not an error.
    CHECK(g_set.size() == 1 && g_set[0] == "kisak_test_cpu=7");
    CHECK(g_warnings.find("no GPU row fits \"AMD Radeon RX 6700 XT\"") != std::string::npos);

    if (g_failures == 0)
        std::printf("configure fallback: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
