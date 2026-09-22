// KisakCOD (ki-pyb5): MP usercmd delta codec translation unit.
//
// VERBATIM code motion from msg_mp.cpp (see msg_bits_mp.cpp for the split
// rationale). This TU holds MSG_WriteDeltaUsercmdKey /
// MSG_ReadDeltaUsercmdKey with the static MSG_HorMoveTo / MSG_HorMoveFrom
// helpers and the four static per-arm encode/decode helpers, so the whole
// usercmd wire contract stays in one place.
//
// Deliberate text deviations from the verbatim bodies (all
// behavior-preserving; see the PR disposition comment):
//   - The retail-size MSG_WriteDeltaUsercmdKey and MSG_ReadDeltaUsercmdKey
//     bodies are split: the identical-field predicates become
//     MSG_UsercmdCoreFieldsEqual, and each encode/decode branch becomes a
//     static MSG_WriteUsercmdDeltaArm{Small,Full} /
//     MSG_ReadUsercmdDeltaArm{Small,Full} helper. Statement order, keys,
//     field widths and call sequences are unchanged, so the emitted and
//     accepted byte streams are identical (pinned by
//     kisakcod-msg-wire-contract-tests).
//   - MSG_ReadDeltaUsercmdKey copies usercmd_s with struct assignment
//     rather than memcpy(to, from, sizeof(usercmd_s)) (a static-analysis
//     buffer-copy finding at the retail call site); usercmd_s is a plain
//     POD struct, so the copy is field-identical and the wire bytes are
//     unchanged.

#ifndef KISAK_MP
static_assert(false, "This File is MultiPlayer Only");
#endif

#include "msg_mp.h"
#include "huffman.h"
#include "msg_huffman_data.h"
#include "sys_time.h"
#include "sv_msg_write_mp.h"

// Declared in msg_mp.h: the capture contract tests express the wire-visible
// movement expectation through this quantizer (the retail decompiled body is
// unchanged; linkage only was static).
int __cdecl MSG_HorMoveTo(int iForwardMove, int iRightMove)
{
    int iFlags; // [esp+0h] [ebp-4h]

    iFlags = 0;
    if (iForwardMove <= 10)
    {
        if (iForwardMove < -10)
            iFlags = 2;
    }
    else
    {
        iFlags = 1;
    }
    if (iRightMove > 10)
        return iFlags | 4;
    if (iRightMove < -10)
        return iFlags | 8;
    return iFlags;
}

void __cdecl MSG_HorMoveFrom(char iFlags, char *pForwardMove, char *pRightMove)
{
    if ((iFlags & 1) != 0)
    {
        *pForwardMove = 127;
    }
    else if ((iFlags & 2) != 0)
    {
        *pForwardMove = -127;
    }
    else
    {
        *pForwardMove = 0;
    }
    if ((iFlags & 4) != 0)
    {
        *pRightMove = 127;
    }
    else if ((iFlags & 8) != 0)
    {
        *pRightMove = -127;
    }
    else
    {
        *pRightMove = 0;
    }
}

static bool __cdecl MSG_UsercmdCoreFieldsEqual(const usercmd_s *from, const usercmd_s *to)
{
    return from->buttons >> 1 == to->buttons >> 1
        && from->weapon == to->weapon
        && from->offHandIndex == to->offHandIndex
        && from->angles[2] == to->angles[2]
        && to->meleeChargeYaw == from->meleeChargeYaw
        && from->meleeChargeDist == to->meleeChargeDist;
}

// Encode arm for commands whose "core" fields (buttons>>1, weapon, offhand,
// angles[2], melee state) are unchanged from *from; only the flag bit, pose
// angles and horizontal move can differ. Emits the same bytes as the retail
// inner else-branch of MSG_WriteDeltaUsercmdKey.
static void __cdecl MSG_WriteUsercmdDeltaArmSmall(
    msg_t *msg,
    int key,
    const usercmd_s *from,
    const usercmd_s *to,
    int horFromMove,
    int horToMove)
{
    int keyb; // [esp+1Ch] [ebp+Ch]

    MSG_WriteKey(msg, key, 1, 1u);
    MSG_WriteKey(msg, key, 0, 1u);
    keyb = to->serverTime ^ key;
    MSG_WriteKey(msg, keyb, to->buttons, 1u);
    MSG_WriteDeltaKeyShort(msg, keyb, from->angles[0], to->angles[0]);
    MSG_WriteDeltaKeyShort(msg, keyb, from->angles[1], to->angles[1]);
    MSG_WriteDeltaKey(msg, keyb, horFromMove, horToMove, 4u);
}

