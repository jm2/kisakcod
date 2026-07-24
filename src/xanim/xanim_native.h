#pragma once

// ============================================================================
//  xanim_native.h -- native-width XAnim runtime view + disk mirror split.
//
//  The retail (x86 32-bit) shipped layout is frozen at exactly 88 bytes per
//  XAnimParts instance. The PRIOR version of this header kept XAnimParts
//  with raw host pointers; on 64-bit hosts that struct widens to 0x88
//  (136 bytes) and the on-disk 88-byte contract silently breaks. The
//  Priority 7 / M4 contract requires the disk mirror to be split from the
//  widened runtime payload BEFORE any 64-bit XAnim translation unit is
//  buildable.
//
//  This header provides the split infrastructure:
//
//    - XAnimParts (the on-disk / wire-mirror view in xanim.h or the test
//                  shim) : every pointer field is a 32-bit disk offset,
//                  so sizeof(XAnimParts) == 88 on every host. The
//                  static_assert in xanim.h (and the matching
//                  ONDISK_SIZE / RUNTIME_SIZE in this header) pins the
//                  88-byte layout on every target.
//
//    - xanim::XAnimPartsNative : the widened native-runtime view with
//                  native-width pointer fields. On 32-bit hosts every
//                  pointer width is 4 bytes so sizeof(XAnimPartsNative)
//                  equals sizeof(XAnimParts); on 64-bit the runtime view
//                  grows to 0x88 (136 bytes) but the disk mirror is
//                  still 88 bytes.
//
//  XAnimClone now allocates sizeof(xanim::XAnimPartsNative) bytes and
//  copies (memcpy) the on-disk 88 bytes into the head of that buffer,
//  then zeroes the trailing bytes that a widened native view would
//  expose but the on-disk source does not carry. The result is that
//  XAnimClone never under-allocates the runtime view on 64-bit and the
//  copy is byte-identical on 32-bit.
//
//  The conversion functions below transfer field values between the two
//  shapes with explicit ordering. On 32-bit they are bit-identical
//  (uint32_t == pointer == 4 bytes); on 64-bit they widen pointer fields
//  to native-width. The remaining migration of every XAnimParts consumer
//  to XAnimPartsNative is a follow-up; this header only provides the
//  split infrastructure and the safe round-trip helpers.
//
//  See docs/task.md Priority 7 + M4.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <universal/kisak_abi.h>

// The on-disk mirror types (XAnimIndices, XAnimNotifyInfo, XAnimDeltaPart,
// XAnimParts) come from xanim.h on the engine side or the test shim
// (tests/xanim_parts_split_test_shim.h) on the portable side. Both
// declarations store pointer fields as 32-bit disk offsets so the wire
// shape is bit-identical on every host. The KISAK_XANIM_*_DECLARED
// macros guard against header redefinition if both sources are seen.

namespace xanim
{

// ----------------------------------------------------------------------------
//  XAnimPartsNative -- native-width runtime view.
//
//  This struct deliberately mirrors XAnimParts field-for-field so the
//  conversion helpers can move values without renumbering offsets.
//  Pointer fields widen from 32-bit (frozen on disk, in XAnimParts) to
//  native-width here. On 32-bit hosts every pointer width is 4 bytes so
//  sizeof(XAnimPartsNative) equals sizeof(XAnimParts); on 64-bit the
//  runtime view grows to 0x88 (136 bytes).
//
//  The "indices" field mirrors the on-disk XAnimIndices union (which
//  stores a 32-bit disk offset) so the conversion helper can read the
//  data slot directly. Engine code that wants the host-pointer-width
//  union uses XAnimIndicesNative (below).
// ----------------------------------------------------------------------------
struct XAnimPartsNative
{
    const char *name;
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
    // alignment padding for the uint32_t fields below is implicit.
    uint32_t randomDataShortCount;
    uint32_t indexCount;
    float framerate;
    float frequency;

    // Pointer fields widen from 32-bit (frozen on disk) to native-width
    // here. On 32-bit hosts every pointer width is 4 bytes so
    // sizeof(XAnimPartsNative) equals sizeof(XAnimParts); on 64-bit the
    // runtime view grows.
    uint16_t *names;
    uint8_t *dataByte;
    int16_t *dataShort;
    int *dataInt;
    int16_t *randomDataShort;
    uint8_t *randomDataByte;
    int *randomDataInt;

