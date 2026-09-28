#pragma once

#include <cstdint>

// Per-asset-family on-disk/runtime layout conversion state
// (docs/design/NATIVE64.md, "Layout classes").
//
// A retail fast-file stores every asset record at its original 32-bit layout:
// a frozen record extent with 32-bit member offsets. The in-memory struct the
// loader builds is a *runtime* struct whose pointer members widen on 64-bit
// hosts, so both sizeof(T) and the member offsets drift away from the retail
// record. Copying a retail record into that struct then lands every member at
// the wrong offset, and the widened tail is left uninitialised -- the
// "drifted sizes" hazard.
//
// A family is only safe to load at 64-bit once its layout pair is complete:
//   - an ONDISK_* mirror (disk32::Ptr32<T> pointer fields) that freezes the
//     retail 32-bit record on every target, and
//   - a RUNTIME_SIZE runtime struct the loader builds.
// Until then the loader must refuse the family rather than guess, which is the
// fail-closed rule this header implements. Mirrors land per family; the FX
// family (docs/design/NATIVE64.md) is the worked example.
//
// The policy is deliberately free of engine types so it compiles standalone in
// the tests; the raw asset-type integers are pinned to the engine XAssetType
// enum by static_assert in database.h, exactly as db_asset_mode.h is.
namespace db::asset_layout
{
enum class Conversion : std::uint8_t
{
    // No complete ONDISK/RUNTIME pair: a 64-bit load would read retail records
    // with the widened runtime sizeof.
    Unconverted,
    // ONDISK mirror plus RUNTIME_SIZE runtime struct; 64-bit loads are safe.
    OndiskRuntimePair,
};

// Asset-type values from the IW3 fast-file contract (XAssetType in
// xanim/xanim.h). Only the types this policy branches on are named here.
inline constexpr std::int32_t kFx = 0x19;
inline constexpr std::int32_t kImpactFx = 0x1A;
inline constexpr std::int32_t kAssetTypeCount = 0x21;

constexpr Conversion ConversionForAssetType(const std::int32_t assetType) noexcept
{
    switch (assetType)
    {
    case kFx:
    case kImpactFx:
        return Conversion::OndiskRuntimePair;
    default:
        // Every other family still loads retail records with runtime sizeof;
        // its mirror is not in the tree yet.
        return Conversion::Unconverted;
    }
}

constexpr bool IsFamilyConverted(const std::int32_t assetType) noexcept
{
    return ConversionForAssetType(assetType) == Conversion::OndiskRuntimePair;
}

// The only families whose pair is materialised through the FX zone adapter
// rather than a loader-side RUNTIME_SIZE walk.
constexpr bool IsFxConversionFamily(const std::int32_t assetType) noexcept
{
    return assetType == kFx || assetType == kImpactFx;
}

// The fail-closed rule. Drift is only possible when the host pointer is wider
// than the retail 32-bit slot, so the 32-bit targets keep loading every family
// exactly as before (the MSVC x86 behaviour invariant).
//
// fxAdapterAvailable is the FX zone-adapter binding state at load time. The FX
// and Impact FX pairs are applied only through that adapter; when it is
// unavailable (headless builds stub the wiring out, and a load outside a bound
// zone has no workspace) both FX loaders fall through to their legacy
// retail-record walk, which copies a 32-bit record into the widened runtime
// struct. Refuse those families here, before either fallback can run.
constexpr bool IsLoadPermitted(
    const std::int32_t assetType,
    const bool targetIs64Bit,
    const bool fxAdapterAvailable) noexcept
{
    if (!targetIs64Bit)
    {
        return true;
    }
    if (!IsFamilyConverted(assetType))
    {
        return false;
    }
    return fxAdapterAvailable || !IsFxConversionFamily(assetType);
}
} // namespace db::asset_layout

// Gate one asset-family load against the layout conversion state.
//
// targetIs64Bit is the *target* pointer width, not a runtime probe: callers
// pass `KISAK_ARCH_64BIT != 0` from kisak_abi.h. fxAdapterAvailable is the
// FX zone-adapter binding state; callers pass
// `db::fx_zone_adapter_wiring::IsFxZoneAdapterBindingActive()`. Returns true
// when the load may proceed. Returns false after raising `Com_Error(ERR_DROP,
// ...)`, naming familyName, so a 64-bit fast-file cannot load a family whose
// record layout would drift under the runtime sizeof.
[[nodiscard]] bool DB_AdmitAssetFamilyLoad(
    std::int32_t assetType,
    const char *familyName,
    bool targetIs64Bit,
    bool fxAdapterAvailable);
