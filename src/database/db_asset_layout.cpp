#include <database/db_asset_layout.h>

#include <qcommon/com_error.h>

bool DB_AdmitAssetFamilyLoad(
    const std::int32_t assetType,
    const char *const familyName,
    const bool targetIs64Bit,
    const bool fxAdapterAvailable)
{
    if (db::asset_layout::IsLoadPermitted(assetType, targetIs64Bit, fxAdapterAvailable))
    {
        return true;
    }

    const char *const name = familyName != nullptr ? familyName : "(unknown)";

    // Fail closed. Loading here would copy a retail record into a widened
    // runtime struct whose members have moved, handing the engine mislanded
    // fields and an uninitialised tail. A dropped zone is recoverable; a
    // corrupted asset is not.
    if (db::asset_layout::IsFxConversionFamily(assetType))
    {
        // The pair exists but the adapter that materialises it is not bound,
        // so the loader would otherwise fall through to its legacy
        // retail-record walk (headless wiring always reports no adapter).
        Com_Error(
            ERR_DROP,
            "Fast-file asset type '%s' has a 64-bit on-disk/runtime layout pair but no FX zone adapter to apply it; refusing to load with drifted sizes",
            name);
        return false;
    }
    Com_Error(
        ERR_DROP,
        "Fast-file asset type '%s' has no 64-bit on-disk/runtime layout pair; refusing to load with drifted sizes",
        name);
    return false;
}
