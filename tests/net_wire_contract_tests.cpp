// Network wire-format contracts.
//
// P0: the protocol must stay compatible with the commercial COD4 binary. Nothing
// in the tree guarded that until now -- no test touched MSG_*, the netfield tables,
// or the delta codec, and `entityState_s` (the primary delta-encoded entity struct)
// had no size contract at all.
//
// Two invariants are pinned here.
//
// 1. LAYOUT. The delta codec locates a field with NETF_BASE, which is offsetof:
//
//        #define NETF_BASE(s, x) #x,(size_t)&((s*)0)->x
//
//    and then reads and writes it through an `int *` (msg_mp.cpp:1000-1001):
//
//        fromF = (const int *)&from[field->offset];
//        toF   = (int *)&to[field->offset];
//
//    Because the offsets are computed rather than hardcoded, reordering a struct is
//    survivable -- but changing the WIDTH of any netfield-referenced member is not.
//    A member that stops being exactly 4 bytes silently changes the bits that go on
//    the wire. So every such member is asserted to be 32 bits, and the wire structs
//    are pinned to their retail sizes on EVERY target (they are pointer-free, so
//    unlike ordinary runtime structs they must NOT widen on LP64).
//
// 2. VALUE. Snapped origins/angles are produced by SnapFloatToInt, which on x86 is
//    the SSE cvtss2si instruction (round-to-nearest-EVEN, per MXCSR). The non-x86
//    fallback must reproduce that bit-for-bit or ARM builds would put different
//    numbers on the wire. The tie cases below are the ones that distinguish
//    round-half-to-even from round-half-away-from-zero.

#include <qcommon/ent.h>
#include <qcommon/msg_mp.h>
#include <qcommon/qcommon.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <climits>
#include <type_traits>
#include <utility>

