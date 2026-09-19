#include "msg_mp.h"
#include "huffman.h"
#include "msg_huffman_data.h"
#include "sys_time.h"

// KisakCOD ABI port: the only Win32 dependency was the DWORD locals in
// MSG_initHuffmanInternal, now spelled uint32_t (identical width/layout on
// MSVC), so the include is guarded and the TU is target-neutral.
#if defined(_WIN32)
#include <Windows.h>
#endif
#include <bgame/bg_local.h>
#include "sv_msg_write_mp.h"
#include <server_mp/server_mp.h>
#ifndef KISAK_DEDI_HEADLESS
#include <client_mp/client_mp.h>
#include <cgame/cg_local.h>
#endif

#define NETF_OBJ(x) NETF_BASE(objective_t, x)

static int __cdecl MSG_GetClientShownet()
{
#ifdef KISAK_DEDI_HEADLESS
    return 0;
#else
    return cl_shownet ? cl_shownet->current.integer : 0;
#endif
}

static bool __cdecl MSG_ShouldPrintClientNetDebug(int exactValue)
{
    const int shownet = MSG_GetClientShownet();
    return shownet >= 2 || shownet == exactValue;
}

static bool __cdecl MSG_IsClientShownet(int value)
{
    return MSG_GetClientShownet() == value;
}

static float __cdecl MSG_GetMapCenterAxis(int axis)
{
#ifdef KISAK_DEDI_HEADLESS
    (void)axis;
    return 0.0f;
#else
    return (*CL_GetMapCenter())[axis];
#endif
}

static bool __cdecl MSG_RestorePredictedOriginForServerTime(int localClientNum, playerState_s *to)
{
#ifdef KISAK_DEDI_HEADLESS
    (void)localClientNum;
    (void)to;
    return true;
#else
    clientActive_t *client = CL_GetLocalClientGlobals(localClientNum);
    return CL_GetPredictedOriginForServerTime(
        client,
        to->commandTime,
        to->origin,
        to->velocity,
        to->viewangles,
        &to->bobCycle,
        &to->movementDir);
#endif
}

static const NetField objectiveFields[6] =
{
  { NETF_OBJ(origin[0]), 0, 0u },
  { NETF_OBJ(origin[1]), 0, 0u },
  { NETF_OBJ(origin[2]), 0, 0u },
  { NETF_OBJ(icon), 12, 0u },
  { NETF_OBJ(entNum), 10, 0u },
  { NETF_OBJ(teamNum), 4, 0u }
}; // idb

//  struct netFieldOrderInfo_t orderInfo 82f87210     msg_mp.obj
//  uint32_t *huffBytesSeen      82f878d0     msg_mp.obj
//  struct huffman_t msgHuff   82f87cd8     msg_mp.obj







void __cdecl MSG_SetDefaultUserCmd(playerState_s *ps, usercmd_s *cmd)
{
    int i; // [esp+0h] [ebp-4h]

    cmd->serverTime = 0;
    cmd->buttons = 0;
    cmd->angles[0] = 0;
    cmd->angles[1] = 0;
    cmd->angles[2] = 0;
    *(uint32_t *)&cmd->weapon = 0;
    cmd->meleeChargeYaw = 0.0;
    *(uint32_t *)&cmd->meleeChargeDist = 0;
    cmd->weapon = ps->weapon;
    cmd->offHandIndex = ps->offHandIndex;
    for (i = 0; i < 2; ++i)
        cmd->angles[i] = (uint16_t)(int)((ps->viewangles[i] - ps->delta_angles[i]) * 182.0444488525391);
    if ((ps->otherFlags & 4) != 0)
    {
        if ((ps->eFlags & 8) != 0)
        {
            cmd->buttons |= 0x100u;
        }
        else if ((ps->eFlags & 4) != 0)
        {
            cmd->buttons |= 0x200u;
        }
        if (ps->leanf <= 0.0)
        {
            if (ps->leanf < 0.0)
                cmd->buttons |= 0x40u;
        }
        else
        {
            cmd->buttons |= 0x80u;
        }
        if (ps->fWeaponPosFrac != 0.0)
            cmd->buttons |= 0x800u;
        if ((ps->pm_flags & PMF_SPRINTING) != 0)
            cmd->buttons |= 2u;
    }
}



