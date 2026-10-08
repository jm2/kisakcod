#include "bg_local.h"
#include "bg_public.h"
#include <qcommon/mem_track.h>
#include <database/database.h>
#include <database/db_validation.h>
#include <universal/q_parse.h>
#include <universal/com_memory.h>
#include <universal/com_files.h>
#include <universal/com_sndalias.h>
#include <universal/surfaceflags.h>

//int surfaceTypeSoundListCount 828010f0     bg_weapons_load_obj.obj
//struct SurfaceTypeSoundList *surfaceTypeSoundLists 828011f8     bg_weapons_load_obj.obj

uint32_t g_playerAnimTypeNamesCount;

SurfaceTypeSoundList surfaceTypeSoundLists[16];

const char *stickinessNames[4] =
{
  "Don't stick",
  "Stick to all",
  "Stick to ground",
  "Stick to ground, maintain yaw"
}; // idb
const char *weapIconRatioNames[3] = { "1:1", "2:1", "4:1" }; // idb
const char *ammoCounterClipNames[7] =
{
  "None",
  "Magazine",
  "ShortMagazine",
  "Shotgun",
  "Rocket",
  "Beltfed",
  "AltWeapon"
}; // idb
const char *overlayInterfaceNames[3] = { "None", "Javelin", "Turret Scope" }; // idb
const char *szWeapFireTypeNames[5] =
{
  "Full Auto",
  "Single Shot",
  "2-Round Burst",
  "3-Round Burst",
  "4-Round Burst"
}; // idb
const char *szWeapInventoryTypeNames[4] = { "primary", "offhand", "item", "altmode" }; // idb
const char *penetrateTypeNames[4] = { "none", "small", "medium", "large" }; // idb
const char *szWeapOverlayReticleNames[2] = { "none", "crosshair" }; // idb
const char *szWeapStanceNames[3] = { "stand", "duck", "prone" }; // idb
const char *accuracyDirName[3] = { "aivsai", "aivsplayer", NULL }; // idb
const char *activeReticleNames[3] = { "None", "Pip-On-A-Stick", "Bouncing diamond" }; // idb
const char *szWeapTypeNames[4] = { "bullet", "grenade", "projectile", "binoculars" }; // idb
const char *guidedMissileNames[4] = { "None", "Sidewinder", "Hellfire", "Javelin" }; // idb
const char *offhandClassNames[4] = { "None", "Frag Grenade", "Smoke Grenade", "Flash Grenade" }; // idb
const char *szProjectileExplosionNames[7] = { "grenade", "rocket", "flashbang", "none", "dud", "smoke", "heavy explosive" }; // idb

const char *impactTypeNames[9] =
{
  "none",
  "bullet_small",
  "bullet_large",
  "bullet_ap",
  "shotgun",
  "grenade_bounce",
  "grenade_explode",
  "rocket_explode",
  "projectile_dud"
}; // idb

