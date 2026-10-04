// db_disk32_sound_tests.cpp: the 64-bit Sound loader (NOW row 12) on hand-built
// disk32 zone images (disk32_fixture.hpp), with SoundCurve's real steps. Speaker
// maps are not converted yet, so every alias fails closed at its speaker map,
// after everything before it loaded into the fixture's native storage.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

snd_alias_list_t g_lists[4];
SndCurve g_curves[2];
int g_listCount = 0;
int g_curveCount = 0;

constexpr std::uint32_t kStreamed = 2 << 6; // flags whose sound-file type is 2
constexpr std::uint32_t kLoaded = 1 << 6;

struct File : FileBuilder<File>
{
    File &List(std::uint32_t name, std::uint32_t head, std::int32_t count)
    {
        return Word(name).Word(head).Word(static_cast<std::uint32_t>(count));
    }
    // A 92-byte retail alias whose float scalars count up from 3.
    File &Alias(std::uint32_t name, std::uint32_t file, std::uint32_t flags, std::uint32_t curve, std::uint32_t map)
    {
        Word(name).Word(0).Word(0).Word(0).Word(file).Word(0x51);
        for (int i = 0; i < 6; ++i)
            Float(3.f + static_cast<float>(i)); // volMin .. distMax
        Word(flags);
        for (int i = 0; i < 4; ++i)
            Float(13.f + static_cast<float>(i)); // slavePercentage .. centerPercentage
        return Word(0x77).Word(curve).Float(23.f).Float(24.f).Float(25.f).Word(map);
    }
    File &SoundFile(std::uint32_t type, std::uint32_t exists, std::uint32_t dir, std::uint32_t name)
    {
        return Word(0xCDCD0000u | exists << 8 | type).Word(dir).Word(name);
    }
    File &Curve(std::uint32_t name, std::int32_t knots = 2)
    {
        Word(name).Word(static_cast<std::uint32_t>(knots)).Float(0).Float(1).Float(1).Float(0);
        for (int i = 0; i < 12; ++i)
            Float(0);
        return *this;
    }
};

using Zone = disk32_test::Zone<1024>;

snd_alias_list_t *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadSndAliasListPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

// The native alias, then its sound file, are the first native storage.
const snd_alias_t &FirstAlias()
{
    return *reinterpret_cast<const snd_alias_t *>(g_arena);
}

bool ScalarsMatch(const snd_alias_t &alias, std::uint32_t flags)
{
    return alias.sequence == 0x51 && alias.volMin == 3.f && alias.distMax == 8.f
        && alias.flags == static_cast<std::int32_t>(flags) && alias.slavePercentage == 13.f
        && alias.centerPercentage == 16.f && alias.startDelay == 0x77 && alias.envelopMin == 23.f
        && alias.envelopPercentage == 25.f;
}

void TestStreamedAlias()
{
    Zone zone;
    g_listCount = g_curveCount = 0;
    // Block 4: name (0..6), aliases (8..100), "a1" (100..103), the sound file
    // (104..116) and its strings (116..127), then "falloff" (127..135). The
    // curve record uses the temp block.
    File().List(kInline, kInline, 1).Text("snd_a").Alias(kInline, kInline, kStreamed, kInline, kInline);
    File().Text("a1").SoundFile(2, 1, kInline, kInline).Text("snd/").Text("x.wav").Curve(kInline).Text("falloff");
    ExpectDrop("an alias with a speaker map", "speaker map", [] { Load(kInline); });
    const snd_alias_t &alias = FirstAlias();
    Expect(g_arenaUsed == sizeof(snd_alias_t) + sizeof(SoundFile) && InArena(alias.soundFile),
           "the alias and its sound file convert into native storage");
    Expect(Is(zone, alias.aliasName, "a1") && !alias.subtitle && ScalarsMatch(alias, kStreamed),
           "the alias's strings and scalars convert from their retail offsets");
    const SoundFile *const file = alias.soundFile;
    Expect(InArena(file) && file->type == 2 && file->exists == 1
               && Is(zone, file->u.streamSnd.filename.info.raw.dir, "snd/")
               && Is(zone, file->u.streamSnd.filename.info.raw.name, "x.wav"),
           "a streamed sound file converts with its two strings");
    Expect(alias.volumeFalloffCurve == &g_curves[0] && Is(zone, g_curves[0].filename, "falloff"),
           "the falloff curve loads through SoundCurve's step");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 135,
           "every disk byte up to the speaker map is consumed at its retail offset");
}