int __cdecl MSG_ReadEntityIndex(msg_t *msg, uint32_t indexBits)
{
    if (MSG_ReadBit(msg))
    {
        if (msg_printEntityNums->current.enabled)
            Com_Printf(16, "Entity num: 1 bit (inc)\n");
        ++msg->lastEntityRef;
    }
    else if (indexBits != 10 || MSG_ReadBit(msg))
    {
        if (msg_printEntityNums->current.enabled)
            Com_Printf(16, "Entity num: %i bits (full)\n", indexBits + 2);
        msg->lastEntityRef = MSG_ReadBits(msg, indexBits);
    }
    else
    {
        if (msg_printEntityNums->current.enabled)
            Com_Printf(16, "Entity num: %i bits (delta)\n", 6);
        msg->lastEntityRef += MSG_ReadBits(msg, 4u);
    }
    if (msg_printEntityNums->current.enabled)
        Com_Printf(16, "Read entity num %i\n", msg->lastEntityRef);
    if (msg->lastEntityRef < 0)
    {
        msg->overflowed = 1;
        return 0;
    }
    return msg->lastEntityRef;
}

void __cdecl MSG_ReadDeltaField(
    msg_t *msg,
    int time,
    const char * const from,
    char *to,
    const NetField *field,
    int print,
    bool noXor)
{
    int Bit; // eax
    int Byte; // eax
    int Long; // eax
    int v10; // eax
    int v11; // eax
    int v12; // eax
    int v13; // eax
    int v14; // eax
    int DeltaTime; // eax
    int DeltaGroundEntity; // eax
    int DeltaEventParamField; // eax
    int v18; // eax
    double OriginFloat; // st7
    double OriginZFloat; // st7
    double Angle16; // st7
    double v22; // st7
    int v23; // eax
    int v24; // [esp+8h] [ebp-50h]
    int v25; // [esp+Ch] [ebp-4Ch]
    const hudelem_color_t *fromColor; // [esp+24h] [ebp-34h]
    hudelem_color_t *toColora; // [esp+28h] [ebp-30h]
    hudelem_color_t *toColor; // [esp+28h] [ebp-30h]
    int j; // [esp+2Ch] [ebp-2Ch]
    int zeroVal; // [esp+30h] [ebp-28h] BYREF
    int trunc; // [esp+34h] [ebp-24h]
    int rawValue; // [esp+38h] [ebp-20h]
    int *toF; // [esp+3Ch] [ebp-1Ch]
    int bits; // [esp+40h] [ebp-18h]
    const int *fromF; // [esp+44h] [ebp-14h]
    int mask; // [esp+48h] [ebp-10h]
    int sgn; // [esp+4Ch] [ebp-Ch]
    int partialBits; // [esp+50h] [ebp-8h]
    int value; // [esp+54h] [ebp-4h]

    zeroVal = 0;
    if (noXor)
        fromF = &zeroVal;
    else
        fromF = (const int *)&from[field->offset];
    toF = (int *)&to[field->offset];
    iassert( !msg->overflowed );
    if (field->changeHints != 2 && !MSG_ReadBit(msg))
    {
        *toF = *fromF;
        return;
    }
    iassert( !msg->overflowed );
    switch (field->bits)
    {
    case 0:
        if (MSG_ReadBit(msg))
        {
            if (MSG_ReadBit(msg))
            {
                Long = MSG_ReadLong(msg);
                *toF = Long;
                *toF ^= *fromF;
                if (print)
                    Com_Printf(16, "%s:%f ", field->name, *(float *)toF);
            }
            else
            {
                trunc = MSG_ReadBits(msg, 5u);
                Byte = MSG_ReadByte(msg);
                trunc += 32 * Byte;
                trunc ^= (int)*(float *)fromF + 4096;
                trunc -= 4096;
                *(float *)toF = (float)trunc;
                if (print)
                    Com_Printf(16, "%s:%i ", field->name, trunc);
            }
        }
        else
        {
            Bit = MSG_ReadBit(msg);
            *toF = Bit << 31;
            iassert( *reinterpret_cast< float * >( toF ) == 0.0f );
        }
        return;
    case static_cast<int>(0xFFFFFFA7):
        if (MSG_ReadBit(msg))
        {
            v11 = MSG_ReadLong(msg);
            *toF = v11;
            *toF ^= *fromF;
            if (print)
                Com_Printf(16, "%s:%f ", field->name, *(float *)toF);
        }
        else
        {
            trunc = MSG_ReadBits(msg, 5u);
            v10 = MSG_ReadByte(msg);
            trunc += 32 * v10;
            trunc ^= (int)*(float *)fromF + 4096;
            trunc -= 4096;
            *(float *)toF = (float)trunc;
            if (print)
                Com_Printf(16, "%s:%i ", field->name, trunc);
        }
        return;
    case static_cast<int>(0xFFFFFFA8):
        v12 = MSG_ReadLong(msg);
        *toF = v12;
        *toF ^= *fromF;
        if (print)
            Com_Printf(16, "%s:%f ", field->name, *(float *)toF);
        return;
    case static_cast<int>(0xFFFFFF9D):
        if (MSG_ReadBit(msg))
        {
            if (MSG_ReadBit(msg))
            {
                value = MSG_ReadLong(msg);
                value ^= *fromF;
                *toF = value;
                if (print)
                    Com_Printf(16, "%s:%f ", field->name, *(float *)toF);
            }
            else
            {
                trunc = MSG_ReadBits(msg, 4u);
                v13 = MSG_ReadByte(msg);
                trunc += 16 * v13;
                trunc ^= (int)*(float *)fromF + 2048;
                trunc -= 2048;
                *(float *)toF = (float)trunc;
                if (print)
                    Com_Printf(16, "%s:%i ", field->name, trunc);
            }
        }
        else
        {
            *toF = 0;
            iassert( *reinterpret_cast< float * >( toF ) == 0.0f );
        }
        if ((uint32_t)(__int64)(*(float *)toF + 2048.0) >= 0x1000)
            MyAssertHandler(
                ".\\qcommon\\msg_mp.cpp",
                1476,
                0,
                "*(float *)toF + HUDELEM_COORD_BIAS doesn't index 1 << HUDELEM_COORD_BITS\n\t%i not in [0, %i)",
                (int)(*(float *)toF + 2048.0),
                4096);
        return;
    case static_cast<int>(0xFFFFFF9E):
        v14 = MSG_Read24BitFlag(msg, *fromF);
        *toF = v14;
        return;
    case static_cast<int>(0xFFFFFF9F):
        DeltaTime = MSG_ReadDeltaTime(msg, time);
        *toF = DeltaTime;
        return;
    case static_cast<int>(0xFFFFFFA0):
        DeltaGroundEntity = MSG_ReadDeltaGroundEntity(msg);
        *toF = DeltaGroundEntity;
        return;
    case static_cast<int>(0xFFFFFFA2):
    case static_cast<int>(0xFFFFFFA3):
        DeltaEventParamField = MSG_ReadDeltaEventParamField(msg);
        *toF = DeltaEventParamField;
        return;
    case static_cast<int>(0xFFFFFFA1):
        v18 = MSG_ReadBits(msg, 7u);
        *toF = 100 * v18;
        return;
    case static_cast<int>(0xFFFFFFA4):
    case static_cast<int>(0xFFFFFFA5):
        OriginFloat = MSG_ReadOriginFloat(field->bits, msg, *(float *)fromF);
        *(float *)toF = OriginFloat;
        if (print)
            Com_Printf(16, "%s:%f ", field->name, *(float *)toF);
        return;
    case static_cast<int>(0xFFFFFFA6):
        OriginZFloat = MSG_ReadOriginZFloat(msg, *(float *)fromF);
        *(float *)toF = OriginZFloat;
        if (print)
            Com_Printf(16, "%s:%f ", field->name, *(float *)toF);
        return;
    case static_cast<int>(0xFFFFFF9C):
        if (!MSG_ReadBit(msg))
        {
            *(float *)toF = 0.0;
            return;
        }
        goto LABEL_74;
    case static_cast<int>(0xFFFFFFA9):
    LABEL_74:
        Angle16 = MSG_ReadAngle16(msg);
        *(float *)toF = Angle16;
        return;
    case static_cast<int>(0xFFFFFFAA):
        v22 = (double)MSG_ReadBits(msg, 5u) * 1.0 / 10.0 + 1.399999976158142;
        *(float *)toF = v22;
        break;
    case static_cast<int>(0xFFFFFFAB):
        if (MSG_ReadBit(msg))
        {
            fromColor = (const hudelem_color_t *)fromF;
            toColora = (hudelem_color_t *)toF;
            *toF = *fromF;
            toColora->a = fromColor->a != 0 ? 0 : -1;
        }
        else
        {
            toColor = (hudelem_color_t *)toF;
            if (!MSG_ReadBit(msg))
            {
                toColor->r = MSG_ReadByte(msg);
                toColor->g = MSG_ReadByte(msg);
                toColor->b = MSG_ReadByte(msg);
            }
            toColor->a = 8 * MSG_ReadBits(msg, 5u);
        }
        break;
    default:
        if (MSG_ReadBit(msg))
        {
            sgn = field->bits < 0;
            if (sgn)
                v25 = -field->bits;
            else
                v25 = field->bits;
            bits = v25;
            partialBits = v25 & 7;
            if ((v25 & 7) != 0)
                v24 = MSG_ReadBits(msg, partialBits);
            else
                v24 = 0;
            rawValue = v24;
            for (j = partialBits; j < bits; j += 8)
            {
                v23 = MSG_ReadByte(msg);
                rawValue |= v23 << j;
            }
            iassert( (bits <= 32) );
            if (bits == 32)
                mask = -1;
            else
                mask = (1 << bits) - 1;
            value = rawValue ^ mask & *fromF;
            if (sgn && (value & (1 << (bits - 1))) != 0)
                value |= ~mask;
            if (print)
                Com_Printf(16, "%s:%i ", field->name, *toF);
            *toF = value;
        }
        else
        {
            *toF = 0;
        }
        break;
    }
}