cspField_t weaponDefFields[502] =
{
  { "displayName", offsetof(WeaponDef, szDisplayName), 0 },
  { "AIOverlayDescription", offsetof(WeaponDef, szOverlayName), 0 },
  { "modeName", offsetof(WeaponDef, szModeName), 0 },
  { "playerAnimType", offsetof(WeaponDef, playerAnimType), 20 },
  { "gunModel", offsetof(WeaponDef, gunXModel[0]), 9 },
  { "gunModel2", offsetof(WeaponDef, gunXModel[1]), 9 },
  { "gunModel3", offsetof(WeaponDef, gunXModel[2]), 9 },
  { "gunModel4", offsetof(WeaponDef, gunXModel[3]), 9 },
  { "gunModel5", offsetof(WeaponDef, gunXModel[4]), 9 },
  { "gunModel6", offsetof(WeaponDef, gunXModel[5]), 9 },
  { "gunModel7", offsetof(WeaponDef, gunXModel[6]), 9 },
  { "gunModel8", offsetof(WeaponDef, gunXModel[7]), 9 },
  { "gunModel9", offsetof(WeaponDef, gunXModel[8]), 9 },
  { "gunModel10", offsetof(WeaponDef, gunXModel[9]), 9 },
  { "gunModel11", offsetof(WeaponDef, gunXModel[10]), 9 },
  { "gunModel12", offsetof(WeaponDef, gunXModel[11]), 9 },
  { "gunModel13", offsetof(WeaponDef, gunXModel[12]), 9 },
  { "gunModel14", offsetof(WeaponDef, gunXModel[13]), 9 },
  { "gunModel15", offsetof(WeaponDef, gunXModel[14]), 9 },
  { "gunModel16", offsetof(WeaponDef, gunXModel[15]), 9 },
  { "handModel", offsetof(WeaponDef, handXModel), 9 },
  { "hideTags", offsetof(WeaponDef, hideTags[0]), 33 },
  { "notetrackSoundMap", offsetof(WeaponDef, notetrackSoundMapKeys[0]), 34 },
  { "idleAnim", offsetof(WeaponDef, szXAnims[1]), 0 },
  { "emptyIdleAnim", offsetof(WeaponDef, szXAnims[2]), 0 },
  { "fireAnim", offsetof(WeaponDef, szXAnims[3]), 0 },
  { "holdFireAnim", offsetof(WeaponDef, szXAnims[4]), 0 },
  { "lastShotAnim", offsetof(WeaponDef, szXAnims[5]), 0 },
  { "detonateAnim", offsetof(WeaponDef, szXAnims[25]), 0 },
  { "rechamberAnim", offsetof(WeaponDef, szXAnims[6]), 0 },
  { "meleeAnim", offsetof(WeaponDef, szXAnims[7]), 0 },
  { "meleeChargeAnim", offsetof(WeaponDef, szXAnims[8]), 0 },
  { "reloadAnim", offsetof(WeaponDef, szXAnims[9]), 0 },
  { "reloadEmptyAnim", offsetof(WeaponDef, szXAnims[10]), 0 },
  { "reloadStartAnim", offsetof(WeaponDef, szXAnims[11]), 0 },
  { "reloadEndAnim", offsetof(WeaponDef, szXAnims[12]), 0 },
  { "raiseAnim", offsetof(WeaponDef, szXAnims[13]), 0 },
  { "dropAnim", offsetof(WeaponDef, szXAnims[15]), 0 },
  { "firstRaiseAnim", offsetof(WeaponDef, szXAnims[14]), 0 },
  { "altRaiseAnim", offsetof(WeaponDef, szXAnims[16]), 0 },
  { "altDropAnim", offsetof(WeaponDef, szXAnims[17]), 0 },
  { "quickRaiseAnim", offsetof(WeaponDef, szXAnims[18]), 0 },
  { "quickDropAnim", offsetof(WeaponDef, szXAnims[19]), 0 },
  { "emptyRaiseAnim", offsetof(WeaponDef, szXAnims[20]), 0 },
  { "emptyDropAnim", offsetof(WeaponDef, szXAnims[21]), 0 },
  { "sprintInAnim", offsetof(WeaponDef, szXAnims[22]), 0 },
  { "sprintLoopAnim", offsetof(WeaponDef, szXAnims[23]), 0 },
  { "sprintOutAnim", offsetof(WeaponDef, szXAnims[24]), 0 },
  { "nightVisionWearAnim", offsetof(WeaponDef, szXAnims[26]), 0 },
  { "nightVisionRemoveAnim", offsetof(WeaponDef, szXAnims[27]), 0 },
  { "adsFireAnim", offsetof(WeaponDef, szXAnims[28]), 0 },
  { "adsLastShotAnim", offsetof(WeaponDef, szXAnims[29]), 0 },
  { "adsRechamberAnim", offsetof(WeaponDef, szXAnims[30]), 0 },
  { "adsUpAnim", offsetof(WeaponDef, szXAnims[31]), 0 },
  { "adsDownAnim", offsetof(WeaponDef, szXAnims[32]), 0 },
  { "script", offsetof(WeaponDef, szScript), 0 },
  { "weaponType", offsetof(WeaponDef, weapType), 12 },
  { "weaponClass", offsetof(WeaponDef, weapClass), 13 },
  { "penetrateType", offsetof(WeaponDef, penetrateType), 15 },
  { "impactType", offsetof(WeaponDef, impactType), 16 },
  { "inventoryType", offsetof(WeaponDef, inventoryType), 26 },
  { "fireType", offsetof(WeaponDef, fireType), 27 },
  { "offhandClass", offsetof(WeaponDef, offhandClass), 19 },
  { "viewFlashEffect", offsetof(WeaponDef, viewFlashEffect), 8 },
  { "worldFlashEffect", offsetof(WeaponDef, worldFlashEffect), 8 },
  { "pickupSound", offsetof(WeaponDef, pickupSound), 11 },
  { "pickupSoundPlayer", offsetof(WeaponDef, pickupSoundPlayer), 11 },
  { "ammoPickupSound", offsetof(WeaponDef, ammoPickupSound), 11 },
  { "ammoPickupSoundPlayer", offsetof(WeaponDef, ammoPickupSoundPlayer), 11 },
  { "projectileSound", offsetof(WeaponDef, projectileSound), 11 },
  { "pullbackSound", offsetof(WeaponDef, pullbackSound), 11 },
  { "pullbackSoundPlayer", offsetof(WeaponDef, pullbackSoundPlayer), 11 },
  { "fireSound", offsetof(WeaponDef, fireSound), 11 },
  { "fireSoundPlayer", offsetof(WeaponDef, fireSoundPlayer), 11 },
  { "loopFireSound", offsetof(WeaponDef, fireLoopSound), 11 },
  { "loopFireSoundPlayer", offsetof(WeaponDef, fireLoopSoundPlayer), 11 },
  { "stopFireSound", offsetof(WeaponDef, fireStopSound), 11 },
  { "stopFireSoundPlayer", offsetof(WeaponDef, fireStopSoundPlayer), 11 },
  { "lastShotSound", offsetof(WeaponDef, fireLastSound), 11 },
  { "lastShotSoundPlayer", offsetof(WeaponDef, fireLastSoundPlayer), 11 },
  { "emptyFireSound", offsetof(WeaponDef, emptyFireSound), 11 },
  { "emptyFireSoundPlayer", offsetof(WeaponDef, emptyFireSoundPlayer), 11 },
  { "meleeSwipeSound", offsetof(WeaponDef, meleeSwipeSound), 11 },
  { "meleeSwipeSoundPlayer", offsetof(WeaponDef, meleeSwipeSoundPlayer), 11 },
  { "meleeHitSound", offsetof(WeaponDef, meleeHitSound), 11 },
  { "meleeMissSound", offsetof(WeaponDef, meleeMissSound), 11 },
  { "rechamberSound", offsetof(WeaponDef, rechamberSound), 11 },
  { "rechamberSoundPlayer", offsetof(WeaponDef, rechamberSoundPlayer), 11 },
  { "reloadSound", offsetof(WeaponDef, reloadSound), 11 },
  { "reloadSoundPlayer", offsetof(WeaponDef, reloadSoundPlayer), 11 },
  { "reloadEmptySound", offsetof(WeaponDef, reloadEmptySound), 11 },
  { "reloadEmptySoundPlayer", offsetof(WeaponDef, reloadEmptySoundPlayer), 11 },
  { "reloadStartSound", offsetof(WeaponDef, reloadStartSound), 11 },
  { "reloadStartSoundPlayer", offsetof(WeaponDef, reloadStartSoundPlayer), 11 },
  { "reloadEndSound", offsetof(WeaponDef, reloadEndSound), 11 },
  { "reloadEndSoundPlayer", offsetof(WeaponDef, reloadEndSoundPlayer), 11 },
  { "detonateSound", offsetof(WeaponDef, detonateSound), 11 },
  { "detonateSoundPlayer", offsetof(WeaponDef, detonateSoundPlayer), 11 },
  { "nightVisionWearSound", offsetof(WeaponDef, nightVisionWearSound), 11 },
  { "nightVisionWearSoundPlayer", offsetof(WeaponDef, nightVisionWearSoundPlayer), 11 },
  { "nightVisionRemoveSound", offsetof(WeaponDef, nightVisionRemoveSound), 11 },
  { "nightVisionRemoveSoundPlayer", offsetof(WeaponDef, nightVisionRemoveSoundPlayer), 11 },
  { "raiseSound", offsetof(WeaponDef, raiseSound), 11 },
  { "raiseSoundPlayer", offsetof(WeaponDef, raiseSoundPlayer), 11 },
  { "firstRaiseSound", offsetof(WeaponDef, firstRaiseSound), 11 },
  { "firstRaiseSoundPlayer", offsetof(WeaponDef, firstRaiseSoundPlayer), 11 },
  { "altSwitchSound", offsetof(WeaponDef, altSwitchSound), 11 },
  { "altSwitchSoundPlayer", offsetof(WeaponDef, altSwitchSoundPlayer), 11 },
  { "putawaySound", offsetof(WeaponDef, putawaySound), 11 },
  { "putawaySoundPlayer", offsetof(WeaponDef, putawaySoundPlayer), 11 },
  { "bounceSound", offsetof(WeaponDef, bounceSound), 23 },
  { "viewShellEjectEffect", offsetof(WeaponDef, viewShellEjectEffect), 8 },
  { "worldShellEjectEffect", offsetof(WeaponDef, worldShellEjectEffect), 8 },
  { "viewLastShotEjectEffect", offsetof(WeaponDef, viewLastShotEjectEffect), 8 },
  { "worldLastShotEjectEffect", offsetof(WeaponDef, worldLastShotEjectEffect), 8 },
  { "reticleCenter", offsetof(WeaponDef, reticleCenter), 10 },
  { "reticleSide", offsetof(WeaponDef, reticleSide), 10 },
  { "reticleCenterSize", offsetof(WeaponDef, iReticleCenterSize), 4 },
  { "reticleSideSize", offsetof(WeaponDef, iReticleSideSize), 4 },
  { "reticleMinOfs", offsetof(WeaponDef, iReticleMinOfs), 4 },
  { "activeReticleType", offsetof(WeaponDef, activeReticleType), 21 },
  { "standMoveF", offsetof(WeaponDef, vStandMove[0]), 6 },
  { "standMoveR", offsetof(WeaponDef, vStandMove[1]), 6 },
  { "standMoveU", offsetof(WeaponDef, vStandMove[2]), 6 },
  { "standRotP", offsetof(WeaponDef, vStandRot[0]), 6 },
  { "standRotY", offsetof(WeaponDef, vStandRot[1]), 6 },
  { "standRotR", offsetof(WeaponDef, vStandRot[2]), 6 },
  { "duckedOfsF", offsetof(WeaponDef, vDuckedOfs[0]), 6 },
  { "duckedOfsR", offsetof(WeaponDef, vDuckedOfs[1]), 6 },
  { "duckedOfsU", offsetof(WeaponDef, vDuckedOfs[2]), 6 },
  { "duckedMoveF", offsetof(WeaponDef, vDuckedMove[0]), 6 },
  { "duckedMoveR", offsetof(WeaponDef, vDuckedMove[1]), 6 },
  { "duckedMoveU", offsetof(WeaponDef, vDuckedMove[2]), 6 },
  { "duckedRotP", offsetof(WeaponDef, vDuckedRot[0]), 6 },
  { "duckedRotY", offsetof(WeaponDef, vDuckedRot[1]), 6 },
  { "duckedRotR", offsetof(WeaponDef, vDuckedRot[2]), 6 },
  { "proneOfsF", offsetof(WeaponDef, vProneOfs[0]), 6 },
  { "proneOfsR", offsetof(WeaponDef, vProneOfs[1]), 6 },
  { "proneOfsU", offsetof(WeaponDef, vProneOfs[2]), 6 },
  { "proneMoveF", offsetof(WeaponDef, vProneMove[0]), 6 },
  { "proneMoveR", offsetof(WeaponDef, vProneMove[1]), 6 },
  { "proneMoveU", offsetof(WeaponDef, vProneMove[2]), 6 },
  { "proneRotP", offsetof(WeaponDef, vProneRot[0]), 6 },
  { "proneRotY", offsetof(WeaponDef, vProneRot[1]), 6 },
  { "proneRotR", offsetof(WeaponDef, vProneRot[2]), 6 },
  { "posMoveRate", offsetof(WeaponDef, fPosMoveRate), 6 },
  { "posProneMoveRate", offsetof(WeaponDef, fPosProneMoveRate), 6 },
  { "standMoveMinSpeed", offsetof(WeaponDef, fStandMoveMinSpeed), 6 },
  { "duckedMoveMinSpeed", offsetof(WeaponDef, fDuckedMoveMinSpeed), 6 },
  { "proneMoveMinSpeed", offsetof(WeaponDef, fProneMoveMinSpeed), 6 },
  { "posRotRate", offsetof(WeaponDef, fPosRotRate), 6 },
  { "posProneRotRate", offsetof(WeaponDef, fPosProneRotRate), 6 },
  { "standRotMinSpeed", offsetof(WeaponDef, fStandRotMinSpeed), 6 },
  { "duckedRotMinSpeed", offsetof(WeaponDef, fDuckedRotMinSpeed), 6 },
  { "proneRotMinSpeed", offsetof(WeaponDef, fProneRotMinSpeed), 6 },
  { "worldModel", offsetof(WeaponDef, worldModel[0]), 9 },
  { "worldModel2", offsetof(WeaponDef, worldModel[1]), 9 },
  { "worldModel3", offsetof(WeaponDef, worldModel[2]), 9 },
  { "worldModel4", offsetof(WeaponDef, worldModel[3]), 9 },
  { "worldModel5", offsetof(WeaponDef, worldModel[4]), 9 },
  { "worldModel6", offsetof(WeaponDef, worldModel[5]), 9 },
  { "worldModel7", offsetof(WeaponDef, worldModel[6]), 9 },
  { "worldModel8", offsetof(WeaponDef, worldModel[7]), 9 },
  { "worldModel9", offsetof(WeaponDef, worldModel[8]), 9 },
  { "worldModel10", offsetof(WeaponDef, worldModel[9]), 9 },
  { "worldModel11", offsetof(WeaponDef, worldModel[10]), 9 },
  { "worldModel12", offsetof(WeaponDef, worldModel[11]), 9 },
  { "worldModel13", offsetof(WeaponDef, worldModel[12]), 9 },
  { "worldModel14", offsetof(WeaponDef, worldModel[13]), 9 },
  { "worldModel15", offsetof(WeaponDef, worldModel[14]), 9 },
  { "worldModel16", offsetof(WeaponDef, worldModel[15]), 9 },
  { "worldClipModel", offsetof(WeaponDef, worldClipModel), 9 },
  { "rocketModel", offsetof(WeaponDef, rocketModel), 9 },
  { "knifeModel", offsetof(WeaponDef, knifeModel), 9 },
  { "worldKnifeModel", offsetof(WeaponDef, worldKnifeModel), 9 },
  { "hudIcon", offsetof(WeaponDef, hudIcon), 10 },
  { "hudIconRatio", offsetof(WeaponDef, hudIconRatio), 29 },
  { "ammoCounterIcon", offsetof(WeaponDef, ammoCounterIcon), 10 },
  { "ammoCounterIconRatio", offsetof(WeaponDef, ammoCounterIconRatio), 30 },
  { "ammoCounterClip", offsetof(WeaponDef, ammoCounterClip), 28 },
  { "startAmmo", offsetof(WeaponDef, iStartAmmo), 4 },
  { "ammoName", offsetof(WeaponDef, szAmmoName), 0 },
  { "clipName", offsetof(WeaponDef, szClipName), 0 },
  { "maxAmmo", offsetof(WeaponDef, iMaxAmmo), 4 },
  { "clipSize", offsetof(WeaponDef, iClipSize), 4 },
  { "shotCount", offsetof(WeaponDef, shotCount), 4 },
  { "sharedAmmoCapName", offsetof(WeaponDef, szSharedAmmoCapName), 0 },
  { "sharedAmmoCap", offsetof(WeaponDef, iSharedAmmoCap), 4 },
  { "damage", offsetof(WeaponDef, damage), 4 },
  { "playerDamage", offsetof(WeaponDef, playerDamage), 4 },
  { "meleeDamage", offsetof(WeaponDef, iMeleeDamage), 4 },
  { "minDamage", offsetof(WeaponDef, minDamage), 4 },
  { "minPlayerDamage", offsetof(WeaponDef, minPlayerDamage), 4 },
  { "maxDamageRange", offsetof(WeaponDef, fMaxDamageRange), 6 },
  { "minDamageRange", offsetof(WeaponDef, fMinDamageRange), 6 },
  { "destabilizationRateTime", offsetof(WeaponDef, destabilizationRateTime), 6 },
  { "destabilizationCurvatureMax", offsetof(WeaponDef, destabilizationCurvatureMax), 6 },
  { "destabilizeDistance", offsetof(WeaponDef, destabilizeDistance), 4 },
  { "fireDelay", offsetof(WeaponDef, iFireDelay), 7 },
  { "meleeDelay", offsetof(WeaponDef, iMeleeDelay), 7 },
  { "meleeChargeDelay", offsetof(WeaponDef, meleeChargeDelay), 7 },
  { "fireTime", offsetof(WeaponDef, iFireTime), 7 },
  { "rechamberTime", offsetof(WeaponDef, iRechamberTime), 7 },
  { "rechamberBoltTime", offsetof(WeaponDef, iRechamberBoltTime), 7 },
  { "holdFireTime", offsetof(WeaponDef, iHoldFireTime), 7 },
  { "detonateTime", offsetof(WeaponDef, iDetonateTime), 7 },
  { "detonateDelay", offsetof(WeaponDef, iDetonateDelay), 7 },
  { "meleeTime", offsetof(WeaponDef, iMeleeTime), 7 },
  { "meleeChargeTime", offsetof(WeaponDef, meleeChargeTime), 7 },
  { "reloadTime", offsetof(WeaponDef, iReloadTime), 7 },
  { "reloadShowRocketTime", offsetof(WeaponDef, reloadShowRocketTime), 7 },
  { "reloadEmptyTime", offsetof(WeaponDef, iReloadEmptyTime), 7 },
  { "reloadAddTime", offsetof(WeaponDef, iReloadAddTime), 7 },
  { "reloadStartTime", offsetof(WeaponDef, iReloadStartTime), 7 },
  { "reloadStartAddTime", offsetof(WeaponDef, iReloadStartAddTime), 7 },
  { "reloadEndTime", offsetof(WeaponDef, iReloadEndTime), 7 },
  { "dropTime", offsetof(WeaponDef, iDropTime), 7 },
  { "raiseTime", offsetof(WeaponDef, iRaiseTime), 7 },
  { "altDropTime", offsetof(WeaponDef, iAltDropTime), 7 },
  { "altRaiseTime", offsetof(WeaponDef, iAltRaiseTime), 7 },
  { "quickDropTime", offsetof(WeaponDef, quickDropTime), 7 },
  { "quickRaiseTime", offsetof(WeaponDef, quickRaiseTime), 7 },
  { "firstRaiseTime", offsetof(WeaponDef, iFirstRaiseTime), 7 },
  { "emptyRaiseTime", offsetof(WeaponDef, iEmptyRaiseTime), 7 },
  { "emptyDropTime", offsetof(WeaponDef, iEmptyDropTime), 7 },
  { "sprintInTime", offsetof(WeaponDef, sprintInTime), 7 },
  { "sprintLoopTime", offsetof(WeaponDef, sprintLoopTime), 7 },
  { "sprintOutTime", offsetof(WeaponDef, sprintOutTime), 7 },
  { "nightVisionWearTime", offsetof(WeaponDef, nightVisionWearTime), 7 },
  { "nightVisionWearTimeFadeOutEnd", offsetof(WeaponDef, nightVisionWearTimeFadeOutEnd), 7 },
  { "nightVisionWearTimePowerUp", offsetof(WeaponDef, nightVisionWearTimePowerUp), 7 },
  { "nightVisionRemoveTime", offsetof(WeaponDef, nightVisionRemoveTime), 7 },
  { "nightVisionRemoveTimePowerDown", offsetof(WeaponDef, nightVisionRemoveTimePowerDown), 7 },
  { "nightVisionRemoveTimeFadeInStart", offsetof(WeaponDef, nightVisionRemoveTimeFadeInStart), 7 },
  { "fuseTime", offsetof(WeaponDef, fuseTime), 7 },
  { "aifuseTime", offsetof(WeaponDef, aiFuseTime), 7 },
  { "requireLockonToFire", offsetof(WeaponDef, requireLockonToFire), 5 },
  { "noAdsWhenMagEmpty", offsetof(WeaponDef, noAdsWhenMagEmpty), 5 },
  { "avoidDropCleanup", offsetof(WeaponDef, avoidDropCleanup), 5 },
  { "autoAimRange", offsetof(WeaponDef, autoAimRange), 6 },
  { "aimAssistRange", offsetof(WeaponDef, aimAssistRange), 6 },
  { "aimAssistRangeAds", offsetof(WeaponDef, aimAssistRangeAds), 6 },
  { "aimPadding", offsetof(WeaponDef, aimPadding), 6 },
  { "enemyCrosshairRange", offsetof(WeaponDef, enemyCrosshairRange), 6 },
  { "crosshairColorChange", offsetof(WeaponDef, crosshairColorChange), 5 },
  { "moveSpeedScale", offsetof(WeaponDef, moveSpeedScale), 6 },
  { "adsMoveSpeedScale", offsetof(WeaponDef, adsMoveSpeedScale), 6 },
  { "sprintDurationScale", offsetof(WeaponDef, sprintDurationScale), 6 },
  { "idleCrouchFactor", offsetof(WeaponDef, fIdleCrouchFactor), 6 },
  { "idleProneFactor", offsetof(WeaponDef, fIdleProneFactor), 6 },
  { "gunMaxPitch", offsetof(WeaponDef, fGunMaxPitch), 6 },
  { "gunMaxYaw", offsetof(WeaponDef, fGunMaxYaw), 6 },
  { "swayMaxAngle", offsetof(WeaponDef, swayMaxAngle), 6 },
  { "swayLerpSpeed", offsetof(WeaponDef, swayLerpSpeed), 6 },
  { "swayPitchScale", offsetof(WeaponDef, swayPitchScale), 6 },
  { "swayYawScale", offsetof(WeaponDef, swayYawScale), 6 },
  { "swayHorizScale", offsetof(WeaponDef, swayHorizScale), 6 },
  { "swayVertScale", offsetof(WeaponDef, swayVertScale), 6 },
  { "swayShellShockScale", offsetof(WeaponDef, swayShellShockScale), 6 },
  { "adsSwayMaxAngle", offsetof(WeaponDef, adsSwayMaxAngle), 6 },
  { "adsSwayLerpSpeed", offsetof(WeaponDef, adsSwayLerpSpeed), 6 },
  { "adsSwayPitchScale", offsetof(WeaponDef, adsSwayPitchScale), 6 },
  { "adsSwayYawScale", offsetof(WeaponDef, adsSwayYawScale), 6 },
  { "adsSwayHorizScale", offsetof(WeaponDef, adsSwayHorizScale), 6 },
  { "adsSwayVertScale", offsetof(WeaponDef, adsSwayVertScale), 6 },
  { "rifleBullet", offsetof(WeaponDef, bRifleBullet), 5 },
  { "armorPiercing", offsetof(WeaponDef, armorPiercing), 5 },
  { "boltAction", offsetof(WeaponDef, bBoltAction), 5 },
  { "aimDownSight", offsetof(WeaponDef, aimDownSight), 5 },
  { "rechamberWhileAds", offsetof(WeaponDef, bRechamberWhileAds), 5 },
  { "adsViewErrorMin", offsetof(WeaponDef, adsViewErrorMin), 6 },
  { "adsViewErrorMax", offsetof(WeaponDef, adsViewErrorMax), 6 },
  { "clipOnly", offsetof(WeaponDef, bClipOnly), 5 },
  { "cookOffHold", offsetof(WeaponDef, bCookOffHold), 5 },
  { "adsFire", offsetof(WeaponDef, adsFireOnly), 5 },
  { "cancelAutoHolsterWhenEmpty", offsetof(WeaponDef, cancelAutoHolsterWhenEmpty), 5 },
  { "suppressAmmoReserveDisplay", offsetof(WeaponDef, suppressAmmoReserveDisplay), 5 },
  { "enhanced", offsetof(WeaponDef, enhanced), 5 },
  { "laserSightDuringNightvision", offsetof(WeaponDef, laserSightDuringNightvision), 5 },
  { "killIcon", offsetof(WeaponDef, killIcon), 10 },
  { "killIconRatio", offsetof(WeaponDef, killIconRatio), 31 },
  { "flipKillIcon", offsetof(WeaponDef, flipKillIcon), 5 },
  { "dpadIcon", offsetof(WeaponDef, dpadIcon), 10 },
  { "dpadIconRatio", offsetof(WeaponDef, dpadIconRatio), 32 },
  { "noPartialReload", offsetof(WeaponDef, bNoPartialReload), 5 },
  { "segmentedReload", offsetof(WeaponDef, bSegmentedReload), 5 },
  { "reloadAmmoAdd", offsetof(WeaponDef, iReloadAmmoAdd), 4 },
  { "reloadStartAdd", offsetof(WeaponDef, iReloadStartAdd), 4 },
  { "altWeapon", offsetof(WeaponDef, szAltWeaponName), 0 },
  { "dropAmmoMin", offsetof(WeaponDef, iDropAmmoMin), 4 },
  { "dropAmmoMax", offsetof(WeaponDef, iDropAmmoMax), 4 },
  { "blocksProne", offsetof(WeaponDef, blocksProne), 5 },
  { "silenced", offsetof(WeaponDef, silenced), 5 },
  { "explosionRadius", offsetof(WeaponDef, iExplosionRadius), 4 },
  { "explosionRadiusMin", offsetof(WeaponDef, iExplosionRadiusMin), 4 },
  { "explosionInnerDamage", offsetof(WeaponDef, iExplosionInnerDamage), 4 },
  { "explosionOuterDamage", offsetof(WeaponDef, iExplosionOuterDamage), 4 },
  { "damageConeAngle", offsetof(WeaponDef, damageConeAngle), 6 },
  { "projectileSpeed", offsetof(WeaponDef, iProjectileSpeed), 4 },
  { "projectileSpeedUp", offsetof(WeaponDef, iProjectileSpeedUp), 4 },
  { "projectileSpeedForward", offsetof(WeaponDef, iProjectileSpeedForward), 4 },
  { "projectileActivateDist", offsetof(WeaponDef, iProjectileActivateDist), 4 },
  { "projectileLifetime", offsetof(WeaponDef, projLifetime), 6 },
  { "timeToAccelerate", offsetof(WeaponDef, timeToAccelerate), 6 },
  { "projectileCurvature", offsetof(WeaponDef, projectileCurvature), 6 },
  { "projectileModel", offsetof(WeaponDef, projectileModel), 9 },
  { "projExplosionType", offsetof(WeaponDef, projExplosion), 18 },
  { "projExplosionEffect", offsetof(WeaponDef, projExplosionEffect), 8 },
  { "projExplosionEffectForceNormalUp", offsetof(WeaponDef, projExplosionEffectForceNormalUp), 5 },
  { "projExplosionSound", offsetof(WeaponDef, projExplosionSound), 11 },
  { "projDudEffect", offsetof(WeaponDef, projDudEffect), 8 },
  { "projDudSound", offsetof(WeaponDef, projDudSound), 11 },
  { "projImpactExplode", offsetof(WeaponDef, bProjImpactExplode), 5 },
  { "stickiness", offsetof(WeaponDef, stickiness), 24 },
  { "hasDetonator", offsetof(WeaponDef, hasDetonator), 5 },
  { "timedDetonation", offsetof(WeaponDef, timedDetonation), 5 },
  { "rotate", offsetof(WeaponDef, rotate), 5 },
  { "holdButtonToThrow", offsetof(WeaponDef, holdButtonToThrow), 5 },
  { "freezeMovementWhenFiring", offsetof(WeaponDef, freezeMovementWhenFiring), 5 },
  { "lowAmmoWarningThreshold", offsetof(WeaponDef, lowAmmoWarningThreshold), 6 },
  { "parallelDefaultBounce", offsetof(WeaponDef, parallelBounce[0]), 6 },
  { "parallelBarkBounce", offsetof(WeaponDef, parallelBounce[1]), 6 },
  { "parallelBrickBounce", offsetof(WeaponDef, parallelBounce[2]), 6 },
  { "parallelCarpetBounce", offsetof(WeaponDef, parallelBounce[3]), 6 },
  { "parallelClothBounce", offsetof(WeaponDef, parallelBounce[4]), 6 },
  { "parallelConcreteBounce", offsetof(WeaponDef, parallelBounce[5]), 6 },
  { "parallelDirtBounce", offsetof(WeaponDef, parallelBounce[6]), 6 },
  { "parallelFleshBounce", offsetof(WeaponDef, parallelBounce[7]), 6 },
  { "parallelFoliageBounce", offsetof(WeaponDef, parallelBounce[8]), 6 },
  { "parallelGlassBounce", offsetof(WeaponDef, parallelBounce[9]), 6 },
  { "parallelGrassBounce", offsetof(WeaponDef, parallelBounce[10]), 6 },
  { "parallelGravelBounce", offsetof(WeaponDef, parallelBounce[11]), 6 },
  { "parallelIceBounce", offsetof(WeaponDef, parallelBounce[12]), 6 },
  { "parallelMetalBounce", offsetof(WeaponDef, parallelBounce[13]), 6 },
  { "parallelMudBounce", offsetof(WeaponDef, parallelBounce[14]), 6 },
  { "parallelPaperBounce", offsetof(WeaponDef, parallelBounce[15]), 6 },
  { "parallelPlasterBounce", offsetof(WeaponDef, parallelBounce[16]), 6 },
  { "parallelRockBounce", offsetof(WeaponDef, parallelBounce[17]), 6 },
  { "parallelSandBounce", offsetof(WeaponDef, parallelBounce[18]), 6 },
  { "parallelSnowBounce", offsetof(WeaponDef, parallelBounce[19]), 6 },
  { "parallelWaterBounce", offsetof(WeaponDef, parallelBounce[20]), 6 },
  { "parallelWoodBounce", offsetof(WeaponDef, parallelBounce[21]), 6 },
  { "parallelAsphaltBounce", offsetof(WeaponDef, parallelBounce[22]), 6 },
  { "parallelCeramicBounce", offsetof(WeaponDef, parallelBounce[23]), 6 },
  { "parallelPlasticBounce", offsetof(WeaponDef, parallelBounce[24]), 6 },
  { "parallelRubberBounce", offsetof(WeaponDef, parallelBounce[25]), 6 },
  { "parallelCushionBounce", offsetof(WeaponDef, parallelBounce[26]), 6 },
  { "parallelFruitBounce", offsetof(WeaponDef, parallelBounce[27]), 6 },
  { "parallelPaintedMetalBounce", offsetof(WeaponDef, parallelBounce[28]), 6 },
  { "perpendicularDefaultBounce", offsetof(WeaponDef, perpendicularBounce[0]), 6 },
  { "perpendicularBarkBounce", offsetof(WeaponDef, perpendicularBounce[1]), 6 },
  { "perpendicularBrickBounce", offsetof(WeaponDef, perpendicularBounce[2]), 6 },
  { "perpendicularCarpetBounce", offsetof(WeaponDef, perpendicularBounce[3]), 6 },
  { "perpendicularClothBounce", offsetof(WeaponDef, perpendicularBounce[4]), 6 },
  { "perpendicularConcreteBounce", offsetof(WeaponDef, perpendicularBounce[5]), 6 },
  { "perpendicularDirtBounce", offsetof(WeaponDef, perpendicularBounce[6]), 6 },
  { "perpendicularFleshBounce", offsetof(WeaponDef, perpendicularBounce[7]), 6 },
  { "perpendicularFoliageBounce", offsetof(WeaponDef, perpendicularBounce[8]), 6 },
  { "perpendicularGlassBounce", offsetof(WeaponDef, perpendicularBounce[9]), 6 },
  { "perpendicularGrassBounce", offsetof(WeaponDef, perpendicularBounce[10]), 6 },
  { "perpendicularGravelBounce", offsetof(WeaponDef, perpendicularBounce[11]), 6 },
  { "perpendicularIceBounce", offsetof(WeaponDef, perpendicularBounce[12]), 6 },
  { "perpendicularMetalBounce", offsetof(WeaponDef, perpendicularBounce[13]), 6 },
  { "perpendicularMudBounce", offsetof(WeaponDef, perpendicularBounce[14]), 6 },
  { "perpendicularPaperBounce", offsetof(WeaponDef, perpendicularBounce[15]), 6 },
  { "perpendicularPlasterBounce", offsetof(WeaponDef, perpendicularBounce[16]), 6 },
  { "perpendicularRockBounce", offsetof(WeaponDef, perpendicularBounce[17]), 6 },
  { "perpendicularSandBounce", offsetof(WeaponDef, perpendicularBounce[18]), 6 },
  { "perpendicularSnowBounce", offsetof(WeaponDef, perpendicularBounce[19]), 6 },
  { "perpendicularWaterBounce", offsetof(WeaponDef, perpendicularBounce[20]), 6 },
  { "perpendicularWoodBounce", offsetof(WeaponDef, perpendicularBounce[21]), 6 },
  { "perpendicularAsphaltBounce", offsetof(WeaponDef, perpendicularBounce[22]), 6 },
  { "perpendicularCeramicBounce", offsetof(WeaponDef, parallelBounce[23]), 6 },
  { "perpendicularPlasticBounce", offsetof(WeaponDef, parallelBounce[24]), 6 },
  { "perpendicularRubberBounce", offsetof(WeaponDef, parallelBounce[25]), 6 },
  { "perpendicularCushionBounce", offsetof(WeaponDef, perpendicularBounce[26]), 6 },
  { "perpendicularFruitBounce", offsetof(WeaponDef, perpendicularBounce[27]), 6 },
  { "perpendicularPaintedMetalBounce", offsetof(WeaponDef, perpendicularBounce[28]), 6 },
  { "projTrailEffect", offsetof(WeaponDef, projTrailEffect), 8 },
  { "projectileRed", offsetof(WeaponDef, vProjectileColor[0]), 6 },
  { "projectileGreen", offsetof(WeaponDef, vProjectileColor[1]), 6 },
  { "projectileBlue", offsetof(WeaponDef, vProjectileColor[2]), 6 },
  { "guidedMissileType", offsetof(WeaponDef, guidedMissileType), 22 },
  { "maxSteeringAccel", offsetof(WeaponDef, maxSteeringAccel), 6 },
  { "projIgnitionDelay", offsetof(WeaponDef, projIgnitionDelay), 4 },
  { "projIgnitionEffect", offsetof(WeaponDef, projIgnitionEffect), 8 },
  { "projIgnitionSound", offsetof(WeaponDef, projIgnitionSound), 11 },
  { "adsTransInTime", offsetof(WeaponDef, iAdsTransInTime), 7 },
  { "adsTransOutTime", offsetof(WeaponDef, iAdsTransOutTime), 7 },
  { "adsIdleAmount", offsetof(WeaponDef, fAdsIdleAmount), 6 },
  { "adsIdleSpeed", offsetof(WeaponDef, adsIdleSpeed), 6 },
  { "adsZoomFov", offsetof(WeaponDef, fAdsZoomFov), 6 },
  { "adsZoomInFrac", offsetof(WeaponDef, fAdsZoomInFrac), 6 },
  { "adsZoomOutFrac", offsetof(WeaponDef, fAdsZoomOutFrac), 6 },
  { "adsOverlayShader", offsetof(WeaponDef, overlayMaterial), 10 },
  { "adsOverlayShaderLowRes", offsetof(WeaponDef, overlayMaterialLowRes), 10 },
  { "adsOverlayReticle", offsetof(WeaponDef, overlayReticle), 14 },
  { "adsOverlayInterface", offsetof(WeaponDef, overlayInterface), 25 },
  { "adsOverlayWidth", offsetof(WeaponDef, overlayWidth), 6 },
  { "adsOverlayHeight", offsetof(WeaponDef, overlayHeight), 6 },
  { "adsBobFactor", offsetof(WeaponDef, fAdsBobFactor), 6 },
  { "adsViewBobMult", offsetof(WeaponDef, fAdsViewBobMult), 6 },
  { "adsAimPitch", offsetof(WeaponDef, fAdsAimPitch), 6 },
  { "adsCrosshairInFrac", offsetof(WeaponDef, fAdsCrosshairInFrac), 6 },
  { "adsCrosshairOutFrac", offsetof(WeaponDef, fAdsCrosshairOutFrac), 6 },
  { "adsReloadTransTime", offsetof(WeaponDef, iPositionReloadTransTime), 7 },
  { "adsGunKickReducedKickBullets", offsetof(WeaponDef, adsGunKickReducedKickBullets), 4 },
  { "adsGunKickReducedKickPercent", offsetof(WeaponDef, adsGunKickReducedKickPercent), 6 },
  { "adsGunKickPitchMin", offsetof(WeaponDef, fAdsGunKickPitchMin), 6 },
  { "adsGunKickPitchMax", offsetof(WeaponDef, fAdsGunKickPitchMax), 6 },
  { "adsGunKickYawMin", offsetof(WeaponDef, fAdsGunKickYawMin), 6 },
  { "adsGunKickYawMax", offsetof(WeaponDef, fAdsGunKickYawMax), 6 },
  { "adsGunKickAccel", offsetof(WeaponDef, fAdsGunKickAccel), 6 },
  { "adsGunKickSpeedMax", offsetof(WeaponDef, fAdsGunKickSpeedMax), 6 },
  { "adsGunKickSpeedDecay", offsetof(WeaponDef, fAdsGunKickSpeedDecay), 6 },
  { "adsGunKickStaticDecay", offsetof(WeaponDef, fAdsGunKickStaticDecay), 6 },
  { "adsViewKickPitchMin", offsetof(WeaponDef, fAdsViewKickPitchMin), 6 },
  { "adsViewKickPitchMax", offsetof(WeaponDef, fAdsViewKickPitchMax), 6 },
  { "adsViewKickYawMin", offsetof(WeaponDef, fAdsViewKickYawMin), 6 },
  { "adsViewKickYawMax", offsetof(WeaponDef, fAdsViewKickYawMax), 6 },
  { "adsViewKickCenterSpeed", offsetof(WeaponDef, fAdsViewKickCenterSpeed), 6 },
  { "adsSpread", offsetof(WeaponDef, fAdsSpread), 6 },
  { "guidedMissileType", offsetof(WeaponDef, guidedMissileType), 22 },
  { "hipSpreadStandMin", offsetof(WeaponDef, fHipSpreadStandMin), 6 },
  { "hipSpreadDuckedMin", offsetof(WeaponDef, fHipSpreadDuckedMin), 6 },
  { "hipSpreadProneMin", offsetof(WeaponDef, fHipSpreadProneMin), 6 },
  { "hipSpreadMax", offsetof(WeaponDef, hipSpreadStandMax), 6 },
  { "hipSpreadDuckedMax", offsetof(WeaponDef, hipSpreadDuckedMax), 6 },
  { "hipSpreadProneMax", offsetof(WeaponDef, hipSpreadProneMax), 6 },
  { "hipSpreadDecayRate", offsetof(WeaponDef, fHipSpreadDecayRate), 6 },
  { "hipSpreadFireAdd", offsetof(WeaponDef, fHipSpreadFireAdd), 6 },
  { "hipSpreadTurnAdd", offsetof(WeaponDef, fHipSpreadTurnAdd), 6 },
  { "hipSpreadMoveAdd", offsetof(WeaponDef, fHipSpreadMoveAdd), 6 },
  { "hipSpreadDuckedDecay", offsetof(WeaponDef, fHipSpreadDuckedDecay), 6 },
  { "hipSpreadProneDecay", offsetof(WeaponDef, fHipSpreadProneDecay), 6 },
  { "hipReticleSidePos", offsetof(WeaponDef, fHipReticleSidePos), 6 },
  { "hipIdleAmount", offsetof(WeaponDef, fHipIdleAmount), 6 },
  { "hipIdleSpeed", offsetof(WeaponDef, hipIdleSpeed), 6 },
  { "hipGunKickReducedKickBullets", offsetof(WeaponDef, hipGunKickReducedKickBullets), 4 },
  { "hipGunKickReducedKickPercent", offsetof(WeaponDef, hipGunKickReducedKickPercent), 6 },
  { "hipGunKickPitchMin", offsetof(WeaponDef, fHipGunKickPitchMin), 6 },
  { "hipGunKickPitchMax", offsetof(WeaponDef, fHipGunKickPitchMax), 6 },
  { "hipGunKickYawMin", offsetof(WeaponDef, fHipGunKickYawMin), 6 },
  { "hipGunKickYawMax", offsetof(WeaponDef, fHipGunKickYawMax), 6 },
  { "hipGunKickAccel", offsetof(WeaponDef, fHipGunKickAccel), 6 },
  { "hipGunKickSpeedMax", offsetof(WeaponDef, fHipGunKickSpeedMax), 6 },
  { "hipGunKickSpeedDecay", offsetof(WeaponDef, fHipGunKickSpeedDecay), 6 },
  { "hipGunKickStaticDecay", offsetof(WeaponDef, fHipGunKickStaticDecay), 6 },
  { "hipViewKickPitchMin", offsetof(WeaponDef, fHipViewKickPitchMin), 6 },
  { "hipViewKickPitchMax", offsetof(WeaponDef, fHipViewKickPitchMax), 6 },
  { "hipViewKickYawMin", offsetof(WeaponDef, fHipViewKickYawMin), 6 },
  { "hipViewKickYawMax", offsetof(WeaponDef, fHipViewKickYawMax), 6 },
  { "hipViewKickCenterSpeed", offsetof(WeaponDef, fHipViewKickCenterSpeed), 6 },
  { "leftArc", offsetof(WeaponDef, leftArc), 6 },
  { "rightArc", offsetof(WeaponDef, rightArc), 6 },
  { "topArc", offsetof(WeaponDef, topArc), 6 },
  { "bottomArc", offsetof(WeaponDef, bottomArc), 6 },
  { "accuracy", offsetof(WeaponDef, accuracy), 6 },
  { "aiSpread", offsetof(WeaponDef, aiSpread), 6 },
  { "playerSpread", offsetof(WeaponDef, playerSpread), 6 },
  { "maxVertTurnSpeed", offsetof(WeaponDef, maxTurnSpeed[0]), 6 },
  { "maxHorTurnSpeed", offsetof(WeaponDef, maxTurnSpeed[1]), 6 },
  { "minVertTurnSpeed", offsetof(WeaponDef, minTurnSpeed[0]), 6 },
  { "minHorTurnSpeed", offsetof(WeaponDef, minTurnSpeed[1]), 6 },
  { "pitchConvergenceTime", offsetof(WeaponDef, pitchConvergenceTime), 6 },
  { "yawConvergenceTime", offsetof(WeaponDef, yawConvergenceTime), 6 },
  { "suppressionTime", offsetof(WeaponDef, suppressTime), 6 },
  { "maxRange", offsetof(WeaponDef, maxRange), 6 },
  { "animHorRotateInc", offsetof(WeaponDef, fAnimHorRotateInc), 6 },
  { "playerPositionDist", offsetof(WeaponDef, fPlayerPositionDist), 6 },
  { "stance", offsetof(WeaponDef, stance), 17 },
  { "useHintString", offsetof(WeaponDef, szUseHintString), 0 },
  { "dropHintString", offsetof(WeaponDef, dropHintString), 0 },
  { "horizViewJitter", offsetof(WeaponDef, horizViewJitter), 6 },
  { "vertViewJitter", offsetof(WeaponDef, vertViewJitter), 6 },
  { "fightDist", offsetof(WeaponDef, fightDist), 6 },
  { "maxDist", offsetof(WeaponDef, maxDist), 6 },
  { "aiVsAiAccuracyGraph", offsetof(WeaponDef, accuracyGraphName[0]), 0 },
  { "aiVsPlayerAccuracyGraph", offsetof(WeaponDef, accuracyGraphName[1]), 0 },
  { "locNone", offsetof(WeaponDef, locationDamageMultipliers[0]), 6 },
  { "locHelmet", offsetof(WeaponDef, locationDamageMultipliers[1]), 6 },
  { "locHead", offsetof(WeaponDef, locationDamageMultipliers[2]), 6 },
  { "locNeck", offsetof(WeaponDef, locationDamageMultipliers[3]), 6 },
  { "locTorsoUpper", offsetof(WeaponDef, locationDamageMultipliers[4]), 6 },
  { "locTorsoLower", offsetof(WeaponDef, locationDamageMultipliers[5]), 6 },
  { "locRightArmUpper", offsetof(WeaponDef, locationDamageMultipliers[6]), 6 },
  { "locRightArmLower", offsetof(WeaponDef, locationDamageMultipliers[8]), 6 },
  { "locRightHand", offsetof(WeaponDef, locationDamageMultipliers[10]), 6 },
  { "locLeftArmUpper", offsetof(WeaponDef, locationDamageMultipliers[7]), 6 },
  { "locLeftArmLower", offsetof(WeaponDef, locationDamageMultipliers[9]), 6 },
  { "locLeftHand", offsetof(WeaponDef, locationDamageMultipliers[11]), 6 },
  { "locRightLegUpper", offsetof(WeaponDef, locationDamageMultipliers[12]), 6 },
  { "locRightLegLower", offsetof(WeaponDef, locationDamageMultipliers[14]), 6 },
  { "locRightFoot", offsetof(WeaponDef, locationDamageMultipliers[16]), 6 },
  { "locLeftLegUpper", offsetof(WeaponDef, locationDamageMultipliers[13]), 6 },
  { "locLeftLegLower", offsetof(WeaponDef, locationDamageMultipliers[15]), 6 },
  { "locLeftFoot", offsetof(WeaponDef, locationDamageMultipliers[17]), 6 },
  { "locGun", offsetof(WeaponDef, locationDamageMultipliers[18]), 6 },
  { "fireRumble", offsetof(WeaponDef, fireRumble), 0 },
  { "meleeImpactRumble", offsetof(WeaponDef, meleeImpactRumble), 0 },
  { "adsDofStart", offsetof(WeaponDef, adsDofStart), 6 },
  { "adsDofEnd", offsetof(WeaponDef, adsDofEnd), 6 }
}; // idb