void TestEmptyLists()
{
    Zone zone;
    g_listCount = g_curveCount = 0;
    File().List(kInline, 0, 0).Text("empty"); // after the alias slot: 4..10
    File().List(VirtualOffset(4), 0, 0);
    const snd_alias_list_t *const shared = Load(disk32::kSharedInline);
    const snd_alias_list_t *const named = Load(kInline);
    Expect(shared == &g_lists[0] && named == &g_lists[1] && !shared->head && !shared->count,
           "an empty list publishes with no aliases");
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX && Load(VirtualOffset(0)) == shared,
           "a list alias token resolves to the full native pointer");
    Expect(named == &g_lists[1] && named->aliasName == shared->aliasName && !g_arenaUsed && g_read == g_file.size(),
           "a name offset resolves to the earlier string, and no native storage is used");
}

struct Malformed
{
    const char *what;
    void (*build)();
    const char *error;
    std::size_t arena = kArenaBytes;
};

// One list holding one alias, then its name.
File Prefix(std::uint32_t file = kInline, std::uint32_t flags = kStreamed, std::uint32_t map = kInline)
{
    File().List(kInline, kInline, 1).Text("s").Alias(kInline, file, flags, kInline, map).Text("a");
    return File();
}

const Malformed kMalformed[] = {
    {"truncated list", [] { File().Word(kInline).Word(kInline); }, "ended unexpectedly"},
    {"negative count", [] { File().List(kInline, kInline, -1).Text("s"); }, "direct span"},
    {"count without aliases", [] { File().List(kInline, 0, 1).Text("s"); }, "pointer/count"},
    {"present-empty list", [] { File().List(kInline, kInline, 0).Text("s"); }, "pointer/count"},
    {"null name", [] { File().List(0, 0, 0); }, "no name"},
    {"empty name", [] { File().List(kInline, 0, 0).Text(""); }, "no name"},
    {"truncated aliases", [] { File().List(kInline, kInline, 2).Text("s").Word(0); }, "ended unexpectedly"},
    {"aliases named by offset", [] { File().List(kInline, VirtualOffset(8), 1).Text("s"); }, "by offset"},
    {"a sound file named by offset", [] { Prefix(VirtualOffset(4)); }, "by offset"},
    {"sound-file type 3", [] { Prefix().SoundFile(3, 0, 0, 0); }, "sound-file header"},
    {"a loaded-sound file", [] { Prefix(kInline, kLoaded).SoundFile(1, 0, kInline, 0); }, "loaded-sound"},
    {"sound-file exists 2", [] { Prefix().SoundFile(2, 2, 0, 0); }, "sound-file header"},
    {"bad falloff curve", [] { Prefix().SoundFile(2, 0, 0, 0).Curve(kInline, 1).Text("c"); }, "sound curve"},
    {"alias without a sound file", [] { Prefix(0, kStreamed, 0).Curve(kInline).Text("c"); },
     "completed fast-file sound alias"},
    {"native storage exhausted", [] { Prefix().SoundFile(2, 0, 0, 0).Curve(kInline).Text("c"); }, "exhausted",
     sizeof(snd_alias_t)},
    {"unmapped list alias", [] {}, "alias offset"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        g_listCount = g_curveCount = 0;
        g_arenaCapacity = test.arena;
        test.build();
        const std::uintptr_t slot = g_file.empty() ? VirtualOffset(16) : kInline;
        ExpectDrop(test.what, test.error, [&] { Load(slot); });
    }
    Zone zone;
    ExpectDrop("slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

void __cdecl Load_snd_alias_list_Asset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    snd_alias_list_t &entry = g_lists[g_listCount++];
    ++g_published;
    entry = *header->sound;
    Expect(entry.aliasName && entry.aliasName[0] != '\0', "a published list has a name");
    header->sound = &entry;
}

void __cdecl Load_SndCurveAsset(XAssetHeader *header)
{
    g_curves[g_curveCount] = *header->sndCurve;
    header->sndCurve = &g_curves[g_curveCount++];
}

int main()
{
    return Run({TestStreamedAlias, TestEmptyLists, TestMalformedFailsClosed});
}