int __cdecl MSG_ReadDeltaTime(msg_t *msg, int timeBase)
{
    if (MSG_ReadBit(msg))
        return MSG_ReadLong(msg);
    else
        return timeBase - MSG_ReadBits(msg, 8u);
}

int __cdecl MSG_ReadDeltaGroundEntity(msg_t *msg)
{
    int j; // [esp+4h] [ebp-10h]
    int value; // [esp+10h] [ebp-4h]

    if (MSG_ReadBit(msg) == 1)
        return ENTITYNUM_WORLD;
    if (MSG_ReadBit(msg) == 1)
        return 0;
    value = MSG_ReadBits(msg, 2u);
    for (j = 2; j < 10; j += 8)
        value |= MSG_ReadByte(msg) << j;
    return value;
}

int __cdecl MSG_ReadDeltaEventParamField(msg_t *msg)
{
    return MSG_ReadByte(msg);
}

int __cdecl MSG_Read24BitFlag(msg_t *msg, int oldFlags)
{
    uint32_t bitChanged; // [esp+0h] [ebp-10h]
    int j; // [esp+4h] [ebp-Ch]
    int value; // [esp+Ch] [ebp-4h]

    iassert( !msg->overflowed );
    if (MSG_ReadBit(msg) == 1)
    {
        value = 0;
        for (j = 0; j < 24; j += 8)
            value |= MSG_ReadByte(msg) << j;
    }
    else
    {
        bitChanged = MSG_ReadBits(msg, 5u);
        if (bitChanged > 0x18)
        {
            msg->overflowed = 1;
            return oldFlags;
        }
        return oldFlags ^ (1 << bitChanged);
    }
    return value;
}

