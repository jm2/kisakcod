#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/12-menulist.schema
#include <database/db_validation.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// MenuList, a wave-2 parse family (docs/design/FASTFILE_LOADER.md): only the
// client's UI reads menus. The header slot and the inserted pointer step are
// generated from its schema entry; the record body below is custom. It
// mirrors Load_MenuList and Load_menuDef_ptrArray in db_load.cpp: the 12-byte
// record streams into the temp block, its name into block 4, then, for any
// non-null menus token, menuCount 4-byte menu tokens 4-aligned in block 4,
// converted into zone-lifetime native pointers. Menu has no 64-bit loader
// yet, so a non-null menu token fails closed; a list of null menus loads.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
// The menu tokens at the 4-aligned block-4 position, each into a native slot.
bool LoadMenus(std::int32_t count, menuDef_t ***out)
{
    std::int32_t bytes = 0;
    std::uint8_t *const tokens = DB_AllocStreamPos(3);
    if (!db::validation::CheckedArrayBytes(count, sizeof(disk32::PointerToken), &bytes))
        return Drop("Invalid fast-file menu count");
    if (!StreamBytes(tokens, bytes))
        return false;
    // An empty list still points at its stream position, as on x86.
    menuDef_t **const menus = count ? AllocNative<menuDef_t *>(count) : reinterpret_cast<menuDef_t **>(tokens);
    if (!menus)
        return false;
    for (std::int32_t index = 0; index < count; ++index)
    {
        disk32::PointerToken token{};
        std::memcpy(&token, tokens + static_cast<std::size_t>(index) * sizeof(token), sizeof(token));
        if (!token.isNull())
            return Drop("Fast-file menu list names a menu, which has no 64-bit loader yet");
        menus[index] = nullptr;
    }
    *out = menus;
    return true;
}
} // namespace

// The record at the temp block's position, then its name and its menus with
// block 4 pushed.
bool LoadMenuList(MenuList *out)
{
    disk32::MenuListDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    out->menuCount = disk.menuCount;
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file menu list has no name"); // the asset pool hashes it
    out->menus = nullptr;
    // As in the 32-bit loader, any non-null token means the menus follow inline.
    if (!disk.menus.token.isNull() && !LoadMenus(disk.menuCount, &out->menus))
        return false;
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadMenuListPtrDisk32(bool atStreamStart, MenuList **slot)
{
    db::disk32_load::LoadMenuListHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