// const char *szWeapTypeNames[4] = { "bullet", "grenade", "projectile", "binoculars" }; // idb
const char *szWeapClassNames[10] =
{
  "rifle",
  "mg",
  "smg",
  "spread",
  "pistol",
  "grenade",
  "rocketlauncher",
  "turret",
  "non-player",
  "item"
}; // idb

char *g_playerAnimTypeNames[64];

WeaponDef bg_defaultWeaponDefs;

char *__cdecl BG_GetPlayerAnimTypeName(int32_t index)
{
    return g_playerAnimTypeNames[index];
}

void __cdecl TRACK_bg_weapons_load_obj()
{
    track_static_alloc_internal(szWeapOverlayReticleNames, 8, "szWeapOverlayReticleNames", 9);
    track_static_alloc_internal(szWeapStanceNames, 12, "szWeapStanceNames", 9);
    track_static_alloc_internal(weaponDefFields, 6024, "weaponDefFields", 9);
    track_static_alloc_internal(&bg_defaultWeaponDefs, sizeof(WeaponDef), "bg_defaultWeaponDefs", 9);
    track_static_alloc_internal(penetrateTypeNames, 16, "penetrateTypeNames", 9);
    track_static_alloc_internal(szWeapTypeNames, 16, "szWeapTypeNames", 9);
    track_static_alloc_internal(szWeapClassNames, 40, "szWeapClassNames", 9);
    track_static_alloc_internal(g_playerAnimTypeNames, 256, "g_playerAnimTypeNames", 9);
    track_static_alloc_internal(szWeapInventoryTypeNames, 16, "szWeapInventoryTypeNames", 9);
}