double __cdecl MSG_ReadOriginFloat(int bits, msg_t *msg, float oldValue)
{
    int roundedCenter; // [esp+8h] [ebp-14h]
    int index; // [esp+Ch] [ebp-10h]

    if (MSG_ReadBit(msg))
    {
        if (bits == -92)
        {
            index = 0;
        }
        else
        {
            iassert( bits == MSG_FIELD_ORIGINY );
            index = 1;
        }
        roundedCenter = (int)(MSG_GetMapCenterAxis(index) + 0.5f);
        return (float)(roundedCenter + (((int)oldValue + 0x8000 - roundedCenter) ^ MSG_ReadBits(msg, 0x10u)) - 0x8000);
    }
    else
    {
        return (float)((double)(MSG_ReadBits(msg, 7u) - 64) + oldValue);
    }
}

double __cdecl MSG_ReadOriginZFloat(msg_t *msg, float oldValue)
{
    int roundedCenter; // [esp+8h] [ebp-10h]

    if (MSG_ReadBit(msg))
    {
        roundedCenter = (int)(MSG_GetMapCenterAxis(2) + 0.5f);
        return (float)(roundedCenter + (((int)oldValue + 0x8000 - roundedCenter) ^ MSG_ReadBits(msg, 0x10u)) - 0x8000);
    }
    else
    {
        return (float)((double)(MSG_ReadBits(msg, 7u) - 64) + oldValue);
    }
}

int __cdecl MSG_ReadDeltaEntity(msg_t *msg, int time, entityState_s *from, entityState_s *to, uint32_t number)
{
    return MSG_ReadDeltaEntityStruct(msg, time, (char *)from, (char *)to, number);
}

