// cg_weapon_files_tests.cpp: CG_SetupWeaponDef (cgame/cg_weapons.cpp) at
// 64-bit. It splits the weapon-files config string into names in a stack
// buffer and hands their pointers to ParseWeaponDefFiles, which checks each
// name's weapon index. The pointers were kept in a 32-bit _DWORD array, so at
// 64-bit every name pointer was truncated and the client crashed in
// BG_GetWeaponIndexForName while initializing cgame for a map.
//
// The engine boundary is weak: --gc-sections drops the cgame code the check
// never reaches. Com_Error fails the test.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <bgame/bg_local.h>
#include <cgame/cg_local.h>
#include <client_mp/client_mp.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

const char *g_weaponFiles = "";
std::vector<std::string> g_names; // what BG_GetWeaponIndexForName was asked
} // namespace

// Engine boundary.
WEAK const char *CL_GetConfigString(int32_t, uint32_t)
{
    return g_weaponFiles;
}
WEAK uint32_t BG_GetWeaponIndexForName(const char *name, void (*)(uint32_t))
{
    g_names.emplace_back(name);
    return static_cast<uint32_t>(g_names.size());
}
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error: %s\n", fmt);
    std::exit(1);
}

int main()
{
    // Space-separated names, as the server sets CS_WEAPONFILES: each reaches
    // the index check intact and in order.
    g_weaponFiles = "beretta_mp colt45_mp usp_mp";
    CG_SetupWeaponDef(0);
    CHECK((g_names == std::vector<std::string>{"beretta_mp", "colt45_mp", "usp_mp"}));

    // Runs of spaces separate names without empty ones; one name stands alone.
    g_names.clear();
    g_weaponFiles = "ak47_mp  m4_mp";
    CG_SetupWeaponDef(0);
    CHECK((g_names == std::vector<std::string>{"ak47_mp", "m4_mp"}));
    g_names.clear();
    g_weaponFiles = "rpg_mp";
    CG_SetupWeaponDef(0);
    CHECK((g_names == std::vector<std::string>{"rpg_mp"}));

    if (g_failures == 0)
        std::printf("cg weapon files: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