// Encode arm for commands whose core fields changed. Emits the same bytes as
// the retail outer else-branch of MSG_WriteDeltaUsercmdKey.
static void __cdecl MSG_WriteUsercmdDeltaArmFull(
    msg_t *msg,
    int key,
    const usercmd_s *from,
    const usercmd_s *to,
    int horFromMove,
    int horToMove)
{
    int keya; // [esp+1Ch] [ebp+Ch]

    MSG_WriteKey(msg, key, 1, 1u);
    MSG_WriteKey(msg, key, 1, 1u);
    MSG_WriteKey(msg, key, to->buttons, 1u);
    MSG_WriteDeltaKeyShort(msg, key, from->angles[0], to->angles[0]);
    MSG_WriteDeltaKeyShort(msg, key, from->angles[1], to->angles[1]);
    MSG_WriteDeltaKey(msg, key, horFromMove, horToMove, 4u);
    keya = to->serverTime ^ key;
    MSG_WriteDeltaKeyShort(msg, keya, from->angles[2], to->angles[2]);
    MSG_WriteDeltaKey(msg, keya, from->buttons >> 1, to->buttons >> 1, 0x14u);
    MSG_WriteDeltaKey(msg, keya, from->weapon, to->weapon, 7u);
    MSG_WriteDeltaKey(msg, keya, from->offHandIndex, to->offHandIndex, 7u);
    if ((to->buttons & 0x10000) != 0)
    {
        MSG_WriteDeltaKeyByte(msg, keya, from->selectedLocation[0], to->selectedLocation[0]);
        MSG_WriteDeltaKeyByte(msg, keya, from->selectedLocation[1], to->selectedLocation[1]);
    }
    if ((to->buttons & 4) != 0)
    {
        MSG_WriteDeltaKeyShort(
            msg,
            keya,
            (int)(from->meleeChargeYaw * 182.0444488525391),
            (int)(to->meleeChargeYaw * 182.0444488525391));
        MSG_WriteDeltaKey(msg, keya, from->meleeChargeDist, to->meleeChargeDist, 8u);
    }
}

void __cdecl MSG_WriteDeltaUsercmdKey(msg_t *msg, int key, const usercmd_s *from, const usercmd_s *to)
{
    int horToMove; // [esp+4h] [ebp-Ch]
    uint32_t delta; // [esp+8h] [ebp-8h]
    int horFromMove; // [esp+Ch] [ebp-4h]

    iassert( !msg->readOnly );
    iassert( from->buttons < (1 << BUTTON_BIT_COUNT) );
    iassert( to->buttons < (1 << BUTTON_BIT_COUNT) );
    iassert( from->weapon < (1 << MAX_WEAPONS_BITS) );
    iassert( to->weapon < (1 << MAX_WEAPONS_BITS) );
    iassert( from->offHandIndex < (1 << MAX_WEAPONS_BITS) );
    iassert( to->offHandIndex < (1 << MAX_WEAPONS_BITS) );
    delta = to->serverTime - from->serverTime;
    if (delta >= 0x100)
    {
        MSG_WriteBit0(msg);
        MSG_WriteLong(msg, to->serverTime);
    }
    else
    {
        MSG_WriteBit1(msg);
        MSG_WriteByte(msg, delta);
    }
    horToMove = MSG_HorMoveTo(to->forwardmove, to->rightmove);
    horFromMove = MSG_HorMoveTo(from->forwardmove, from->rightmove);
    if (MSG_UsercmdCoreFieldsEqual(from, to))
    {
        if (from->angles[0] == to->angles[0]
            && from->angles[1] == to->angles[1]
            && (from->buttons & 1) == (to->buttons & 1)
            && horFromMove == horToMove)
        {
            MSG_WriteKey(msg, key, 0, 1u);
        }
        else
        {
            MSG_WriteUsercmdDeltaArmSmall(msg, key, from, to, horFromMove, horToMove);
        }
    }
    else
    {
        MSG_WriteUsercmdDeltaArmFull(msg, key, from, to, horFromMove, horToMove);
    }
}