int __cdecl MSG_ReadDeltaEntityStruct(msg_t *msg, int time, char *from, char *to, uint32_t number)
{
    char *EntityTypeName; // eax
    const NetFieldList *stateFieldList; // [esp+38h] [ebp-20h]
    int print; // [esp+3Ch] [ebp-1Ch]
    const NetField *field; // [esp+40h] [ebp-18h]
    const NetField *fielda; // [esp+40h] [ebp-18h]
    uint32_t lc; // [esp+44h] [ebp-14h]
    const NetField *stateFields; // [esp+48h] [ebp-10h]
    uint32_t i; // [esp+54h] [ebp-4h]
    uint32_t ia; // [esp+54h] [ebp-4h]

    iassert( number < (1 << GENTITYNUM_BITS) );
    if (MSG_ReadBit(msg) == 1)
    {
        if (MSG_ShouldPrintClientNetDebug(-1))
            Com_Printf(16, "%3i: #%-3i remove\n", msg->readcount, number);
        return 1;
    }
    else if (MSG_ReadBit(msg))
    {
        lc = MSG_ReadLastChangedField(msg, 61);
        if (MSG_ShouldPrintClientNetDebug(-1))
        {
            print = 1;
            Com_Printf(16, "%3i: #%-3i ", msg->readcount, *(uint32_t *)to);
        }
        else
        {
            print = 0;
        }
        *(uint32_t *)to = number;
        if (strcmp(entityStateFields[0].name, "eType"))
            MyAssertHandler(".\\qcommon\\msg_mp.cpp", 1763, 0, "%s", "strcmp( entityStateFields[0].name, \"eType\" ) == 0");
        MSG_ReadDeltaField(msg, time, from, to, entityStateFields, print, 0);
        stateFieldList = MSG_GetStateFieldListForEntityType(*((uint32_t *)to + 1));
        stateFields = stateFieldList->array;
        if (lc <= stateFieldList->count)
        {
            if (msg_dumpEnts->current.enabled)
            {
                EntityTypeName = BG_GetEntityTypeName(*((uint32_t *)to + 1));
                Com_Printf(14, "%3i: changed ent, eType %s\n", number, EntityTypeName);
            }
            if (strcmp(stateFields->name, "eType"))
                MyAssertHandler(".\\qcommon\\msg_mp.cpp", 1782, 0, "%s", "strcmp( stateFields[0].name, \"eType\" ) == 0");
            i = 1;
            field = stateFields + 1;
            while (i < lc)
            {
                MSG_ReadDeltaField(msg, time, from, to, field, print, 0);
                ++i;
                ++field;
            }
            ia = lc;
            fielda = &stateFields[lc];
            while (ia < stateFieldList->count)
            {
                *(uint32_t *)&to[fielda->offset] = *(uint32_t *)&from[fielda->offset];
                ++ia;
                ++fielda;
            }
            return 0;
        }
        else
        {
            msg->overflowed = 1;
            return 0;
        }
    }
    else
    {
        memcpy(to, from, 0xF4u);
        return 0;
    }
}

int __cdecl MSG_ReadLastChangedField(msg_t *msg, int totalFields)
{
    int lastChanged; // [esp+0h] [ebp-8h]
    uint32_t idealBits; // [esp+4h] [ebp-4h]

    idealBits = GetMinBitCountForNum(totalFields);
    lastChanged = MSG_ReadBits(msg, idealBits);
    if (lastChanged < 0 || lastChanged > totalFields)
    {
        msg->overflowed = 1;
        return 0;
    }
    return lastChanged;
}


const int numEntityStateFields = 59;
const int numEventEntityStateFields = 59;
const int numPlayerEntityStateFields = 59;
const int numCorpseEntityStateFields = 59;
const int numVehicleEntityStateFields = 59;
const int numItemEntityStateFields = 59;
const int numSoundBlendEntityStateFields = 59;
const int numLoopFxEntityStateFields = 59;
const int numMissileEntityStateFields = 59;
const int numArchivedEntityFields = 69;
const int numClientStateFields = 24;
const int numPlayerStateFields = 141;
const int numObjectiveFields = 6;
const int numHudElemFields = 40;


int __cdecl MSG_ReadDeltaArchivedEntity(
    msg_t *msg,
    int time,
    archivedEntity_s *from,
    archivedEntity_s *to,
    uint32_t number)
{
    return MSG_ReadDeltaStruct(
        msg,
        time,
        (char *)from,
        (char *)to,
        number,
        numArchivedEntityFields,
        10,
        archivedEntityFields,
        numArchivedEntityFields);
}

