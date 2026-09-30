#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/database.h>
#include <database/db_disk32_mirrors.h> // generated from db_disk32.schema
#include <qcommon/com_error.h>

#include <cstdint>
#include <cstring>

// Wave-1 family PhysPreset (docs/design/FASTFILE_LOADER.md). It follows the
// RawFile loader in db_disk32_load.cpp step for step. Each step mirrors
// Load_PhysPresetPtr/Load_PhysPreset in db_load.cpp: the same stream pushes,
// alignments and reads, so block offsets stay the retail ones. The 44-byte
// disk32 record is read into its mirror and converted field by field into a
// native PhysPreset (56 bytes), and both strings resolve at full width. The
// native header is a temporary because Load_PhysPresetAsset copies it into the
// asset pool. Frames hold no destructors, since a production
// Com_Error(ERR_DROP) longjmps out of them.
//
// Drop, StreamBytes and LoadXString repeat the RawFile loader's helpers, and
// LoadPhysPresetPtr repeats its alias and pool steps with the family's kind
// and pool call. They are the shared part that moves into the generator.
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

bool LoadPhysPreset(PhysPreset *out)
{
    disk32::PhysPresetDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));

    out->type = disk.type;
    out->mass = disk.mass;
    out->bounce = disk.bounce;
    out->friction = disk.friction;
    out->bulletForceScale = disk.bulletForceScale;
    out->explosiveForceScale = disk.explosiveForceScale;
    out->piecesSpreadFraction = disk.piecesSpreadFraction;
    out->piecesUpwardVelocity = disk.piecesUpwardVelocity;
    // The disk byte never lands in a C++ bool: any nonzero byte is true.
    out->tempDefaultToCylinder = disk.tempDefaultToCylinder != 0;

    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file physics preset has no name"); // the asset pool hashes it
    // As in the 32-bit loader, a null sound-alias prefix loads as null.
    if (!LoadXString(disk.sndAliasPrefix, &out->sndAliasPrefix))
        return false;
    DB_PopStreamPos();
    return true;
}

void LoadPhysPresetPtr(disk32::PointerToken token, PhysPreset **slot)
{
    if (token.isOffset())
    {
        std::uintptr_t pointer = 0;
        const db::relocation::Status status =
            DB_ResolveInsertedPointer(token, DBAliasKind::PhysPreset, 0, &pointer);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return;
        }
        *slot = reinterpret_cast<PhysPreset *>(pointer);
        return;
    }
    if (token.isNull() || !DB_AllocStreamPos(3))
        return;
    const DBAliasHandle inserted =
        token.isSharedInline() ? DB_InsertPointer(DBAliasKind::PhysPreset) : DBAliasHandle{};
    PhysPreset native{};
    if ((token.isSharedInline() && !inserted) || !LoadPhysPreset(&native))
        return;
    XAssetHeader header;
    header.physPreset = &native;
    Load_PhysPresetAsset(&header);
    *slot = header.physPreset;
    if (inserted)
        DB_SetInsertedPointer(inserted, DBAliasKind::PhysPreset, header.physPreset);
}
} // namespace

void __cdecl DB_LoadPhysPresetPtrDisk32(bool atStreamStart, PhysPreset **slot)
{
    // Headers arrive inside the already-streamed XAsset array, and a nested
    // slot inside an already-converted parent; a native slot is never the
    // 4-byte disk slot at the stream position.
    if (atStreamStart || !slot)
    {
        Drop("Invalid 64-bit fast-file physics-preset request");
        return;
    }
    std::uintptr_t raw = 0;
    std::memcpy(&raw, slot, sizeof(raw));
    *slot = nullptr;
    if (raw > UINT32_MAX)
    {
        Drop("Fast-file physics-preset slot holds no disk32 token");
        return;
    }
    DB_PushStreamPos(kTempBlock);
    LoadPhysPresetPtr(disk32::PointerToken{static_cast<std::uint32_t>(raw)}, slot);
    DB_PopStreamPos();
}

#endif // KISAK_ARCH_64BIT
