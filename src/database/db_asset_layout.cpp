#include <database/db_asset_layout.h>

#include <qcommon/com_error.h>

bool DB_AdmitAssetFamilyLoad(
    const std::int32_t assetType,
    const char *const familyName,
    const bool targetIs64Bit)
{
    if (db::asset_layout::IsLoadPermitted(assetType, targetIs64Bit))
    {
        return true;
    }

    // Fail closed. Loading here would copy a retail record into a widened
    // runtime struct whose members have moved, handing the engine mislanded
    // fields and an uninitialised tail. A dropped zone is recoverable; a
    // corrupted asset is not.
    Com_Error(
        ERR_DROP,
        "Fast-file asset type '%s' has no 64-bit on-disk/runtime layout pair; refusing to load with drifted sizes",
        familyName != nullptr ? familyName : "(unknown)");
    return false;
}
