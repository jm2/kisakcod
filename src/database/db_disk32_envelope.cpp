#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/database.h>
#include <database/db_disk32_load_internal.h>
#include <database/db_xasset_disk32.h>
#include <qcommon/com_error.h>

#include <cstddef>
#include <cstdint>

// The 64-bit XAssetList/XAsset envelope (docs/design/FASTFILE_LOADER.md). A
// retail root is the 16-byte XAssetListDisk32 and each asset is an 8-byte
// XAssetDisk32; the native records are 32 and 16 bytes. The disk bytes are
// streamed where the 32-bit Load_XAssetListCustom, Load_ScriptStringList and
// Load_XAssetArrayCustom stream them, so every block offset stays the retail
// one. The native script-string table and XAsset array live in zone-lifetime
// native storage (DB_AllocZoneNative), as the 32-bit arrays live in block 4.
// As in db_disk32_load.cpp, frames hold no destructors, since a production
// Com_Error(ERR_DROP) longjmps out of them.
namespace db::xasset
{
namespace
{
using db::disk32_load::AllocNative;
using db::disk32_load::Drop;
using db::disk32_load::kVirtualBlock;
using db::disk32_load::StreamBytes;

// Load_XAssetListCustom: the root is read outside the zone blocks.
bool LoadRoot(XAssetListDisk32 *root)
{
    DB_LoadXFileData(reinterpret_cast<std::uint8_t *>(root), sizeof(*root));
    XAssetListDisk32Layout layout{};
    switch (TryValidateXAssetListDisk32Header(root, &layout))
    {
    case XAssetListDisk32Status::Success:
        break;
    case XAssetListDisk32Status::InvalidScriptStringCount:
    case XAssetListDisk32Status::InvalidScriptStringPointerCount:
        Com_Error(ERR_DROP, "Invalid fast-file script-string list count %d", root->stringList.count);
        return false;
    case XAssetListDisk32Status::InvalidAssetPointerCount:
        return Drop("Invalid fast-file asset list pointer/count combination");
    default:
        Com_Error(ERR_DROP, "Invalid fast-file asset count %d", root->assetCount);
        return false;
    }
    // A retail root streams both arrays inline. The 32-bit loader takes any
    // non-null token as inline; here any other token is malformed.
    if ((root->assetCount && !root->assets.token.isInline())
        || (root->stringList.count && !root->stringList.strings.token.isInline()))
    {
        return Drop("Fast-file asset list arrays are not inline");
    }
    return true;
}

// Load_ScriptStringList: count 4-byte tokens, then each inline string. Every
// native slot holds the interned string id, as each 32-bit slot does.
bool LoadScriptStrings(const ScriptStringListDisk32 &disk, ScriptStringList *native)
{
    if (!disk.count)
        return true;
    const std::uint32_t bytes = static_cast<std::uint32_t>(disk.count) * sizeof(ScriptStringTokenDisk32);
    std::uint8_t *const tokens = DB_AllocStreamPos(3);
    ScriptStringListDisk32Iterator strings{};
    if (!StreamBytes(tokens, static_cast<std::int32_t>(bytes)))
        return false;
    if (TryBeginScriptStringListDisk32(&disk, tokens, bytes, &strings) != ScriptStringListDisk32Status::Success)
        return Drop("Fast-file script string is shared-inline");
    const char **const scriptStrings = AllocNative<const char *>(disk.count);
    if (!scriptStrings)
        return false;
    native->strings = scriptStrings;
    ScriptStringTokenDisk32 token{};
    ScriptStringListDisk32Status status{};
    while ((status = TryNextScriptStringTokenDisk32(&strings, &token)) == ScriptStringListDisk32Status::Success)
    {
        const char **const slot = &scriptStrings[strings.nextIndex() - 1];
        *slot = nullptr;
        if (token.token.isInline())
        {
            char *text = reinterpret_cast<char *>(DB_AllocStreamPos(0));
            if (!text)
                return false;
            Load_TempStringCustom(&text); // leaves the string id in text
            *slot = text;
        }
        else if (!token.token.isNull())
        {
            std::uint32_t id = token.token.value;
            DB_ConvertOffsetToTempString(&id, db::relocation::BlockBit(kVirtualBlock));
            *slot = reinterpret_cast<const char *>(static_cast<std::uintptr_t>(id));
        }
    }
    return status == ScriptStringListDisk32Status::End || Drop("Invalid fast-file script-string token");
}

bool AdmitType(void *, std::int32_t type) noexcept
{
    return DB_IsXAssetTypeSupportedForBuild(static_cast<XAssetType>(type));
}

// Load_XAssetArrayCustom: count 8-byte records, then each asset. Every type
// is checked before the first asset loads.
bool LoadAssets(const XAssetListDisk32 &root, XAssetList *native)
{
    if (!root.assetCount)
        return true;
    const std::uint32_t bytes = static_cast<std::uint32_t>(root.assetCount) * sizeof(XAssetDisk32);
    std::uint8_t *const records = DB_AllocStreamPos(3);
    const XAssetTypeDisk32Policy policy{ASSET_TYPE_COUNT, nullptr, AdmitType};
    XAssetListDisk32Iterator assets{};
    if (!StreamBytes(records, static_cast<std::int32_t>(bytes)))
        return false;
    switch (TryBeginXAssetListDisk32(&root, records, bytes, policy, &assets))
    {
    case XAssetListDisk32Status::Success:
        break;
    case XAssetListDisk32Status::UnsupportedAssetType:
        return Drop("Fast-file asset list holds a type this build does not support");
    default:
        return Drop("Invalid fast-file asset type in the asset list");
    }
    XAsset *const nativeAssets = AllocNative<XAsset>(root.assetCount);
    if (!nativeAssets)
        return false;
    XAssetDisk32 disk{};
    XAssetListDisk32Status status{};
    while ((status = TryNextXAssetDisk32(&assets, &disk)) == XAssetListDisk32Status::Success)
    {
        XAsset &asset = nativeAssets[assets.nextIndex() - 1];
        asset.type = static_cast<XAssetType>(disk.type);
        // The header slot contract: the zero-extended disk32 token on entry
        // to the family loader, the native pointer on return.
        asset.header.data = reinterpret_cast<void *>(std::uintptr_t{disk.header.token.value});
    }
    if (status != XAssetListDisk32Status::End)
        return Drop("Invalid fast-file asset record");
    native->assets = nativeAssets;
    for (std::int32_t index = 0; index < root.assetCount; ++index)
    {
        varXAsset = &nativeAssets[index];
        Load_XAsset(false);
        DB_RecordXAssetHeaderSlot(records + sizeof(XAssetDisk32) * index + 4, nativeAssets[index].type,
                                  nativeAssets[index].header.data);
    }
    return true;
}

void LoadList(XAssetList *list)
{
    XAssetListDisk32 root{};
    if (!LoadRoot(&root))
        return;
    list->stringList.count = root.stringList.count;
    list->assetCount = root.assetCount;
    DB_PushStreamPos(kVirtualBlock);
    const bool loaded = LoadScriptStrings(root.stringList, &list->stringList);
    DB_PopStreamPos();
    if (!loaded)
        return;
    DB_PushStreamPos(kVirtualBlock);
    LoadAssets(root, list);
    DB_PopStreamPos();
}
} // namespace
} // namespace db::xasset

void __cdecl DB_LoadXAssetListDisk32(XAssetList *list)
{
    if (!list)
    {
        Com_Error(ERR_DROP, "Invalid 64-bit fast-file asset list request");
        return;
    }
    *list = XAssetList{};
    varXAssetList = list;
    varScriptStringList = &list->stringList;
    db::xasset::LoadList(list);
}

#endif // KISAK_ARCH_64BIT
