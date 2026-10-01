// db_disk32_xanimparts_tests.cpp: the 64-bit XAnimParts loader (NOW row 12,
// wave 4) on hand-built disk32 zone images (disk32_fixture.hpp), then the
// notetrack and delta-part readers in xanim.cpp and xanim_calc.cpp on what it
// loaded. Beyond the fixture's seams, only the asset pool
// (Load_XAnimPartsAsset), the zone's script-string table, which
// Load_ScriptStringCustom reads, and the readers' boundary are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <xanim/xanim.h>
#include <xanim/xanim_calc.h>

#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <vector>

XAssetList *varXAssetList; // the envelope's native list (db_disk32_envelope.cpp)
XAnimParts *XAnimClone(XAnimParts *fromParts, void *(*Alloc)(int));

namespace
{
using namespace disk32_test;

XAnimParts g_pool[4];              // what Load_XAnimPartsAsset published
std::vector<std::uint32_t> g_refs; // the ids SL_AddRefToString received
// Zone script-string index n holds interned id 100 + n, as the envelope leaves it.
std::array<const char *, 6> g_strings{};
XAssetList g_list{{6, g_strings.data()}, 0, nullptr};

// The counts of a retail record; each non-zero count streams its array inline.
struct Spec
{
    std::uint32_t name = kInline;
    std::uint16_t numframes = 255;
    std::uint8_t bones = 0;
    std::uint8_t notifies = 0;
    std::uint16_t bytes = 0;
    std::uint16_t shorts = 0;
    std::uint16_t ints = 0;
    std::uint16_t randomBytes = 0;
    std::uint16_t randomInts = 0;
    std::uint32_t randomShorts = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t deltaPart = 0;
};

struct File : FileBuilder<File>
{
    File &Byte(std::uint8_t value)
    {
        g_file.push_back(value);
        return *this;
    }
    File &Half(std::uint16_t value)
    {
        return Byte(static_cast<std::uint8_t>(value)).Byte(static_cast<std::uint8_t>(value >> 8));
    }
    // The 88-byte retail record, field by field.
    File &Record(const Spec &s)
    {
        const std::size_t start = g_file.size();
        Word(s.name).Half(s.bytes).Half(s.shorts).Half(s.ints).Half(s.randomBytes).Half(s.randomInts);
        Half(s.numframes).Byte(2).Byte(0); // bLoop as any nonzero byte, bDelta
        for (std::uint8_t bone = 0; bone < 9; ++bone)
            Byte(bone);
        Byte(s.bones).Byte(s.notifies).Byte(1).Byte(0).Byte(0); // assetType 1, isDefault, a pad byte
        Word(s.randomShorts).Word(s.indexCount).Float(30.0f).Float(1.5f);
        // names, dataByte, dataShort, dataInt, randomDataShort, randomDataByte, randomDataInt, indices, notify
        for (std::uint32_t count : std::initializer_list<std::uint32_t>{s.bones, s.bytes, s.shorts, s.ints,
                 s.randomShorts, s.randomBytes, s.randomInts, s.indexCount, s.notifies})
            Word(count ? kInline : 0);
        Word(s.deltaPart);
        Expect(g_file.size() - start == 88, "the retail record is 88 bytes");
        return *this;
    }
    // A notetrack: its zone script-string index and when it fires.
    File &Note(std::uint16_t name, float seconds)
    {
        return Half(name).Half(0xEEEE).Float(seconds); // the pad bytes are ignored
    }
    // A translation or rotation head: size, smallTrans (a pad byte in a rotation) and a pad byte.
    File &Head(std::uint16_t size, std::uint8_t smallTrans = 0)
    {
        return Half(size).Byte(smallTrans).Byte(0xEE);
    }
    // A translation's frames head: its mins, its size and an inline frames token.
    File &Bounds()
    {
        return Float(1.0f).Float(2.0f).Float(3.0f).Float(10.0f).Float(20.0f).Float(30.0f).Word(kInline);
    }
};

// Block 4 is 256 bytes; the temp block holds one 88-byte record.
struct Zone : disk32_test::Zone<256>
{
    explicit Zone(std::uint32_t tempBytes = 128) : disk32_test::Zone<256>(tempBytes)
    {
        for (std::uintptr_t index = 1; index < 6; ++index)
            g_strings[index] = reinterpret_cast<const char *>(100 + index);
        varXAssetList = &g_list;
    }
    bool At(const void *pointer, std::size_t offset) const { return pointer == virt + offset; }
};

// Loaded values, compared as a list so a check reads as data.
template <typename... T>
std::vector<double> Values(T... values)
{
    return {static_cast<double>(values)...};
}

// Where each pointer lies, as an offset into block 4.
template <typename... T>
std::vector<std::intptr_t> Offsets(const Zone &zone, const T *...pointers)
{
    const auto base = reinterpret_cast<std::intptr_t>(zone.virt);
    return {reinterpret_cast<std::intptr_t>(pointers) - base...};
}

// Where each pointer lies, as an offset into the zone's native storage.
template <typename... T>
std::vector<std::intptr_t> ArenaOffsets(const T *...pointers)
{
    const auto base = reinterpret_cast<std::intptr_t>(g_arena);
    return {reinterpret_cast<std::intptr_t>(pointers) - base...};
}

XAnimParts *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadXAnimPartsPtrDisk32, slotValue);
}