const char *__cdecl BG_GetWeaponTypeName(weapType_t type)
{
    bcassert(type < WEAPTYPE_NUM, ARRAY_COUNT(szWeapTypeNames));

    return szWeapTypeNames[type];
}

const char *__cdecl BG_GetWeaponClassName(weapClass_t type)
{
    bcassert(type < WEAPCLASS_NUM, ARRAY_COUNT(szWeapClassNames));

    return szWeapClassNames[type];
}

const char *__cdecl BG_GetWeaponInventoryTypeName(weapInventoryType_t type)
{
    bcassert(type < WEAPINVENTORYCOUNT, ARRAY_COUNT(szWeapInventoryTypeNames));

    return szWeapInventoryTypeNames[type];
}

#ifdef KISAK_MP
void __cdecl BG_LoadWeaponStrings()
{
    uint32_t i; // [esp+0h] [ebp-4h]

    for (i = 0; i < g_playerAnimTypeNamesCount; ++i)
        BG_InitWeaponString(i, g_playerAnimTypeNames[i]);
}
#endif

void __cdecl BG_LoadPlayerAnimTypes()
{
#ifdef KISAK_MP
    char v0; // [esp+3h] [ebp-29h]
    char *v1; // [esp+8h] [ebp-24h]
    const char *v2; // [esp+Ch] [ebp-20h]
    char *buf; // [esp+20h] [ebp-Ch]
    const char *text_p; // [esp+24h] [ebp-8h] BYREF
    const char *token; // [esp+28h] [ebp-4h]

    g_playerAnimTypeNamesCount = 0;
    buf = Com_LoadRawTextFile("mp/playeranimtypes.txt");
    if (!buf)
        Com_Error(ERR_DROP, "Couldn',27h,'t load file %s", "mp/playeranimtypes.txt");
    text_p = buf;
    Com_BeginParseSession("BG_AnimParseAnimScript");
    while (1)
    {
        token = (const char *)Com_Parse(&text_p);
        if (!token || !*token)
            break;
        if (g_playerAnimTypeNamesCount >= 0x40)
            Com_Error(ERR_DROP, "Player anim type array size exceeded");
        g_playerAnimTypeNames[g_playerAnimTypeNamesCount] = (char *)Hunk_Alloc(
            strlen(token) + 1,
            "BG_LoadPlayerAnimTypes",
            9);
        v2 = token;
        v1 = g_playerAnimTypeNames[g_playerAnimTypeNamesCount];
        do
        {
            v0 = *v2;
            *v1++ = *v2++;
        } while (v0);
        ++g_playerAnimTypeNamesCount;
    }
    Com_EndParseSession();
    Com_UnloadRawTextFile(buf);
#elif KISAK_SP
    g_playerAnimTypeNamesCount = 1;
    g_playerAnimTypeNames[0] = (char*)"none";
#endif
}

