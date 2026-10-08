// bg_weapons_load_obj_tests.cpp: the weapon-file field table
// (bgame/bg_weapons_load_obj.cpp, weaponDefFields) at native width. Its 502
// entries were 32-bit byte offsets into WeaponDef; at 64-bit WeaponDef grows
// from 0x878 to 0xB10 bytes, so every entry must follow the real layout.
// InitWeaponDef writes a string pointer through every string entry, into a
// WeaponDef allocated at exactly its size (ASan sees any stray write), and the
// production ParseConfigStringToStructCustomSize parses a weapon file's keys
// through the table into the named members.

#include <bgame/bg_local.h>
#include <bgame/bg_public.h>
#include <bgame/bg_weapons.h>
#include <universal/q_shared.h>
#include <xanim/xanim.h>
#include <qcommon/qcommon.h>
#include <qcommon/threads.h>
#include <script/scr_stringlist.h>
#include <universal/com_sndalias.h>
#include <universal/q_parse.h>
#include <universal/surfaceflags.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

extern cspField_t weaponDefFields[502];
void __cdecl InitWeaponDef(WeaponDef *weapDef);
int __cdecl BG_ParseWeaponDefSpecificFieldType(uint8_t *pStruct, const char *pValue, int iFieldType);

namespace
{
int g_failures = 0;
std::vector<void *> g_hunk;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

bool Equals(const char *const actual, const char *const expected)
{
    return actual && std::strcmp(actual, expected) == 0;
}

std::unique_ptr<WeaponDef> NewWeaponDef()
{
    std::unique_ptr<WeaponDef> weapDef(new WeaponDef);
    std::memset(static_cast<void *>(weapDef.get()), 0xCD, sizeof(WeaponDef));
    InitWeaponDef(weapDef.get());
    return weapDef;
}

void TestInitWeaponDef()
{
    const std::unique_ptr<WeaponDef> weapDef = NewWeaponDef();
    Check(Equals(weapDef->szInternalName, ""), "init: internal name");
    Check(Equals(weapDef->szDisplayName, ""), "init: display name");
    Check(Equals(weapDef->szOverlayName, ""), "init: overlay name");
    Check(Equals(weapDef->szModeName, ""), "init: mode name");
}

void TestParseWeaponFile()
{
    const std::unique_ptr<WeaponDef> weapDef = NewWeaponDef();
    char weaponFile[] =
        "\\displayName\\WEAPON_TEST_RIFLE"
        "\\clipSize\\30\\maxAmmo\\180\\damage\\40"
        "\\rifleBullet\\1"
        "\\moveSpeedScale\\0.95"
        "\\fireTime\\0.1\\reloadTime\\2.5";
    Check(ParseConfigStringToStructCustomSize(reinterpret_cast<uint8_t *>(weapDef.get()), weaponDefFields, 502,
              weaponFile, 35, BG_ParseWeaponDefSpecificFieldType, SetConfigString2),
        "parse: succeeds");
    Check(Equals(weapDef->szDisplayName, "WEAPON_TEST_RIFLE"), "parse: displayName (string)");
    Check(weapDef->iClipSize == 30, "parse: clipSize (int)");
    Check(weapDef->iMaxAmmo == 180, "parse: maxAmmo (int)");
    Check(weapDef->damage == 40, "parse: damage (int)");
    Check(weapDef->bRifleBullet == 1, "parse: rifleBullet (bool)");
    Check(weapDef->moveSpeedScale > 0.949f && weapDef->moveSpeedScale < 0.951f, "parse: moveSpeedScale (float)");
    Check(weapDef->iFireTime == 100, "parse: fireTime (seconds to msec)");
    Check(weapDef->iReloadTime == 2500, "parse: reloadTime (seconds to msec)");
    // Keys the file does not name keep the defaults InitWeaponDef wrote.
    Check(Equals(weapDef->szOverlayName, ""), "parse: unnamed string stays default");
}
} // namespace

// The engine boundary the parser and the weapon field types reach. Paths the
// checks never take abort, so reaching one fails the test.
int32_t surfaceTypeSoundListCount; // bg_weapons.cpp

void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "engine assert %s:%d %s\n", file, line, fmt);
    std::abort();
}

void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error %s\n", fmt);
    std::abort();
}

void Com_PrintWarning(int, const char *, ...)
{
}

void *__cdecl Sys_GetValue(int valueIndex)
{
    static va_info_t vaInfo;
    if (valueIndex != 1)
        std::abort();
    return &vaInfo;
}

bool Sys_IsMainThread()
{
    return true;
}

snd_alias_list_t *__cdecl Com_FindSoundAlias(const char *)
{
    std::abort();
}

const char *__cdecl Com_SurfaceTypeToName(int)
{
    std::abort();
}

parseInfo_t *__cdecl Com_Parse(const char **)
{
    std::abort();
}

uint32_t SL_GetStringOfSize(const char *, uint32_t, uint32_t, int)
{
    std::abort();
}

uint32_t SL_GetLowercaseString(const char *, uint32_t)
{
    std::abort();
}

uint32_t SL_ConvertToLowercase(uint32_t, uint32_t, int)
{
    std::abort();
}

uint8_t *__cdecl Hunk_AllocLow(uint32_t, const char *, int)
{
    std::abort();
}

// The hunk SetConfigString copies strings into.
uint8_t *__cdecl Hunk_AllocLowAlign(uint32_t size, int, const char *, int)
{
    void *const block = std::calloc(1, size);
    g_hunk.push_back(block);
    return static_cast<uint8_t *>(block);
}

int main()
{
    TestInitWeaponDef();
    TestParseWeaponFile();
    for (void *const block : g_hunk)
        std::free(block);
    if (g_failures == 0)
        std::puts("weapon field table contracts passed");
    return g_failures == 0 ? 0 : 1;
}
