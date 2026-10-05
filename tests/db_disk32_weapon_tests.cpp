// db_disk32_weapon_tests.cpp: the 64-bit weapon loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). The retail offsets here
// are written out by hand, apart from the schema. Script strings go through
// the production Load_ScriptStringCustom. Beyond the fixture's seams, only the
// asset pool (Load_WeaponDefAsset) and the sound lookup by name
// (DB_FindXAssetHeader) are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <cstddef>
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
using namespace disk32_test;

WeaponDef g_weapons[2]; // what Load_WeaponDefAsset published
std::map<std::string, snd_alias_list_t> g_sounds; // what DB_FindXAssetHeader found, by name

// Zone script-string index n holds interned id 100 + n, as the envelope leaves it.
std::array<const char *, 6> g_strings{};
XAssetList g_list{{6, g_strings.data()}, 0, nullptr};

constexpr std::uint32_t kRecordBytes = 2168;

struct Zone : disk32_test::Zone<256, 2304>
{
    explicit Zone(std::uint32_t tempBytes = 2304) : disk32_test::Zone<256, 2304>(tempBytes)
    {
        for (std::uintptr_t index = 1; index < g_strings.size(); ++index)
            g_strings[index] = reinterpret_cast<const char *>(100 + index);
        varXAssetList = &g_list;
        g_sounds.clear();
    }
};

// A retail run of scalar words: its offset, its first native member's, and
// its length in words.
struct ScalarRun
{
    std::uint32_t disk;
    std::size_t native;
    std::uint32_t words;
};

const ScalarRun kRuns[] = {
    {0x128, offsetof(WeaponDef, playerAnimType), 9},
    {0x224, offsetof(WeaponDef, iReticleCenterSize), 38},
    {0x310, offsetof(WeaponDef, hudIconRatio), 1},
    {0x318, offsetof(WeaponDef, ammoCounterIconRatio), 3},
    {0x328, offsetof(WeaponDef, iAmmoIndex), 1},
    {0x330, offsetof(WeaponDef, iClipIndex), 4},
    {0x344, offsetof(WeaponDef, iSharedAmmoCapIndex), 59},
    {0x438, offsetof(WeaponDef, overlayReticle), 56},
    {0x51C, offsetof(WeaponDef, killIconRatio), 2},
    {0x528, offsetof(WeaponDef, dpadIconRatio), 5},
    {0x540, offsetof(WeaponDef, altWeaponIndex), 17},
    {0x588, offsetof(WeaponDef, projExplosion), 1},
    {0x590, offsetof(WeaponDef, projExplosionEffectForceNormalUp), 1},
    {0x5A0, offsetof(WeaponDef, bProjImpactExplode), 66},
    {0x6AC, offsetof(WeaponDef, vProjectileColor), 6},
    {0x6CC, offsetof(WeaponDef, fAdsAimPitch), 40},
    {0x784, offsetof(WeaponDef, accuracyGraphKnotCount), 22},
    {0x7E4, offsetof(WeaponDef, iUseHintStringIndex), 4},
    {0x7F8, offsetof(WeaponDef, fOOPosAnimLength), 28},
    {0x870, offsetof(WeaponDef, adsDofStart), 2},
};

// The record: every run word holds its own retail offset, every pointer is
// null but the strings Strings() names.
struct Record
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kRecordBytes);

    Record()
    {
        for (const ScalarRun &run : kRuns)
            for (std::uint32_t word = 0; word < run.words; ++word)
                Set(run.disk + word * 4, run.disk + word * 4);
    }
    Record &Set(std::uint32_t at, std::uint32_t value)
    {
        for (std::uint32_t byte = 0; byte < 4; ++byte)
            bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
        return *this;
    }
    // The name, display name, third animation, script and fire rumble,
    // inline; the second graph names the animation's string.
    Record &Strings()
    {
        return Set(0x000, kInline).Set(0x004, kInline).Set(0x050 + 2 * 4, kInline).Set(0x770, VirtualOffset(8))
            .Set(0x7F4, kInline).Set(0x868, kInline);
    }
};

struct File : FileBuilder<File>
{
    File &Write(const Record &record, bool strings = true)
    {
        g_file.insert(g_file.end(), record.bytes.begin(), record.bytes.end());
        return strings ? Text("wpn").Text("WPN").Text("anim").Text("scr").Text("rmb") : *this;
    }
};