void __cdecl InitWeaponDef(WeaponDef *weapDef)
{
    const cspField_t *pField; // [esp+4h] [ebp-8h]
    int iField; // [esp+8h] [ebp-4h]

    weapDef->szInternalName = "";
    iField = 0;
    pField = weaponDefFields;
    while (iField < 502)
    {
        if (!pField->iFieldType)
            *(const char **)((char *)&weapDef->szInternalName + pField->iOffset) = "";
        ++iField;
        ++pField;
    }
}

char __cdecl G_ParseAIWeaponAccurayGraphFile(
    const char *buffer,
    const char *fileName,
    float (*knots)[2],
    int *knotCount)
{
    if (!buffer || !fileName || !knots || !knotCount)
        return 0;
    *knotCount = 0;

    Com_BeginParseSession(fileName);
    parseInfo_t *token = Com_Parse(&buffer);
    const int declaredKnotCount = atoi(token->token);
    if (!db::validation::CountInRange(declaredKnotCount, 2, 16))
    {
        Com_EndParseSession();
        Com_PrintError(15, "ERROR: \"%s\" graph knot count must be between 2 and 16\n", fileName);
        return 0;
    }

    int knotCountIndex = 0;
    while (1)
    {
        token = Com_Parse(&buffer);
        if (!token->token[0] || token->token[0] == 125)
            break;
        if (knotCountIndex >= 16)
        {
            Com_PrintWarning(15, "WARNING: \"%s\" has too many graph knots\n", fileName);
            Com_EndParseSession();
            return 0;
        }

        const float x = static_cast<float>(atof(token->token));
        token = Com_Parse(&buffer);
        if (!token->token[0] || token->token[0] == 125)
        {
            Com_EndParseSession();
            Com_PrintError(15, "ERROR: \"%s\" graph knot is missing its value\n", fileName);
            return 0;
        }
        const float y = static_cast<float>(atof(token->token));
        knots[knotCountIndex][0] = x;
        knots[knotCountIndex][1] = y;
        ++knotCountIndex;
    }
    Com_EndParseSession();
    if (knotCountIndex != declaredKnotCount)
    {
        Com_PrintError(15, "ERROR: \"%s\" Error in parsing an ai weapon accuracy file\n", fileName);
        return 0;
    }
    if (!db::validation::NormalizedGraphKnots(
            knots,
            static_cast<uint32_t>(knotCountIndex)))
    {
        Com_PrintError(15, "ERROR: \"%s\" has invalid normalized graph knots\n", fileName);
        return 0;
    }

    *knotCount = knotCountIndex;
    return 1;
}

