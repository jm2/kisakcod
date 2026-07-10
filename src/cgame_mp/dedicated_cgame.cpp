#ifndef KISAK_DEDI_HEADLESS
#error dedicated_cgame.cpp is only part of the headless dedicated source profile
#endif

#include <database/database.h>
#include <qcommon/qcommon.h>
#include <win32/win_local.h>

void __cdecl CL_InitDedicated()
{
    Dvar_RegisterBool(
        "onlinegame",
        true,
        DVAR_CHEAT,
        "Current game is an online game with stats, custom classes, unlocks");

    XZoneInfo zones[5]{};
    uint32_t zoneCount = 0;

    zones[zoneCount++] = { "code_post_gfx_mp", 2, 0 };
    zones[zoneCount++] = { "localized_code_post_gfx_mp", 0, 0 };
    zones[zoneCount++] = { "common_mp", 4, 0 };
    zones[zoneCount++] = { "localized_common_mp", 1, 0 };
    if (DB_ModFileExists())
        zones[zoneCount++] = { "mod", 16, 0 };

    DB_LoadXAssets(zones, zoneCount, 0);
    Sys_ShowConsole();
    Sys_NormalExit();
}