WeaponDef *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadWeaponDefPtrDisk32, slotValue);
}

// Every run holds the retail words at its native offset.
bool RunsConverted(const WeaponDef &weapon)
{
    for (const ScalarRun &run : kRuns)
    {
        for (std::uint32_t word = 0; word < run.words; ++word)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, reinterpret_cast<const std::uint8_t *>(&weapon) + run.native + word * 4, sizeof(value));
            if (value != run.disk + word * 4)
                return false;
        }
    }
    return true;
}

// The strings Strings() names point at their bytes in block 4.
bool StringsLoaded(const Zone &zone, const WeaponDef &weapon)
{
    return weapon.szInternalName == zone.At(0) && weapon.szDisplayName == zone.At(4) && weapon.szXAnims[2] == zone.At(8)
        && weapon.accuracyGraphName[1] == zone.At(8) && weapon.szScript == zone.At(13) && weapon.fireRumble == zone.At(17)
        && !std::strcmp(weapon.fireRumble, "rmb") && !weapon.szXAnims[1] && !weapon.szOverlayName;
}

void TestRecord()
{
    Zone zone;
    Record record;
    File().Write(record.Strings());
    const WeaponDef *const weapon = Load(kInline);
    Expect(weapon == &g_weapons[0] && g_published == 1, "an inline weapon publishes one pool entry");
    if (weapon != &g_weapons[0])
        return;
    Expect(RunsConverted(*weapon), "every scalar run converts to its native offset");
    Expect(StringsLoaded(zone, *weapon), "the strings load in order into block 4; a string offset resolves");
    Expect(!weapon->gunXModel[0] && !weapon->bounceSound && !weapon->accuracyGraphKnots[0],
           "every other pointer is null");
    Expect(!std::memcmp(zone.temp, record.bytes.data(), kRecordBytes),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 21,
           "every disk byte is consumed and block 4 advances by the strings");
}

// The tags and notetrack maps, and the sounds: the first named sound and the
// bounce table's first and last slots, with the last named sound and the
// projectile ignition sound naming their holders by offset.
Record &Sounded(Record &record)
{
    record.Set(0x0D8, 3).Set(0x0E8 + 28, 5u << 16).Set(0x108, 1);
    return record.Set(0x154, kInline).Set(0x154 + 44 * 4, VirtualOffset(16)).Set(0x208, kInline)
        .Set(0x6C8, VirtualOffset(144));
}

// Block 4: the first three strings (0..13), the holder at 16 and its name,
// the bounce table at 28, the second holder at 144 and its name, then the
// script and rumble.
void WriteSounded(const Record &record, const char *firstName = "snd_a")
{
    File file;
    g_file.insert(g_file.end(), record.bytes.begin(), record.bytes.end());
    file.Text("wpn").Text("WPN").Text("anim").Word(kInline).Text(firstName);
    for (std::uint32_t slot = 0; slot < disk32::kWeaponBounceSoundCount; ++slot)
        file.Word(slot == 0 ? VirtualOffset(16) : slot == 28 ? kInline : 0);
    file.Word(kInline).Text("snd_b").Text("scr").Text("rmb");
}

const snd_alias_list_t *Sound(const char *name)
{
    const auto found = g_sounds.find(name);
    return found == g_sounds.end() ? nullptr : &found->second;
}

// The sounds Sounded() names.
bool SoundsLoaded(const WeaponDef &weapon)
{
    const snd_alias_list_t *const first = Sound("snd_a");
    const snd_alias_list_t *const second = Sound("snd_b");
    return first && second && weapon.pickupSound == first && weapon.putawaySoundPlayer == first
        && weapon.projIgnitionSound == second && InArena(weapon.bounceSound) && weapon.bounceSound[0] == first
        && weapon.bounceSound[28] == second && !weapon.bounceSound[1] && !weapon.fireSound;
}