// Decode arm for the inner branch (second MSG_ReadKey hit): full delta
// against *from, including the melee payload and the retail button/weapon/
// offhand validation. Reads the same fields in the same order as the retail
// inner body of MSG_ReadDeltaUsercmdKey.
static void __cdecl MSG_ReadUsercmdDeltaArmFull(msg_t *msg, int key, const usercmd_s *from, usercmd_s *to)
{
    char horToMovea; // [esp+Ch] [ebp-8h]
    int horFromMovea; // [esp+10h] [ebp-4h]
    int keya; // [esp+20h] [ebp+Ch]

    to->buttons |= MSG_ReadKey(msg, key, 1u);
    to->angles[0] = (uint16_t)MSG_ReadDeltaKeyShort(msg, key, from->angles[0]);
    to->angles[1] = (uint16_t)MSG_ReadDeltaKeyShort(msg, key, from->angles[1]);
    horFromMovea = MSG_HorMoveTo(from->forwardmove, from->rightmove);
    horToMovea = MSG_ReadDeltaKey(msg, key, horFromMovea, 4u);
    MSG_HorMoveFrom(horToMovea, &to->forwardmove, &to->rightmove);
    keya = to->serverTime ^ key;
    to->angles[2] = (uint16_t)MSG_ReadDeltaKeyShort(msg, keya, from->angles[2]);
    to->buttons &= 1u;
    to->buttons |= 2 * MSG_ReadDeltaKey(msg, keya, from->buttons >> 1, 0x14u);
    to->weapon = MSG_ReadDeltaKey(msg, keya, from->weapon, 7u);
    to->offHandIndex = MSG_ReadDeltaKey(msg, keya, from->offHandIndex, 7u);
    if ((to->buttons & 0x10000) != 0)
    {
        to->selectedLocation[0] = MSG_ReadDeltaKeyByte(msg, keya, from->selectedLocation[0]);
        to->selectedLocation[1] = MSG_ReadDeltaKeyByte(msg, keya, from->selectedLocation[1]);
    }
    if ((to->buttons & 4) != 0)
    {
        to->meleeChargeYaw = (double)MSG_ReadDeltaKeyShort(
            msg,
            keya,
            (uint16_t)(int)(from->meleeChargeYaw * 182.0444488525391))
            * 0.0054931640625;
        to->meleeChargeDist = MSG_ReadDeltaKey(msg, keya, from->meleeChargeDist, 8u);
    }
    if (to->buttons >= 0x200000)
    {
        Com_PrintError(15, "client sent an invalid buttons value %i\n", to->buttons);
        to->buttons = from->buttons;
    }
    if (to->weapon >= 0x80u)
    {
        Com_PrintError(15, "client sent an invalid weapon number %i\n", to->weapon);
        to->weapon = from->weapon;
    }
    if (to->offHandIndex >= 0x80u)
    {
        Com_PrintError(15, "client sent an invalid offhand index %i\n", to->offHandIndex);
        to->offHandIndex = from->offHandIndex;
    }
}

// Decode arm for the else branch (exactly one MSG_ReadKey hit): the retail
// compact delta. keyb is derived by the caller from to->serverTime exactly
// as the retail code did.
static void __cdecl MSG_ReadUsercmdDeltaArmSmall(msg_t *msg, int keyb, const usercmd_s *from, usercmd_s *to)
{
    char horToMove; // [esp+Ch] [ebp-8h]
    int horFromMove; // [esp+10h] [ebp-4h]

    to->buttons |= MSG_ReadKey(msg, keyb, 1u);
    to->angles[0] = (uint16_t)MSG_ReadDeltaKeyShort(msg, keyb, from->angles[0]);
    to->angles[1] = (uint16_t)MSG_ReadDeltaKeyShort(msg, keyb, from->angles[1]);
    horFromMove = MSG_HorMoveTo(from->forwardmove, from->rightmove);
    horToMove = MSG_ReadDeltaKey(msg, keyb, horFromMove, 4u);
    MSG_HorMoveFrom(horToMove, &to->forwardmove, &to->rightmove);
}

void __cdecl MSG_ReadDeltaUsercmdKey(msg_t *msg, int key, const usercmd_s *from, usercmd_s *to)
{
    iassert( from->buttons < (1 << BUTTON_BIT_COUNT) );
    iassert( from->weapon < (1 << MAX_WEAPONS_BITS) );
    iassert( from->offHandIndex < (1 << MAX_WEAPONS_BITS) );
    // Struct assignment instead of the retail memcpy(to, from,
    // sizeof(usercmd_s)): usercmd_s is a plain POD struct, so the copy is
    // field-identical and the wire bytes are unchanged (pinned by
    // kisakcod-msg-wire-contract-tests).
    *to = *from;
    if (MSG_ReadBit(msg))
        to->serverTime = from->serverTime + MSG_ReadByte(msg);
    else
        to->serverTime = MSG_ReadLong(msg);
    if (MSG_ReadKey(msg, key, 1u))
    {
        to->buttons &= ~1u;
        if (MSG_ReadKey(msg, key, 1u))
            MSG_ReadUsercmdDeltaArmFull(msg, key, from, to);
        else
            MSG_ReadUsercmdDeltaArmSmall(msg, to->serverTime ^ key, from, to);
    }
}
