// db_disk32_loadedsound_tests.cpp: the 64-bit LoadedSound loader (NOW row 12)
// on hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_LoadedSoundAsset) is replaced. The sound
// bytes are synthetic.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_loaders.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>
#include <string_view>

namespace
{
using namespace disk32_test;

LoadedSound g_pool[4]; // what Load_LoadedSoundAsset published

constexpr std::string_view kPcm{"\x10\x32\x54\x76\x98\xBA\xDC\xFE\x01\x23\x45\x67\x89\xAB\xCD\xEF", 16};
constexpr std::uint32_t kRecordBytes = sizeof(disk32::LoadedSoundDisk32);

struct Sound
{
    std::uint32_t name = kInline;
    std::uint32_t dataLen = 0;
    std::uint32_t data = 0;
    std::uint32_t rate = 22050;
    std::uint32_t samples = 8;
};

struct File : FileBuilder<File>
{
    // The 44-byte retail record. data_ptr and initial_ptr hold stale linker
    // values; the loader must never read them as tokens.
    File &Record(const Sound &sound)
    {
        return Word(sound.name).Word(7).Word(0xDEADBEEF).Word(sound.dataLen).Word(sound.rate).Word(16).Word(2)
            .Word(sound.samples).Word(4).Word(kInline).Word(sound.data);
    }
    File &Bytes(std::string_view bytes)
    {
        g_file.insert(g_file.end(), bytes.begin(), bytes.end());
        return *this;
    }
};

// A zone with the two blocks a LoadedSound touches: temp (0) and virtual (4).
struct Zone : disk32_test::Zone<128>
{
    using disk32_test::Zone<128>::Zone;
    bool Is(const char *text, std::string_view expected) const
    {
        return Holds(text) && text == expected;
    }
};

LoadedSound *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadLoadedSoundPtrDisk32, slotValue);
}

bool OwnsNoBuffer(const LoadedSound &sound)
{
    return !sound.sound.data && !sound.sound.info.data_ptr && !sound.sound.info.initial_ptr;
}

