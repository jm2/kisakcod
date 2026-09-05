#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include "taginfo_save.h"

#include <game/taginfo_disk32.h>

#include <cstring>

namespace taginfo_save
{
namespace
{
// Derive the validated 1-based save-stream entity index from a FULL native
// entity pointer. Null maps to 0 ("no entity"). Every other pointer must sit
// exactly on an entity boundary inside [base, base + count * stride); the
// operator rework of 2026-09-04 requires the full pointer to be classified —
// the legacy WriteField1 SF_ENTITY path truncated through the low 32 bits,
// which corrupts the value as soon as a host widens the pointer field.
std::uint32_t EntityIndexFromPointer(
    const TagInfoEntityMap &entities,
    TagInfoFailFn fail,
    const void *const pointer)
{
    if (pointer == nullptr)
        return 0;

    const auto base = reinterpret_cast<std::uintptr_t>(entities.base);
    const auto candidate = reinterpret_cast<std::uintptr_t>(pointer);
    if (candidate < base)
        fail("tagInfo save: entity pointer precedes the entity array");

    const std::uintptr_t delta = candidate - base;
    if ((delta % entities.stride) != 0)
        fail("tagInfo save: entity pointer is not on an entity boundary");

    const std::uintptr_t slot = delta / entities.stride;
    if (slot >= static_cast<std::uintptr_t>(entities.count))
        fail("tagInfo save: entity index out of range");

    return static_cast<std::uint32_t>(slot) + 1u;
}

// Inverse of EntityIndexFromPointer: validate the 1-based save-stream index
// and rebuild the full native entity pointer.
const void *EntityPointerFromIndex(
    const TagInfoEntityMap &entities,
    TagInfoFailFn fail,
    const std::uint32_t index)
{
    if (index == 0)
        return nullptr;
    if (index > entities.count)
        fail("tagInfo load: entity index out of range");

    return static_cast<const std::uint8_t *>(entities.base)
        + static_cast<std::uintptr_t>(index - 1u) * entities.stride;
}

void ValidateIoArgs(
    const MemoryFile *file,
    const TagInfoEntityMap &entities,
    TagInfoFailFn fail)
{
    if (file == nullptr)
        fail("tagInfo record I/O: no memory file");
    if (entities.base == nullptr || entities.stride == 0 || entities.count == 0)
        fail("tagInfo record I/O: invalid entity map");
}
} // namespace

void WriteHostRecord(
    MemoryFile *file,
    const TagInfoEntityMap &entities,
    TagInfoFailFn fail,
    TagInfoStringFromHandleFn stringFromHandle,
    const TagInfoLiveRecord &live)
{
    ValidateIoArgs(file, entities, fail);

    // Retail mark semantics: the wire carries 1 when a name CString follows
    // and 0 when the record has no name — never the live handle value.
    const std::uint16_t nameMark = live.name != 0u ? 1u : 0u;

    tagInfoHostView view{};
    view.parent = EntityIndexFromPointer(entities, fail, live.parent);
    view.next = EntityIndexFromPointer(entities, fail, live.next);
    view.name = nameMark;
    view.index = live.index;
    std::memcpy(view.axis, live.axis, sizeof(view.axis));
    std::memcpy(view.parentInvAxis, live.parentInvAxis,
        sizeof(view.parentInvAxis));

    const tagInfoDisk32_s disk = TagInfoToDisk32(view);
    MemFile_WriteData(file, static_cast<int>(kTagInfoDisk32Bytes), &disk);
    if (nameMark != 0u)
        MemFile_WriteCString(file, stringFromHandle(live.name));
}

void ReadHostRecord(
    MemoryFile *file,
    const TagInfoEntityMap &entities,
    TagInfoFailFn fail,
    TagInfoHandleFromStringFn handleFromString,
    TagInfoRestoredRecord &out)
{
    ValidateIoArgs(file, entities, fail);

    tagInfoDisk32_s disk{};
    MemFile_ReadData(
        file,
        static_cast<int>(kTagInfoDisk32Bytes),
        reinterpret_cast<std::uint8_t *>(&disk));
    const tagInfoHostView view = TagInfoFromDisk32(disk);

    out.parent = EntityPointerFromIndex(entities, fail, view.parent);
    out.next = EntityPointerFromIndex(entities, fail, view.next);
    out.name = 0;
    if (view.name != 0u)
        out.name = handleFromString(MemFile_ReadCString(file));
    out.index = view.index;
    std::memcpy(out.axis, view.axis, sizeof(out.axis));
    std::memcpy(out.parentInvAxis, view.parentInvAxis,
        sizeof(out.parentInvAxis));
}
} // namespace taginfo_save
