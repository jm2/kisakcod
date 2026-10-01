#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

#include <cstdint>
#include <cstring>

// A wave-3 "parse" family (docs/design/FASTFILE_LOADER.md): sound aliases
// point at it, and the headless server never plays it. Its header slot and
// inserted-alias pointer step are generated from its schema entry; the record
// body below is custom. It mirrors Load_LoadedSound and Load_MssSound in
// db_load.cpp: the 44-byte record streams into the temp block, its name into
// block 4, and its sound data into the temp block after the record. The temp
// block is a stack, so the pops release both; only the name stays.
// The 64-bit targets are headless servers, and Miles ships only as a 32-bit
// Windows DLL, so as the 32-bit headless load does, the native record keeps
// the sound's format but owns no playback buffer: data, data_ptr and
// initial_ptr stay null, nothing reaches SND_SetData, and DB_RemoveLoadedSound
// frees nothing. Frames hold no destructors, since a production ERR_DROP
// longjmps out.
namespace db::disk32_load
{
namespace
{
// The sound data, as Load_MssSound streams it in the temp block: inline bytes
// of the declared length (-2 also registers them as a SoundData alias), or an
// offset token naming an earlier sound's data of the same length. Either way
// the headless record keeps no pointer to it.
bool LoadSoundData(const disk32::LoadedSoundDisk32 &disk)
{
    const disk32::PointerToken token = disk.data.token;
    if (token.isNull())
        return true;
    DB_PushStreamPos(kTempBlock);
    if (token.isOffset())
    {
        std::uintptr_t data = 0;
        const db::relocation::Status status =
            DB_ResolveInsertedPointer(token, DBAliasKind::SoundData, disk.data_len, &data);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return false;
        }
    }
    else
    {
        if (disk.data_len > INT32_MAX)
            return Drop("Invalid fast-file sound data length");
        std::uint8_t *const bytes = DB_AllocStreamPos(0);
        const DBAliasHandle inserted =
            token.isSharedInline() ? DB_InsertPointer(DBAliasKind::SoundData) : DBAliasHandle{};
        if ((token.isSharedInline() && !inserted)
            || !StreamBytes(bytes, static_cast<std::int32_t>(disk.data_len)))
        {
            return false;
        }
        if (inserted)
            DB_SetInsertedPointer(inserted, DBAliasKind::SoundData, bytes, disk.data_len);
    }
    DB_PopStreamPos();
    return true;
}
} // namespace

// Streams the record at the current position of the temp block and converts
// it into *out, the native temporary the pool call copies.
bool LoadLoadedSound(LoadedSound *out)
{
    disk32::LoadedSoundDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file loaded sound has no name"); // the asset pool hashes it
    if (!LoadSoundData(disk))
        return false;
    DB_PopStreamPos();

    _AILSOUNDINFO_COD4 &info = out->sound.info;
    info.format = disk.format;
    info.data_len = disk.data_len;
    info.rate = disk.rate;
    info.bits = disk.bits;
    info.channels = disk.channels;
    info.samples = disk.samples;
    info.block_size = disk.block_size;
    info.data_ptr = nullptr;
    info.initial_ptr = nullptr;
    out->sound.data = nullptr;
    return true;
}
} // namespace db::disk32_load

void __cdecl DB_LoadLoadedSoundPtrDisk32(bool atStreamStart, LoadedSound **slot)
{
    db::disk32_load::LoadLoadedSoundHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