    // The XAnimIndices union's data slot, widened to a host pointer.
    // The on-disk mirror stores the address as a 32-bit value; on 64-bit
    // the runtime view carries the full host pointer. The notify and
    // deltaPart trailing pointers widen similarly.
    void *indicesData;
    XAnimNotifyInfo *notify;
    XAnimDeltaPart *deltaPart;
};

RUNTIME_SIZE(XAnimPartsNative, 88, 0x88);

// The on-disk mirror is now pointer-width-stable on every target because
// every pointer field is already a 32-bit disk offset. The contract
// holds for the XAnimPartsNative view separately via RUNTIME_SIZE above.
ONDISK_SIZE(XAnimParts, 88);

// ----------------------------------------------------------------------------
//  XAnimPartsDiskToNative -- copy every field from the on-disk mirror
//  into the widened runtime view. The function is a sequence of scalar
//  copies so the compiler cannot elide field ordering. On 32-bit the two
//  layouts are bit-identical and the copy degenerates to a memcpy.
// ----------------------------------------------------------------------------
inline XAnimPartsNative XAnimPartsDiskToNative(const XAnimParts &disk)
{
    XAnimPartsNative out{};
    out.name = reinterpret_cast<const char *>(static_cast<uintptr_t>(disk.name));
    out.dataByteCount = disk.dataByteCount;
    out.dataShortCount = disk.dataShortCount;
    out.dataIntCount = disk.dataIntCount;
    out.randomDataByteCount = disk.randomDataByteCount;
    out.randomDataIntCount = disk.randomDataIntCount;
    out.numframes = disk.numframes;
    out.bLoop = disk.bLoop;
    out.bDelta = disk.bDelta;
    for (int i = 0; i < 10; ++i)
        out.boneCount[i] = disk.boneCount[i];
    out.notifyCount = disk.notifyCount;
    out.assetType = disk.assetType;
    out.isDefault = disk.isDefault;
    out.randomDataShortCount = disk.randomDataShortCount;
    out.indexCount = disk.indexCount;
    out.framerate = disk.framerate;
    out.frequency = disk.frequency;
    out.names = reinterpret_cast<uint16_t *>(static_cast<uintptr_t>(disk.names));
    out.dataByte = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(disk.dataByte));
    out.dataShort = reinterpret_cast<int16_t *>(static_cast<uintptr_t>(disk.dataShort));
    out.dataInt = reinterpret_cast<int *>(static_cast<uintptr_t>(disk.dataInt));
    out.randomDataShort = reinterpret_cast<int16_t *>(static_cast<uintptr_t>(disk.randomDataShort));
    out.randomDataByte = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(disk.randomDataByte));
    out.randomDataInt = reinterpret_cast<int *>(static_cast<uintptr_t>(disk.randomDataInt));
    // The XAnimIndices union: only the data slot carries an address on
    // disk; the _1 / _2 typed views are aliases the consumer never
    // writes through. Copy the address through `data` so the union is
    // set.
    out.indicesData = reinterpret_cast<void *>(static_cast<uintptr_t>(disk.indices.data));
    out.notify = reinterpret_cast<XAnimNotifyInfo *>(static_cast<uintptr_t>(disk.notify));
    out.deltaPart = reinterpret_cast<XAnimDeltaPart *>(static_cast<uintptr_t>(disk.deltaPart));
    return out;
}

// ----------------------------------------------------------------------------
//  XAnimPartsNativeToDisk -- copy a widened runtime view back into the
//  on-disk mirror. Used by writers that need to serialize the runtime
//  back into the on-disk shape. The copy mirrors XAnimPartsDiskToNative
//  field-for-field so any new field in XAnimParts that is missed on
//  either side trips a static_assert-driven test failure.
// ----------------------------------------------------------------------------
inline void XAnimPartsNativeToDisk(const XAnimPartsNative &native, XAnimParts &disk)
{
    disk.name = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.name));
    disk.dataByteCount = native.dataByteCount;
    disk.dataShortCount = native.dataShortCount;
    disk.dataIntCount = native.dataIntCount;
    disk.randomDataByteCount = native.randomDataByteCount;
    disk.randomDataIntCount = native.randomDataIntCount;
    disk.numframes = native.numframes;
    disk.bLoop = native.bLoop;
    disk.bDelta = native.bDelta;
    for (int i = 0; i < 10; ++i)
        disk.boneCount[i] = native.boneCount[i];
    disk.notifyCount = native.notifyCount;
    disk.assetType = native.assetType;
    disk.isDefault = native.isDefault;
    disk.randomDataShortCount = native.randomDataShortCount;
    disk.indexCount = native.indexCount;
    disk.framerate = native.framerate;
    disk.frequency = native.frequency;
    disk.names = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.names));
    disk.dataByte = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.dataByte));
    disk.dataShort = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.dataShort));
    disk.dataInt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.dataInt));
    disk.randomDataShort = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.randomDataShort));
    disk.randomDataByte = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.randomDataByte));
    disk.randomDataInt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.randomDataInt));
    disk.indices.data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.indicesData));
    disk.notify = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.notify));
    disk.deltaPart = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.deltaPart));
}

}  // namespace xanim
