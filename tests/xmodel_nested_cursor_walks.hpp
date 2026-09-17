// xmodel_nested_cursor_walks: the production-walk helper section of the
// nested-cursor contract suite (ki-okmr / #124).
//
// Organizational split only: xmodel_nested_cursor_test.cpp grew past the
// 600-line analyzer budget with this section inline, so the reusable
// production-walk helpers (the config/collision/LOD walks, the nested
// parts window and the checkpoint resolution used by position
// assertions) live here and the contract test functions stay in the
// suite TU. Every CHECK call is preserved verbatim — the header is
// included by exactly one TU, xmodel_nested_cursor_test.cpp, AFTER that
// TU defines its Checker instance and the CHECK macro the helpers
// evaluate through; no contract state is shared and no assertion is
// weakened or duplicated.

#ifndef XMODEL_NESTED_CURSOR_WALKS_HPP
#define XMODEL_NESTED_CURSOR_WALKS_HPP

#include <xanim/buf_cursor.h>

#include "xmodel_cursor_test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace xmodel_nested_cursor_walks
{
using xmodel_cursor_test_support::BuildModelFile;
using xmodel_cursor_test_support::BuildPartsFile;
using xmodel_cursor_test_support::BuildSurfsHeaderFile;
using xmodel_cursor_test_support::ByteWriter;

// Resolve a cursor-owned offset checkpoint back to a pointer inside the
// caller's buffer. Test code holds the real buffer start, so begin +
// offset is always within the object (the cursor validated the offset
// against that same window). Keeps the position assertions readable
// without reintroducing raw-pointer checkpoints into the cursor API.
inline const unsigned char *ResolveCheckpoint(const buf_cursor::Checkpoint &checkpoint,
                                              const unsigned char *bufferBegin)
{
    return bufferBegin + checkpoint.offset;
}

// Config header + collision header: everything XModelLoadFile reads
// before the LOD table.
inline void RunConfigAndCollisionHeader(unsigned char *&pos)
{
    uint16_t version = Buf_Read<unsigned short>(&pos);
    CHECK(version == 25);
    (void)Buf_Read<unsigned char>(&pos);
    for (int i = 0; i < 6; ++i)
        (void)Buf_Read<float>(&pos);
    char physPreset[64];
    CHECK(buf_cursor::ReadString(physPreset, sizeof(physPreset)));
    CHECK(std::strcmp(physPreset, "phys/x") == 0);
    for (int i = 0; i < 4; ++i)
    {
        (void)Buf_Read<float>(&pos);
        char entry[64];
        CHECK(buf_cursor::ReadString(entry, sizeof(entry)));
    }
    (void)Buf_Read<int>(&pos);
    CHECK(!buf_cursor::Failed());

    int numCollSurfs = Buf_Read<int>(&pos);
    CHECK(numCollSurfs == 0);
    CHECK(!buf_cursor::Failed());
}

// The first LOD walk (per LOD: numsurfs + that many surface names).
inline void RunLodTableWalk(unsigned char *&pos)
{
    static const char *const kExpectedNames[] = {
        "mat_first_a", "mat_first_b", "mat_first_c"};
    int nameIndex = 0;
    for (int lod = 0; lod < 2; ++lod)
    {
        uint16_t numsurfs = Buf_Read<unsigned short>(&pos);
        CHECK(numsurfs == (lod == 0 ? 2 : 1));
        for (uint16_t s = 0; s < numsurfs; ++s)
        {
            char surfName[128];
            CHECK(buf_cursor::ReadString(surfName, sizeof(surfName)));
            CHECK(std::strcmp(surfName, kExpectedNames[nameIndex]) == 0);
            ++nameIndex;
        }
    }
    CHECK(!buf_cursor::Failed());
}

// The per-child-bone body walk: parent-index read, the mid-loop
// re-anchor the production loader issues after each parent-index read,
// then the trans floats and quat shorts.
inline void RunNestedPartsBoneBodies(unsigned char *&pos, int numBones, int numRootBones)
{
    for (int i = numRootBones; i < numBones; ++i)
    {
        uint8_t parentIndex = buf_cursor::ReadWeight();
        buf_cursor::AnchorPos(&pos);  // production re-anchor per bone
        CHECK(parentIndex == 1);
        for (int f = 0; f < 3; ++f)
            (void)Buf_Read<float>(&pos);
        for (int q = 0; q < 4; ++q)
            (void)Buf_Read<unsigned short>(&pos);
    }
}

// The bone-name scan. Returns false where the production loader would
// reject the file (a failed name read); the caller owns the Deactivate.
inline bool RunNestedPartsBoneNames(int numBones)
{
    static const char *const kExpectedBoneNames[] = {"tag_root", "tag_child"};
    for (int i = 0; i < numBones; ++i)
    {
        char nameBuf[128];
        if (!buf_cursor::ReadString(nameBuf, sizeof(nameBuf)))
            return false;
        CHECK(std::strcmp(nameBuf, kExpectedBoneNames[i]) == 0);
    }
    return true;
}

// partClassification room pre-check + bulk read + useBones byte — the
// production tail sequence of XModelPartsLoadFile. Returns false where
// the production loader would reject the file (short classification
// room); the caller owns the Deactivate.
inline bool RunNestedPartsClassification(int numBones, bool &useBones)
{
    const buf_cursor::BufCursor *cursor = buf_cursor::Current();
    CHECK(cursor != nullptr);
    CHECK(static_cast<size_t>(cursor->end - cursor->current) >= static_cast<size_t>(numBones + 1));
    unsigned char classification[2] = {0xEE, 0xEE};
    if (!buf_cursor::ReadBytes(classification, sizeof(classification), numBones))
        return false;
    CHECK(classification[0] == 0 && classification[1] == 1);
    useBones = (buf_cursor::ReadWeight() != 0);
    CHECK(useBones);
    CHECK(!buf_cursor::Failed());
    return true;
}

// The production XModelPartsLoadFile window: activate over the parts
// buffer, anchor, read the full body, deactivate. Sets no domain
// limits — exactly like production, where Activate resets the nested
// scope's limits to the defaults. Returns false exactly where the
// production loader would reject the file.
inline bool RunNestedPartsWindow(const ByteWriter &partsFile, bool &useBones)
{
    unsigned char *pos = const_cast<unsigned char *>(partsFile.bytes.data());
    buf_cursor::Activate(partsFile.bytes.data(), partsFile.bytes.size());
    buf_cursor::AnchorPos(&pos);

    uint16_t version = Buf_Read<unsigned short>(&pos);
    if (version != 25)
    {
        buf_cursor::Deactivate();
        return false;
    }
    uint16_t numChildBones = Buf_Read<unsigned short>(&pos);
    uint16_t numRootBones = Buf_Read<unsigned short>(&pos);
    const int numBones = numChildBones + numRootBones;
    CHECK(numBones == 2);

    RunNestedPartsBoneBodies(pos, numBones, numRootBones);
    if (!RunNestedPartsBoneNames(numBones))
    {
        buf_cursor::Deactivate();
        return false;
    }
    if (!RunNestedPartsClassification(numBones, useBones))
    {
        buf_cursor::Deactivate();
        return false;
    }

    buf_cursor::Deactivate();
    return true;
}
}  // namespace xmodel_nested_cursor_walks

#endif  // XMODEL_NESTED_CURSOR_WALKS_HPP
