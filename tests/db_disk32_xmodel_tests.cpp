// db_disk32_xmodel_tests.cpp: the 64-bit XModel loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with the production
// Load_ScriptStringCustom for bone names. Surfaces are not converted yet, so
// every model fails closed at its surfaces, after its name and bone arrays
// load; the checks read what those left in block 4.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <array>
#include <cstring>

XAssetList *varXAssetList; // the envelope's native list (db_disk32_envelope.cpp)

namespace
{
using namespace disk32_test;

// Zone script-string index n holds interned id 100 + n, as the envelope leaves it.
std::array<const char *, 6> g_strings{};
XAssetList g_list{{6, g_strings.data()}, 0, nullptr};

// A model's bone arrays: each token, in Load_XModel's order.
struct Bones
{
    std::uint32_t names = kInline;
    std::uint32_t parents = kInline;
    std::uint32_t quats = kInline;
    std::uint32_t trans = kInline;
    std::uint32_t classes = kInline;
    std::uint32_t baseMats = kInline;
};

struct File : FileBuilder<File>
{
    // The 220-byte retail record: `bones` bones, one of them a root, and one
    // surface; the LODs, bounds and flags hold distinct values.
    File &Model(std::uint32_t name, std::uint32_t bones, const Bones &arrays, std::uint32_t surfs = kInline,
                std::uint32_t roots = 1, std::uint32_t collSurfs = 0, std::uint32_t collCount = 0)
    {
        Word(name).Word(bones | roots << 8 | 1u << 16);
        Word(arrays.names).Word(arrays.parents).Word(arrays.quats).Word(arrays.trans).Word(arrays.classes);
        Word(arrays.baseMats).Word(surfs).Word(kInline);
        for (int lod = 0; lod < 4; ++lod)
            Float(10.f * static_cast<float>(lod)).Word(1).Word(1).Word(0).Word(0).Word(0).Word(0x00CC0000u | lod);
        Word(collSurfs).Word(collCount).Word(0).Word(kInline).Float(5).Float(-1).Float(-2).Float(-3);
        return Float(1).Float(2).Float(3).Word(1).Word(0xDEADBEEF).Word(0x1234).Word(0x0201).Word(0).Word(0);
    }
    File &Arrays(std::uint32_t bones) // names, parents, quats, trans, classes, base matrices
    {
        for (std::uint32_t bone = 0; bone < bones; ++bone)
            g_file.push_back(static_cast<std::uint8_t>(bone + 1)), g_file.push_back(0);
        for (std::uint32_t bone = 1; bone < bones; ++bone)
            g_file.push_back(static_cast<std::uint8_t>(bone));
        for (std::uint32_t value = 0; value < (bones - 1) * 4; ++value)
            g_file.push_back(static_cast<std::uint8_t>(value)), g_file.push_back(0x40);
        for (std::uint32_t value = 0; value < (bones - 1) * 4; ++value)
            Float(static_cast<float>(value));
        for (std::uint32_t bone = 0; bone < bones; ++bone)
            g_file.push_back(0);
        for (std::uint32_t value = 0; value < bones * 8; ++value)
            Float(value % 8 == 3 ? 1.f : 0.f);
        return *this;
    }
};

struct Zone : disk32_test::Zone<1024>
{
    explicit Zone(std::uint32_t tempBytes = 512) : disk32_test::Zone<1024>(tempBytes)
    {
        for (std::uintptr_t index = 1; index < 6; ++index)
            g_strings[index] = reinterpret_cast<const char *>(100 + index);
        varXAssetList = &g_list;
    }
};

XModel *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadXModelPtrDisk32, slotValue);
}

std::uint16_t Half(const Zone &zone, std::size_t offset)
{
    std::uint16_t value = 0;
    std::memcpy(&value, zone.virt + offset, sizeof(value));
    return value;
}

