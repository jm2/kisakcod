#pragma once

#include <cstddef>
#include <cstdint>

#include <universal/kisak_abi.h>

namespace disk32
{
constexpr uint32_t kInline = UINT32_MAX;
constexpr uint32_t kSharedInline = UINT32_MAX - 1;
constexpr uint32_t kOffsetMask = 0x0FFFFFFF;

// Material fast-file layout constants. Keep these separate from native
// sizeof: every corresponding runtime type widens when pointer fields become
// native-width on 64-bit hosts.
constexpr uint32_t kMaterialVertexDeclarationBytes = 100;
constexpr uint32_t kMaterialTechniqueHeaderBytes = 8;
constexpr uint32_t kMaterialPassBytes = 20;
constexpr uint32_t kMaterialTechniqueSchema =
    (kMaterialTechniqueHeaderBytes << 16) | kMaterialPassBytes;
constexpr uint32_t kMaterialVertexShaderBytes = 16;
constexpr uint32_t kMaterialPixelShaderBytes = 16;
constexpr uint32_t kMaterialShaderProgramBytes = 12;
constexpr uint32_t kMaterialShaderLoadDefBytes = 8;
constexpr uint32_t kMaterialWaterBytes = 68;
constexpr uint32_t kMaterialTextureDefBytes = 12;

// Pointer-bearing non-asset objects which may be shared by direct block-4
// offsets. These remain fixed disk32 extents even after their runtime structs
// widen on 64-bit hosts.
constexpr uint32_t kSoundFileBytes = 12;
constexpr uint32_t kSpeakerMapBytes = 408;
constexpr uint32_t kSndAliasBytes = 92;
constexpr uint32_t kWeaponBounceSoundCount = 29;
constexpr uint32_t kWeaponBounceSoundTableBytes =
    kWeaponBounceSoundCount * sizeof(uint32_t);
constexpr uint32_t kGfxLightBytes = 64;
constexpr uint32_t kStringTableBytes = 16;
constexpr uint32_t kXModelPieceBytes = 16;
constexpr uint32_t kXModelPiecesBytes = 12;
constexpr uint32_t kXSurfaceCollisionTreeBytes = 40;
constexpr uint32_t kXSurfaceCollisionNodeBytes = 16;
constexpr uint32_t kXSurfaceCollisionLeafBytes = 2;
constexpr uint32_t kXRigidVertListBytes = 12;
constexpr uint32_t kBrushWrapperBytes = 80;
constexpr uint32_t kPhysGeomInfoBytes = 68;
constexpr uint32_t kPhysGeomListBytes = 44;
constexpr uint32_t kCBrushSideBytes = 12;
constexpr uint32_t kCPlaneBytes = 20;

struct PointerToken
{
    uint32_t value;

    constexpr bool isNull() const { return value == 0; }
    constexpr bool isInline() const { return value == kInline; }
    constexpr bool isSharedInline() const { return value == kSharedInline; }
    constexpr bool isOffset() const { return !isNull() && !isInline() && !isSharedInline(); }
};
ONDISK_SIZE(PointerToken, 4);

// The single canonical Ptr32 in the tree (docs/PORTING.md section 8: "do not stand
// up a parallel Ptr32<T>"). A 32-bit packed pointer FIELD for on-disk / wire mirror
// structs: it carries a phantom pointee type for readability and never dereferences
// itself - resolution goes through DecodeOffset + the zone block table. Widening to
// a native runtime pointer happens in the load-time relocation pass (M5), never here.
template <class T>
struct Ptr32
{
    PointerToken token;
    using pointee = T;
};
ONDISK_SIZE(Ptr32<void>, 4);

struct DecodedOffset
{
    uint32_t block;
    uint32_t offset;
};

constexpr bool DecodeOffset(
    PointerToken token,
    const uint32_t *blockSizes,
    std::size_t blockCount,
    uint32_t requiredBytes,
    DecodedOffset *decoded)
{
    if (!token.isOffset() || !blockSizes || !decoded)
        return false;

    const uint32_t adjusted = token.value - 1;
    const uint32_t block = adjusted >> 28;
    const uint32_t offset = adjusted & kOffsetMask;
    if (block >= blockCount)
        return false;

    const uint32_t blockSize = blockSizes[block];
    if (offset > blockSize || requiredBytes > blockSize - offset)
        return false;

    decoded->block = block;
    decoded->offset = offset;
    return true;
}
}
