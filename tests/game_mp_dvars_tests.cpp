// game_mp_dvars_tests.cpp: the dvars the production G_RegisterDvars
// (game_mp/g_main_mp.cpp) registers carry the Steam 1.8 executable's flags.
// The register calls are recorded, not run; --gc-sections drops the rest of
// the game module.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <game/game_public.h>
#include <game_mp/g_main_mp.h>
#include <game_mp/g_public_mp.h>
#include <qcommon/qcommon.h>
#include <qcommon/wire_profile.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// Each registration's name and flags, in order.
struct Registration
{
    std::string name;
    uint16_t flags;
    int value; // a bool's or int's default
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

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK const dvar_s *Dvar_RegisterBool(const char *name, bool value, uint16_t flags, const char *) { return Record(name, flags, value); }
WEAK const dvar_s *Dvar_RegisterInt(const char *name, int, DvarLimits, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterFloat(const char *name, float, DvarLimits, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterString(const char *name, const char *, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterEnum(const char *name, const char **, int, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const dvar_s *Dvar_RegisterColor(const char *name, float, float, float, float, uint16_t flags, const char *) { return Record(name, flags); }
WEAK const char *Com_WireGameName() { return "Call of Duty 4"; }
WEAK void BG_RegisterDvars() {}
WEAK void G_RegisterMissileDvars() {}
WEAK void G_RegisterMissileDebugDvars() {}
WEAK void G_VehRegisterDvars() {}
WEAK void Helicopter_RegisterDvars() {}

int main()
{
    G_RegisterDvars();

    // 1.8 makes antilag read-only: the server can't turn it off, and it no
    // longer appears in serverinfo.
    const Registration *const antilag = Registered("g_antilag");
    CHECK(antilag != nullptr);
    CHECK(antilag && antilag->flags == DVAR_ROM);

    // 1.8 retail defaults: cheats off, and the setexpfog mirrors are not DVAR_TEMP.
    const Registration *const cheats = Registered("sv_cheats");
    CHECK(cheats && cheats->value == 0 && cheats->flags == DVAR_NOFLAG);
    for (const char *fog : {"g_fogColorReadOnly", "g_fogStartDistReadOnly", "g_fogHalfDistReadOnly"})
    {
        const Registration *const registration = Registered(fog);
        CHECK(registration && registration->flags == (DVAR_ROM | DVAR_CHEAT | DVAR_SAVED));
    }

    if (g_failures == 0)
        std::printf("game_mp dvars: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