void TestBoneArrays()
{
    Zone zone;
    // Block 4: "mdl" (0..4), names (4..10), parents (10..12), quats (12..28),
    // trans (28..60), classes (60..63) and base matrices 4-aligned (64..160).
    File().Model(kInline, 3, {}).Text("mdl").Arrays(3);
    ExpectDrop("a model with surfaces", "surfaces are not converted", [] { Load(kInline); });
    Expect(Half(zone, 4) == 101 && Half(zone, 6) == 102 && Half(zone, 8) == 103,
           "inline bone names become interned script-string ids in place");
    Expect(zone.virt[10] == 1 && zone.virt[11] == 2 && Half(zone, 12) == 0x4000 && zone.virt[60] == 0,
           "the parents, quaternions and classifications stream at their retail offsets");
    Expect(!std::memcmp(zone.virt + 28, g_file.data() + 220 + 4 + 6 + 2 + 16, 32)
               && !std::memcmp(zone.virt + 64, g_file.data() + 220 + 4 + 6 + 2 + 16 + 32 + 3, 96),
           "the translations and the 4-aligned base matrices keep their retail bytes");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 160 && !g_arenaUsed,
           "every disk byte up to the surfaces is consumed, and no native storage is used");
}

void TestNamedArrays()
{
    Zone zone;
    // The second model names the first's arrays by offset: names are not
    // interned again.
    File().Model(kInline, 3, {}, 0).Text("a").Arrays(3);
    const Bones named{VirtualOffset(2), VirtualOffset(8), VirtualOffset(10), VirtualOffset(28), VirtualOffset(60),
                      VirtualOffset(64)};
    File().Model(kInline, 3, named).Text("b");
    ExpectDrop("a model without surfaces", "model header", [] { Load(kInline); });
    ExpectDrop("a model naming another's arrays", "surfaces are not converted", [] { Load(kInline); });
    Expect(Half(zone, 2) == 101 && g_read == g_file.size(), "named bone names are not interned twice");
}

struct Malformed
{
    const char *what;
    void (*build)();
    const char *error;
};

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(3); }, "ended unexpectedly"},
    {"more roots than bones", [] { File().Model(kInline, 1, {}, kInline, 2).Text("m"); }, "bone counts"},
    {"collision surfaces without a count", [] { File().Model(kInline, 1, {}, kInline, 1, kInline, 0).Text("m"); },
     "collision or bone counts"},
    {"a collision count without surfaces", [] { File().Model(kInline, 1, {}, kInline, 1, 0, 2).Text("m"); },
     "collision or bone counts"},
    {"negative collision count", [] { File().Model(kInline, 1, {}, kInline, 1, kInline, 0x80000000u).Text("m"); },
     "collision or bone counts"},
    {"bone name past the string list", [] { File().Model(kInline, 1, {}).Text("m").Word(9).Word(0); },
     "script-string index"},
    {"truncated base matrices", [] { File().Model(kInline, 2, {}).Text("m").Arrays(2); g_file.resize(g_file.size() - 4); },
     "ended unexpectedly"},
    {"unmapped parent offset", [] { File().Model(kInline, 2, {kInline, VirtualOffset(512)}).Text("m").Word(1); },
     "pointer offset"},
    {"misaligned name offset", [] { File().Model(kInline, 1, {VirtualOffset(1)}).Text("mmm"); }, "pointer offset"},
    {"unmapped model alias", [] {}, "alias offset"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        test.build();
        const std::uintptr_t slot = g_file.empty() ? VirtualOffset(16) : kInline;
        ExpectDrop(test.what, test.error, [&] { Load(slot); });
    }
    Zone zone;
    ExpectDrop("slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

// db_stringtable_load.cpp's marking path, which no load reaches.
db::load_legacy_bridge::LegacyBridgeStatus db::load_legacy_bridge::DbLoadLegacyBridge::TryAddUser4(std::uint32_t) noexcept
{
    Expect(false, "loading marks no script string");
    return LegacyBridgeStatus::Success;
}

bool db::load_legacy_bridge::DbLoadLegacyBridge::InSession() noexcept
{
    return false;
}

void __cdecl Load_XModelAsset(XAssetHeader *)
{
    Expect(false, "no model publishes before its surfaces convert");
}

void __cdecl Load_PhysPresetAsset(XAssetHeader *)
{
    Expect(false, "no physics preset loads before its model's surfaces convert");
}

int main()
{
    return Run({TestBoneArrays, TestNamedArrays, TestMalformedFailsClosed});
}
