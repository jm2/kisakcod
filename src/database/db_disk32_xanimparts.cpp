#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

#include <cstdint>
#include <cstring>

// XAnimParts (wave 4, docs/design/FASTFILE_LOADER.md). Its header slot,
// inserted pointer step and scalar copy (copy=scalars) are generated from its
// schema entry; the rest of the record body below is custom. It mirrors Load_XAnimParts in
// db_load.cpp: the 88-byte record streams into the temp block, then its name
// and arrays stream into block 4 in the retail order and alignment. Those
// arrays keep their layout at 64-bit (the schema asserts XAnimNotifyInfo at 8
// bytes on both widths), so the native record points straight at them. Bone
// and notetrack names are zone script-string indices, remapped in place
// through Load_ScriptStringCustom as the 32-bit Load_ScriptString remaps them.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
constexpr auto kRecordBytes = static_cast<std::int32_t>(sizeof(disk32::XAnimPartsDisk32));
constexpr int kAllBones = 9; // boneCount[9] counts every bone: the names array
// The AllocLoad_* alignments of the 32-bit loader, as DB_AllocStreamPos masks.
constexpr int kAlign1 = 0;
constexpr int kAlign2 = 1;
constexpr int kAlign4 = 3;

// count elements of T at the given alignment. As in the 32-bit loader, any
// non-null token means the elements follow inline, and a count that is
// negative as an int32_t or overflows the byte size is malformed.
template <typename T>
bool LoadArray(disk32::Ptr32<void> field, std::int32_t count, int alignMask, T **out)
{
    *out = nullptr;
    if (field.token.isNull())
        return true;
    std::int32_t bytes = 0;
    if (!db::validation::CheckedArrayBytes(count, sizeof(T), &bytes))
        return Drop("Invalid fast-file xanim array size");
    std::uint8_t *const at = DB_AllocStreamPos(alignMask);
    if (!StreamBytes(at, bytes))
        return false;
    *out = reinterpret_cast<T *>(at);
    return true;
}

// Load_ScriptStringArray: each zone index becomes its interned string id.
bool LoadBoneNames(const disk32::XAnimPartsDisk32 &disk, std::uint16_t **names)
{
    const std::int32_t count = disk.boneCount[kAllBones];
    if (!LoadArray(disk.names, count, kAlign2, names))
        return false;
    for (std::int32_t index = 0; *names && index < count; ++index)
        Load_ScriptStringCustom(&(*names)[index]);
    return true;
}

// Load_XAnimNotifyInfoArray: the notetracks, each name remapped as above.
bool LoadNotify(const disk32::XAnimPartsDisk32 &disk, XAnimNotifyInfo **notify)
{
    if (!LoadArray(disk.notify, disk.notifyCount, kAlign4, notify))
        return false;
    for (std::int32_t index = 0; *notify && index < disk.notifyCount; ++index)
        Load_ScriptStringCustom(&(*notify)[index].name);
    return true;
}

bool LoadDeltaPart(const disk32::XAnimPartsDisk32 &disk, XAnimDeltaPart **deltaPart)
{
    *deltaPart = nullptr;
    return disk.deltaPart.token.isNull() || Drop("Fast-file xanim delta parts are not converted yet");
}

// Load_XAnimIndices: 16-bit frame indices once the animation has 256 frames.
bool LoadIndices(const disk32::XAnimPartsDisk32 &disk, XAnimIndices *indices)
{
    const auto count = static_cast<std::int32_t>(disk.indexCount);
    if (disk.numframes >= 0x100)
        return LoadArray(disk.indices, count, kAlign2, &indices->_2);
    return LoadArray(disk.indices, count, kAlign1, &indices->_1);
}
// The data and random data arrays, then the frame indices, in stream order.
bool LoadData(const disk32::XAnimPartsDisk32 &disk, XAnimParts *out)
{
    return LoadArray(disk.dataByte, disk.dataByteCount, kAlign1, &out->dataByte)
        && LoadArray(disk.dataShort, disk.dataShortCount, kAlign2, &out->dataShort)
        && LoadArray(disk.dataInt, disk.dataIntCount, kAlign4, &out->dataInt)
        && LoadArray(disk.randomDataShort, static_cast<std::int32_t>(disk.randomDataShortCount), kAlign2,
                     &out->randomDataShort)
        && LoadArray(disk.randomDataByte, disk.randomDataByteCount, kAlign1, &out->randomDataByte)
        && LoadArray(disk.randomDataInt, disk.randomDataIntCount, kAlign4, &out->randomDataInt)
        && LoadIndices(disk, &out->indices);
}
} // namespace

// Streams the record at the stream position and converts it into the native
// temporary *out, which Load_XAnimPartsAsset copies.
bool LoadXAnimParts(XAnimParts *out)
{
    disk32::XAnimPartsDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, kRecordBytes))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    CopyXAnimPartsScalars(disk, out);
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file xanim has no name"); // the asset pool hashes it
    if (!LoadBoneNames(disk, &out->names) || !LoadNotify(disk, &out->notify)
        || !LoadDeltaPart(disk, &out->deltaPart) || !LoadData(disk, out))
    {
        return false;
    }
    DB_PopStreamPos();
    return true;
}
} // namespace db::disk32_load

void __cdecl DB_LoadXAnimPartsPtrDisk32(bool atStreamStart, XAnimParts **slot)
{
    db::disk32_load::LoadXAnimPartsHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