// A wire struct is frozen at the SAME size on every target. This is deliberately
// not RUNTIME_SIZE: there is no "64-bit size" for these -- widening one is a bug.
#define WIRE_SIZE(T, n) \
    static_assert(sizeof(T) == (n), #T " wire layout drift -- breaks protocol compatibility with the retail binary")

#define WIRE_OFFSET(T, field, n) \
    static_assert(offsetof(T, field) == (n), #T "." #field " wire offset drift")

// The delta codec reads every netfield through an `int *`. Anything not exactly
// 32 bits wide would serialize the wrong bytes.
#define WIRE_FIELD_32BIT(T, field) \
    static_assert(sizeof(decltype(std::declval<T &>().field)) == 4, \
                  #T "." #field " must stay 32 bits: the delta codec reads it via (int *)")

// ---------------------------------------------------------------------------
// 1. Wire struct sizes (retail, from the decompile). Must hold on ILP32 and LP64.
// ---------------------------------------------------------------------------

WIRE_SIZE(entityState_s, 0xF4);      // primary delta-encoded entity -- was UNPINNED before this test
WIRE_SIZE(archivedEntity_s, 0x118);
WIRE_SIZE(LerpEntityState, 0x68);
WIRE_SIZE(usercmd_s, 0x20);          // client -> server command

// These unions are the payload of several netfields; the codec assumes 4 bytes.
WIRE_SIZE(entityState_s_type_index, 0x4);
WIRE_SIZE(entityState_s_un1, 0x4);
WIRE_SIZE(entityState_s_un2, 0x4);

// ---------------------------------------------------------------------------
// 2. Every netfield-referenced member of entityState_s stays 32 bits.
// ---------------------------------------------------------------------------

WIRE_FIELD_32BIT(entityState_s, number);
WIRE_FIELD_32BIT(entityState_s, eType);
WIRE_FIELD_32BIT(entityState_s, time2);
WIRE_FIELD_32BIT(entityState_s, otherEntityNum);
WIRE_FIELD_32BIT(entityState_s, attackerEntityNum);
WIRE_FIELD_32BIT(entityState_s, groundEntityNum);
WIRE_FIELD_32BIT(entityState_s, loopSound);
WIRE_FIELD_32BIT(entityState_s, surfType);
WIRE_FIELD_32BIT(entityState_s, clientNum);
WIRE_FIELD_32BIT(entityState_s, iHeadIcon);
WIRE_FIELD_32BIT(entityState_s, iHeadIconTeam);
WIRE_FIELD_32BIT(entityState_s, solid);
WIRE_FIELD_32BIT(entityState_s, eventParm);
WIRE_FIELD_32BIT(entityState_s, eventSequence);
WIRE_FIELD_32BIT(entityState_s, weapon);
WIRE_FIELD_32BIT(entityState_s, weaponModel);
WIRE_FIELD_32BIT(entityState_s, legsAnim);
WIRE_FIELD_32BIT(entityState_s, torsoAnim);
WIRE_FIELD_32BIT(entityState_s, fTorsoPitch);
WIRE_FIELD_32BIT(entityState_s, fWaistPitch);

// usercmd_s: serverTime/buttons/angles[] are read as 32-bit by the codec.
WIRE_FIELD_32BIT(usercmd_s, serverTime);
WIRE_FIELD_32BIT(usercmd_s, buttons);
WIRE_FIELD_32BIT(usercmd_s, meleeChargeYaw);

// Key offsets. If one of these moves, a NETF entry now points at a different member.
WIRE_OFFSET(entityState_s, number, 0x0);
WIRE_OFFSET(entityState_s, eType, 0x4);
WIRE_OFFSET(entityState_s, lerp, 0x8);

// ---------------------------------------------------------------------------
// 3. The NetField descriptor itself is NOT a wire struct -- it is an in-memory
//    table. Its `offset` is a size_t, so it legitimately widens on LP64.
// ---------------------------------------------------------------------------

RUNTIME_SIZE(NetField, 0x10, 0x18);

// ---------------------------------------------------------------------------
// 4. SnapFloatToInt must match SSE cvtss2si (round-half-to-EVEN) on every target.
// ---------------------------------------------------------------------------

namespace
{
struct SnapCase
{
    float input;
    int expected;
    const char *what;
};

// Ground truth is cvtss2si on x86. Ties go to the EVEN integer -- this is what
// distinguishes it from the more common round-half-away-from-zero.
constexpr SnapCase kSnapCases[] = {
    {0.5f, 0, "tie 0.5 -> 0 (even)"},
    {1.5f, 2, "tie 1.5 -> 2 (even)"},
    {2.5f, 2, "tie 2.5 -> 2 (even, NOT 3)"},
    {3.5f, 4, "tie 3.5 -> 4 (even)"},
    {4.5f, 4, "tie 4.5 -> 4 (even, NOT 5)"},
    {-0.5f, 0, "tie -0.5 -> 0 (even)"},
    {-1.5f, -2, "tie -1.5 -> -2 (even)"},
    {-2.5f, -2, "tie -2.5 -> -2 (even, NOT -3)"},
    {-3.5f, -4, "tie -3.5 -> -4 (even)"},
    {0.4999999f, 0, "just below tie"},
    {0.5000001f, 1, "just above tie"},
    {-0.4999999f, 0, "just above negative tie"},
    {1.4999999f, 1, "below tie"},
    {0.0f, 0, "zero"},
    {-0.0f, 0, "negative zero"},
    {123.0f, 123, "exact integer"},
    {-123.0f, -123, "exact negative integer"},
    {8388608.0f, 8388608, "2^23"},
    {2147483520.0f, 2147483520, "largest float below INT_MAX"},
    {-2147483648.0f, INT_MIN, "INT_MIN exactly"},
    // Out of range / NaN: cvtss2si yields the "integer indefinite" value INT_MIN.
    {2147483648.0f, INT_MIN, "overflow -> integer indefinite"},
    {-2147483904.0f, INT_MIN, "underflow -> integer indefinite"},
};
} // namespace

int main()
{
    bool ok = true;

    for (const SnapCase &c : kSnapCases)
    {
        const int got = SnapFloatToInt(c.input);
        if (got != c.expected)
        {
            std::printf("SnapFloatToInt(%.9g) = %d, expected %d  [%s]\n",
                        static_cast<double>(c.input), got, c.expected, c.what);
            ok = false;
        }
    }

    // SnapFloat() is the float-returning form used by Sys_SnapVector; it must agree.
    for (const SnapCase &c : kSnapCases)
    {
        const float got = SnapFloat(c.input);
        if (got != static_cast<float>(c.expected))
        {
            std::printf("SnapFloat(%.9g) = %.9g, expected %.9g  [%s]\n",
                        static_cast<double>(c.input), static_cast<double>(got),
                        static_cast<double>(c.expected), c.what);
            ok = false;
        }
    }

    if (ok)
        std::printf("net wire contracts OK (%zu snap cases)\n",
                    sizeof(kSnapCases) / sizeof(kSnapCases[0]));

    return ok ? 0 : 1;
}
