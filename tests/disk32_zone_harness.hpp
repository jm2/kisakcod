#pragma once

// disk32_zone_harness.hpp: what db_disk32_zone_tests.cpp builds its zone with.
// The harness (disk32_zone_harness.cpp) writes the zone as a .ff, loads it
// through the real 64-bit path, checks every asset is published, runs the
// zone file's own checks, then unloads it.

#include <database/database.h>

#include <bit>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace zone_test
{
void Expect(bool ok, const char *what, const char *detail = "");

constexpr std::uint32_t kInline = 0xFFFFFFFFu;
constexpr std::uint32_t kShared = 0xFFFFFFFEu;
constexpr std::uint32_t Virt(std::uint32_t offset) { return ((4u << 28) | offset) + 1; }

// The inflated zone, in stream order. `virt` follows block 4's offset, so a
// later offset token can name what an earlier asset streamed there.
struct Image
{
    std::vector<std::uint8_t> bytes;
    std::uint32_t virt = 0;

    Image &Word(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        return *this;
    }
    Image &Float(float value) { return Word(std::bit_cast<std::uint32_t>(value)); }
    Image &Fill(std::size_t count, std::uint8_t value = 0)
    {
        bytes.insert(bytes.end(), count, value);
        return *this;
    }
    // A string streamed into block 4.
    Image &V(std::string_view text)
    {
        bytes.insert(bytes.end(), text.begin(), text.end());
        bytes.push_back(0);
        virt += static_cast<std::uint32_t>(text.size() + 1);
        return *this;
    }
    // Block 4 aligns, then takes n bytes the caller streams; returns their offset.
    std::uint32_t VAlloc(std::uint32_t n, std::uint32_t align = 4)
    {
        virt = (virt + align - 1) & ~(align - 1);
        const std::uint32_t at = virt;
        virt += n;
        return at;
    }
};

// One top-level asset: its type, header token (or the block-4 alias slot an
// offset token names), name, and the writer that streams it.
struct Asset
{
    XAssetType type;
    std::uint32_t token;
    const char *name;
    void (*write)(Image &);
    const std::uint32_t *alias = nullptr;

    std::uint32_t Token() const { return alias ? Virt(*alias) : token; }
};

// What db_disk32_zone_tests.cpp defines: the assets, the zone's script
// strings and the checks after the load.
std::span<const Asset> ZoneAssets();
std::span<const char *const> ZoneScriptStrings();
void CheckZone();

// The pool lookup the engine makes; null when the zone published no such asset.
XAssetHeader Find(XAssetType type, const char *name);

// The published asset `pointer` names.
inline bool Is(const void *pointer, XAssetType type, const char *name)
{
    return pointer && pointer == Find(type, name).data;
}
} // namespace zone_test
