// disk32_fixture.cpp: the engine seams the 64-bit loader tests replace
// (disk32_fixture.hpp): the error handler, the inflater, script-string
// interning and the zone's native storage.

#include "disk32_fixture.hpp"

#include <database/db_load_legacy_bridge.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>

using namespace disk32_test;

void __cdecl Com_Error(errorParm_t code, const char *fmt, ...)
{
    Drop drop{};
    va_list args;
    va_start(args, fmt);
    // Flawfinder: ignore -- the engine's literal formats into a bounded, terminated buffer.
    std::vsnprintf(drop.message, sizeof(drop.message), fmt, args);
    va_end(args);
    if (code != ERR_DROP)
        std::snprintf(drop.message, sizeof(drop.message), "unexpected error code %d", code);
    throw drop;
}

void __cdecl DB_LoadXFileData(std::uint8_t *pos, std::uint32_t size)
{
    if (!pos || !size || size > g_file.size() - g_read)
        Com_Error(ERR_DROP, "Fast-file ended unexpectedly");
    std::copy_n(g_file.data() + g_read, size, pos);
    g_read += size;
    const db::relocation::Status status = DB_MarkStreamRangeMaterialized(pos, size);
    const bool outside = status == db::relocation::Status::InvalidContext
        || status == db::relocation::Status::OutOfRange;
    if (status != db::relocation::Status::Ok && !(outside && g_allowReadsOutsideBlocks))
        Com_Error(ERR_DROP, "Cannot record fast-file output range");
}

db::load_legacy_bridge::LegacyBridgeStatus
db::load_legacy_bridge::DbLoadLegacyBridge::TryInternUser4StringOfSize(
    const char *bytes, std::uint32_t byteCount, LegacyBridgeStringId *outString) noexcept
{
    const std::string_view bytesView(bytes, byteCount); // with its terminator
    const std::string text(bytesView.substr(0, bytesView.find('\0')));
    auto found = std::find(g_interned.begin(), g_interned.end(), text);
    if (found == g_interned.end())
        found = g_interned.insert(found, text);
    outString->stringId = static_cast<std::uint32_t>(found - g_interned.begin()) + 1;
    return LegacyBridgeStatus::Success;
}

// Pointer-aligned storage from g_arena, filled with junk as PMem does not
// zero; null past g_arenaCapacity.
std::uint8_t *__cdecl DB_AllocZoneNative(std::size_t size, std::size_t alignment)
{
    const std::size_t start = (g_arenaUsed + alignment - 1) & ~(alignment - 1);
    if (!size || alignment != alignof(void *) || start > g_arenaCapacity || size > g_arenaCapacity - start)
        return nullptr;
    g_arenaUsed = start + size;
    std::fill_n(g_arena + start, size, std::uint8_t{0xCD});
    return g_arena + start;
}
