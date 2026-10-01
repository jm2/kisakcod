// db_disk32_load_tests.cpp: the 64-bit RawFile loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_RawFileAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

RawFile g_pool[4]; // what Load_RawFileAsset published

struct File : FileBuilder<File>
{
    File &Record(std::uint32_t name, std::int32_t len, std::uint32_t buffer)
    {
        return Word(name).Word(static_cast<std::uint32_t>(len)).Word(buffer);
    }
};

// A zone with the two blocks a RawFile touches: temp (0) and virtual (4).
using Zone = disk32_test::Zone<96>;

RawFile *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadRawFilePtrDisk32, slotValue);
}

void TestInlineRawFile()
{
    Zone zone;
    File().Record(kInline, 5, kInline).Text("maps/mp/test.gsc").Text("hello");
    const RawFile *const loaded = Load(kInline);
    Expect(loaded == &g_pool[0] && g_published == 1, "inline raw file publishes one pool entry");
    if (loaded != &g_pool[0])
        return;
    Expect(!std::strcmp(loaded->name, "maps/mp/test.gsc") && zone.Holds(loaded->name),
           "name points at its bytes in block 4");
    Expect(loaded->len == 5 && !std::strcmp(loaded->buffer, "hello") && zone.Holds(loaded->buffer),
           "buffer points at len + 1 bytes in block 4");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::RawFileDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 17 + 6,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File()
        .Record(kInline, 2, 0).Text("shared.cfg") // after its 4-byte alias slot
        .Record(VirtualOffset(4), 3, kInline).Text("abc");
    const RawFile *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && !g_pool[0].buffer && g_pool[0].len == 2,
           "shared-inline raw file publishes with a null buffer");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const RawFile *const second = Load(kInline);
    Expect(second == &g_pool[1], "the second record publishes the second pool entry");
    if (second != &g_pool[1])
        return;
    Expect(second->name == shared->name, "a name offset token resolves to the earlier string");
    Expect(!std::strcmp(second->buffer, "abc") && g_read == g_file.size(),
           "the second record streams after the first");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
};

const Malformed kMalformed[] = {
    {"truncated buffer", [] { File().Record(kInline, 32, kInline).Text("a").Text("short"); },
     kInline, "ended unexpectedly"},
    {"buffer past its block", [] { File().Record(kInline, 200, kInline).Text("a"); },
     kInline, "exceeds stream block"},
    {"unterminated buffer", [] { File().Record(kInline, 1, kInline).Text("a").Word(0x41414141); },
     kInline, "not terminated"},
    {"negative length", [] { File().Record(kInline, -1, kInline).Text("a"); }, kInline, "raw-file length"},
    {"null name", [] { File().Record(0, 0, 0); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(8), 0, 0); }, kInline, "string offset"},
    {"name runs off its block",
     [] { File().Record(kInline, 0, 0); for (int i = 0; i < 40; ++i) File().Word(0x42424242); },
     kInline, "Unterminated"},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        test.build();
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_RawFileAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    RawFile &entry = g_pool[g_published++];
    entry = *header->rawfile;
    Expect(entry.name && entry.name[0] != '\0', "a published raw file has a name");
    header->rawfile = &entry;
}

int main()
{
    return Run({TestInlineRawFile, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}