int __cdecl MSG_ReadDeltaStruct(
    msg_t *msg,
    int time,
    char *from,
    char *to,
    uint32_t number,
    int numFields,
    char indexBits,
    const NetField *stateFields,
    int totalFields)
{
    int print; // [esp+0h] [ebp-18h]
    const NetField *field; // [esp+4h] [ebp-14h]
    const NetField *fielda; // [esp+4h] [ebp-14h]
    int lc; // [esp+8h] [ebp-10h]
    int i; // [esp+14h] [ebp-4h]
    int ia; // [esp+14h] [ebp-4h]

    iassert( number < (1u << indexBits) );
    if (MSG_ReadBit(msg) == 1)
    {
        if (MSG_ShouldPrintClientNetDebug(-1))
            Com_Printf(16, "%3i: #%-3i remove\n", msg->readcount, number);
        return 1;
    }
    else if (MSG_ReadBit(msg))
    {
        lc = MSG_ReadLastChangedField(msg, totalFields);
        if (lc <= numFields)
        {
            if (MSG_ShouldPrintClientNetDebug(-1))
            {
                print = 1;
                Com_Printf(16, "%3i: #%-3i ", msg->readcount, *(uint32_t *)to);
            }
            else
            {
                print = 0;
            }
            *(uint32_t *)to = number;
            i = 0;
            field = stateFields;
            while (i < lc)
            {
                MSG_ReadDeltaField(msg, time, from, to, field, print, 0);
                ++i;
                ++field;
            }
            ia = lc;
            fielda = &stateFields[lc];
            while (ia < numFields)
            {
                *(uint32_t *)&to[fielda->offset] = *(uint32_t *)&from[fielda->offset];
                ++ia;
                ++fielda;
            }
            return 0;
        }
        else
        {
            msg->overflowed = 1;
            return 0;
        }
    }
    else
    {
        memcpy((uint8_t *)to, (uint8_t *)from, 4 * numFields + 4);
        return 0;
    }
}

int __cdecl MSG_ReadDeltaClient(msg_t *msg, int time, clientState_s *from, clientState_s *to, uint32_t number)
{
    clientState_s dummy; // [esp+4h] [ebp-70h] BYREF

    if (!from)
    {
        from = &dummy;
        memset((uint8_t *)&dummy, 0, sizeof(dummy));
    }
    return MSG_ReadDeltaStruct(
        msg,
        time,
        (char *)from,
        (char *)to,
        number,
        numClientStateFields,
        6,
        clientStateFields,
        numClientStateFields);
}

static void __cdecl MSG_ReadDeltaFields(
    msg_t *msg,
    int time,
    const char *const from,
    char *to,
    int numFields,
    const NetField *stateFields)
{
    if (MSG_ReadBit(msg))
    {
        for (int i = 0; i < numFields; ++i)
            MSG_ReadDeltaField(msg, time, from, to, &stateFields[i], 0, 0);
    }
    // LWSS: redundant since the caller does memcpy(to, from) on the entire block
    //else
    //{
    //    for (int i = 0; i < numFields; ++i)
    //        *(uint32_t *)&to[stateFields[i].offset] = *(uint32_t *)&from[stateFields[i].offset];
    //}
}

static void __cdecl MSG_ReadDeltaHudElems(msg_t *msg, int time, const hudelem_s *from, hudelem_s *to, int count)
{
    uint32_t j; // [esp+8h] [ebp-18h]
    uint32_t lc; // [esp+Ch] [ebp-14h]

    if (count != 31)
        MyAssertHandler(
            ".\\qcommon\\msg_mp.cpp",
            1884,
            0,
            "%s",
            "count == MAX_HUDELEMS_ARCHIVAL || count == MAX_HUDELEMS_CURRENT");

    int inuse = MSG_ReadBits(msg, 5u);

    for (int i = 0; i < inuse; ++i)
    {
        lc = MSG_ReadBits(msg, 6);
        if (lc >= static_cast<uint32_t>(numHudElemFields))
        {
            msg->overflowed = 1;
            return;
        }

        for (j = 0; j <= lc; ++j)
            MSG_ReadDeltaField(msg, time, (const char *)&from[i], (char *)&to[i], &hudElemFields[j], 0, 0);

        // Redundant since caller does memcpy(to, from)
        //while (j < numHudElemFields)
        //{
        //    *(&to[i].type + hudElemFields[j].offset) = *(&from[i].type + hudElemFields[j].offset);
        //    ++j;
        //}

        iassert(!(from[i].alignOrg & ~15));
        iassert(!(to[i].alignOrg & ~15));

        {
            int alignX = ((from[i].alignOrg >> 2) & 3);
            int alignY = (from[i].alignOrg & 3);
            iassert(alignX == 0 || alignX == 1 || alignX == 2);
            iassert(alignY == 0 || alignY == 1 || alignY == 2);
        }
        {
            int alignX = ((to[i].alignOrg >> 2) & 3);
            int alignY = (to[i].alignOrg & 3);
            iassert(alignX == 0 || alignX == 1 || alignX == 2);
            iassert(alignY == 0 || alignY == 1 || alignY == 2);
        }
    }

    while (inuse < count && to[inuse].type)
    {
        memset(&to[inuse], 0, sizeof(hudelem_s));
        iassert(to[inuse].type == HE_TYPE_FREE);
        ++inuse;
    }
}

