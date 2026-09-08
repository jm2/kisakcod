// SPDX-License-Identifier: GPL-3.0
//
// Tests for the tagInfo_s <-> tagInfoDisk32_s save-record converter. The
// production owner is src/game/taginfo_save.cpp (the record module
// g_save.cpp's WriteField2 / ReadField SF_TYPE_TAG_INFO branches delegate
// to); the converter is defined in taginfo_disk32.h so it can be exercised
// without the g_save.cpp dependency tree. The record module itself carries
// its own production-path test (save_taginfo_production_tests.cpp) driven
// through the real memfile stream primitives.
//
// The retail wire image is fixed at 112 bytes (0x70) regardless of host
// pointer width — on x86-64 the host tagInfo_s grows to 0x78 bytes because
// parent/next widen to 8 bytes. The converter must therefore:
//
//   1. Always emit the 0x70-byte image with the pinned offsets regardless of
//      whether the host is ILP32 or LP64/LLP64.
//   2. Carry the parent / next fields as 4-byte 1-based entity indices, not
//      raw pointer truncations, so the record module can validate them and
//      rebuild the full native pointer on the way back.
//   3. Round-trip the float arrays and the 16-bit name handle verbatim.
//
// The test deliberately avoids the game-side record module; it exercises the
// converter in isolation and pins the wire image byte-for-byte.

#include <game/taginfo_disk32.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace
{
namespace taginfo = taginfo_save;

int failures = 0;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

constexpr std::uint32_t kParentIndex = 0x00112233u;
constexpr std::uint32_t kNextIndex = 0x44556677u;
constexpr std::uint16_t kName = 0xABCDu;
constexpr std::int32_t kIndex = 0x12345678;
constexpr float kAxis[4][3] = {
    { 1.0f, 2.0f, 3.0f },
    { 4.0f, 5.0f, 6.0f },
    { 7.0f, 8.0f, 9.0f },
    { 10.0f, 11.0f, 12.0f },
};
constexpr float kParentInvAxis[4][3] = {
    { -1.0f, -2.0f, -3.0f },
    { -4.0f, -5.0f, -6.0f },
    { -7.0f, -8.0f, -9.0f },
    { -10.0f, -11.0f, -12.0f },
};

std::uint32_t LoadLittleEndianU32(const std::uint8_t *const bytes,
                                  const std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24);
}

std::uint16_t LoadLittleEndianU16(const std::uint8_t *const bytes,
                                  const std::size_t offset)
{
    return static_cast<std::uint16_t>(
        bytes[offset] | (bytes[offset + 1u] << 8));
}

taginfo::tagInfoHostView MakeView()
{
    taginfo::tagInfoHostView view{};
    view.parent = kParentIndex;
    view.next = kNextIndex;
    view.name = kName;
    view.index = kIndex;
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            view.axis[r][c] = kAxis[r][c];
            view.parentInvAxis[r][c] = kParentInvAxis[r][c];
        }
    }
    return view;
}

bool FloatArraysEqual(const float a[4][3], const float b[4][3])
{
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            if (a[r][c] != b[r][c])
                return false;
        }
    }
    return true;
}

void TestStaticContracts()
{
    CHECK(sizeof(taginfo::tagInfoDisk32_s) == 0x70u);
    CHECK(alignof(taginfo::tagInfoDisk32_s) == 4u);
    CHECK(std::is_standard_layout_v<taginfo::tagInfoDisk32_s>);
    CHECK(std::is_trivially_copyable_v<taginfo::tagInfoDisk32_s>);
    CHECK(taginfo::kTagInfoDisk32Bytes == 0x70u);

    CHECK(offsetof(taginfo::tagInfoDisk32_s, parent) == 0x00u);
    CHECK(offsetof(taginfo::tagInfoDisk32_s, next) == 0x04u);
    CHECK(offsetof(taginfo::tagInfoDisk32_s, name) == 0x08u);
    CHECK(offsetof(taginfo::tagInfoDisk32_s, index) == 0x0Cu);
    CHECK(offsetof(taginfo::tagInfoDisk32_s, axis) == 0x10u);
    CHECK(offsetof(taginfo::tagInfoDisk32_s, parentInvAxis) == 0x40u);

    // The host view is the same shape as the Disk32 image because both
    // fix the (x86) field layout — the only host/Disk32 difference is the
    // pointer-width fields that the converter handles.
    CHECK(sizeof(taginfo::tagInfoHostView) == 0x70u);
}

