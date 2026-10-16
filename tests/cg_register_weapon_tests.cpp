// cg_register_weapon_tests.cpp: CG_RegisterWeapon (cgame/cg_weapons.cpp) at
// 64-bit stores a weapon's HUD icon as its cursor-hint material,
// cgMedia.hintMaterials[weaponNum + WEAPON_HINT_OFFSET]. The decompile wrote
// it as stanceMaterials[weaponNum - 129], the same address on x86 (the 133
// hint materials precede the 4 stance ones), but at 64-bit the wrapped index
// scales by 8 and the store lands far outside cgMedia.
//
// The engine boundary is weak: --gc-sections drops what the check never
// reaches. A weapon with no view model skips the DObj setup.

#include <cstdio>
#include <cstdlib>

#include <bgame/bg_local.h>
#include <cgame/cg_local.h>
#include <cgame_mp/cg_local_mp.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

WeaponDef g_weaponDef{};
Material *const kIcon = reinterpret_cast<Material *>(std::uintptr_t{0x1C0}); // a marker
} // namespace

// The globals the check reads, owned here.
cgMedia_t cgMedia;
weaponInfo_s cg_weaponsArray[1][128];
gitem_s bg_itemlist[2048];
scr_const_t scr_const;
const dvar_t *loc_warnings;
const dvar_t *loc_warningsAsErrors;

// Engine boundary. A weapon with no view model never reaches the DObj and
// XAnim calls; they only have to link.
WEAK WeaponDef *BG_GetWeaponDef(uint32_t) { return &g_weaponDef; }
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error: %s\n", fmt);
    std::exit(1);
}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK const char *SL_ConvertToString(uint32_t) { return ""; }
WEAK void BG_CreateXAnim(XAnim_s *, uint32_t, const char *) {}
WEAK DObj_s *Com_ClientDObjCreate(DObjModel_s *, uint16_t, XAnimTree_s *, uint32_t, int) { std::abort(); }
WEAK int DObjGetBoneIndex(const DObj_s *, uint32_t, uint8_t *) { std::abort(); }
WEAK void DObjSetHidePartBits(DObj_s *, const uint32_t *) { std::abort(); }
WEAK void DObjUpdateClientInfo(DObj_s *, float, bool) { std::abort(); }
WEAK void *Hunk_AllocXAnimClient(int) { std::abort(); }
WEAK void XAnimBlend(XAnim_s *, uint32_t, const char *, uint32_t, uint32_t, uint32_t) { std::abort(); }
WEAK void XAnimClearTreeGoalWeights(XAnimTree_s *, uint32_t, float) { std::abort(); }
WEAK XAnim_s *XAnimCreateAnims(const char *, uint32_t, void *(*)(int)) { std::abort(); }
WEAK XAnimTree_s *XAnimCreateTree(XAnim_s *, void *(*)(int)) { std::abort(); }
WEAK bool XAnimIsLooped(const XAnim_s *, uint32_t) { std::abort(); }
WEAK int XAnimSetGoalWeight(DObj_s *, uint32_t, float, float, float, uint32_t, uint32_t, int) { std::abort(); }
WEAK void XAnimSetTime(XAnimTree_s *, uint32_t, float) { std::abort(); }
WEAK uint32_t BG_GetNumWeapons() { return 128; }
WEAK void SCR_UpdateLoadScreen() {}
WEAK const char *SEH_StringEd_GetString(const char *) { return "localized"; }

int main()
{
    g_weaponDef.hudIcon = kIcon;
    CG_RegisterWeapon(0, 5);
    CHECK(cgMedia.hintMaterials[5 + WEAPON_HINT_OFFSET] == kIcon);
    for (Material *stance : cgMedia.stanceMaterials)
        CHECK(stance == nullptr);

    if (g_failures == 0)
        std::printf("cg register weapon: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