// Every array but the delta part, in the retail stream order, with an odd byte
// count before each aligned one; block-4 offsets in the comments.
void BuildFull()
{
    File()
        .Record({.bones = 2, .notifies = 3, .bytes = 3, .shorts = 2, .ints = 1, .randomBytes = 1, .randomInts = 1,
                 .randomShorts = 1, .indexCount = 4})
        .Text("anim/run")                          // 0..9
        .Half(1).Half(3)                           // names 10..14
        .Note(2, 0.1f).Note(4, 0.5f).Note(5, 0.9f) // notify 16..40
        .Byte(7).Byte(8).Byte(9)                   // dataByte 40..43
        .Half(0xFFFE).Half(300)                    // dataShort 44..48
        .Word(0x01020304)                          // dataInt 48..52
        .Half(0x8000)                              // randomDataShort 52..54
        .Byte(0x55)                                // randomDataByte 54..55
        .Word(0xCAFEF00D)                          // randomDataInt 56..60
        .Byte(0).Byte(10).Byte(200).Byte(255);     // 8-bit indices 60..64
}

void CheckScalars(const XAnimParts &parts)
{
    // A converted bool is exactly 0 or 1; its byte is read, so an invalid bool is seen rather than loaded.
    const unsigned char loopByte = *reinterpret_cast<const unsigned char *>(&parts.bLoop);
    Expect(Values(parts.numframes, loopByte, parts.bDelta, parts.assetType, parts.isDefault, parts.boneCount[8],
                  parts.boneCount[9], parts.notifyCount)
               == Values(255, 1, 0, 1, 0, 8, 2, 3),
           "the frame count, flags and bone counts convert, a nonzero bool byte as true");
    Expect(Values(parts.dataByteCount, parts.dataShortCount, parts.dataIntCount, parts.randomDataByteCount,
                  parts.randomDataIntCount, parts.randomDataShortCount, parts.indexCount, parts.framerate,
                  parts.frequency)
               == Values(3, 2, 1, 1, 1, 1, 4, 30.0f, 1.5f),
           "the data counts and rates convert from their retail offsets");
}

