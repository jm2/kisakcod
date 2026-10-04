#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/13-impactfx.schema

#include <cstddef>
#include <cstdint>
#include <cstring>

// ImpactFx, a wave-2 parse family (docs/design/FASTFILE_LOADER.md): weapons
// name impact tables, and only client code plays their effects. The header
// slot and the inserted pointer step are generated from its schema entry; the
// record body below is custom. It mirrors Load_FxImpactTable and
// Load_FxImpactEntryArray in db_load.cpp: the 8-byte record streams into the
// temp block, its name into block 4, then, for any non-null table token, the
// 12 retail entries 4-aligned in block 4, converted into zone-lifetime native
// storage. Every entry slot is an FxEffectDef token. FX has no 64-bit loader
// yet, so a non-null one fails closed; a table whose slots are all null loads.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
constexpr std::int32_t kSurfaceCount = 12; // Load_FxImpactTable's fixed count

// Load_FxEffectDefHandleArray over one slot array: each token must be null.
template <std::size_t Count>
bool LoadEffects(const disk32::Ptr32<void> (&tokens)[Count], const FxEffectDef *(&out)[Count])
{
    for (std::size_t index = 0; index < Count; ++index)
    {
        if (!tokens[index].token.isNull())
            return Drop("Fast-file impact effects name an FX effect, which has no 64-bit loader yet");
        out[index] = nullptr;
    }
    return true;
}

// The entries at the 4-aligned block-4 position, converted into native storage.
bool LoadEntries(FxImpactEntry **out)
{
    constexpr auto kBytes = static_cast<std::int32_t>(kSurfaceCount * sizeof(disk32::FxImpactEntryDisk32));
    std::uint8_t *const entries = DB_AllocStreamPos(3);
    if (!StreamBytes(entries, kBytes))
        return false;
    FxImpactEntry *const native = AllocNative<FxImpactEntry>(kSurfaceCount);
    if (!native)
        return false;
    for (std::int32_t index = 0; index < kSurfaceCount; ++index)
    {
        disk32::FxImpactEntryDisk32 entry{};
        std::memcpy(&entry, entries + static_cast<std::size_t>(index) * sizeof(entry), sizeof(entry));
        if (!LoadEffects(entry.nonflesh, native[index].nonflesh) || !LoadEffects(entry.flesh, native[index].flesh))
            return false;
    }
    *out = native;
    return true;
}
} // namespace

// The record at the temp block's position, then its name and its entries
// with block 4 pushed.
bool LoadFxImpactTable(FxImpactTable *out)
{
    disk32::FxImpactTableDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file impact effects have no name"); // the asset pool hashes it
    out->table = nullptr;
    // As in the 32-bit loader, any non-null token means the entries follow inline.
    if (!disk.table.token.isNull() && !LoadEntries(&out->table))
        return false;
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadFxImpactTablePtrDisk32(bool atStreamStart, FxImpactTable **slot)
{
    db::disk32_load::LoadFxImpactTableHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
