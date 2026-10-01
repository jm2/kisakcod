#pragma once

// disk32_fixture.hpp: what the 64-bit loader tests (NOW row 12) share. A test
// appends a hand-built disk32 zone image to g_file with its File, loads it
// through the family's DB_Load<Name>PtrDisk32 (or the envelope) and the
// production stream code (db_stream.cpp, db_stream_load.cpp,
// db_relocation.cpp) on a synthetic Zone, and checks what its pool call
// published. disk32_fixture.cpp replaces only the engine seams: Com_Error (it
// throws Drop), the inflater (DB_LoadXFileData reads g_file), script-string
// interning (into g_interned) and the zone's native storage (g_arena). Retail
// data never enters tests (docs/ROADMAP.md).

#include <database/database.h>
#include <database/db_disk32.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace disk32_test
{
inline int g_failures = 0;
inline std::vector<std::uint8_t> g_file; // the inflated fast-file bytes
inline std::size_t g_read = 0;           // how many of them the inflater read
inline int g_published = 0;              // how many assets the pool call published
inline std::vector<std::string> g_interned; // script-string id n is entry n - 1
// Production accepts inflater output outside the zone blocks (the envelope's
// XAssetList root); the family tests keep every read inside a block.
inline bool g_allowReadsOutsideBlocks = false;

// The zone's native storage (DB_AllocZoneNative). As a static it lies above
// 4 GiB, so a pointer narrowed to 32 bits cannot land back on it.
inline constexpr std::size_t kArenaBytes = 256;
alignas(16) inline std::uint8_t g_arena[kArenaBytes];
inline std::size_t g_arenaUsed = 0;
inline std::size_t g_arenaCapacity = kArenaBytes;

inline bool InArena(const void *pointer)
{
    const auto *bytes = static_cast<const std::uint8_t *>(pointer);
    return bytes >= g_arena && bytes < g_arena + g_arenaUsed
        && reinterpret_cast<std::uintptr_t>(pointer) > UINT32_MAX;
}

// Starts a new zone image: no bytes, nothing published, interned or allocated.
inline void ResetImage()
{
    g_file.clear();
    g_read = 0;
    g_published = 0;
    g_interned.clear();
    g_arenaUsed = 0;
    g_arenaCapacity = kArenaBytes;
}

inline void Expect(bool ok, const char *what, const char *detail = "")
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s %s\n", what, detail);
        ++g_failures;
    }
}

// A production ERR_DROP longjmps and never returns; the seam throws this instead.
struct Drop
{
    char message[256];
};

constexpr std::uint32_t kInline = disk32::kInline;

constexpr std::uint32_t VirtualOffset(std::uint32_t offset)
{
    return ((4u << 28) | offset) + 1;
}

// Appends little-endian words and C strings to g_file. A test derives its File
// from this and adds the family's Record.
template <typename Self>
struct FileBuilder
{
    Self &Word(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            g_file.push_back(static_cast<std::uint8_t>(value >> shift));
        return static_cast<Self &>(*this);
    }
    Self &Float(float value)
    {
        return Word(std::bit_cast<std::uint32_t>(value));
    }
    Self &Text(std::string_view text)
    {
        g_file.insert(g_file.end(), text.begin(), text.end());
        g_file.push_back(0);
        return static_cast<Self &>(*this);
    }
};

// A zone with a temp block (0) of up to 64 bytes and the virtual block (4),
// positioned as DB_LoadXFile leaves it for the asset array. Each zone starts
// a new image.
template <std::uint32_t VirtBytes>
struct Zone
{
    alignas(16) std::uint8_t temp[64]{};
    alignas(16) std::uint8_t virt[VirtBytes]{};
    XZoneMemory memory{};

    explicit Zone(std::uint32_t tempBytes = sizeof(temp))
    {
        ResetImage();
        memory.blocks[0] = {temp, tempBytes};
        memory.blocks[4] = {virt, VirtBytes};
        DB_InitStreams(&memory);
        DB_PushStreamPos(4); // DB_LoadXFile walks the asset array in block 4
    }
    // Whether text is a terminated string inside block 4. It reads nothing
    // outside the block, so a mislanded pointer fails here instead of crashing.
    bool Holds(const char *text) const
    {
        const auto at = reinterpret_cast<std::uintptr_t>(text);
        const auto begin = reinterpret_cast<std::uintptr_t>(virt);
        if (at < begin || at >= begin + VirtBytes)
            return false;
        return std::memchr(text, 0, begin + VirtBytes - at) != nullptr;
    }
    const char *At(std::size_t offset) const { return reinterpret_cast<const char *>(virt + offset); }
};

// Load_XAssetHeader's 64-bit call: the header slot holds the disk32 token.
template <typename T>
T *LoadHeader(void (*load)(bool, T **), std::uintptr_t slotValue)
{
    T *slot = nullptr;
    std::memcpy(&slot, &slotValue, sizeof(slot));
    load(false, &slot);
    return slot;
}

// Runs step and returns the ERR_DROP it raised, or "(none)".
template <typename Step>
Drop Catch(Step step)
{
    Drop drop{"(none)"};
    try
    {
        step();
    }
    catch (const Drop &caught)
    {
        drop = caught;
    }
    return drop;
}

// A malformed image must raise ERR_DROP naming error, publish nothing and
// read nothing past the image.
template <typename Step>
void ExpectDrop(const char *what, const char *error, Step step)
{
    const Drop drop = Catch(step);
    Expect(std::strstr(drop.message, error) && g_published == 0 && g_read <= g_file.size(), what, drop.message);
}

// Runs every test and returns the exit code; a well-formed image that raises
// ERR_DROP fails.
inline int Run(std::initializer_list<void (*)()> tests)
{
    for (void (*test)() : tests)
    {
        try
        {
            test();
        }
        catch (const Drop &drop)
        {
            Expect(false, "a well-formed image raised ERR_DROP:", drop.message);
        }
    }
    return g_failures ? 1 : 0;
}
} // namespace disk32_test