void CheckArrays(const Zone &zone, const XAnimParts &parts)
{
    const bool placed = Offsets(zone, parts.name, parts.names, parts.notify, parts.dataByte, parts.dataShort,
                                parts.dataInt, parts.randomDataShort, parts.randomDataByte, parts.randomDataInt,
                                parts.indices._1)
        == std::vector<std::intptr_t>{0, 10, 16, 40, 44, 48, 52, 54, 56, 60};
    Expect(placed && !parts.deltaPart, "every array streams in block 4 at its retail alignment");
    if (!placed)
        return;
    const auto [firstNote, firstSeconds] = parts.notify[0];
    const auto [lastNote, lastSeconds] = parts.notify[2];
    Expect(!std::strcmp(parts.name, "anim/run")
               && Values(parts.names[0], parts.names[1], firstNote, parts.notify[1].name, lastNote)
                      == Values(101, 103, 102, 104, 105),
           "bone and notetrack names hold their interned ids");
    Expect(Values(firstSeconds, lastSeconds) == Values(0.1f, 0.9f), "notetracks keep their firing points");
    Expect(Values(parts.dataByte[2], parts.dataShort[0], parts.dataShort[1], parts.dataInt[0], parts.randomDataShort[0],
                  parts.randomDataByte[0], parts.randomDataInt[0], parts.indices._1[1], parts.indices._1[3])
               == Values(9, -2, 300, 0x01020304, -32768, 0x55, static_cast<int>(0xCAFEF00D), 10, 255),
           "the arrays hold their retail bytes; under 256 frames the indices are 8-bit");
}