char __cdecl G_ParseWeaponAccurayGraphInternal(
    WeaponDef *weaponDef,
    const char *dirName,
    const char *graphName,
    float (*knots)[2],
    int *knotCount)
{
    signed int v6; // [esp+10h] [ebp-205Ch]
    char string[64]; // [esp+14h] [ebp-2058h] BYREF
    char buffer[8196]; // [esp+54h] [ebp-2018h] BYREF
    const char *last; // [esp+205Ch] [ebp-10h]
    int knotCounta; // [esp+2060h] [ebp-Ch] BYREF
    int f; // [esp+2064h] [ebp-8h] BYREF
    int len; // [esp+2068h] [ebp-4h]

    last = "WEAPONACCUFILE";
    len = strlen("WEAPONACCUFILE");
    iassert(weaponDef);
    iassert(graphName);
    iassert(knots);
    iassert(knotCount);
    iassert(dirName);

    if (weaponDef->weapType && weaponDef->weapType != WEAPTYPE_PROJECTILE)
        return 1;

    if (!*graphName)
        return 1;

    snprintf(string, sizeof(string), "accuracy/%s/%s", dirName, graphName);
    v6 = FS_FOpenFileByMode(string, &f, FS_READ);
    if (v6 >= 0)
    {
        FS_Read((uint8_t *)buffer, len, f);
        buffer[len] = 0;
        if (!strncmp(buffer, last, len))
        {
            if (v6 - len < 0x2000)
            {
                memset((uint8_t *)buffer, 0, 0x2000u);
                FS_Read((uint8_t *)buffer, v6 - len, f);
                buffer[v6 - len] = 0;
                FS_FCloseFile(f);
                knotCounta = 0;
                if (G_ParseAIWeaponAccurayGraphFile(buffer, string, knots, &knotCounta))
                {
                    *knotCount = knotCounta;
                    return 1;
                }
                else
                {
                    return 0;
                }
            }
            else
            {
                Com_PrintWarning(15, "WARNING: \"%s\" Is too long of an ai weapon accuracy file to parse\n", string);
                FS_FCloseFile(f);
                return 0;
            }
        }
        else
        {
            Com_PrintWarning(15, "WARNING: \"%s\" does not appear to be an ai weapon accuracy file\n", string);
            FS_FCloseFile(f);
            return 0;
        }
    }
    else
    {
        Com_PrintWarning(15, "WARNING: Could not load ai weapon accuracy file '%s'\n", string);
        return 0;
    }
}

char __cdecl G_ParseWeaponAccurayGraphs(WeaponDef *weaponDef)
{
    uint32_t size; // [esp+4h] [ebp-8Ch]
    int weaponType; // [esp+8h] [ebp-88h]
    int accuracyGraphKnotCount; // [esp+Ch] [ebp-84h] BYREF
    float accuracyGraphKnots[16][2]; // [esp+10h] [ebp-80h] BYREF

    for (weaponType = 0; weaponType < 2; ++weaponType)
    {
        memset((uint8_t *)accuracyGraphKnots, 0, sizeof(accuracyGraphKnots));
        accuracyGraphKnotCount = 0;
        if (!G_ParseWeaponAccurayGraphInternal(
            weaponDef,
            accuracyDirName[weaponType],
            weaponDef->accuracyGraphName[weaponType],
            accuracyGraphKnots,
            &accuracyGraphKnotCount))
            return 0;
        if (accuracyGraphKnotCount > 0)
        {
            size = 8 * accuracyGraphKnotCount;
            weaponDef->accuracyGraphKnots[weaponType] = (float (*)[2])Hunk_AllocLowAlign(
                8 * accuracyGraphKnotCount,
                4,
                "G_ParseWeaponAccurayGraphs",
                9);
            weaponDef->originalAccuracyGraphKnots[weaponType] = weaponDef->accuracyGraphKnots[weaponType];
            memcpy((uint8_t *)weaponDef->accuracyGraphKnots[weaponType], (uint8_t *)accuracyGraphKnots, size);
            weaponDef->accuracyGraphKnotCount[weaponType] = accuracyGraphKnotCount;
            weaponDef->originalAccuracyGraphKnotCount[weaponType] = weaponDef->accuracyGraphKnotCount[weaponType];
        }
    }
    return 1;
}