void TestInlineLoadedSound()
{
    Zone zone;
    File().Record({.dataLen = 16, .data = kInline, .rate = 44100, .samples = 4}).Text("sound/ui/beep.wav").Bytes(kPcm);
    File().Record({.dataLen = 5}).Text("sound/ui/mute.wav"); // a null data token streams nothing
    const LoadedSound *const sound = Load(kInline);
    Expect(sound == &g_pool[0] && g_published == 1, "an inline loaded sound publishes one pool entry");
    if (sound != &g_pool[0])
        return;
    Expect(zone.Is(sound->name, "sound/ui/beep.wav") && sound->name == zone.At(0), "name points at its bytes in block 4");
    const _AILSOUNDINFO_COD4 &info = sound->sound.info;
    Expect(info.format == 7 && info.data_len == 16 && info.rate == 44100 && info.bits == 16 && info.channels == 2
               && info.samples == 4 && info.block_size == 4,
           "the sound info converts from its retail offsets");
    Expect(OwnsNoBuffer(*sound), "the headless record owns no playback buffer");
    Expect(!std::memcmp(zone.temp, g_file.data(), kRecordBytes),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(!std::memcmp(zone.temp + kRecordBytes, kPcm.data(), kPcm.size()),
           "the sound data follows the record in the temp block");
    Expect(DB_GetStreamPos() == zone.virt + 18, "block 4 advances by the name only");

    const LoadedSound *const mute = Load(kInline);
    Expect(mute == &g_pool[1] && zone.Is(mute->name, "sound/ui/mute.wav") && OwnsNoBuffer(*mute)
               && mute->sound.info.data_len == 5 && g_read == g_file.size(),
           "a null data token streams nothing");
    Expect(!std::memcmp(zone.temp, g_file.data() + kRecordBytes + 18 + kPcm.size(), kRecordBytes),
           "the temp block is released, so the next record streams at its start");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    // After the 4-byte LoadedSound alias slot: the name (4..14), then the
    // SoundData alias slot (16..19).
    File().Record({.dataLen = 8, .data = disk32::kSharedInline}).Text("snd_shared").Bytes(kPcm.substr(0, 8));
    File().Record({.name = VirtualOffset(4), .dataLen = 8, .data = VirtualOffset(16)});
    const LoadedSound *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && zone.Is(g_pool[0].name, "snd_shared") && zone.At(4) == g_pool[0].name,
           "a shared-inline loaded sound publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const LoadedSound *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size() && g_published == 2,
           "a data offset token to an earlier sound's data of the same length loads");
    if (second != &g_pool[1])
        return;
    Expect(second->name == shared->name && OwnsNoBuffer(*second), "a name offset token resolves to the earlier string");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

// The Sound family reaches LoadedSound through a SoundFile in block 4: its
// pointer step must stream in the temp block and return to the parent's block.
void TestNestedSlot()
{
    Zone zone;
    File().Word(0x0101).Word(disk32::kSharedInline).Word(0); // a 12-byte parent record
    File().Record({.dataLen = 4, .data = kInline}).Text("nested").Bytes(kPcm.substr(0, 4));
    File().Word(0x0101).Word(VirtualOffset(12)).Word(0); // a second parent, naming the first sound
    for (int parent = 0; parent < 2; ++parent)
    {
        std::uint8_t *const record = DB_AllocStreamPos(3);
        Load_Stream(true, record, 12);
        disk32::PointerToken token{};
        std::memcpy(&token, record + 4, sizeof(token));
        LoadedSound *slot = nullptr;
        std::memset(&slot, 0xCD, sizeof(slot)); // a narrowed write would leave junk above
        db::disk32_load::LoadLoadedSoundPtr(token, &slot);
        Expect(slot == &g_pool[0] && g_published == 1, "a nested slot gets the full native pointer");
    }
    Expect(!std::memcmp(zone.temp, g_file.data() + 12, kRecordBytes),
           "a nested record streams into the temp block at the retail offset");
    Expect(zone.Is(g_pool[0].name, "nested") && g_pool[0].name == zone.At(16),
           "its name follows the parent and the alias slot in block 4");
    Expect(DB_GetStreamPos() == zone.virt + 36 && g_read == g_file.size(),
           "each step returns to the parent's block 4");
}

void TestNoNativeStorage()
{
    // The native header is a temporary the pool call copies, so the family
    // needs no zone-native storage.
    Zone zone;
    g_arenaCapacity = 0;
    File().Record({.dataLen = 4, .data = kInline}).Text("a").Bytes(kPcm.substr(0, 4));
    Expect(Load(kInline) == &g_pool[0] && g_arenaUsed == 0, "a loaded sound loads with native storage exhausted");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::uint32_t tempBytes = 64;
    std::uintptr_t before = 0; // a well-formed sound loaded first
};

void RunOff()
{
    for (int i = 0; i < 40; ++i)
        File().Word(0x42424242);
}

void SharedSound()
{
    File().Record({.dataLen = 8, .data = disk32::kSharedInline}).Text("snd_shared").Bytes(kPcm.substr(0, 8));
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(7); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record({}).Text("a"); }, kInline, "exceeds stream block", 40},
    {"null name", [] { File().Record({.name = 0}); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record({.name = VirtualOffset(8)}); }, kInline, "string offset"},
    {"name runs off its block", [] { File().Record({}); RunOff(); }, kInline, "Unterminated"},
    {"data past its block", [] { File().Record({.dataLen = 21, .data = kInline}).Text("a"); RunOff(); },
     kInline, "exceeds stream block"},
    {"data length overflows its block", [] { File().Record({.dataLen = INT32_MAX, .data = kInline}).Text("a"); RunOff(); },
     kInline, "exceeds stream block"},
    {"negative data length", [] { File().Record({.dataLen = 0x80000000u, .data = kInline}).Text("a"); RunOff(); },
     kInline, "sound data length"},
    {"data length -1", [] { File().Record({.dataLen = UINT32_MAX, .data = kInline}).Text("a"); RunOff(); },
     kInline, "sound data length"},
    {"truncated data", [] { File().Record({.dataLen = 16, .data = kInline}).Text("a").Word(0); },
     kInline, "ended unexpectedly"},
    {"unmapped data offset", [] { File().Record({.dataLen = 8, .data = VirtualOffset(64)}).Text("a"); },
     kInline, "alias offset"},
    {"data alias of another length",
     [] { SharedSound(); File().Record({.name = VirtualOffset(4), .dataLen = 9, .data = VirtualOffset(16)}); },
     kInline, "alias offset", 64, disk32::kSharedInline},
    {"data offset naming a loaded sound",
     [] { SharedSound(); File().Record({.name = VirtualOffset(4), .dataLen = 8, .data = VirtualOffset(0)}); },
     kInline, "alias offset", 64, disk32::kSharedInline},
    {"loaded-sound offset naming sound data", SharedSound, VirtualOffset(16), "alias offset", 64,
     disk32::kSharedInline},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone(test.tempBytes);
        test.build();
        if (test.before)
        {
            Expect(Load(test.before) == &g_pool[0], test.what, "(the sound before it did not load)");
            g_published = 0;
        }
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_LoadedSoundAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    LoadedSound &entry = g_pool[g_published++];
    entry = *header->loadSnd;
    Expect(entry.name && entry.name[0] != '\0', "a published loaded sound has a name");
    header->loadSnd = &entry;
}

int main()
{
    return Run({TestInlineLoadedSound, TestSharedInlineAndOffsets, TestNestedSlot, TestNoNativeStorage,
                TestMalformedFailsClosed});
}