void __cdecl MSG_ReadDeltaPlayerstate(
    int localClientNum,
    msg_t *msg,
    int time,
    const playerState_s *from,
    playerState_s *to,
    bool predictedFieldsIgnoreXor)
{
    int Short; // eax
    int v7; // eax
    objectiveState_t v8; // eax
    uint8_t Byte; // al
    int i; // [esp+20h] [ebp-2F98h]
    int k; // [esp+20h] [ebp-2F98h]
    int print; // [esp+24h] [ebp-2F94h]
    int LastChangedField; // [esp+30h] [ebp-2F88h]
    int Bits; // [esp+2FA8h] [ebp-10h]
    int *v19; // [esp+2FACh] [ebp-Ch]
    bool lc; // [esp+2FB3h] [ebp-5h]

    uint8_t dst[sizeof(playerState_s) + 8]; // [esp+38h] [ebp-2F80h] BYREF

    if (!from)
    {
        from = (playerState_s *)dst;
        memset(dst, 0, sizeof(playerState_s));
    }

    // Copy entire `from` into `to`
    memcpy(to, from, sizeof(playerState_s));

    if (MSG_ShouldPrintClientNetDebug(-2))
    {
        print = 1;
        Com_Printf(16, "%3i: playerstate ", msg->readcount);
    }
    else
    {
        print = 0;
    }

    lc = MSG_ReadBit(msg) > 0;
    LastChangedField = MSG_ReadLastChangedField(msg, numPlayerStateFields);

    {
        int itr = 0;
        NetField *field = (NetField *)playerStateFields;
        while (itr < LastChangedField)
        {
            iassert(!msg->overflowed);

            if (predictedFieldsIgnoreXor && lc && field->changeHints == 3)
                MSG_ReadDeltaField(msg, time, (const char *)from, (char *)to, field, print, 1);
            else
                MSG_ReadDeltaField(msg, time, (const char *)from, (char *)to, field, print, 0);

            iassert(!msg->overflowed);
            ++itr;
            ++field;
        }
    }

    // LWSS: this is made redundant via the memcpy at the start (#36)
    //{
    //    int itr = LastChangedField;
    //    NetField *field = (NetField *)&playerStateFields[LastChangedField];
    //    while (itr < numPlayerStateFields)
    //    {
    //        v19 = (int *)((char *)&from->commandTime + field->offset);
    //        *(int *)((char *)&to->commandTime + field->offset) = *v19;
    //        ++itr;
    //        ++field;
    //    }
    //}


    if (!lc)
    {
        if (!MSG_RestorePredictedOriginForServerTime(localClientNum, to))
        {
            Com_PrintError(14, "Unable to find the origin we sent, delta is not going to work");
            // LWSS: if the above function returns false, there is no data set in `to`. 
            // The below copy code is redundant via the memcpy at the top (#36)
            //to->origin[0] = from->origin[0];
            //to->origin[1] = from->origin[1];
            //to->origin[2] = from->origin[2];
            //to->velocity[0] = from->velocity[0];
            //to->velocity[1] = from->velocity[1];
            //to->velocity[2] = from->velocity[2];
            //to->bobCycle = from->bobCycle;
            //to->movementDir = from->movementDir;
            //to->viewangles[0] = from->viewangles[0];
            //to->viewangles[1] = from->viewangles[1];
            //to->viewangles[2] = from->viewangles[2];
        }
    }

    if (MSG_ReadBit(msg))
    {
        if (MSG_IsClientShownet(4))
            Com_Printf(16, "%s ", "PS_STATS");
        Bits = MSG_ReadBits(msg, 5u);
        if ((Bits & 1) != 0)
            to->stats[0] = MSG_ReadShort(msg);
        if ((Bits & 2) != 0)
            to->stats[1] = MSG_ReadShort(msg);
        if ((Bits & 4) != 0)
            to->stats[2] = MSG_ReadShort(msg);
        if ((Bits & 8) != 0)
            to->stats[3] = MSG_ReadBits(msg, 6u);
        if ((Bits & 0x10) != 0)
            to->stats[4] = MSG_ReadByte(msg);
    }

    if (MSG_ReadBit(msg))
    {
        for (i = 0; i < 4; ++i)
        {
            if (MSG_ReadBit(msg))
            {
                if (MSG_IsClientShownet(4))
                    Com_Printf(16, "%s ", "PS_AMMO");
                Bits = MSG_ReadShort(msg);
                for (int j = 0; j < 16; ++j)
                {
                    if ((Bits & (1 << j)) != 0)
                    {
                        Short = MSG_ReadShort(msg);
                        to->ammo[16 * i + j] = Short;
                    }
                }
            }
        }
    }

    for (k = 0; k < 8; ++k)
    {
        if (MSG_ReadBit(msg))
        {
            if (MSG_IsClientShownet(4))
                Com_Printf(16, "%s ", "PS_AMMOCLIP");
            Bits = MSG_ReadShort(msg);
            for (int j = 0; j < 16; ++j)
            {
                if ((Bits & (1 << j)) != 0)
                {
                    v7 = MSG_ReadShort(msg);
                    to->ammoclip[16 * k + j] = v7;
                }
            }
        }
    }

    if (MSG_ReadBit(msg))
    {
        for (int j = 0; j < MAX_OBJECTIVES; ++j)
        {
            to->objective[j].state = (objectiveState_t)MSG_ReadBits(msg, 3);
            MSG_ReadDeltaFields(
                msg,
                time,
                (const char *)&from->objective[j],
                (char *)&to->objective[j],
                numObjectiveFields,
                objectiveFields);
        }
    }

    if (MSG_ReadBit(msg))
    {
        MSG_ReadDeltaHudElems(msg, time, from->hud.archival, to->hud.archival, 31);
        MSG_ReadDeltaHudElems(msg, time, from->hud.current, to->hud.current, 31);
    }

    if (MSG_ReadBit(msg))
    {
        for (int j = 0; j < 128; ++j)
        {
            Byte = MSG_ReadByte(msg);
            to->weaponmodels[j] = Byte;
        }
    }
}







