#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

// XAnimParts (wave 4, docs/design/FASTFILE_LOADER.md). Its header slot,
// inserted pointer step and scalar copy (copy=scalars) are generated from its
// schema entry; the rest of the record body below is custom. It mirrors Load_XAnimParts in
// db_load.cpp: the 88-byte record streams into the temp block, then its name
// and arrays stream into block 4 in the retail order and alignment. Those
// arrays keep their layout at 64-bit (the schema asserts XAnimNotifyInfo at 8
// bytes on both widths), so the native record points straight at them. Bone
// and notetrack names are zone script-string indices, remapped in place
// through Load_ScriptStringCustom as the 32-bit Load_ScriptString remaps them.
// The delta part's records widen, so they convert into zone-lifetime native
// storage (DB_AllocZoneNative) with their trailing frame indices; their frame
// vectors stay in block 4. Frames hold no destructors, since a production
// ERR_DROP longjmps out.
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

// Frame indices are 16-bit once the animation has 256 frames, as in
// Load_XAnimIndices and Load_XAnimDynamicIndices*.
bool WideIndices(const disk32::XAnimPartsDisk32 &disk)
{
    return disk.numframes >= 0x100;
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

// Where a translation's and a rotation's trailing indices sit natively.
constexpr std::size_t kTransIndices = offsetof(XAnimPartTrans, u.frames.indices);
constexpr std::size_t kQuatIndices = offsetof(XAnimDeltaPartQuat, u.frames.indices);

// Zeroed native storage for T and, past sizeof(T), the rest of its trailing
// indices, which start at indicesOffset.
template <typename T>
T *AllocWithIndices(std::size_t indicesOffset, std::int32_t indexBytes)
{
    const std::size_t bytes = std::max(sizeof(T), indicesOffset + static_cast<std::size_t>(indexBytes));
    std::uint8_t *const storage = DB_AllocZoneNative(bytes, alignof(T));
    if (!storage)
    {
        Drop("Fast-file native storage is exhausted");
        return nullptr;
    }
    std::fill_n(storage, bytes, std::uint8_t{0});
    return reinterpret_cast<T *>(storage);
}

// A translation's or rotation's 4-aligned head, then what follows it at the
// record's position: frame0 (frame0Bytes) when its size is 0, else its frames
// head up to the indices and size + 1 indices of indexWidth bytes each. Fills
// the mirror's head and frames head and returns the bytes after the head; the
// record streams before any native storage is sized from it.
template <typename Disk>
const std::uint8_t *StreamDeltaRecord(std::int32_t frame0Bytes, std::int32_t indexWidth, Disk *disk,
                                      std::int32_t *indexBytes)
{
    constexpr auto headBytes = static_cast<std::int32_t>(offsetof(Disk, frames));
    constexpr auto framesHeadBytes = static_cast<std::int32_t>(offsetof(decltype(Disk::frames), indices));
    std::uint8_t *const head = DB_AllocStreamPos(kAlign4);
    if (!StreamBytes(head, headBytes))
        return nullptr;
    std::copy_n(head, headBytes, reinterpret_cast<std::uint8_t *>(disk));
    *indexBytes = disk->size ? (disk->size + 1) * indexWidth : 0;
    if (!StreamBytes(head + headBytes, disk->size ? framesHeadBytes + *indexBytes : frame0Bytes))
        return nullptr;
    if (disk->size)
        std::copy_n(head + headBytes, framesHeadBytes, reinterpret_cast<std::uint8_t *>(&disk->frames));
    return head + headBytes;
}

// Load_XAnimPartTrans: frame0 is a vec3; frames are the bounds, the indices
// and 3-component vectors, 8-bit when smallTrans and else 16-bit, 4-aligned.
bool LoadTrans(disk32::Ptr32<void> field, std::int32_t indexWidth, XAnimPartTrans **out)
{
    if (field.token.isNull())
        return true;
    disk32::XAnimPartTransDisk32 disk{};
    std::int32_t indexBytes = 0;
    const std::uint8_t *const body = StreamDeltaRecord(sizeof(XAnimPartTransData::frame0), indexWidth, &disk,
                                                       &indexBytes);
    XAnimPartTrans *const trans = body ? AllocWithIndices<XAnimPartTrans>(kTransIndices, indexBytes) : nullptr;
    if (!trans)
        return false;
    *out = trans;
    CopyXAnimPartTransScalars(disk, trans);
    if (!trans->size)
    {
        std::copy_n(body, sizeof(trans->u.frame0), reinterpret_cast<std::uint8_t *>(trans->u.frame0));
        return true;
    }
    std::copy(std::begin(disk.frames.mins), std::end(disk.frames.mins), trans->u.frames.mins);
    std::copy(std::begin(disk.frames.size), std::end(disk.frames.size), trans->u.frames.size);
    std::copy_n(body + offsetof(disk32::XAnimPartTransFramesDisk32, indices), indexBytes,
                reinterpret_cast<std::uint8_t *>(trans) + kTransIndices);
    if (trans->smallTrans)
        return LoadArray(disk.frames.frames, trans->size + 1, kAlign1, &trans->u.frames.frames._1);
    return LoadArray(disk.frames.frames, trans->size + 1, kAlign4, &trans->u.frames.frames._2);
}

// Load_XAnimDeltaPartQuat: frame0 and each 4-aligned frame are two 16-bit
// components.
bool LoadQuat(disk32::Ptr32<void> field, std::int32_t indexWidth, XAnimDeltaPartQuat **out)
{
    if (field.token.isNull())
        return true;
    disk32::XAnimDeltaPartQuatDisk32 disk{};
    std::int32_t indexBytes = 0;
    const std::uint8_t *const body = StreamDeltaRecord(sizeof(XAnimDeltaPartQuatData::frame0), indexWidth, &disk,
                                                       &indexBytes);
    XAnimDeltaPartQuat *const quat = body ? AllocWithIndices<XAnimDeltaPartQuat>(kQuatIndices, indexBytes) : nullptr;
    if (!quat)
        return false;
    *out = quat;
    CopyXAnimDeltaPartQuatScalars(disk, quat);
    if (!quat->size)
    {
        std::copy_n(body, sizeof(quat->u.frame0), reinterpret_cast<std::uint8_t *>(quat->u.frame0));
        return true;
    }
    std::copy_n(body + offsetof(disk32::XAnimDeltaPartQuatDataFramesDisk32, indices), indexBytes,
                reinterpret_cast<std::uint8_t *>(quat) + kQuatIndices);
    return LoadArray(disk.frames.frames, quat->size + 1, kAlign4, &quat->u.frames.frames);
}

// Load_XAnimDeltaPart: the 4-aligned record, then its translation and rotation.
bool LoadDeltaPart(const disk32::XAnimPartsDisk32 &parts, XAnimDeltaPart **out)
{
    *out = nullptr;
    if (parts.deltaPart.token.isNull())
        return true;
    disk32::XAnimDeltaPartDisk32 disk{};
    std::uint8_t *const record = DB_AllocStreamPos(kAlign4);
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    XAnimDeltaPart *const delta = AllocWithIndices<XAnimDeltaPart>(0, 0);
    if (!delta)
        return false;
    *out = delta;
    const std::int32_t indexWidth = WideIndices(parts) ? 2 : 1;
    return LoadTrans(disk.trans, indexWidth, &delta->trans) && LoadQuat(disk.quat, indexWidth, &delta->quat);
}

// Load_XAnimIndices.
bool LoadIndices(const disk32::XAnimPartsDisk32 &disk, XAnimIndices *indices)
{
    const auto count = static_cast<std::int32_t>(disk.indexCount);
    if (WideIndices(disk))
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
