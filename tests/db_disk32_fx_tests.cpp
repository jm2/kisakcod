// db_disk32_fx_tests.cpp: the 64-bit FX loader (NOW row 12) on hand-built
// disk32 zone images (disk32_fixture.hpp), each described in the hand-written
// FX mirrors (fx_fastfile_disk32.h). Beyond the fixture's seams, only the
// asset pool (Load_FxEffectDefAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <EffectsCore/fx_fastfile_disk32.h>

#include <cstring>
#include <string>

namespace
{
using namespace disk32_test;
namespace ff = fx::fastfile;

FxEffectDef g_effects[4]; // what Load_FxEffectDefAsset published

using Zone = disk32_test::Zone<2048>;

// An effect as the hand-written mirrors describe it.
struct Effect
{
    ff::FxEffectDefDisk32 record{{kInline}, 2, 32 + 8, 0, 0, 0, 0, {0}};
    std::string name = "fx_test";
};

struct File : FileBuilder<File>
{
    // The record, then its inline name.
    File &Write(const Effect &e)
    {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&e.record);
        g_file.insert(g_file.end(), bytes, bytes + sizeof(e.record));
        return e.record.name.token.isInline() ? Text(e.name) : *this;
    }
};

const FxEffectDef *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadFxEffectDefHandleDisk32, slotValue);
}

// TestRecord's scalars, and no elements in no native storage.
bool Converted(const FxEffectDef &effect)
{
    return effect.flags == 0x10 && effect.totalSize == 0x1234 && effect.msecLoopingLife == 200
        && !effect.elemDefCountLooping && !effect.elemDefs && g_arenaUsed == 0;
}

void TestRecord()
{
    Zone zone;
    Effect effect;
    effect.record = {{kInline}, 0x10, 0x1234, 200, 0, 0, 0, {0}};
    File().Write(effect);
    const FxEffectDef *const loaded = Load(kInline);
    Expect(loaded == &g_effects[0] && g_published == 1, "an inline effect publishes one pool entry");
    if (loaded != &g_effects[0])
        return;
    Expect(zone.Holds(loaded->name) && loaded->name == zone.At(0) && !std::strcmp(loaded->name, "fx_test"),
           "the name points at its bytes in block 4");
    Expect(Converted(*loaded), "the record's scalars convert, and no elements take no native storage");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(effect.record)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 8,
           "every disk byte is consumed and block 4 advances by the name");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    Effect effect;
    File().Write(effect);
    effect.record.name.token = {VirtualOffset(4)};
    File().Write(effect);
    const FxEffectDef *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_effects[0] && reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "a shared-inline effect publishes into the pool, above 4 GiB");
    Expect(shared && Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const FxEffectDef *const second = Load(kInline);
    Expect(second == &g_effects[1] && shared && second->name == shared->name,
           "a name offset token resolves to the earlier string");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*edit)(Effect &);
    const char *error;
    std::uint32_t tempBytes = 512;
};

const Malformed kMalformed[] = {
    {"a negative count", [](Effect &e) { e.record.elemDefCountEmission = -1; }, "effect header"},
    {"257 elements", [](Effect &e) { e.record = {{kInline}, 0, 0, 0, 1, 1, 255, {kInline}}; }, "effect header"},
    {"counts that wrap 32 bits",
     [](Effect &e) { e.record = {{kInline}, 0, 0, 0, INT32_MAX, INT32_MAX, 2, {0}}; }, "effect header"},
    {"elements with a null token", [](Effect &e) { e.record.elemDefCountOneShot = 1; }, "effect header"},
    {"no elements, but a token", [](Effect &e) { e.record.elemDefs.token = {kInline}; }, "effect header"},
    {"elements, not converted yet", [](Effect &e) { e.record = {{kInline}, 0, 0, 0, 0, 1, 0, {kInline}}; },
     "no 64-bit loader yet"},
    {"a null name", [](Effect &e) { e.record.name.token = {0}; }, "has no name"},
    {"an unmapped name offset", [](Effect &e) { e.record.name.token = {VirtualOffset(40)}; }, "string offset"},
    {"a name past its block", [](Effect &e) { e.name.assign(2100, 'n'); }, "Unterminated"},
    {"a record past the temp block", [](Effect &) {}, "exceeds stream block", 16},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone(test.tempBytes);
        Effect effect;
        test.edit(effect);
        File().Write(effect);
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
    Zone zone;
    g_file.resize(20);
    ExpectDrop("a truncated record", "ended unexpectedly", [] { Load(kInline); });
    ExpectDrop("an unmapped alias", "alias offset", [] { Load(VirtualOffset(16)); });
    ExpectDrop("an alias token in the temp block", "alias offset", [] { Load(1); });
    ExpectDrop("a slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

void __cdecl Load_FxEffectDefAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    FxEffectDef &entry = g_effects[g_published++];
    entry = *header->fx;
    Expect(entry.name && entry.name[0] != '\0', "a published effect has a name");
    header->fx = &entry;
}

int main()
{
    return Run({TestRecord, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}
