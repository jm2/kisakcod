#pragma once

#include <universal/kisak_abi.h>

#include <cstdint>

union hudelem_color_t // sizeof=0x4
{
    struct
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };
    uint32_t rgba;
};
RUNTIME_SIZE(hudelem_color_t, 0x4, 0x4);

#if defined(KISAK_MP) && !defined(KISAK_SP)
enum he_type_t : std::int32_t
{
    HE_TYPE_FREE = 0x0,
    HE_TYPE_TEXT = 0x1,
    HE_TYPE_VALUE = 0x2,
    HE_TYPE_PLAYERNAME = 0x3,
    HE_TYPE_MAPNAME = 0x4,
    HE_TYPE_GAMETYPE = 0x5,
    HE_TYPE_MATERIAL = 0x6,
    HE_TYPE_TIMER_DOWN = 0x7,
    HE_TYPE_TIMER_UP = 0x8,
    HE_TYPE_TENTHS_TIMER_DOWN = 0x9,
    HE_TYPE_TENTHS_TIMER_UP = 0xA,
    HE_TYPE_CLOCK_DOWN = 0xB,
    HE_TYPE_CLOCK_UP = 0xC,
    HE_TYPE_WAYPOINT = 0xD,
    HE_TYPE_COUNT = 0xE,
};
#elif defined(KISAK_SP) && !defined(KISAK_MP)
enum he_type_t : std::int32_t
{
    HE_TYPE_FREE = 0x0,
    HE_TYPE_TEXT = 0x1,
    HE_TYPE_VALUE = 0x2,
    HE_TYPE_MATERIAL = 0x3,
    HE_TYPE_TIMER_DOWN = 0x4,
    HE_TYPE_TIMER_UP = 0x5,
    HE_TYPE_TENTHS_TIMER_DOWN = 0x6,
    HE_TYPE_TENTHS_TIMER_UP = 0x7,
    HE_TYPE_CLOCK_DOWN = 0x8,
    HE_TYPE_CLOCK_UP = 0x9,
    HE_TYPE_WAYPOINT = 0xA,
    HE_TYPE_COUNT = 0xB,
};
#else
#error "HUD element layout requires exactly one of KISAK_MP or KISAK_SP"
#endif

#if defined(KISAK_MP)
struct hudelem_s // sizeof=0xA0
{
    he_type_t type;
    float x;
    float y;
    float z;
    int32_t targetEntNum;
    float fontScale;
    int32_t font;
    int32_t alignOrg;
    int32_t alignScreen;
    hudelem_color_t color;
    hudelem_color_t fromColor;
    int32_t fadeStartTime;
    int32_t fadeTime;
    int32_t label;
    int32_t width;
    int32_t height;
    int32_t materialIndex;
    int32_t offscreenMaterialIdx;
    int32_t fromWidth;
    int32_t fromHeight;
    int32_t scaleStartTime;
    int32_t scaleTime;
    float fromX;
    float fromY;
    int32_t fromAlignOrg;
    int32_t fromAlignScreen;
    int32_t moveStartTime;
    int32_t moveTime;
    int32_t time;
    int32_t duration;
    float value;
    int32_t text;
    float sort;
    hudelem_color_t glowColor;
    int32_t fxBirthTime;
    int32_t fxLetterTime;
    int32_t fxDecayStartTime;
    int32_t fxDecayDuration;
    int32_t soundID;
    int32_t flags;
};
RUNTIME_SIZE(hudelem_s, 0xA0, 0xA0);

struct playerState_s_hud // sizeof=0x26C0
{
    hudelem_s current[31];
    hudelem_s archival[31];
};
RUNTIME_SIZE(playerState_s_hud, 0x26C0, 0x26C0);
#elif defined(KISAK_SP)
struct hudelem_s
{
    he_type_t type;
    float x;
    float y;
    float z;
    int targetEntNum;
    float fontScale;
    float fromFontScale;
    int fontScaleStartTime;
    int fontScaleTime;
    int font;
    int alignOrg;
    int alignScreen;
    hudelem_color_t color;
    hudelem_color_t fromColor;
    int fadeStartTime;
    int fadeTime;
    int label;
    int width;
    int height;
    int materialIndex;
    int offscreenMaterialIdx;
    int fromWidth;
    int fromHeight;
    int scaleStartTime;
    int scaleTime;
    float fromX;
    float fromY;
    int fromAlignOrg;
    int fromAlignScreen;
    int moveStartTime;
    int moveTime;
    int time;
    int duration;
    float value;
    int text;
    float sort;
    hudelem_color_t glowColor;
    int fxBirthTime;
    int fxLetterTime;
    int fxDecayStartTime;
    int fxDecayDuration;
    int soundID;
    int flags;
};
RUNTIME_SIZE(hudelem_s, 0xAC, 0xAC);

struct playerState_s_hud
{
    hudelem_s elem[256];
};
RUNTIME_SIZE(playerState_s_hud, 0xAC00, 0xAC00);
#endif
