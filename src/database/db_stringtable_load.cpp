#include "database.h"

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
    if (*var)
        SL_AddUser(*var, 4u);
}
