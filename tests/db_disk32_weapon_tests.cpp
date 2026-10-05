// db_disk32_weapon_tests.cpp: the 64-bit weapon loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). The retail offsets here
// are written out by hand, apart from the schema. Beyond the fixture's seams,
// only the asset pool (Load_WeaponDefAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace
{
using namespace disk32_test;

WeaponDef g_weapons[2]; // what Load_WeaponDefAsset published

constexpr std::uint32_t kRecordBytes = 2168;
using Zone = disk32_test::Zone<256, 2304>;

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
    {"the last named sound", 0x154 + 44 * 4, kInline, kNotYet},
    {"a bounce-sound table", 0x208, kInline, kNotYet},
    {"a reticle", 0x220, kInline, kNotYet},
    {"the knife model", 0x308, kInline, kNotYet},
    {"a projectile sound", 0x59C, kInline, kNotYet},
    {"accuracy knots", 0x77C + 4, kInline, kNotYet},
    {"a hide tag", 0x0D8, 3, "script strings"},
    {"the last notetrack sound value", 0x108 + 28, 2u << 16, "script strings"},
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
    return Run({TestRecord, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}
