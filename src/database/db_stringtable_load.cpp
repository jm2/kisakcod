#include "database.h"
#include "db_load_legacy_bridge.h"

void __cdecl Load_ScriptStringCustom(uint16_t *var)
{
    if (!var || !varXAssetList || varXAssetList->stringList.count <= 0
        || !varXAssetList->stringList.strings
        || *var >= static_cast<uint32_t>(varXAssetList->stringList.count))
    {
        Com_Error(ERR_DROP, "Invalid fast-file script-string index");
        return;
    }

    const uintptr_t stringValue = reinterpret_cast<uintptr_t>(
        varXAssetList->stringList.strings[*var]);
    if (stringValue > UINT16_MAX)
    {
        Com_Error(ERR_DROP, "Fast-file script-string value exceeds the 16-bit runtime");
        return;
    }
    *var = static_cast<uint16_t>(stringValue);
}

void __cdecl Mark_ScriptStringCustom(uint16_t *var)
{
    using db::load_legacy_bridge::DbLoadLegacyBridge;
    // An unload sequence's registry session keeps the failure and reports it
    // once db_hashCritSect is released (DB_EndRegistrySession).
    if (*var
        && DbLoadLegacyBridge::TryAddUser4(*var)
            != db::load_legacy_bridge::LegacyBridgeStatus::Success
        && !DbLoadLegacyBridge::InSession())
    {
        Com_Error(ERR_DROP, "Database user-4 script-string reference failed");
    }
}