WeaponDef *__cdecl BG_LoadDefaultWeaponDef_LoadObj()
{
    InitWeaponDef(&bg_defaultWeaponDefs);
    bg_defaultWeaponDefs.szInternalName = "none";
    bg_defaultWeaponDefs.accuracyGraphName[0] = "noweapon.accu";
    bg_defaultWeaponDefs.accuracyGraphName[1] = "noweapon.accu";
    bg_defaultWeaponDefs.sprintDurationScale = 1.75;
    G_ParseWeaponAccurayGraphs(&bg_defaultWeaponDefs);
    return &bg_defaultWeaponDefs;
}

WeaponDef *__cdecl BG_LoadDefaultWeaponDef()
{
    if (IsFastFileLoad())
        return BG_LoadDefaultWeaponDef_FastFile();
    else
        return BG_LoadDefaultWeaponDef_LoadObj();
}

WeaponDef *__cdecl BG_LoadDefaultWeaponDef_FastFile()
{
    return DB_FindXAssetHeader(ASSET_TYPE_WEAPON, "none").weapon;
}

int __cdecl Weapon_GetStringArrayIndex(const char *value, char **stringArray, int arraySize)
{
    int arrayIndex; // [esp+0h] [ebp-4h]

    iassert(value);
    iassert(stringArray);

    for (arrayIndex = 0; arrayIndex < arraySize; ++arrayIndex)
    {
        if (!I_stricmp(value, stringArray[arrayIndex]))
            return arrayIndex;
    }
    return -1;
}

snd_alias_list_t **__cdecl BG_RegisterSurfaceTypeSounds(const char *surfaceSoundBase)
{
    char *v2; // eax
    snd_alias_list_t *SoundAlias; // eax
    char v4; // [esp+3h] [ebp-131h]
    char *v5; // [esp+8h] [ebp-12Ch]
    const char *v6; // [esp+Ch] [ebp-128h]
    snd_alias_list_t **result; // [esp+20h] [ebp-114h]
    char aliasName[260]; // [esp+24h] [ebp-110h] BYREF
    snd_alias_list_t *defaultAliasList; // [esp+12Ch] [ebp-8h]
    int i; // [esp+130h] [ebp-4h]

    iassert(surfaceSoundBase);

    if (!*surfaceSoundBase)
        return 0;

    for (i = 0; i < surfaceTypeSoundListCount; ++i)
    {
        if (!I_strcmp(surfaceTypeSoundLists[i].surfaceSoundBase, surfaceSoundBase))
            return surfaceTypeSoundLists[i].soundAliasList;
    }
    if (surfaceTypeSoundListCount == 16)
        Com_Error(ERR_DROP, "Exceeded MAX_SURFACE_TYPE_SOUND_LISTS (%d)", 16);

    result = (snd_alias_list_t **)Hunk_AllocLow(0x74u, "BG_RegisterSurfaceTypeSounds", 15);
    Com_sprintf(aliasName, 0x100u, "%s_default", surfaceSoundBase);
    defaultAliasList = Com_FindSoundAlias(aliasName);
    for (i = 0; i < 29; ++i)
    {
        v2 = (char*)Com_SurfaceTypeToName(i);
        Com_sprintf(aliasName, 0x100u, "%s_%s", surfaceSoundBase, v2);
        SoundAlias = Com_FindSoundAlias(aliasName);
        result[i] = SoundAlias;
        if (!result[i])
            result[i] = defaultAliasList;
    }
    surfaceTypeSoundLists[surfaceTypeSoundListCount].surfaceSoundBase = (char *)Hunk_AllocLow(
        strlen(surfaceSoundBase) + 1,
        "BG_RegisterSurfaceTypeSounds",
        15);
    v6 = surfaceSoundBase;
    v5 = surfaceTypeSoundLists[surfaceTypeSoundListCount].surfaceSoundBase;
    do
    {
        v4 = *v6;
        *v5++ = *v6++;
    } while (v4);
    surfaceTypeSoundLists[surfaceTypeSoundListCount++].soundAliasList = result;
    return result;
}

int __cdecl BG_ParseWeaponDefSpecificFieldType(uint8_t *pStruct, const char *pValue, int iFieldType)
{
    uint16_t LowercaseString_DONE; // ax
    uint16_t v5; // ax
    int result; // eax
    char v7; // [esp+3h] [ebp-91h]
    char *v8; // [esp+8h] [ebp-8Ch]
    const char *v9; // [esp+Ch] [ebp-88h]
    int v10; // [esp+10h] [ebp-84h]
    const char *pos; // [esp+38h] [ebp-5Ch] BYREF
    int numHideTags; // [esp+3Ch] [ebp-58h]
    int numNoteTrackMappings; // [esp+40h] [ebp-54h]
    char keyName[64]; // [esp+44h] [ebp-50h] BYREF
    int arrayIndex; // [esp+88h] [ebp-Ch]
    const char *token; // [esp+8Ch] [ebp-8h]
    WeaponDef *weapDef; // [esp+90h] [ebp-4h]

    iassert(pStruct);
    iassert(pValue);

    weapDef = (WeaponDef *)pStruct;
    switch (iFieldType)
    {
    case 12:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char**)szWeapTypeNames, 4);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon type %s in %s", pValue, weapDef->szInternalName);
        weapDef->weapType = (weapType_t)arrayIndex;
        goto LABEL_86;
    case 13:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char**)szWeapClassNames, 10);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon class %s in %s", pValue, weapDef->szInternalName);
        weapDef->weapClass = (weapClass_t)arrayIndex;
        goto LABEL_86;
    case 14:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)szWeapOverlayReticleNames, 2);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon reticle %s in %s", pValue, weapDef->szInternalName);
        weapDef->overlayReticle = (weapOverlayReticle_t)arrayIndex;
        goto LABEL_86;
    case 15:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)penetrateTypeNames, 4);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon penetrate type %s in %s", pValue, weapDef->szInternalName);
        weapDef->penetrateType = (PenetrateType)arrayIndex;
        goto LABEL_86;
    case 16:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)impactTypeNames, 9);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon impact type %s in %s", pValue, weapDef->szInternalName);
        weapDef->impactType = (ImpactType)arrayIndex;
        goto LABEL_86;
    case 17:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)szWeapStanceNames, 3);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon stance %s in %s", pValue, weapDef->szInternalName);
        weapDef->stance = (weapStance_t)arrayIndex;
        goto LABEL_86;
    case 18:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)szProjectileExplosionNames, 7);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon projExplosion %s in %s", pValue, weapDef->szInternalName);
        weapDef->projExplosion = (weapProjExposion_t)arrayIndex;
        goto LABEL_86;
    case 19:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)offhandClassNames, 4);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon offhand class %s in %s", pValue, weapDef->szInternalName);
        weapDef->offhandClass = (OffhandClass)arrayIndex;
        goto LABEL_86;
    case 20:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, g_playerAnimTypeNames, g_playerAnimTypeNamesCount);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon player anim type %s in %s", pValue, weapDef->szInternalName);
        weapDef->playerAnimType = arrayIndex;
        goto LABEL_86;
    case 21:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)activeReticleNames, 3);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon active reticle type %s in %s", pValue, weapDef->szInternalName);
        weapDef->activeReticleType = (activeReticleType_t)arrayIndex;
        goto LABEL_86;
    case 22:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)guidedMissileNames, 4);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon guided missile type %s in %s", pValue, weapDef->szInternalName);
        weapDef->guidedMissileType = (guidedMissileType_t)arrayIndex;
        goto LABEL_86;
    case 23:
        weapDef->bounceSound = BG_RegisterSurfaceTypeSounds(pValue);
        goto LABEL_86;
    case 24:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)stickinessNames, 4);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon stickiness %s in %s", pValue, weapDef->szInternalName);
        weapDef->stickiness = (WeapStickinessType)arrayIndex;
        goto LABEL_86;
    case 25:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)overlayInterfaceNames, 3);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon overlay interface %s in %s", pValue, weapDef->szInternalName);
        weapDef->overlayInterface = (WeapOverlayInteface_t)arrayIndex;
        goto LABEL_86;
    case 26:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)szWeapInventoryTypeNames, 4);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon inventory type %s in %s", pValue, weapDef->szInternalName);
        weapDef->inventoryType = (weapInventoryType_t)arrayIndex;
        goto LABEL_86;
    case 27:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)szWeapFireTypeNames, 5);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon firetype %s in %s", pValue, weapDef->szInternalName);
        weapDef->fireType = (weapFireType_t)arrayIndex;
        goto LABEL_86;
    case 28:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char**)ammoCounterClipNames, 7);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon ammo counter clip %s in %s", pValue, weapDef->szInternalName);
        weapDef->ammoCounterClip = (ammoCounterClipType_t)arrayIndex;
        goto LABEL_86;
    case 29:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)weapIconRatioNames, 3);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon hud icon ratio %s in %s", pValue, weapDef->szInternalName);
        weapDef->hudIconRatio = (weaponIconRatioType_t)arrayIndex;
        goto LABEL_86;
    case 30:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)weapIconRatioNames, 3);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon ammo counter icon ratio %s in %s", pValue, weapDef->szInternalName);
        weapDef->ammoCounterIconRatio = (weaponIconRatioType_t)arrayIndex;
        goto LABEL_86;
    case 31:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)weapIconRatioNames, 3);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon kill icon ratio %s in %s", pValue, weapDef->szInternalName);
        weapDef->killIconRatio = (weaponIconRatioType_t)arrayIndex;
        goto LABEL_86;
    case 32:
        arrayIndex = Weapon_GetStringArrayIndex(pValue, (char **)weapIconRatioNames, 3);
        if (arrayIndex < 0)
            Com_Error(ERR_DROP, "Unknown weapon dpad icon ratio %s in %s", pValue, weapDef->szInternalName);
        weapDef->dpadIconRatio = (weaponIconRatioType_t)arrayIndex;
        goto LABEL_86;
    case 33:
        numHideTags = 0;
        pos = pValue;
        while (1)
        {
            token = (const char *)Com_Parse(&pos);
            if (!pos)
                break;
            if (numHideTags >= 8)
                Com_Error(ERR_DROP, "maximum hide tags (%s) exceeded: %i > %i'", token, numHideTags, 8);
            weapDef->hideTags[numHideTags] = SL_GetStringOfSize((char *)token, 0, strlen(token) + 1, MT_TYPE_MODEL_PART);
            weapDef->hideTags[numHideTags] = SL_ConvertToLowercase(weapDef->hideTags[numHideTags], 0, MT_TYPE_MODEL_PART);
            ++numHideTags;
        }
        goto LABEL_86;
    case 34:
        numNoteTrackMappings = 0;
        pos = pValue;
        keyName[0] = 0;
        while (1)
        {
            token = (const char *)Com_Parse(&pos);
            if (!pos)
                break;
            if (numNoteTrackMappings >= 16)
                Com_Error(ERR_DROP, "Max notetrack-to-sound mappings (%i) exceeded with entry '%s'", 16, token);
            if (keyName[0])
            {
                LowercaseString_DONE = SL_GetLowercaseString(keyName, 0);
                weapDef->notetrackSoundMapKeys[numNoteTrackMappings] = LowercaseString_DONE;
                v5 = SL_GetLowercaseString(token, 0);
                weapDef->notetrackSoundMapValues[numNoteTrackMappings++] = v5;
                keyName[0] = 0;
            }   
            else
            {
                v10 = strlen(token);
                if (v10 >= 63)
                    Com_Error(ERR_DROP, "Notetrack - to - sound: keyname \"%s\" is too long(length % i / % i).", token, v10, 63);
                v9 = token;
                v8 = keyName;
                do
                {
                    v7 = *v9;
                    *v8++ = *v9++;
                } while (v7);
            }
        }
        if (keyName[0])
            Com_PrintWarning(
                0,
                "Notetrack-to-Sound: Weapon '%s' has bad entry; notetrack '%s' doesn't have a corresponding sound.\n",
                weapDef->szInternalName,
                keyName);
    LABEL_86:
        result = 1;
        break;
    default:
        Com_Error(ERR_DROP, "Bad field type %i in %s", iFieldType, weapDef->szInternalName);
        result = 0;
        break;
    }
    return result;
}

