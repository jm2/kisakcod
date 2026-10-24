// browser_hc_od_tests.cpp: the production server browser path (CL_SetServerInfo,
// LAN_GetServerInfo, UI_BuildServerDisplayList, UI_RegisterDvars) with the MP
// client's defines. The Steam 1.8 executable keeps getinfo "hc" and "od" per
// server and filters the list on ui_browserHardcore and ui_browserOldSchool
// (int, default -1 for any). A server without the keys reads as 0, so it
// stays listed while both filters are -1.
//
// kisakcod-browser-hc-od-tests runs the round trip everywhere the Linux tests
// build; kisakcod-browser-hc-od-filter-tests adds the filter, and builds where
// dxvk-native's <d3d9.h> is (ui_main_mp.cpp needs its Win32 types).
//
// The engine boundary is weak; --gc-sections drops the rest of the TUs.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <client_mp/client_mp.h>
#include <qcommon/qcommon.h>
#include <universal/q_shared.h>
#ifdef KISAK_TEST_BROWSER_FILTER
#include <qcommon/sys_sync.h>
#include <stringed/stringed_hooks.h>
#include <universal/com_memory.h>
#include <ui/ui_shared.h>
#include <ui_mp/ui_mp.h>

extern uiInfo_s uiInfoArray;
extern int numclean;
#endif

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

void SetServer(int n, const char *info)
{
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "%s", info);
    serverInfo_t *const server = &cls.localServers[n];
    std::memset(server, 0, sizeof(*server));
    server->adr.type = NA_IP;
    server->adr.ip[0] = 10;
    server->adr.ip[3] = (uint8_t)(n + 1);
    server->adr.port = 28960;
    CL_SetServerInfo(server, buf, 50);
}

#ifdef KISAK_TEST_BROWSER_FILTER
// How many local servers the browser lists with the given filters.
int Listed(int hardcore, int oldschool)
{
    Dvar_SetIntByName("ui_browserHardcore", hardcore);
    Dvar_SetIntByName("ui_browserOldSchool", oldschool);
    UI_BuildServerDisplayList(&uiInfoArray, 1);
    return numclean;
}
#endif
} // namespace

// The engine boundary.
clientStatic_t cls;
va_info_t g_vaInfo; // value 1: the one thread's va() buffers
WEAK void *__cdecl Sys_GetValue(int valueIndex) { return valueIndex == 1 ? &g_vaInfo : nullptr; }
WEAK bool Sys_IsMainThread() { return true; }
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error: %s\n", fmt);
    std::abort();
}
WEAK const char *NET_AdrToString(netadr_t a)
{
    static char buf[32];
    std::snprintf(buf, sizeof(buf), "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port);
    return buf;
}
#ifdef KISAK_TEST_BROWSER_FILTER
#define UNREACHED std::abort()
WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK bool __cdecl Com_LogFileOpen() { return false; }
WEAK const char *CopyString(const char *in) { return strdup(in ? in : ""); }
WEAK void __cdecl FreeString(const char *str) { std::free(const_cast<char *>(str)); }
WEAK void __cdecl Dvar_AddCommands() {}
WEAK void KISAK_CDECL Sys_LockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_LockWrite(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *) {}
WEAK bool __cdecl Vec4Compare(const float *, const float *) { UNREACHED; }
const dvar_t *loc_warnings;
const dvar_t *loc_warningsAsErrors;
WEAK const char *__cdecl SEH_StringEd_GetString(const char *) { UNREACHED; }
WEAK void Menu_SetFeederSelection(UiContext *, menuDef_t *, int, int, const char *) {}
WEAK char *UI_SafeTranslateString(const char *s) { return const_cast<char *>(s); }
#endif

int main()
{
    const char *const base = "clients\\4\\sv_maxclients\\18\\hostname\\h\\mapname\\mp_crash\\gametype\\war";
    char info[1024];
    std::snprintf(info, sizeof(info), "%s\\hc\\1\\od\\0", base);
    SetServer(0, info);
    std::snprintf(info, sizeof(info), "%s\\hc\\0\\od\\1", base);
    SetServer(1, info);
    SetServer(2, base); // a 1.0 server: no hc or od
    cls.numlocalservers = 3;

    CHECK(cls.localServers[0].hardcore == 1 && cls.localServers[0].oldschool == 0);
    CHECK(cls.localServers[1].hardcore == 0 && cls.localServers[1].oldschool == 1);
    CHECK(cls.localServers[2].hardcore == 0 && cls.localServers[2].oldschool == 0);

    char out[1024];
    LAN_GetServerInfo(0, 0, out, sizeof(out));
    CHECK(!std::strcmp(Info_ValueForKey(out, "hc"), "1"));
    CHECK(!std::strcmp(Info_ValueForKey(out, "od"), "0"));
    LAN_GetServerInfo(0, 1, out, sizeof(out));
    CHECK(!std::strcmp(Info_ValueForKey(out, "hc"), "0"));
    CHECK(!std::strcmp(Info_ValueForKey(out, "od"), "1"));

#ifdef KISAK_TEST_BROWSER_FILTER
    Dvar_Init();
    Dvar_RegisterString("cl_motdString", "motd", DVAR_ROM, "");
    UI_RegisterDvars();
    CHECK(Dvar_GetInt("ui_browserHardcore") == -1);
    CHECK(Dvar_GetInt("ui_browserOldSchool") == -1);
    Dvar_SetIntByName("ui_netSource", 0);
    Dvar_SetBoolByName("ui_browserShowPure", false);
    sharedUiInfo.joinGameTypes[0].gameTypeName = ""; // "all", as the gametypes list loads it
    CHECK(Listed(-1, -1) == 3); // the 1.0 server stays listed
    CHECK(Listed(1, -1) == 1);
    CHECK(Listed(0, -1) == 2);
    CHECK(Listed(-1, 1) == 1);
    CHECK(Listed(0, 0) == 1);
#endif

    if (g_failures)
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
    else
        std::puts("browser hc/od: ok");
    return g_failures ? 1 : 0;
}
