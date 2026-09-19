// Production enum declarations and dispatch bodies; engine services are doubles.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#ifndef _MSC_VER
#define __cdecl
#define __int32 std::int32_t
#endif
namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "gameplay value contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#include "gameplay_perk_enum.inc"
#include "gameplay_damage_enum.inc"
namespace sp {
#include "gameplay_mod_sp.inc"
}
namespace mp {
#include "gameplay_mod_mp.inc"
}
static_assert(std::is_same_v<std::underlying_type_t<perksEnum>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<DAMAGE_FLAGS>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<sp::meansOfDeath_t>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<mp::meansOfDeath_t>, std::int32_t>);
static_assert(PERK_JAMRADAR == 0 && PERK_ACCURACY == 1 && PERK_FASTRELOAD == 2
    && PERK_RATEOFFIRE == 3 && PERK_EXTRABREATH == 4 && PERK_EXTRABP == 5
    && PERK_GRENADEDEATH == 6 && PERK_PISTOLDEATH == 7 && PERK_QUIETMOVE == 8
    && PERK_PARABOLIC == 9 && PERK_LONGERSPRINT == 10 && PERK_DETECTEXPLOSIVE == 11
    && PERK_EXPLOSIVEDMG == 12 && PERK_EXPOSEENEMY == 13 && PERK_BULLETDMG == 14
    && PERK_EXTRAAMMO == 15 && PERK_TWOPRIMARIES == 16 && PERK_ARMORVEST == 17
    && PERK_FRAGGRENADE == 18 && PERK_SPECIALGRENADE == 19
    && PERK_COUNT == 20 && PERK_UNKNOWN == 20);
static_assert(DAMAGE_NOFLAG == 0 && DAMAGE_RADIUS == 1 && DAMAGE_NO_ARMOR == 2
    && DAMAGE_NO_KNOCKBACK == 4 && DAMAGE_PENETRATION == 8);
#define MOD_VALUE(name, number) static_assert(sp::name == number && mp::name == number)
MOD_VALUE(MOD_UNKNOWN, 0);
MOD_VALUE(MOD_PISTOL_BULLET, 1);
MOD_VALUE(MOD_RIFLE_BULLET, 2);
MOD_VALUE(MOD_GRENADE, 3);
MOD_VALUE(MOD_GRENADE_SPLASH, 4);
MOD_VALUE(MOD_PROJECTILE, 5);
MOD_VALUE(MOD_PROJECTILE_SPLASH, 6);
MOD_VALUE(MOD_MELEE, 7);
MOD_VALUE(MOD_HEAD_SHOT, 8);
MOD_VALUE(MOD_CRUSH, 9);
MOD_VALUE(MOD_TELEFRAG, 10);
MOD_VALUE(MOD_FALLING, 11);
MOD_VALUE(MOD_SUICIDE, 12);
MOD_VALUE(MOD_TRIGGER_HURT, 13);
MOD_VALUE(MOD_EXPLOSIVE, 14);
MOD_VALUE(MOD_IMPACT, 15);
MOD_VALUE(MOD_NUM, 16);
#undef MOD_VALUE
int I_stricmp(const char *left, const char *right)
{
    for (;;) {
        const int a = std::tolower(static_cast<unsigned char>(*left++));
        const int b = std::tolower(static_cast<unsigned char>(*right++));
        if (a != b || !a) return a - b;
    }
}
#include "gameplay_perk_names.inc"
#include "gameplay_perk_lookup.inc"
void CheckPerks()
{
    constexpr const char *names[] = {
        "specialty_gpsjammer", "specialty_bulletaccuracy", "specialty_fastreload",
        "specialty_rof", "specialty_holdbreath", "specialty_bulletpenetration",
        "specialty_grenadepulldeath", "specialty_pistoldeath", "specialty_quieter",
        "specialty_parabolic", "specialty_longersprint", "specialty_detectexplosive",
        "specialty_explosivedamage", "specialty_exposeenemy", "specialty_bulletdamage",
        "specialty_extraammo", "specialty_twoprimaries", "specialty_armorvest",
        "specialty_fraggrenade", "specialty_specialgrenade"
    };
    static_assert(sizeof(names) / sizeof(names[0]) == 20);
    for (uint32_t index = 0; index < 20; ++index) {
        CHECK(BG_GetPerkIndexForName(names[index]) == index);
        char uppercase[64]{};
        for (std::size_t i = 0; names[index][i]; ++i)
            uppercase[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(names[index][i])));
        CHECK(BG_GetPerkIndexForName(uppercase) == index);
    }
    CHECK(BG_GetPerkIndexForName(nullptr) == 20);
    CHECK(BG_GetPerkIndexForName("") == 20);
    CHECK(BG_GetPerkIndexForName("specialty_unknown") == 20);
}
namespace combat {
using namespace mp;
int assertions;
void MyAssertHandler(const char *, int, int, const char *, ...) { ++assertions; }
#include "gameplay_bullet_mod.inc"
struct vehicle_info_t {
    bool bulletDamage, armorPiercingDamage, heavyExplosiveDamage,
        grenadeDamage, projectileDamage, projectileSplashDamage;
};
struct scr_vehicle_s { int infoIdx; };
struct gentity_s { scr_vehicle_s *scr_vehicle; };
vehicle_info_t s_vehicleInfos[1];
constexpr int WEAPPROJEXP_HEAVY = 3;
struct WeaponDef { int projExplosion; };
WeaponDef ordinaryWeapon{0}, heavyWeapon{WEAPPROJEXP_HEAVY};
WeaponDef *BG_GetWeaponDef(uint32_t weapon) { CHECK(weapon < 2); return weapon ? &heavyWeapon : &ordinaryWeapon; }
#include "gameplay_vehicle_immunity.inc"
void Run()
{
    for (int mod = 0; mod < 16; ++mod)
        CHECK(IsBulletImpactMOD(static_cast<meansOfDeath_t>(mod)) == (mod == 1 || mod == 2 || mod == 8));
    CHECK(!IsBulletImpactMOD(static_cast<meansOfDeath_t>(16)));
    CHECK(!IsBulletImpactMOD(static_cast<meansOfDeath_t>(-1)));
    CHECK(assertions == 2);
    scr_vehicle_s vehicle{0};
    gentity_s entity{&vehicle};
    for (unsigned int capabilities = 0; capabilities < 64; ++capabilities) {
        s_vehicleInfos[0] = {bool(capabilities & 1), bool(capabilities & 2),
            bool(capabilities & 4), bool(capabilities & 8), bool(capabilities & 16), bool(capabilities & 32)};
        for (int flags = 0; flags < 16; ++flags) {
            for (uint32_t weapon = 0; weapon < 2; ++weapon) {
                const bool accepts[] = {
                    false,
                    bool(capabilities & 1) || (bool(capabilities & 2) && bool(flags & 2)),
                    bool(capabilities & 1) || (bool(capabilities & 2) && bool(flags & 2)),
                    bool(capabilities & (weapon ? 4 : 8)), bool(capabilities & (weapon ? 4 : 8)),
                    bool(capabilities & 16), bool(capabilities & 32),
                    false, false, false, false, false, false, false, true, false
                };
                for (int mod = 0; mod < 16; ++mod)
                    CHECK(G_VehImmuneToDamage(&entity, mod, static_cast<char>(flags), weapon) == !accepts[mod]);
            }
        }
    }
    CHECK(assertions == 2);
}
} // namespace combat
} // namespace
void RunGameplayValueContracts()
{
    CheckPerks();
    combat::Run();
}
