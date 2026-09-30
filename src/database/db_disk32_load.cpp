#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/database.h>
#include <database/db_disk32_mirrors.h> // generated from db_disk32.schema
#include <database/db_validation.h>
#include <qcommon/com_error.h>

#include <cstdint>
#include <cstring>

// The first converted family (docs/design/FASTFILE_LOADER.md, wave 1). Each
// step mirrors Load_RawFilePtr/Load_RawFile in db_load.cpp: the same stream
// pushes, alignments and reads, so the zone's block offsets stay the retail
// ones. The disk32 record is read into its mirror and converted into a native
// RawFile, and every pointer resolves at full width. The native header is a
// temporary because Load_RawFileAsset copies it into the asset pool, as it
// copies the 32-bit loader's temp-block header. Frames here hold no
// destructors, since a production Com_Error(ERR_DROP) longjmps out of them;
// an error return may leave a stream pushed, and DB_InitStreams resets the
// stack for the next zone.
namespace
{
constexpr std::uint32_t kTempBlock = 0;
constexpr std::uint32_t kVirtualBlock = 4;

bool Drop(const char *message)
{
    Com_Error(ERR_DROP, "%s", message);
    return false;
}

// Streams size disk bytes at the current position of the current block, as
// Load_Stream does for the 32-bit loader, and reports whether they arrived.
bool StreamBytes(std::uint8_t *at, std::int32_t size)
{
    if (!at)
        return false;
    Load_Stream(true, at, size);
    return DB_GetStreamPos() == at + size;
}

// Load_XString: a null token, an inline string streamed here, or an offset
// token naming a string an earlier record streamed.
bool LoadXString(disk32::Ptr32<const char> field, const char **out)
{
    *out = nullptr;
    if (field.token.isNull())
        return true;
    if (field.token.isInline())
    {
        char *const text = reinterpret_cast<char *>(DB_AllocStreamPos(0));
        char *cursor = text;
        if (!text || !Load_XStringCustom(&cursor))
            return false;
        *out = text;
        return true;
    }
    std::uintptr_t address = 0;
    std::uint32_t byteCount = 0;
    const db::relocation::Status status = DB_ResolveOffsetCString(
        field.token, db::relocation::BlockBit(kVirtualBlock), &address, &byteCount);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file string offset: %s", db::relocation::StatusName(status));
        return false;
    }
    *out = reinterpret_cast<const char *>(address);
    return true;
}

bool LoadRawFile(RawFile *out)
{
    disk32::RawFileDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));

    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file raw file has no name"); // the asset pool hashes it
    out->len = disk.len;
    // As in the 32-bit loader, any non-null buffer token means len + 1 bytes
    // follow inline. Consumers read the buffer as a C string, so its last
    // byte must be the terminator.
    if (!disk.buffer.token.isNull())
    {
        std::int32_t byteCount = 0;
        std::uint8_t *const bytes = DB_AllocStreamPos(0);
        if (!db::validation::CheckedCountSum(disk.len, 1, &byteCount))
            return Drop("Invalid fast-file raw-file length");
        if (!StreamBytes(bytes, byteCount))
            return false;
        if (bytes[byteCount - 1] != '\0')
            return Drop("Fast-file raw file is not terminated");
        out->buffer = reinterpret_cast<const char *>(bytes);
    }
    DB_PopStreamPos();
    return true;
}

void LoadRawFilePtr(disk32::PointerToken token, RawFile **slot)
{
    if (token.isOffset())
    {
        std::uintptr_t pointer = 0;
        const db::relocation::Status status =
            DB_ResolveInsertedPointer(token, DBAliasKind::RawFile, 0, &pointer);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return;
        }
        *slot = reinterpret_cast<RawFile *>(pointer);
        return;
    }
    if (token.isNull() || !DB_AllocStreamPos(3))
        return;
    const DBAliasHandle inserted =
        token.isSharedInline() ? DB_InsertPointer(DBAliasKind::RawFile) : DBAliasHandle{};
    RawFile native{};
    if ((token.isSharedInline() && !inserted) || !LoadRawFile(&native))
        return;
    XAssetHeader header;
    header.rawfile = &native;
    Load_RawFileAsset(&header);
    *slot = header.rawfile;
    if (inserted)
        DB_SetInsertedPointer(inserted, DBAliasKind::RawFile, header.rawfile);
}
} // namespace

void __cdecl DB_LoadRawFilePtrDisk32(bool atStreamStart, RawFile **slot)
{
    // Asset headers arrive inside the already-streamed XAsset array; a native
    // slot is never the 4-byte disk slot at the stream position.
    if (atStreamStart || !slot)
    {
        Drop("Invalid 64-bit fast-file raw-file header request");
        return;
    }
    std::uintptr_t raw = 0;
    std::memcpy(&raw, slot, sizeof(raw));
    *slot = nullptr;
    if (raw > UINT32_MAX)
    {
        Drop("Fast-file raw-file header slot holds no disk32 token");
        return;
    }
    DB_PushStreamPos(kTempBlock);
    LoadRawFilePtr(disk32::PointerToken{static_cast<std::uint32_t>(raw)}, slot);
    DB_PopStreamPos();
}

#endif // KISAK_ARCH_64BIT