void TestFullRecord()
{
    Zone zone;
    BuildFull();
    const XAnimParts *const parts = Load(kInline);
    Expect(parts == &g_pool[0] && g_published == 1, "the record publishes one pool entry");
    if (parts != &g_pool[0])
        return;
    CheckScalars(*parts);
    CheckArrays(zone, *parts);
    Expect(!std::memcmp(zone.temp, g_file.data(), 88), "the record streams into the temp block");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 64,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

// 256 frames switch the indices to 16 bits, aligned past an odd byte.
void TestWideIndices()
{
    Zone zone;
    File().Record({.numframes = 256, .randomBytes = 1, .indexCount = 3}).Text("w") // 0..2
        .Byte(1).Half(0).Half(0x1234).Half(255);                                // 2..3, indices 4..10
    const XAnimParts *const parts = Load(kInline);
    Expect(parts == &g_pool[0] && g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 10,
           "16-bit indices stream two bytes each");
    if (parts == &g_pool[0])
        Expect(zone.At(parts->indices._2, 4) && Values(parts->indices._2[1], parts->indices._2[2]) == Values(0x1234, 255),
               "from 256 frames the indices are 16-bit and 2-aligned");
}

// A translation's or rotation's trailing indices, read as a consumer reads them.
template <typename T>
const T *Indices(const XAnimPartTrans &trans)
{
    return reinterpret_cast<const T *>(trans.u.frames.indices._1);
}
template <typename T>
const T *Indices(const XAnimDeltaPartQuat &quat)
{
    return reinterpret_cast<const T *>(quat.u.frames.indices._1);
}

// Under 256 frames: 8-bit indices, 16-bit translation vectors and rotation
// frames, 4-aligned. Block-4 offsets in the comments; the native delta part,
// translation and rotation take 16, 48 and 24 bytes of native storage.
void BuildDeltaPart()
{
    File().Record({.deltaPart = kInline}).Text("d").Word(kInline).Word(kInline) // 0..2, delta part 4..12
        .Head(2).Bounds().Byte(0).Byte(100).Byte(254)                           // 12..16, 16..44, indices 44..47
        .Half(1).Half(2).Half(3).Half(4).Half(5).Half(6).Half(7).Half(8).Half(0xFFFF) // vectors 48..66
        .Head(1).Word(kInline).Byte(0).Byte(254)                                // 68..72, 72..76, indices 76..78
        .Half(0x7FFF).Half(0).Half(0).Half(0x8001);                             // frames 80..88
}

// The loaded delta part, once it and its translation and rotation lie in native storage at these offsets.
const XAnimDeltaPart *LoadedDelta(std::vector<std::intptr_t> offsets, const char *what)
{
    const XAnimParts *const parts = Load(kInline);
    const XAnimDeltaPart *const delta = parts == &g_pool[0] ? parts->deltaPart : nullptr;
    const bool placed = delta && InArena(delta) && ArenaOffsets(delta, delta->trans, delta->quat) == offsets;
    Expect(placed, what);
    return placed ? delta : nullptr;
}

void TestDeltaPart()
{
    Zone zone;
    BuildDeltaPart();
    const XAnimDeltaPart *const delta = LoadedDelta({0, 16, 64}, "the delta part converts into native storage");
    if (!delta)
        return;
    const XAnimPartTrans &trans = *delta->trans;
    const XAnimDeltaPartQuat &quat = *delta->quat;
    const bool vectors = Offsets(zone, trans.u.frames.frames._2, quat.u.frames.frames) == std::vector<std::intptr_t>{48, 80};
    Expect(vectors && g_arenaUsed == 88, "the frame vectors stay in block 4, 4-aligned");
    if (!vectors)
        return;
    Expect(Values(trans.size, trans.smallTrans, trans.u.frames.mins[2], trans.u.frames.size[0],
                  Indices<std::uint8_t>(trans)[1], Indices<std::uint8_t>(trans)[2], trans.u.frames.frames._2[2][2])
               == Values(2, 0, 3.0f, 10.0f, 100, 254, 0xFFFF),
           "the translation keeps its bounds and 8-bit indices natively");
    Expect(Values(quat.size, Indices<std::uint8_t>(quat)[1], quat.u.frames.frames[0][0], quat.u.frames.frames[1][1])
               == Values(1, 254, 0x7FFF, -32767),
           "the rotation keeps its 8-bit indices natively");
    Expect(!std::memcmp(zone.virt + 4, g_file.data() + 90, 8) && g_read == g_file.size()
               && DB_GetStreamPos() == zone.virt + 88,
           "the delta part streams 4-aligned and takes its retail extent");
}

// From 256 frames: 16-bit indices, 8-bit translation vectors (smallTrans)
// unaligned, and a rotation without frames.
void TestWideDeltaPart()
{
    Zone zone;
    File().Record({.numframes = 300, .deltaPart = kInline}).Text("w").Word(kInline).Word(kInline) // 0..2, 4..12
        .Head(2, 1).Bounds().Half(0).Half(150).Half(299)                         // 12..16, 16..44, indices 44..50
        .Byte(1).Byte(2).Byte(3).Byte(4).Byte(5).Byte(6).Byte(7).Byte(8).Byte(9) // vectors 50..59
        .Head(0).Half(0x1234).Half(0xFEDC);                                      // rotation 60..64, frame0 64..68
    const XAnimDeltaPart *const delta = LoadedDelta({0, 16, 64}, "a wide delta part converts into native storage");
    if (!delta)
        return;
    const XAnimPartTrans &trans = *delta->trans;
    const bool vectors = zone.At(trans.u.frames.frames._1, 50);
    Expect(vectors, "8-bit vectors stream unaligned in block 4");
    if (!vectors)
        return;
    Expect(Values(trans.size, trans.smallTrans, Indices<std::uint16_t>(trans)[2], trans.u.frames.frames._1[2][2],
                  delta->quat->size, delta->quat->u.frame0[0], delta->quat->u.frame0[1])
               == Values(2, 1, 299, 9, 0, 0x1234, -292),
           "16-bit translation indices stay native, and a rotation without frames converts its frame0");
    Expect(g_arenaUsed == 88 && g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 68,
           "the wide delta part takes its retail extent");
}

// A translation without frames and no rotation.
void TestStaticDeltaPart()
{
    Zone zone;
    File().Record({.deltaPart = kInline}).Text("s").Word(kInline).Word(0) // 0..2, delta part 4..12
        .Head(0).Float(1.5f).Float(-2.0f).Float(8.0f);                    // translation 12..16, frame0 16..28
    const XAnimParts *const parts = Load(kInline);
    const XAnimDeltaPart *const delta = parts == &g_pool[0] ? parts->deltaPart : nullptr;
    const bool placed = delta && InArena(delta) && ArenaOffsets(delta, delta->trans) == std::vector<std::intptr_t>{0, 16};
    Expect(placed && !delta->quat, "a static delta part converts into native storage, without a rotation");
    if (placed)
        Expect(Values(delta->trans->size, delta->trans->u.frame0[0], delta->trans->u.frame0[2], g_arenaUsed)
                       == Values(0, 1.5f, 8.0f, 64)
                   && DB_GetStreamPos() == zone.virt + 28,
               "a translation without frames converts its frame0, and a null rotation stays null");
}

XAnimParts g_clone; // XAnimClone's allocation
void *CloneAlloc(int size)
{
    return size == static_cast<int>(sizeof(g_clone)) ? &g_clone : nullptr;
}

// The notetrack readers (xanim.cpp) on TestFullRecord's record: the next
// notetrack by firing point, and the script-string references XAnimClone takes.
void TestNotetrackReaders()
{
    Zone zone;
    BuildFull();
    XAnimParts *const parts = Load(kInline);
    if (parts != &g_pool[0])
        return Expect(false, "the notetrack record loads");
    Expect(Values(XAnimGetNextNotifyIndex(parts, 0.0f), XAnimGetNextNotifyIndex(parts, 0.3f),
                  XAnimGetNextNotifyIndex(parts, 0.9f))
               == Values(0, 1, 2),
           "XAnimGetNextNotifyIndex reads the loaded notetrack firing points");
    g_refs.clear();
    Expect(XAnimClone(parts, CloneAlloc) == &g_clone
               && g_refs == std::vector<std::uint32_t>{101, 103, 102, 104, 105},
           "XAnimClone references the interned bone and notetrack names");
}

// Each value within a relative 1e-4 of its expected one, in order.
bool NearAll(const float *values, std::initializer_list<double> expected)
{
    for (double each : expected)
    {
        if (std::fabs(*values++ - each) > 1e-4 * std::fmax(1.0, std::fabs(each)))
            return false;
    }
    return true;
}

// The delta-part reader (xanim_calc.cpp) on TestDeltaPart's record, with the
// values its bytes give: at the end it takes the last frames, and half way
// (frame 127.5 of 255) it finds the key frames through the native indices.
void TestDeltaReader()
{
    Zone zone;
    BuildDeltaPart();
    const XAnimDeltaPart *const delta = LoadedDelta({0, 16, 64}, "the delta record loads");
    // The reader's key-frame search assumes increasing indices: a broken load fails here rather than hangs.
    if (!delta || Values(Indices<std::uint8_t>(*delta->trans)[2], Indices<std::uint8_t>(*delta->quat)[1]) != Values(254, 254))
        return Expect(false, "the delta record loads with its indices");
    float rot[2]{};
    float4 pos{};
    XAnim_CalcDeltaForTime(&g_pool[0], 1.0f, rot, &pos);
    Expect(NearAll(rot, {0, -32767}) && NearAll(pos.v, {10 * 7 + 1, 20 * 8 + 2, 30 * 65535.0 + 3}),
           "the delta at the end reads the last rotation frame and translation vector");
    XAnim_CalcDeltaForTime(&g_pool[0], 0.5f, rot, &pos);
    const double trans = 27.5 / 154; // between indices 100 and 254
    const double quat = 127.5 / 254; // between indices 0 and 254
    Expect(NearAll(pos.v, {10 * (4 + 3 * trans) + 1, 20 * (5 + 3 * trans) + 2, 30 * (6 + 65529 * trans) + 3})
               && NearAll(rot, {32767 * (1 - quat), -32767 * quat}),
           "the delta half way lerps the key frames the native indices select");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::uint32_t tempBytes = 128;
    std::size_t arena = kArenaBytes;
};

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(0); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record({}).Text("a"); }, kInline, "exceeds stream block", 64},
    {"null name", [] { File().Record({.name = 0}); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record({.name = VirtualOffset(8)}); }, kInline, "string offset"},
    {"ints past their block", [] { File().Record({.ints = 64}).Text("a"); }, kInline, "exceeds stream block"},
    {"notetracks past their block", [] { File().Record({.notifies = 40}).Text("a"); },
     kInline, "exceeds stream block"},
    {"16-bit indices past their block", [] { File().Record({.numframes = 300, .indexCount = 200}).Text("a"); },
     kInline, "exceeds stream block"},
    {"negative index count", [] { File().Record({.indexCount = 0x80000000u}).Text("a"); }, kInline, "xanim array size"},
    {"overflowing 16-bit index count", [] { File().Record({.numframes = 256, .indexCount = 0x40000000u}).Text("a"); },
     kInline, "xanim array size"},
    {"negative short count", [] { File().Record({.randomShorts = 0x80000001u}).Text("a"); },
     kInline, "xanim array size"},
    {"overflowing short count", [] { File().Record({.randomShorts = 0x40000000u}).Text("a"); },
     kInline, "xanim array size"},
    {"truncated notetracks", [] { File().Record({.notifies = 2}).Text("a").Note(1, 0.0f); },
     kInline, "ended unexpectedly"},
    {"bone name past the string list", [] { File().Record({.bones = 1}).Text("a").Half(6); },
     kInline, "script-string index"},
    {"truncated delta part", [] { File().Record({.deltaPart = kInline}).Text("a").Word(kInline); },
     kInline, "ended unexpectedly"},
    {"16-bit translation indices past their block",
     [] { File().Record({.numframes = 300, .deltaPart = kInline}).Text("a").Word(kInline).Word(0).Head(200).Bounds(); },
     kInline, "exceeds stream block"},
    {"rotation frames past their block",
     [] { File().Record({.deltaPart = kInline}).Text("a").Word(0).Word(kInline).Head(100).Word(kInline);
          for (std::uint8_t index = 0; index <= 100; ++index) File().Byte(index); },
     kInline, "exceeds stream block"},
    {"native storage exhausted",
     [] { File().Record({.deltaPart = kInline}).Text("a").Word(kInline).Word(0).Head(0).Float(0).Float(0).Float(0); },
     kInline, "exhausted", 128, 16 + 40},
    {"trailing indices past native storage", // 9 8-bit indices end at 49, past the 48-byte translation
     [] { File().Record({.deltaPart = kInline}).Text("a").Word(kInline).Word(0).Head(8).Bounds();
          for (std::uint8_t index = 0; index < 9; ++index) File().Byte(index); },
     kInline, "exhausted", 128, 16 + 48},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone(test.tempBytes);
        g_arenaCapacity = test.arena;
        test.build();
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
    Zone zone;
    XAnimParts *slot = nullptr;
    const Drop drop = Catch([&] { DB_LoadXAnimPartsPtrDisk32(true, &slot); });
    Expect(std::strstr(drop.message, "header request") && !slot, "a header is never at the stream start");
}
} // namespace

// The readers' boundary: script-string references and engine asserts.
void SL_AddRefToString(std::uint32_t stringValue)
{
    g_refs.push_back(stringValue);
}

void MyAssertHandler(const char *, int line, int, const char *, ...)
{
    std::fprintf(stderr, "FAIL: engine assert at line %d\n", line);
    ++g_failures;
}

void __cdecl Load_XAnimPartsAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    XAnimParts &entry = g_pool[g_published++];
    entry = *header->parts;
    Expect(entry.name != nullptr, "a published xanim has a name");
    header->parts = &entry;
}

int main()
{
    return Run({TestFullRecord, TestWideIndices, TestDeltaPart, TestWideDeltaPart, TestStaticDeltaPart,
                TestMalformedFailsClosed, TestNotetrackReaders, TestDeltaReader});
}
