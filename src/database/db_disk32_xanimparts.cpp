#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

// XAnimParts (wave 4, docs/design/FASTFILE_LOADER.md). Its header slot,
// inserted pointer step and scalar copy are generated from its schema entry;
// the rest of the record body below is custom. It mirrors Load_XAnimParts in
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
bool LoadArray(disk32::Ptr32<const void> field, std::int32_t count, int alignMask, T **out)
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

// XAnimPartTrans and XAnimDeltaPartQuat open with a 4-byte head (size, then
// smallTrans or a pad byte) and their union u, which moves from 4 to 8. Until
// the generator has struct-typed fields, these two are described here.
constexpr std::int32_t kHeadBytes = 4;
RUNTIME_OFFSET(XAnimPartTrans, u, 0x4, 0x8);
RUNTIME_OFFSET(XAnimDeltaPartQuat, u, 0x4, 0x8);
constexpr std::size_t kTransIndices = offsetof(XAnimPartTrans, u.frames.indices);
constexpr std::size_t kQuatIndices = offsetof(XAnimDeltaPartQuat, u.frames.indices);

// Frame indices are 16-bit once the animation has 256 frames, as in
// Load_XAnimIndices and Load_XAnimDynamicIndices*.
bool WideIndices(const disk32::XAnimPartsDisk32 &disk)
{
    return disk.numframes >= 0x100;
}

// Zeroed native storage for T, and past sizeof(T) the rest of its trailing
// indices, which start at indicesOffset.
template <typename T>
T *AllocNative(std::size_t indicesOffset, std::int32_t indexBytes)
{
    const std::size_t bytes = std::max(sizeof(T), indicesOffset + static_cast<std::size_t>(indexBytes));
    std::uint8_t *const storage = DB_AllocZoneNative(bytes, alignof(T));
    if (!storage)
    {
        Drop("Fast-file native storage is exhausted");
        return nullptr;
    }
    std::memset(storage, 0, bytes);
    return reinterpret_cast<T *>(storage);
}

// A translation's or rotation's 4-aligned head, then the body that follows it
// at the record's position: frame0 when size is 0, else the frames head up to
// its indices and size + 1 indices. *indexBytes is 0 without frames.
std::uint8_t *StreamHeadAndBody(std::int32_t frame0Bytes, std::int32_t framesHeadBytes, std::int32_t indexWidth,
                                std::int32_t *indexBytes)
{
    std::uint8_t *const head = DB_AllocStreamPos(kAlign4);
    if (!StreamBytes(head, kHeadBytes))
        return nullptr;
    std::uint16_t size = 0;
    std::memcpy(&size, head, sizeof(size));
    *indexBytes = size ? (size + 1) * indexWidth : 0;
    return StreamBytes(head + kHeadBytes, size ? framesHeadBytes + *indexBytes : frame0Bytes) ? head : nullptr;
}

// Load_XAnimPartTrans: frame0 is a vec3; frames are the bounds, the indices
// and 3-component vectors, 8-bit when smallTrans and else 16-bit, 4-aligned.
bool LoadTrans(disk32::Ptr32<const void> field, std::int32_t indexWidth, XAnimPartTrans **out)
{
    using FramesDisk = disk32::XAnimPartTransFramesDisk32;
    constexpr auto framesHeadBytes = static_cast<std::int32_t>(offsetof(FramesDisk, indices));
    if (field.token.isNull())
        return true;
    std::int32_t indexBytes = 0;
    std::uint8_t *const head = StreamHeadAndBody(sizeof(XAnimPartTransData::frame0), framesHeadBytes, indexWidth,
                                                 &indexBytes);
    XAnimPartTrans *const trans = head ? AllocNative<XAnimPartTrans>(kTransIndices, indexBytes) : nullptr;
    if (!trans)
        return false;
    *out = trans;
    std::memcpy(&trans->size, head, sizeof(trans->size));
    trans->smallTrans = head[2];
    const std::uint8_t *const body = head + kHeadBytes;
    if (!trans->size)
    {
        std::memcpy(trans->u.frame0, body, sizeof(trans->u.frame0));
        return true;
    }
    FramesDisk frames{};
    std::memcpy(&frames, body, framesHeadBytes);
    std::memcpy(trans->u.frames.mins, frames.mins, sizeof(frames.mins));
    std::memcpy(trans->u.frames.size, frames.size, sizeof(frames.size));
    std::memcpy(reinterpret_cast<std::uint8_t *>(trans) + kTransIndices, body + framesHeadBytes,
                static_cast<std::size_t>(indexBytes));
    if (trans->smallTrans)
        return LoadArray(frames.frames, trans->size + 1, kAlign1, &trans->u.frames.frames._1);
    return LoadArray(frames.frames, trans->size + 1, kAlign4, &trans->u.frames.frames._2);
}

// Load_XAnimDeltaPartQuat: frame0 and each 4-aligned frame are two 16-bit
// components.
bool LoadQuat(disk32::Ptr32<const void> field, std::int32_t indexWidth, XAnimDeltaPartQuat **out)
{
    using FramesDisk = disk32::XAnimDeltaPartQuatDataFramesDisk32;
    constexpr auto framesHeadBytes = static_cast<std::int32_t>(offsetof(FramesDisk, indices));
    if (field.token.isNull())
        return true;
    std::int32_t indexBytes = 0;
    std::uint8_t *const head = StreamHeadAndBody(sizeof(XAnimDeltaPartQuatData::frame0), framesHeadBytes,
                                                 indexWidth, &indexBytes);
    XAnimDeltaPartQuat *const quat = head ? AllocNative<XAnimDeltaPartQuat>(kQuatIndices, indexBytes) : nullptr;
    if (!quat)
        return false;
    *out = quat;
    std::memcpy(&quat->size, head, sizeof(quat->size));
    const std::uint8_t *const body = head + kHeadBytes;
    if (!quat->size)
    {
        std::memcpy(quat->u.frame0, body, sizeof(quat->u.frame0));
        return true;
    }
    FramesDisk frames{};
    std::memcpy(&frames, body, framesHeadBytes);
    std::memcpy(reinterpret_cast<std::uint8_t *>(quat) + kQuatIndices, body + framesHeadBytes,
                static_cast<std::size_t>(indexBytes));
    return LoadArray(frames.frames, quat->size + 1, kAlign4, &quat->u.frames.frames);
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
    XAnimDeltaPart *const delta = AllocNative<XAnimDeltaPart>(0, 0);
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
        || !LoadDeltaPart(disk, &out->deltaPart)
        || !LoadArray(disk.dataByte, disk.dataByteCount, kAlign1, &out->dataByte)
        || !LoadArray(disk.dataShort, disk.dataShortCount, kAlign2, &out->dataShort)
        || !LoadArray(disk.dataInt, disk.dataIntCount, kAlign4, &out->dataInt)
        || !LoadArray(disk.randomDataShort, static_cast<std::int32_t>(disk.randomDataShortCount), kAlign2,
                      &out->randomDataShort)
        || !LoadArray(disk.randomDataByte, disk.randomDataByteCount, kAlign1, &out->randomDataByte)
        || !LoadArray(disk.randomDataInt, disk.randomDataIntCount, kAlign4, &out->randomDataInt)
        || !LoadIndices(disk, &out->indices))
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