void __cdecl BG_SetupTransitionTimes(WeaponDef *weapDef)
{
    double v1; // st7
    double v2; // st7

    if (weapDef->iAdsTransInTime <= 0)
        v1 = 1.0 / (float)300.0;
    else
        v1 = 1.0 / (double)weapDef->iAdsTransInTime;
    weapDef->fOOPosAnimLength[0] = v1;
    if (weapDef->iAdsTransOutTime <= 0)
        v2 = 1.0 / (float)500.0;
    else
        v2 = 1.0 / (double)weapDef->iAdsTransOutTime;
    weapDef->fOOPosAnimLength[1] = v2;
}

void __cdecl BG_CheckWeaponDamageRanges(WeaponDef *weapDef)
{
    if (weapDef->fMaxDamageRange <= 0.0f)
        weapDef->fMaxDamageRange = 999999.0f;
    if (weapDef->fMinDamageRange <= 0.0f)
        weapDef->fMinDamageRange = 999999.12f;
}

void __cdecl BG_CheckProjectileValues(WeaponDef *weaponDef)
{
    iassert(weaponDef->weapType == WEAPTYPE_PROJECTILE);

    if ((double)weaponDef->iProjectileSpeed <= 0.0)
        Com_Error(ERR_DROP, "Projectile speed for WeapType %s must be greater than 0.0", weaponDef->szDisplayName);

    if (weaponDef->destabilizationCurvatureMax >= 1000000000.0f || weaponDef->destabilizationCurvatureMax < 0.0)
        Com_Error(
            ERR_DROP,
            "Destabilization angle for for WeapType %s must be between 0 and 45 degrees",
            weaponDef->szDisplayName);

    if (weaponDef->destabilizationRateTime < 0.0)
        Com_Error(ERR_DROP, "Destabilization rate time for for WeapType %s must be non-negative", weaponDef->szDisplayName);
}

WeaponDef *__cdecl BG_LoadWeaponDefInternal(const char *one, const char *two)
{
    snd_alias_list_t *SoundAlias; // eax
    snd_alias_list_t *v4; // eax
    snd_alias_list_t *v5; // eax
    snd_alias_list_t *v6; // eax
    snd_alias_list_t *v7; // eax
    char buffer[10244]; // [esp+1Ch] [ebp-2858h] BYREF
    int f; // [esp+2820h] [ebp-54h] BYREF
    int len; // [esp+2824h] [ebp-50h]
    signed int v11; // [esp+2828h] [ebp-4Ch]
    char dest[64]; // [esp+282Ch] [ebp-48h] BYREF
    WeaponDef *weapDef; // [esp+2870h] [ebp-4h]

    len = strlen("WEAPONFILE");
    weapDef = (WeaponDef *)Hunk_AllocLow(sizeof(WeaponDef), "BG_LoadWeaponDefInternal", 9);
    InitWeaponDef(weapDef);
    Com_sprintf(dest, 0x40u, "weapons/%s/%s", one, two);
    v11 = FS_FOpenFileByMode(dest, &f, FS_READ);
    if (v11 >= 0)
    {
        FS_Read((uint8_t *)buffer, len, f);
        buffer[len] = 0;
        if (!strncmp(buffer, "WEAPONFILE", len))
        {
            if ((uint32_t)(v11 - len) < 0x2800)
            {
                memset((uint8_t *)buffer, 0, 0x2800u);
                FS_Read((uint8_t *)buffer, v11 - len, f);
                buffer[v11 - len] = 0;
                FS_FCloseFile(f);
                if (Info_Validate(buffer))
                {
                    SetConfigString((char **)weapDef, two);
                    if (ParseConfigStringToStructCustomSize(
                        (uint8_t *)weapDef,
                        weaponDefFields,
                        502,
                        buffer,
                        35,
                        BG_ParseWeaponDefSpecificFieldType,
                        SetConfigString2))
                    {
                        if (I_stricmp(two, "defaultweapon_mp"))
                        {
                            if (!weapDef->viewLastShotEjectEffect)
                                weapDef->viewLastShotEjectEffect = weapDef->viewShellEjectEffect;
                            if (!weapDef->worldLastShotEjectEffect)
                                weapDef->worldLastShotEjectEffect = weapDef->worldShellEjectEffect;
                            if (!weapDef->raiseSound)
                            {
                                SoundAlias = Com_FindSoundAlias("weap_raise");
                                weapDef->raiseSound = SoundAlias;
                            }
                            if (!weapDef->putawaySound)
                            {
                                v4 = Com_FindSoundAlias("weap_putaway");
                                weapDef->putawaySound = v4;
                            }
                            if (!weapDef->pickupSound)
                            {
                                v5 = Com_FindSoundAlias("weap_pickup");
                                weapDef->pickupSound = v5;
                            }
                            if (!weapDef->ammoPickupSound)
                            {
                                v6 = Com_FindSoundAlias("weap_ammo_pickup");
                                weapDef->ammoPickupSound = v6;
                            }
                            if (!weapDef->emptyFireSound)
                            {
                                v7 = Com_FindSoundAlias("weap_dryfire_smg_npc");
                                weapDef->emptyFireSound = v7;
                            }
                        }
                        BG_SetupTransitionTimes(weapDef);
                        BG_CheckWeaponDamageRanges(weapDef);
                        if (weapDef->enemyCrosshairRange > 15000.0)
                            Com_Error(ERR_DROP, "Enemy crosshair ranges should be less than %f ", 15000.0);
                        if (weapDef->weapType == WEAPTYPE_PROJECTILE)
                            BG_CheckProjectileValues(weapDef);
                        if (G_ParseWeaponAccurayGraphs(weapDef))
                        {
                            I_strlwr((char *)weapDef->szAmmoName);
                            I_strlwr((char *)weapDef->szClipName);
                            return weapDef;
                        }
                        else
                        {
                            return 0;
                        }
                    }
                    else
                    {
                        return 0;
                    }
                }
                else
                {
                    Com_PrintWarning(17, "WARNING: \"%s\" is not a valid weapon file\n", dest);
                    return 0;
                }
            }
            else
            {
                Com_PrintWarning(
                    17,
                    "WARNING: \"%s\" Is too long of a weapon file to parse (fileLength = %d identifierLength = %d)\n",
                    dest,
                    v11,
                    len);
                FS_FCloseFile(f);
                return 0;
            }
        }
        else
        {
            Com_PrintWarning(17, "WARNING: \"%s\" does not appear to be a weapon file\n", dest);
            FS_FCloseFile(f);
            return 0;
        }
    }
    else
    {
        Com_PrintWarning(17, "WARNING: Could not load weapon file '%s'\n", dest);
        return 0;
    }
}


WeaponDef *__cdecl BG_LoadWeaponDef_LoadObj(const char *name)
{
    WeaponDef *weapDef; // [esp+0h] [ebp-4h]

    if (!*name)
        return 0;
#ifdef KISAK_MP
    weapDef = BG_LoadWeaponDefInternal("mp", name);
#elif KISAK_SP
    weapDef = BG_LoadWeaponDefInternal("sp", name);
#endif
    if (weapDef)
        return weapDef;

#ifdef KISAK_MP
    weapDef = BG_LoadWeaponDefInternal("mp", "defaultweapon_mp");
#elif KISAK_SP
    weapDef = BG_LoadWeaponDefInternal("sp", "defaultweapon");
#endif

    if (!weapDef)
        Com_Error(ERR_DROP, "BG_LoadWeaponDef: Could not find default weapon");

    SetConfigString((char **)weapDef, name);
    return weapDef;
}
