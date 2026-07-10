#include "database/db_asset_mode.h"

#include <cstdint>
#include <cstdio>

namespace
{
int g_failures;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}
} // namespace

int main()
{
    using db::asset_mode::BuildMode;
    using db::asset_mode::IsAssetTypeSupported;
    using db::asset_mode::Requirement;
    using db::asset_mode::RequirementForAssetType;

    constexpr BuildMode mp = BuildMode::Multiplayer;
    constexpr BuildMode sp = BuildMode::SinglePlayer;

    Expect(
        RequirementForAssetType(db::asset_mode::kClipMap) == Requirement::SinglePlayer,
        "clipmap must be classified as single-player-only");
    Expect(
        RequirementForAssetType(db::asset_mode::kClipMapPvs) == Requirement::Multiplayer,
        "clipmap PVS must be classified as multiplayer-only");
    Expect(
        RequirementForAssetType(db::asset_mode::kGameWorldSp) == Requirement::SinglePlayer,
        "SP game world must be classified as single-player-only");
    Expect(
        RequirementForAssetType(db::asset_mode::kGameWorldMp) == Requirement::Multiplayer,
        "MP game world must be classified as multiplayer-only");

    Expect(!IsAssetTypeSupported(mp, db::asset_mode::kClipMap), "MP must reject SP clipmaps");
    Expect(IsAssetTypeSupported(mp, db::asset_mode::kClipMapPvs), "MP must accept PVS clipmaps");
    Expect(!IsAssetTypeSupported(mp, db::asset_mode::kGameWorldSp), "MP must reject SP worlds");
    Expect(IsAssetTypeSupported(mp, db::asset_mode::kGameWorldMp), "MP must accept MP worlds");

    Expect(IsAssetTypeSupported(sp, db::asset_mode::kClipMap), "SP must accept SP clipmaps");
    Expect(!IsAssetTypeSupported(sp, db::asset_mode::kClipMapPvs), "SP must reject PVS clipmaps");
    Expect(IsAssetTypeSupported(sp, db::asset_mode::kGameWorldSp), "SP must accept SP worlds");
    Expect(!IsAssetTypeSupported(sp, db::asset_mode::kGameWorldMp), "SP must reject MP worlds");

    constexpr std::int32_t sharedTypes[] = {0, 0xC, 0xF, 0x20};
    for (const std::int32_t type : sharedTypes)
    {
        Expect(IsAssetTypeSupported(mp, type), "MP must accept shared asset types");
        Expect(IsAssetTypeSupported(sp, type), "SP must accept shared asset types");
    }

    constexpr std::int32_t unavailableTypes[] = {
        db::asset_mode::kUiMap,
        db::asset_mode::kSndDriverGlobals,
        db::asset_mode::kAiType,
        db::asset_mode::kMpType,
        db::asset_mode::kCharacter,
        db::asset_mode::kXModelAlias,
    };
    for (const std::int32_t type : unavailableTypes)
    {
        Expect(
            RequirementForAssetType(type) == Requirement::Unavailable,
            "null-handler asset types must be classified as unavailable");
        Expect(!IsAssetTypeSupported(mp, type), "MP must reject unavailable asset types");
        Expect(!IsAssetTypeSupported(sp, type), "SP must reject unavailable asset types");
    }

    constexpr std::int32_t invalidTypes[] = {-1, db::asset_mode::kAssetTypeCount, 0x7FFFFFFF};
    for (const std::int32_t type : invalidTypes)
    {
        Expect(
            RequirementForAssetType(type) == Requirement::Invalid,
            "out-of-range asset types must be classified as invalid");
        Expect(!IsAssetTypeSupported(mp, type), "MP must reject invalid asset types");
        Expect(!IsAssetTypeSupported(sp, type), "SP must reject invalid asset types");
    }

    if (g_failures != 0)
        return 1;

    std::puts("asset-mode policy tests passed");
    return 0;
}