void TestSoundsAndScriptStrings()
{
    Zone zone;
    Record record;
    WriteSounded(Sounded(record.Strings()));
    Record second;
    second.Set(0x000, kInline).Set(0x154, VirtualOffset(144)).Set(0x208, VirtualOffset(28));
    File().Write(second, false).Text("w2");
    const WeaponDef *const weapon = Load(kInline);
    const WeaponDef *const other = Load(kInline);
    Expect(weapon == &g_weapons[0] && other == &g_weapons[1], "two weapons with sounds publish");
    if (!weapon || !other)
        return;
    Expect(weapon->hideTags[0] == 103 && weapon->notetrackSoundMapKeys[15] == 105 && weapon->notetrackSoundMapValues[0] == 101
               && !weapon->hideTags[1],
           "the tags and notetrack maps become interned ids");
    Expect(SoundsLoaded(*weapon) && g_sounds.size() == 2, "sounds load by name, through holders and their offsets");
    Expect(other->pickupSound == Sound("snd_b") && other->bounceSound == weapon->bounceSound,
           "a later weapon names a holder and the bounce table by offset");
    Expect(weapon->szScript == zone.At(154) && g_read == g_file.size(),
           "the strings after the sounds stream in order, and every disk byte is consumed");
}

// Sounded() broken at a word of the record, or by its first name.
struct SoundBreak
{
    const char *what;
    std::uint32_t at;
    std::uint32_t value;
    const char *error;
    const char *firstName = "snd_a";
};

const SoundBreak kSoundBreaks[] = {
    {"an empty sound name", 0, 0, "has no value", ""},
    {"an unmapped holder offset", 0x154 + 44 * 4, VirtualOffset(20), "alias offset"},
    {"a shared-inline holder", 0x154 + 44 * 4, disk32::kSharedInline, "alias offset"},
    {"an unmapped bounce-table offset", 0x208, VirtualOffset(16), "alias offset"},
    {"a script string past the list", 0x0D8, 9, "script-string index"},
};

void TestSoundBreaksFailClosed()
{
    for (const SoundBreak &test : kSoundBreaks)
    {
        Zone zone;
        Record record;
        Sounded(record.Strings());
        if (test.at)
            record.Set(test.at, test.value);
        WriteSounded(record, test.firstName);
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File().Write(Record().Strings());
    const WeaponDef *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_weapons[0] && reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "a shared-inline weapon publishes into the pool, above 4 GiB");
    Expect(shared && Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    Expect(!Load(0) && g_published == 1, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    std::uint32_t at; // a word of Strings() set to value
    std::uint32_t value;
    const char *error;
};

constexpr const char *kNotYet = "no 64-bit loader yet";

const Malformed kMalformed[] = {
    {"a gun model", 0x00C + 15 * 4, kInline, kNotYet},
    {"a flash effect", 0x150, kInline, kNotYet},
    {"a reticle", 0x220, kInline, kNotYet},
    {"the knife model", 0x308, kInline, kNotYet},
    {"accuracy knots", 0x77C + 4, kInline, kNotYet},
    {"a null name", 0x000, 0, "has no name"},
    {"an unmapped string offset", 0x7F4, VirtualOffset(400), "string offset"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        File().Write(Record().Strings().Set(test.at, test.value));
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
    {
        Zone zone(2000);
        File().Write(Record().Strings());
        ExpectDrop("a record past the temp block", "exceeds stream block", [] { Load(kInline); });
    }
    Zone zone;
    File().Write(Record().Strings(), false);
    g_file.resize(kRecordBytes - 8);
    ExpectDrop("a truncated record", "ended unexpectedly", [] { Load(kInline); });
    ExpectDrop("an unmapped alias", "alias offset", [] { Load(VirtualOffset(16)); });
    ExpectDrop("a slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

XAssetList *varXAssetList; // the envelope's native list (db_disk32_envelope.cpp)

XAssetHeader __cdecl DB_FindXAssetHeader(XAssetType type, const char *name)
{
    Expect(type == ASSET_TYPE_SOUND && name && name[0], "only sounds are looked up, by a nonempty name");
    XAssetHeader header;
    header.sound = &g_sounds[name];
    return header;
}

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

void __cdecl Load_WeaponDefAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    WeaponDef &entry = g_weapons[g_published++];
    entry = *header->weapon;
    Expect(entry.szInternalName && entry.szInternalName[0] != '\0', "a published weapon has a name");
    header->weapon = &entry;
}

int main()
{
    return Run({TestRecord, TestSoundsAndScriptStrings, TestSharedInlineAndOffsets, TestSoundBreaksFailClosed,
                TestMalformedFailsClosed});
}
