// veh_required_tags_tests.cpp: VEH_DObjHasRequiredTags (production
// game_mp/g_vehicles_mp.cpp). A tank (vehicle type 1) must carry six tags. The
// MP wheel table has four entries, and retail 1.8 reads the last two from the
// table right after it, s_flashTags (0x71cef4 then 0x71cf04): tag_flash and
// tag_flash_11. The decompile indexed s_wheelTags[4..5], past its end.

#include <game_mp/g_public_mp.h>
#include <script/scr_const.h>
#include <server/sv_game.h>

#include <cstdio>
#include <vector>

char __cdecl VEH_DObjHasRequiredTags(gentity_s *ent, int32_t infoIdx);

scr_const_t scr_const;
vehicle_info_t s_vehicleInfos[32];

namespace
{
std::vector<uint32_t> g_queried;
}

int __cdecl SV_DObjGetBoneIndex(const gentity_s *, uint32_t boneName)
{
    g_queried.push_back(boneName);
    return 0;
}

int main()
{
    scr_const.tag_wheel_front_left = 11;
    scr_const.tag_wheel_front_right = 12;
    scr_const.tag_wheel_back_left = 13;
    scr_const.tag_wheel_back_right = 14;
    scr_const.tag_wheel_middle_left = 15;
    scr_const.tag_wheel_middle_right = 16;
    scr_const.tag_flash = 21;
    scr_const.tag_flash_11 = 22;
    s_vehicleInfos[0].type = 1; // tank

    gentity_s ent{};
    const bool ok = VEH_DObjHasRequiredTags(&ent, 0) == 1
        && g_queried == std::vector<uint32_t>{11, 12, 13, 14, 21, 22};
    if (!ok)
    {
        std::fputs("FAIL a tank's required tags are the four wheels plus tag_flash and tag_flash_11\n", stderr);
        return 1;
    }
    std::puts("vehicle required tags: ok");
    return 0;
}
