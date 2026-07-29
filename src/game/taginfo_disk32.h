#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <universal/kisak_abi.h>

// Compiler-independent mirror of the legacy 112-byte tagInfo_s save record.
// The retail wire image is fixed by the original retail save format and must
// remain byte-for-byte identical across x86-32, x86-64, AArch64, and any
// other host that inherits the same image. Pinning the size and every member
// offset here keeps the production save contract anchored to the Disk32
// image rather than to the host tagInfo_s, which widens on 64-bit hosts
// (parent/next go from 4 to 8 bytes).
//
// The header is self-contained — it does not include <bgame/bg_public.h> or
// pull in <game/enthandle.h> — so it compiles standalone against the
// production wire image and can be exercised by the save_taginfo_test
// without dragging in the g_save.cpp dependency tree (which requires
// KISAK_MP / KISAK_SP and direct link access to the full game module).
namespace taginfo_save
{
// Pointer-free on-disk layout. Every field is a fixed-width integer or
// float; the host pointer fields become 4-byte entity indices on disk and
// the pre-processor / post-processor pair is responsible for the host-side
// resolution.
struct tagInfoDisk32_s
{
    std::uint32_t parent;
    std::uint32_t next;
    std::uint16_t name;
    std::uint16_t padding;
    std::int32_t index;
    float axis[4][3];
    float parentInvAxis[4][3];
};

static_assert(alignof(tagInfoDisk32_s) == 4,
    "tagInfo Disk32 record must be 4-byte aligned");
static_assert(std::is_standard_layout_v<tagInfoDisk32_s>,
    "tagInfo Disk32 record must be a standard-layout type");
static_assert(std::is_trivially_copyable_v<tagInfoDisk32_s>,
    "tagInfo Disk32 record must be trivially copyable");

// The Disk32 offsets are pinned to the wire image, not the host layout —
// they would shift on 64-bit hosts where tagInfo_s grows to 0x78 bytes.
ONDISK_SIZE(tagInfoDisk32_s, 0x70);
ONDISK_OFFSET(tagInfoDisk32_s, parent, 0x00);
ONDISK_OFFSET(tagInfoDisk32_s, next, 0x04);
ONDISK_OFFSET(tagInfoDisk32_s, name, 0x08);
ONDISK_OFFSET(tagInfoDisk32_s, index, 0x0C);
ONDISK_OFFSET(tagInfoDisk32_s, axis, 0x10);
ONDISK_OFFSET(tagInfoDisk32_s, parentInvAxis, 0x40);

constexpr std::size_t kTagInfoDisk32Bytes = sizeof(tagInfoDisk32_s);

// POD-shaped view of the host tagInfo_s. Game code (g_save.cpp) translates
// the live tagInfo_s into this view before and after the field walker, but
// the layout is host-independent: parent/next are 4-byte entity indices
// (matching the Disk32 wire image), and the name/index/axis fields match
// the host offsets one-for-one. This lets the test exercise the converter
// without touching the production tagInfo_s type.
struct tagInfoHostView
{
    std::uint32_t parent;
    std::uint32_t next;
    std::uint16_t name;
    std::uint16_t padding;
    std::int32_t index;
    float axis[4][3];
    float parentInvAxis[4][3];
};

ONDISK_SIZE(tagInfoHostView, 0x70);

// Reinterpret a host pointer field as the 4-byte entity index the SF_ENTITY
// pre-processor / post-processor works with. On x64 only the lower 4 bytes
// of the pointer-sized field are guaranteed to be set; the re-interpret
// therefore picks up exactly the intended index value on every host width.
inline std::uint32_t HostIndexFromPointer(const void *const pointer)
{
    return static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(pointer));
}

// Inverse of HostIndexFromPointer. The returned pointer is a 4-byte value
// zero-extended on 64-bit hosts; the SF_ENTITY post-processor rebuilds the
// real host pointer from the entity index.
inline void *HostPointerFromIndex(const std::uint32_t index)
{
    return reinterpret_cast<void *>(
        static_cast<std::uintptr_t>(index));
}

// Materialize the 0x70-byte Disk32 record from a flat host view. The caller
// has already resolved entity pointers into 4-byte indices (the SF_ENTITY
// pre-processor does this) so the field copies are exact on every host.
inline tagInfoDisk32_s TagInfoToDisk32(const tagInfoHostView &view)
{
    tagInfoDisk32_s disk{};
    disk.parent = view.parent;
    disk.next = view.next;
    disk.name = view.name;
    disk.index = view.index;
    std::memcpy(disk.axis, view.axis, sizeof(disk.axis));
    std::memcpy(disk.parentInvAxis, view.parentInvAxis,
        sizeof(disk.parentInvAxis));
    return disk;
}

// Inverse of TagInfoToDisk32. Reads the 4-byte indices back into the flat
// host view so the post-processor can rebuild host pointers using the
// SF_ENTITY semantics.
inline tagInfoHostView TagInfoFromDisk32(const tagInfoDisk32_s &disk)
{
    tagInfoHostView view{};
    view.parent = disk.parent;
    view.next = disk.next;
    view.name = disk.name;
    view.index = disk.index;
    std::memcpy(view.axis, disk.axis, sizeof(view.axis));
    std::memcpy(view.parentInvAxis, disk.parentInvAxis,
        sizeof(view.parentInvAxis));
    return view;
}
} // namespace taginfo_save
