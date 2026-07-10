#pragma once

#ifndef KISAK_MP
#error This File is MultiPlayer Only
#endif

#include <bgame/bg_local.h>
#include <universal/kisak_abi.h>

// Multiplayer state shared by the game, server, snapshot, and client layers.
// Keep these definitions independent of desktop client/media headers so a
// headless dedicated server can compile its authoritative game state directly.
enum ConstStringOffsets
{
    CS_GAME_VERSION           = 2,
    CS_MESSAGE                = 3,
    CS_SCORES1                = 4,
    CS_SCORES2                = 5,
    CS_CULLDIST               = 6,
    CS_SUNLIGHT               = 7,
    CS_SUNDIR                 = 8,
    CS_FOGVARS                = 9,
    CS_MOTD                   = 10,
    CS_GAMEENDTIME            = 11,
    CS_MAPCENTER              = 12,
    CS_VOTE_TIME              = 13,
    CS_VOTE_STRING            = 14,
    CS_VOTE_YES               = 15,
    CS_VOTE_NO                = 16,
    CS_VOTE_MAPNAME           = 17,
    CS_VOTE_GAMETYPE          = 18,
    CS_MULTI_MAPWINNER        = 19,
    CS_CODINFO                = 20,
    CS_CODINFO_LAST           = 147,
    CS_CODINFO_VALUE          = 148,
    CS_CODINFO_VALUE_LAST     = 275,
    CS_ENEMY_CROSSHAIR        = 276,
    CS_USE_TRIG_STRINGS       = 277,
    CS_USE_TRIG_STRINGS_LAST  = 308,
    CS_LOCALIZED_STRINGS      = 309,
    CS_LOCALIZED_STRINGS_LAST = 820,
    CS_CASE_INSENSITIVE_BEGIN = 821,
    CS_AMBIENT                = 821,
    CS_NORTHYAW               = 822,
    CS_MINIMAP                = 823,
    CS_VISIONSET_NAKED        = 824,
    CS_VISIONSET_NIGHT        = 825,
    CS_NIGHTVISION            = 826,
    CS_LOC_SEL_MTLS           = 827,
    CS_LOC_SEL_MTLS_LAST      = 829,
    CS_MODELS                 = 830,
    CS_MODELS_LAST            = 1341,
    CS_SOUNDALIASES           = 1342,
    CS_SOUNDALIASES_LAST      = 1597,
    CS_EFFECT_NAMES           = 1598,
    CS_EFFECT_NAMES_LAST      = 1697,
    CS_EFFECT_TAGS            = 1698,
    CS_EFFECT_TAGS_LAST       = 1953,
    CS_SHELLSHOCKS            = 1954,
    CS_SHELLSHOCKS_LAST       = 1969,
    CS_SCRIPT_MENUS           = 1970,
    CS_SCRIPT_MENUS_LAST      = 2001,
    CS_SERVER_MATERIALS       = 2002,
    CS_SERVER_MATERIALS_LAST  = 2257,
    CS_WEAPONFILES            = 2258,
    CS_STATUS_ICONS           = 2259,
    CS_STATUS_ICONS_LAST      = 2266,
    CS_HEAD_ICONS             = 2267,
    CS_HEAD_ICONS_LAST        = 2281,
    CS_TAGS                   = 2282,
    CS_TAGS_LAST              = 2313,
    CS_ITEMS                  = 2314,
    CS_MAX                    = 2315,
};

enum sessionState_t : __int32
{
    SESS_STATE_PLAYING = 0x0,
    SESS_STATE_DEAD = 0x1,
    SESS_STATE_SPECTATOR = 0x2,
    SESS_STATE_INTERMISSION = 0x3,
};

enum clientConnected_t : __int32
{
    CON_DISCONNECTED = 0x0,
    CON_CONNECTING = 0x1,
    CON_CONNECTED = 0x2,
};

struct playerTeamState_t // sizeof=0x4
{
    int32_t location;
};

struct clientState_s // sizeof=0x64
{
    int32_t clientIndex;
    team_t team;
    int32_t modelindex;
    int32_t attachModelIndex[6];
    int32_t attachTagIndex[6];
    char name[16];
    float maxSprintTimeMultiplier;
    int32_t rank;
    int32_t prestige;
    int32_t perks;
    int32_t attachedVehEntNum;
    int32_t attachedVehSlotIndex;
};
RUNTIME_SIZE(clientState_s, 0x64, 0x64);

struct clientSession_t // sizeof=0x110
{
    sessionState_t sessionState;
    int32_t forceSpectatorClient;
    int32_t killCamEntity;
    int32_t status_icon;
    int32_t archiveTime;
    int32_t score;
    int32_t deaths;
    int32_t kills;
    int32_t assists;
    uint16_t scriptPersId;
    clientConnected_t connected;
    usercmd_s cmd;
    usercmd_s oldcmd;
    int32_t localClient;
    int32_t predictItemPickup;
    char newnetname[16];
    int32_t maxHealth;
    int32_t enterTime;
    playerTeamState_t teamState;
    int32_t voteCount;
    int32_t teamVoteCount;
    float moveSpeedScaleMultiplier;
    int32_t viewmodelIndex;
    int32_t noSpectate;
    int32_t teamInfo;
    clientState_s cs;
    int32_t psOffsetTime;
};
RUNTIME_SIZE(clientSession_t, 0x110, 0x110);

struct gclient_s // sizeof=0x3184
{
    playerState_s ps;
    clientSession_t sess;
    int32_t spectatorClient;
    int32_t noclip;
    int32_t ufo;
    int32_t bFrozen;
    int32_t lastCmdTime;
    int32_t buttons;
    int32_t oldbuttons;
    int32_t latched_buttons;
    int32_t buttonsSinceLastFrame;
    float oldOrigin[3];
    float fGunPitch;
    float fGunYaw;
    int32_t damage_blood;
    float damage_from[3];
    int32_t damage_fromWorld;
    int32_t accurateCount;
    int32_t accuracy_shots;
    int32_t accuracy_hits;
    int32_t inactivityTime;
    int32_t inactivityWarning;
    int32_t lastVoiceTime;
    int32_t switchTeamTime;
    float currentAimSpreadScale;
    gentity_s *persistantPowerup;
    int32_t portalID;
    int32_t dropWeaponTime;
    int32_t sniperRifleFiredTime;
    float sniperRifleMuzzleYaw;
    int32_t PCSpecialPickedUpCount;
    EntHandle useHoldEntity;
    int32_t useHoldTime;
    int32_t useButtonDone;
    int32_t iLastCompassPlayerInfoEnt;
    int32_t compassPingTime;
    int32_t damageTime;
    float v_dmg_roll;
    float v_dmg_pitch;
    float swayViewAngles[3];
    float swayOffset[3];
    float swayAngles[3];
    float vLastMoveAng[3];
    float fLastIdleFactor;
    float vGunOffset[3];
    float vGunSpeed[3];
    int32_t weapIdleTime;
    int32_t lastServerTime;
    int32_t lastSpawnTime;
    uint32_t lastWeapon;
    bool previouslyFiring;
    bool previouslyUsingNightVision;
    bool previouslySprinting;
    int32_t hasRadar;
    int32_t lastStand;
    int32_t lastStandTime;
};
RUNTIME_SIZE(gclient_s, 0x3184, 0x3188);

struct VoicePacket_t // sizeof=0x108
{
    uint8_t talker;
    uint8_t data[256];
    int32_t dataSize;
};
RUNTIME_SIZE(VoicePacket_t, 0x108, 0x108);
