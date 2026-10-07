#pragma once

#include <cstdint>

namespace db::asset_mode
{
enum class BuildMode : std::uint8_t
{
    Multiplayer,
    SinglePlayer,
};

enum class Requirement : std::uint8_t
{
    Invalid,
    Unavailable,
    Shared,
    Multiplayer,
    SinglePlayer,
};

// These values are part of the IW3 fast-file asset-type contract. The engine
// enum is pinned to them with static assertions in database.h.
inline constexpr std::int32_t kClipMap = 0xA;
inline constexpr std::int32_t kClipMapPvs = 0xB;
inline constexpr std::int32_t kGameWorldSp = 0xD;
inline constexpr std::int32_t kGameWorldMp = 0xE;
inline constexpr std::int32_t kUiMap = 0x12;
inline constexpr std::int32_t kSndDriverGlobals = 0x18;
inline constexpr std::int32_t kAiType = 0x1B;
inline constexpr std::int32_t kMpType = 0x1C;
inline constexpr std::int32_t kCharacter = 0x1D;
inline constexpr std::int32_t kXModelAlias = 0x1E;
inline constexpr std::int32_t kAssetTypeCount = 0x21;

constexpr Requirement RequirementForAssetType(const std::int32_t assetType) noexcept
{
    if (assetType < 0 || assetType >= kAssetTypeCount)
        return Requirement::Invalid;

    switch (assetType)
    {
    case kUiMap:
    case kSndDriverGlobals:
    case kAiType:
    case kMpType:
    case kCharacter:
    case kXModelAlias:
        return Requirement::Unavailable;
    case kClipMap:
    case kGameWorldSp:
        return Requirement::SinglePlayer;
    case kClipMapPvs:
    case kGameWorldMp:
        return Requirement::Multiplayer;
    default:
        return Requirement::Shared;
    }
}

// Retail PC zones list a SndDriverGlobals asset (code_post_gfx_mp holds one) but
// store no record for it: its header slot holds a stale build-tool address, not
// an inline (-1/-2) or offset token, and the PC loader's dispatch has no case for
// it. It has no PC pool either, so it stays Unavailable for allocation and
// publishing; the asset-list admission and the load/mark dispatch skip it.
constexpr bool IsSkippedByLoader(const std::int32_t assetType) noexcept
{
    return assetType == kSndDriverGlobals;
}

// A skipped asset's header token: any stale value is skippable, but an inline
// record (-1/-2) holds bytes the loader would have to consume, so it is not.
constexpr bool IsSkippableListedOnlyToken(const std::uint32_t token) noexcept
{
    return token != 0xFFFFFFFFu && token != 0xFFFFFFFEu;
}

constexpr bool IsAssetTypeSupported(
    const BuildMode buildMode,
    const std::int32_t assetType) noexcept
{
    const Requirement requirement = RequirementForAssetType(assetType);
    return requirement != Requirement::Invalid
        && requirement != Requirement::Unavailable
        && (requirement == Requirement::Shared
            || (requirement == Requirement::Multiplayer && buildMode == BuildMode::Multiplayer)
            || (requirement == Requirement::SinglePlayer && buildMode == BuildMode::SinglePlayer));
}
} // namespace db::asset_mode