void __cdecl MSG_DumpNetFieldChanges_f()
{
    int arraySize[6]; // [esp+0h] [ebp-58h]
    const int *array; // [esp+18h] [ebp-40h]
    int iSize; // [esp+1Ch] [ebp-3Ch]
    const char *arrayNames[6]; // [esp+20h] [ebp-38h]
    int i; // [esp+38h] [ebp-20h]
    uint32_t iArrayNum; // [esp+3Ch] [ebp-1Ch]
    const int *changeArray[6]; // [esp+40h] [ebp-18h]

    changeArray[0] = (const int *)&orderInfo;
    changeArray[1] = orderInfo.arcEntState;
    changeArray[2] = orderInfo.clientState;
    changeArray[3] = orderInfo.playerState;
    changeArray[4] = orderInfo.objective;
    changeArray[5] = orderInfo.hudElem;
    arraySize[0] = 64;
    arraySize[1] = 128;
    arraySize[2] = 32;
    arraySize[3] = 160;
    arraySize[4] = 8;
    arraySize[5] = 40;
    arrayNames[0] = "Entity State";
    arrayNames[1] = "Archived Entity State";
    arrayNames[2] = "Client State";
    arrayNames[3] = "Player State";
    arrayNames[4] = "Objective";
    arrayNames[5] = "HUD Elem";
    Com_Printf(0, "========================================\n");
    Com_Printf(0, "NetField changes. format: field# : #changes\n");
    for (iArrayNum = 0; iArrayNum < 6; ++iArrayNum)
    {
        Com_Printf(0, "========================================\n");
        Com_Printf(0, "    %s\n", arrayNames[iArrayNum]);
        Com_Printf(0, "--------------------\n");
        array = changeArray[iArrayNum];
        iSize = arraySize[iArrayNum];
        for (i = 0; i < iSize; ++i)
        {
            if (array[i])
                Com_Printf(0, "%3i :%8i\n", i, array[i]);
        }
    }
    Com_Printf(0, "========================================\n");
    Com_Printf(0, "========================================\n");
}
