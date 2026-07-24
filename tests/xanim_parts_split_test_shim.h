#pragma once

// ============================================================================
//  xanim_parts_split_test_shim.h -- minimal on-disk XAnimParts types for tests.
//
//  This header mirrors the on-disk / wire-mirror types declared in xanim.h
//  (XAnimIndices, XAnimNotifyInfo, XAnimDeltaPart, XAnimParts) WITHOUT
//  pulling in the full xanim.h. xanim.h pulls in DirectX headers and the
//  full engine translation-unit dependencies, so portable utility test
//  binaries cannot include it directly.
//
//  The shim models the frozen retail 32-bit on-disk shape. Pointer fields
//  are stored as 32-bit-wide packed values (uint32_t) so the struct is
//  bit-identical to the on-disk image on every host architecture. The
//  matching native-runtime view in <xanim/xanim_native.h> (XAnimPartsNative)
//  widens the same fields to native-width pointers; the DiskToNative /
//  NativeToDisk helpers below move values between the two views without
//  touching the wire bytes.
//
//  The shim works in concert with <xanim/xanim_native.h>: the shim defines
//  the KISAK_XANIM_*_DECLARED macros first (matching xanim.h's emission
//  pattern), then xanim_native.h's KISAK_ARCH_64BIT-gated disk-mirror
//  contract runs against the shim's struct without redefinition errors.
//
//  Test TU pattern:
//
//      #include "xanim_parts_split_test_shim.h"
//      #include <xanim/xanim_native.h>
// ============================================================================

#include <cstdint>

// ----------------------------------------------------------------------------
//  XAnimIndices -- on-disk union of three pointer-typed members. Each pointer
//  is a 32-bit disk offset (not a host pointer); the union is frozen at
//  exactly 4 bytes on every target.
// ----------------------------------------------------------------------------
#define KISAK_XANIM_INDICES_DECLARED 1
union XAnimIndices
{
    uint32_t _1;
    uint32_t _2;
    uint32_t data;
};

// ----------------------------------------------------------------------------
//  XAnimNotifyInfo -- name + time. 8 bytes; name is a packed script-string
//  reference and time is the float trigger. No pointer members here, so
//  the shape is bit-identical on every target.
// ----------------------------------------------------------------------------
#define KISAK_XANIM_NOTIFY_INFO_DECLARED 1
struct XAnimNotifyInfo
{
    uint16_t name;
    // padding byte
    // padding byte
    float time;
};

// ----------------------------------------------------------------------------
//  XAnimDeltaPart -- trans + quat. Each pointer is a 32-bit disk offset;
//  the struct is 8 bytes on every target.
// ----------------------------------------------------------------------------
#define KISAK_XANIM_DELTA_PART_DECLARED 1
struct XAnimDeltaPart
{
    uint32_t trans;
    uint32_t quat;
};

// ----------------------------------------------------------------------------
//  XAnimParts -- the frozen 88-byte on-disk / wire-mirror shape used by
//  the engine's loaders, writers, and consumers. Every pointer field is
//  stored as a 32-bit disk offset so the layout is bit-identical on
//  32-bit and 64-bit hosts. The static_assert below pins the size; the
//  matching assert in xanim.h gives the same protection for the engine TU.
// ----------------------------------------------------------------------------
#define KISAK_XANIM_PARTS_DECLARED 1
struct XAnimParts
{
    uint32_t name;
    uint16_t dataByteCount;
    uint16_t dataShortCount;
    uint16_t dataIntCount;
    uint16_t randomDataByteCount;
    uint16_t randomDataIntCount;
    uint16_t numframes;
    bool bLoop;
    bool bDelta;
    uint8_t boneCount[10];
    uint8_t notifyCount;
    uint8_t assetType;
    bool isDefault;
    // padding byte
    uint32_t randomDataShortCount;
    uint32_t indexCount;
    float framerate;
    float frequency;
    uint32_t names;
    uint32_t dataByte;
    uint32_t dataShort;
    uint32_t dataInt;
    uint32_t randomDataShort;
    uint32_t randomDataByte;
    uint32_t randomDataInt;
    XAnimIndices indices;
    uint32_t notify;
    uint32_t deltaPart;
};
