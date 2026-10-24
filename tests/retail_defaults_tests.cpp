// retail_defaults_tests.cpp: the Steam 1.8 MP executable's defaults and flags
// for the shared dvars the debug-built 1.0 decompile registers differently:
// the production Com_InitDvars (qcommon/common.cpp) and Con_Init
// (client/cl_console.cpp), plus R_RegisterDvars (gfx_d3d/r_dvars.cpp) where
// dxvk-native's <d3d9.h> is. The MP client's defines; SP keeps 1.0.
//
// The dvar registrations are recorded, not created; the rest of the engine
// boundary is weak, and --gc-sections drops the code no check reaches.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <client/client.h>
#include <client_mp/client_mp.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/threads.h>
#include <universal/q_shared.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// Each registration's name, flags and (bool or int) default, in order.
struct Registration
{
    std::string name;
    uint16_t flags;
    int value;
};
std::vector<Registration> g_registered;
dvar_s g_dvar{};

const dvar_s *Record(const char *name, uint16_t flags, int value = 0)
{
    g_registered.push_back({name, flags, value});
    return &g_dvar;
}

const Registration *Registered(const char *name)
{
    for (const Registration &registration : g_registered)
    {
        if (registration.name == name)
            return &registration;
    }
    return nullptr;
}
} // namespace

void Com_InitDvars();
void __cdecl Con_Init();
#ifdef KISAK_TEST_RETAIL_RENDERER
void __cdecl R_RegisterDvars();
// Defined in the renderer TUs the test doesn't link.
const dvar_t *r_fullscreen, *r_warningRepeatDelay, *vid_xpos, *vid_ypos, *sv_cheats;
WEAK void __cdecl R_RegisterSunDvars() {}
WEAK void __cdecl Material_PreventOverrideTechniqueGeneration() {}
#endif

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK const dvar_s *Dvar_RegisterBool(const char *name, bool value, uint16_t flags, const char *)
{
    return Record(name, flags, value);
}
WEAK const dvar_s *Dvar_RegisterInt(const char *name, int value, DvarLimits, uint16_t flags, const char *)
{
    return Record(name, flags, value);
}
WEAK const dvar_t *Dvar_RegisterInt(const char *name, int value, uint32_t, uint32_t, uint32_t flags, const char *)
{
    return Record(name, (uint16_t)flags, value);
}
WEAK const dvar_s *Dvar_RegisterFloat(const char *name, float, DvarLimits, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterFloat(const char *name, float, float, float, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterString(const char *name, const char *, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterEnum(const char *name, const char **, int, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterColor(const char *name, float, float, float, float, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterVec2(const char *name, float, float, DvarLimits, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterVec3(const char *name, float, float, float, DvarLimits, uint16_t flags, const char *)
{
    return Record(name, flags);
}
WEAK const dvar_s *Dvar_RegisterVec3(const char *name, float, float, float, float, float, uint16_t flags, const char *)
{
    return Record(name, flags);
}
WEAK const dvar_s *Dvar_RegisterVec4(const char *name, float, float, float, float, DvarLimits, uint16_t flags, const char *)
{
    return Record(name, flags);
}
WEAK void __cdecl Dvar_SetModified(dvar_s *) {}

// The rest of the engine boundary.
field_t g_consoleField;
field_t historyEditLines[32];
PlayerKeyState playerKeys[1];
clientUIActive_t clientUIActives[STATIC_MAX_LOCAL_CLIENTS];
WEAK void __cdecl Cmd_AddCommandInternal(const char *, void(__cdecl *)(), cmd_function_s *) {}
WEAK std::uint32_t __cdecl Sys_GetCpuCount() { return 1; }
WEAK void __cdecl CL_GetScreenDimensions(int32_t *width, int32_t *height, float *aspect)
{
    *width = 640;
    *height = 480;
    *aspect = 4.0f / 3.0f;
}

int main()
{
    Com_InitDvars();
    Con_Init();

    const Registration *const logfile = Registered("logfile");
    CHECK(logfile && logfile->value == 0); // 1.8 writes no log unless asked
    const Registration *const script = Registered("developer_script");
    CHECK(script && script->value == 0 && script->flags == DVAR_INIT);
    const Registration *const timescale = Registered("com_timescale");
    CHECK(timescale && timescale->flags == (DVAR_SYSTEMINFO | DVAR_ROM | DVAR_CHEAT | DVAR_SAVED));
    const Registration *const monkeytoy = Registered("monkeytoy");
    CHECK(monkeytoy && monkeytoy->value == 0 && monkeytoy->flags == DVAR_ARCHIVE); // console unrestricted

#ifdef KISAK_TEST_RETAIL_RENDERER
    R_RegisterDvars();
    const Registration *const missing = Registered("r_showMissingLightGrid");
    CHECK(missing && missing->value == 0 && missing->flags == DVAR_CHEAT);
    const Registration *const cheats = Registered("sv_cheats");
    CHECK(cheats && cheats->value == 0 && cheats->flags == (DVAR_SYSTEMINFO | DVAR_ROM));
#endif

    if (g_failures == 0)
        std::printf("retail defaults: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
