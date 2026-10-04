// db_disk32_sound_tests.cpp: the 64-bit Sound loader (NOW row 12) on hand-built
// disk32 zone images (disk32_fixture.hpp), with LoadedSound's and SoundCurve's
// real steps.
// Aliases, sound files and speaker maps live in the fixture's native storage.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

snd_alias_list_t g_lists[4];
SndCurve g_curves[2];
LoadedSound g_loaded[2];
int g_listCount = 0;
int g_curveCount = 0;
int g_loadedCount = 0;

constexpr std::uint32_t kStreamed = 2 << 6; // flags whose sound-file type is 2
constexpr std::uint32_t kLoaded = 1 << 6;

struct File : FileBuilder<File>
{
    File &List(std::uint32_t name, std::uint32_t head, std::int32_t count)
    {
        return Word(name).Word(head).Word(static_cast<std::uint32_t>(count));
    }
    // A 92-byte retail alias whose float scalars count up from `base`.
    File &Alias(std::uint32_t name, std::uint32_t file, std::uint32_t flags, std::uint32_t curve, std::uint32_t map,
                float base = 3.f)
    {
        Word(name).Word(0).Word(0).Word(0).Word(file).Word(0x51);
        for (int i = 0; i < 6; ++i)
            Float(base + static_cast<float>(i)); // volMin .. distMax
        Word(flags);
        for (int i = 0; i < 4; ++i)
            Float(base + 10.f + static_cast<float>(i)); // slavePercentage .. centerPercentage
        return Word(0x77).Word(curve).Float(base + 20.f).Float(base + 21.f).Float(base + 22.f).Word(map);
    }
    // One 100-byte channel map, valid unless `badCount` or `badLevel`.
    File &Channels(std::uint32_t source, std::uint32_t speakers, bool badCount, bool badLevel)
    {
        Word(speakers + (badCount ? 1 : 0));
        for (std::uint32_t speaker = 0; speaker < 6; ++speaker)
        {
            const bool used = speaker < speakers;
            Word(used ? speaker : 0).Word(used ? source + 1 : 0);
            Float(badLevel && used ? 2.f : 0.25f * static_cast<float>(speaker % 4)).Float(0.5f);
        }
        return *this;
    }
    // A 408-byte speaker map; its last channel map is bad if asked.
    File &Map(std::uint32_t isDefault, std::uint32_t name, bool badCount = false, bool badLevel = false)
    {
        Word(0xCDCDCD00u | isDefault).Word(name);
        Channels(0, 2, false, false).Channels(0, 6, false, false).Channels(1, 2, false, false);
        return Channels(1, 6, badCount, badLevel);
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
    File &Loaded(std::uint32_t name) // a 44-byte LoadedSound with no data
    {
        return Word(name).Word(1).Word(0xDEADBEEF).Word(0).Word(22050).Word(16).Word(1).Word(0).Word(2).Word(0).Word(0);
    }
};

using Zone = disk32_test::Zone<2048>;

snd_alias_list_t *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadSndAliasListPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

bool ScalarsMatch(const snd_alias_t &alias, std::uint32_t flags, float base)
{
    return alias.sequence == 0x51 && alias.volMin == base && alias.distMax == base + 5.f
        && alias.flags == static_cast<std::int32_t>(flags) && alias.slavePercentage == base + 10.f
        && alias.centerPercentage == base + 13.f && alias.startDelay == 0x77 && alias.envelopMin == base + 20.f
        && alias.envelopPercentage == base + 22.f;
}

bool MapMatches(const SpeakerMap *map)
{
    return InArena(map) && !map->isDefault && map->channelMaps[1][1].speakerCount == 6
        && map->channelMaps[1][1].speakers[5].speaker == 5 && map->channelMaps[1][1].speakers[5].numLevels == 2
        && map->channelMaps[0][0].speakers[1].levels[0] == 0.25f;
}

// The first alias TestStreamedAliases builds: its fields, sound file, curve and map.
void ExpectStreamedAlias(const Zone &zone, const snd_alias_t &alias)
{
    Expect(Is(zone, alias.aliasName, "a1") && !alias.subtitle && ScalarsMatch(alias, kStreamed, 3.f),
           "the alias's strings and scalars convert from their retail offsets");
    const SoundFile *const file = alias.soundFile;
    Expect(InArena(file) && file->type == 2 && file->exists == 1
               && Is(zone, file->u.streamSnd.filename.info.raw.dir, "snd/")
               && Is(zone, file->u.streamSnd.filename.info.raw.name, "x.wav"),
           "a streamed sound file converts with its two strings");
    Expect(alias.volumeFalloffCurve == &g_curves[0] && Is(zone, g_curves[0].filename, "falloff"),
           "the falloff curve loads through SoundCurve's step");
    Expect(MapMatches(alias.speakerMap) && Is(zone, alias.speakerMap->name, "map")
               && !std::memcmp(alias.speakerMap->channelMaps, zone.virt + 236, 400),
           "the speaker map converts, its channel maps copied from their retail bytes");
}

void ExpectSecondAlias(const Zone &zone, const snd_alias_t &second, const snd_alias_t &first)
{
    Expect(Is(zone, second.aliasName, "a2") && ScalarsMatch(second, kStreamed, 40.f) && InArena(second.soundFile)
               && second.soundFile != first.soundFile,
           "the second alias converts from its own retail element");
    Expect(InArena(second.speakerMap) && second.speakerMap->isDefault && Is(zone, second.speakerMap->name, ""),
           "a default speaker map may have an empty name");
}

void TestStreamedAliases()
{
    Zone zone;
    g_listCount = g_curveCount = g_loadedCount = 0;
    // Block 4: name (0..6), two aliases (8..192), "a1" (192..195), the sound
    // file (196..208) and its strings (208..219), "falloff" (219..227), the
    // speaker map (228..636) and "map" (636..640); then the second alias's
    // name, file, curve name and map. Curve records use the temp block.
    File().List(kInline, kInline, 2).Text("snd_a").Alias(kInline, kInline, kStreamed, kInline, kInline, 3.f);
    File().Alias(kInline, kInline, kStreamed, kInline, kInline, 40.f).Text("a1").SoundFile(2, 1, kInline, kInline);
    File().Text("snd/").Text("x.wav").Curve(kInline).Text("falloff").Map(0, kInline).Text("map");
    File().Text("a2").SoundFile(2, 0, 0, 0).Curve(kInline).Text("c2").Map(1, kInline).Text("");
    const snd_alias_list_t *const list = Load(kInline);
    Expect(list == &g_lists[0] && g_curveCount == 2, "a list of two aliases publishes, with two curves");
    if (list != &g_lists[0] || !InArena(list->head))
        return;
    Expect(Is(zone, list->aliasName, "snd_a") && list->count == 2, "the list converts");
    ExpectStreamedAlias(zone, list->head[0]);
    ExpectSecondAlias(zone, list->head[1], list->head[0]);
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 1069,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

void TestLoadedAlias()
{
    Zone zone;
    g_listCount = g_curveCount = g_loadedCount = 0;
    File().List(kInline, kInline, 1).Text("snd_l").Alias(kInline, kInline, kLoaded, kInline, kInline);
    File().Text("l").SoundFile(1, 0, kInline, 0xEEEEEEEE).Loaded(kInline).Text("loaded").Curve(kInline).Text("c");
    File().Map(0, kInline).Text("m");
    const snd_alias_list_t *const list = Load(kInline);
    Expect(list == &g_lists[0] && InArena(list->head) && g_loadedCount == 1, "a loaded-sound alias publishes");
    if (list != &g_lists[0] || !InArena(list->head))
        return;
    const SoundFile *const file = list->head[0].soundFile;
    Expect(InArena(file) && file->type == 1 && file->u.loadSnd == &g_loaded[0] && Is(zone, g_loaded[0].name, "loaded"),
           "a type-1 sound file loads its LoadedSound through that family's step");
    Expect(InArena(file) && !file->u.streamSnd.filename.info.raw.name && g_read == g_file.size(),
           "the union's other half is cleared, and every disk byte is consumed");
}

// An alias whose string, sound file and speaker map name `shared`'s.
void ExpectNamedObjects(const snd_alias_t &shared, const snd_alias_t &named)
{
    Expect(named.aliasName == shared.aliasName && named.soundFile == shared.soundFile
               && named.speakerMap == shared.speakerMap && InArena(named.soundFile) && InArena(named.speakerMap),
           "string, sound-file and speaker-map offsets resolve to the earlier native objects");
    Expect(ScalarsMatch(named, kStreamed, 9.f), "the third list's alias is its own");
}

void TestSharedAndOffsets()
{
    Zone zone;
    g_listCount = g_curveCount = g_loadedCount = 0;
    // List A: "A" (0..2), aliases (4..96), "a" (96..98), the sound file
    // (100..112), "f" (112..114), the speaker map (116..524) and "m".
    File().List(kInline, kInline, 1).Text("A").Alias(kInline, kInline, kStreamed, kInline, kInline);
    File().Text("a").SoundFile(2, 0, 0, kInline).Text("f").Curve(kInline).Text("c").Map(0, kInline).Text("m");
    File().List(VirtualOffset(0), VirtualOffset(4), 1); // B shares A's aliases
    File().List(kInline, kInline, 1).Text("C");         // C's alias names A's objects
    File().Alias(VirtualOffset(96), VirtualOffset(100), kStreamed, kInline, VirtualOffset(116), 9.f).Curve(kInline);
    File().Text("c2");
    const snd_alias_list_t *const a = Load(kInline);
    const snd_alias_list_t *const b = Load(kInline);
    const snd_alias_list_t *const c = Load(kInline);
    Expect(a == &g_lists[0] && b == &g_lists[1] && c == &g_lists[2], "three lists publish");
    if (c != &g_lists[2] || !InArena(a->head) || !InArena(c->head))
        return;
    Expect(b->head == a->head && b->aliasName == a->aliasName, "an alias-array offset resolves to the native array");
    ExpectNamedObjects(a->head[0], c->head[0]);
    Expect(g_read == g_file.size(), "every disk byte is consumed");
}

void TestEmptyLists()
{
    Zone zone;
    g_listCount = g_curveCount = g_loadedCount = 0;
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

// Prefix, then a streamed sound file and the curve: everything up to the map.
File WithFile()
{
    return Prefix().SoundFile(2, 0, 0, 0).Curve(kInline).Text("c");
}

const Malformed kMalformed[] = {
    {"truncated list", [] { File().Word(kInline).Word(kInline); }, "ended unexpectedly"},
    {"negative count", [] { File().List(kInline, kInline, -1).Text("s"); }, "direct span"},
    {"count without aliases", [] { File().List(kInline, 0, 1).Text("s"); }, "pointer/count"},
    {"present-empty list", [] { File().List(kInline, kInline, 0).Text("s"); }, "pointer/count"},
    {"null name", [] { File().List(0, 0, 0); }, "no name"},
    {"empty name", [] { File().List(kInline, 0, 0).Text(""); }, "no name"},
    {"truncated aliases", [] { File().List(kInline, kInline, 2).Text("s").Word(0); }, "ended unexpectedly"},
    {"unmapped alias array", [] { File().List(kInline, VirtualOffset(200), 1).Text("s"); }, "alias offset"},
    {"alias array offset of another length", [] { File().List(kInline, kInline, 1).Text("s").Alias(kInline, kInline,
         kStreamed, kInline, kInline).Text("a").SoundFile(2, 0, 0, 0).Curve(kInline).Text("c").Map(0, kInline).Text("m")
         .List(kInline, VirtualOffset(4), 2).Text("t"); Load(kInline); g_published = 0; }, "alias offset"},
    {"a sound file naming its pending alias array", [] { Prefix(VirtualOffset(4)); }, "alias offset"},
    {"sound-file type 3", [] { Prefix().SoundFile(3, 0, 0, 0); }, "sound-file header"},
    {"sound-file exists 2", [] { Prefix().SoundFile(2, 2, 0, 0); }, "sound-file header"},
    {"bad falloff curve", [] { Prefix().SoundFile(2, 0, 0, 0).Curve(kInline, 1).Text("c"); }, "sound curve"},
    {"alias without a sound file", [] { Prefix(0, kStreamed, 0).Curve(kInline).Text("c"); },
     "completed fast-file sound alias"},
    {"speaker map with a null name", [] { WithFile().Map(0, 0); }, "identity"},
    {"speaker map default flag 2", [] { WithFile().Map(2, kInline).Text("m"); }, "default flag"},
    {"non-default speaker map with an empty name", [] { WithFile().Map(0, kInline).Text(""); }, "default flag or name"},
    {"speaker map channel count", [] { WithFile().Map(0, kInline, true).Text("m"); }, "channel count"},
    {"speaker map level above 1", [] { WithFile().Map(0, kInline, false, true).Text("m"); }, "levels"},
    {"a speaker map naming the sound file", [] { Prefix(kInline, kStreamed, VirtualOffset(100))
                                                     .SoundFile(2, 0, 0, 0).Curve(kInline).Text("c"); }, "alias offset"},
    {"type the flags do not name", [] { Prefix(kInline, kLoaded).SoundFile(2, 0, 0, 0).Curve(kInline).Text("c")
                                            .Map(0, kInline).Text("m"); }, "completed fast-file sound alias"},
    {"alias without a speaker map", [] { Prefix(kInline, kStreamed, 0).SoundFile(2, 0, 0, 0).Curve(kInline).Text("c"); },
     "completed fast-file sound alias"},
    {"native storage exhausted", [] { WithFile().Map(0, kInline).Text("m"); }, "exhausted",
     sizeof(snd_alias_t) + sizeof(SoundFile)},
    {"unmapped list alias", [] {}, "alias offset"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        g_listCount = g_curveCount = g_loadedCount = 0;
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

void __cdecl Load_LoadedSoundAsset(XAssetHeader *header)
{
    g_loaded[g_loadedCount] = *header->loadSnd;
    header->loadSnd = &g_loaded[g_loadedCount++];
}

void __cdecl Load_SndCurveAsset(XAssetHeader *header)
{
    g_curves[g_curveCount] = *header->sndCurve;
    header->sndCurve = &g_curves[g_curveCount++];
}

int main()
{
    return Run({TestStreamedAliases, TestLoadedAlias, TestSharedAndOffsets, TestEmptyLists, TestMalformedFailsClosed});
}