void TestForwardRoundTrip()
{
    taginfo::tagInfoHostView view = MakeView();
    taginfo::tagInfoDisk32_s disk = taginfo::TagInfoToDisk32(view);

    CHECK(disk.parent == kParentIndex);
    CHECK(disk.next == kNextIndex);
    CHECK(disk.name == kName);
    CHECK(disk.index == kIndex);
    CHECK(FloatArraysEqual(disk.axis, kAxis));
    CHECK(FloatArraysEqual(disk.parentInvAxis, kParentInvAxis));

    taginfo::tagInfoHostView round = taginfo::TagInfoFromDisk32(disk);
    CHECK(round.parent == kParentIndex);
    CHECK(round.next == kNextIndex);
    CHECK(round.name == kName);
    CHECK(round.index == kIndex);
    CHECK(FloatArraysEqual(round.axis, kAxis));
    CHECK(FloatArraysEqual(round.parentInvAxis, kParentInvAxis));
}

void TestWireImageBytes()
{
    taginfo::tagInfoHostView view = MakeView();
    taginfo::tagInfoDisk32_s disk = taginfo::TagInfoToDisk32(view);

    // Read the converter output through an unsigned-char object view instead
    // of copying into a staging buffer: every access below names the exact
    // pinned wire offset it verifies, and the image is checked byte-for-byte
    // with no library copy call in the analysis path.
    const auto *const bytes = reinterpret_cast<const std::uint8_t *>(&disk);

    CHECK(LoadLittleEndianU32(bytes, 0x00u) == kParentIndex);
    CHECK(LoadLittleEndianU32(bytes, 0x04u) == kNextIndex);
    CHECK(LoadLittleEndianU16(bytes, 0x08u) == kName);
    CHECK(LoadLittleEndianU16(bytes, 0x0Au) == 0u);

    const std::int32_t index = static_cast<std::int32_t>(
        LoadLittleEndianU32(bytes, 0x0Cu));
    CHECK(index == kIndex);

    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            const std::uint32_t encoded = LoadLittleEndianU32(
                bytes, 0x10u + (r * 3 + c) * 4u);
            const float round = std::bit_cast<float>(encoded);
            CHECK(round == kAxis[r][c]);
        }
    }
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            const std::uint32_t encoded = LoadLittleEndianU32(
                bytes, 0x40u + (r * 3 + c) * 4u);
            const float round = std::bit_cast<float>(encoded);
            CHECK(round == kParentInvAxis[r][c]);
        }
    }
}

void TestZeroIndexView()
{
    // The record module writes the 4-byte index 0 to mean "no entity"; the
    // converter must round-trip that without leaking host garbage into the
    // higher pointer bits on x64.
    taginfo::tagInfoHostView view{};
    view.parent = 0u;
    view.next = 0u;
    view.name = 0u;
    view.index = 0;

    taginfo::tagInfoDisk32_s disk = taginfo::TagInfoToDisk32(view);
    CHECK(disk.parent == 0u);
    CHECK(disk.next == 0u);
    CHECK(disk.name == 0u);
    CHECK(disk.index == 0);

    taginfo::tagInfoHostView round = taginfo::TagInfoFromDisk32(disk);
    CHECK(round.parent == 0u);
    CHECK(round.next == 0u);
    CHECK(round.name == 0u);
    CHECK(round.index == 0);
}

void TestLargeIndexView()
{
    // The entity index hits MAX_GENTITIES-1 in production; pick an index
    // that exercises the high bit of the 4-byte field without overflowing.
    constexpr std::uint32_t kLargeParent = 0x8000FFFEu;
    constexpr std::uint32_t kLargeNext = 0x8000FFFFu;

    taginfo::tagInfoHostView view = MakeView();
    view.parent = kLargeParent;
    view.next = kLargeNext;

    taginfo::tagInfoDisk32_s disk = taginfo::TagInfoToDisk32(view);
    CHECK(disk.parent == kLargeParent);
    CHECK(disk.next == kLargeNext);

    taginfo::tagInfoHostView round = taginfo::TagInfoFromDisk32(disk);
    CHECK(round.parent == kLargeParent);
    CHECK(round.next == kLargeNext);
}
} // namespace

int main()
{
    TestStaticContracts();
    TestForwardRoundTrip();
    TestWireImageBytes();
    TestZeroIndexView();
    TestLargeIndexView();

    if (failures != 0)
    {
        std::fprintf(stderr, "save_taginfo_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
