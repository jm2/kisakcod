// db_stream_intern_wait_tests.cpp: the database thread's stream interns
// (Load_TempStringCustom, DB_ConvertOffsetToTempString) against the real bridge,
// facade, coordinator and script-string registry while another thread holds
// db_hashCritSect or a registry session, as a main-thread lookup or unload does
// during an async zone load. The intern must wait for the holder and then
// succeed. On the code that reported that contention as a failure, it raised
// ERR_DROP and the zone load failed on timing alone. With the hold on the
// intern's own thread it must fail at once, not wait on itself; a watchdog turns
// such a spin into a failure. So it must when a window poisoned elsewhere keeps
// its locks for good. Production TUs at 64-bit with the headless defines, as in
// db_registry_unload_tests.cpp.

#include <database/database.h>
#include <database/db_load_legacy_bridge.h>
#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_table.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <script/scr_stringlist.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

FastCriticalSection db_hashCritSect; // db_registry.cpp's, not linked here

namespace
{
using db::load_legacy_bridge::DbLoadLegacyBridge;

int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// A production ERR_DROP longjmps out of the zone load; the stub throws this.
struct Drop
{
    char message[128];
};

std::atomic<int> g_waits{0}; // Sys_Sleep calls: the bridge's waits between attempts

// The inflated zone bytes DB_LoadXFileData hands out, and the zone's virtual
// block (4), where streamed strings land.
std::vector<uint8_t> g_file;
size_t g_read = 0;
alignas(16) uint8_t g_virtual[512];
XZoneMemory g_zoneMemory{};

// Load_TempStringCustom: streams name into block 4, leaving its string id.
uint32_t StreamIntern(std::string_view name)
{
    g_file.insert(g_file.end(), name.begin(), name.end());
    g_file.push_back(0);
    char *text = reinterpret_cast<char *>(DB_AllocStreamPos(0));
    Load_TempStringCustom(&text);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text));
}

// DB_ConvertOffsetToTempString: the token of the string streamed first, at
// offset 0 of block 4.
uint32_t OffsetIntern()
{
    uint32_t token = ((4u << 28) | 0u) + 1u;
    DB_ConvertOffsetToTempString(&token, db::relocation::BlockBit(4));
    return token;
}

enum class Hold { Write, Read, Session };

void Acquire(Hold hold)
{
    if (hold == Hold::Write)
        Sys_LockWrite(&db_hashCritSect);
    else if (hold == Hold::Read)
        Sys_LockRead(&db_hashCritSect);
    else
        DbLoadLegacyBridge::BeginSession();
}

void Release(Hold hold)
{
    if (hold == Hold::Write)
        Sys_UnlockWrite(&db_hashCritSect);
    else if (hold == Hold::Read)
        Sys_UnlockRead(&db_hashCritSect);
    else
        CHECK(DbLoadLegacyBridge::FinishSession() == db::load_legacy_bridge::LegacyBridgeStatus::Success);
}

template <typename Intern>
void RunWhileHeldElsewhere(const char *what, Hold hold, std::string_view expected, Intern intern)
{
    std::atomic<bool> held{false}, release{false}, finished{false};
    std::thread holder([&] {
        Acquire(hold);
        held = true;
        while (!release)
            std::this_thread::yield();
        Release(hold);
    });
    while (!held)
        std::this_thread::yield();
    g_waits = 0;

    uint32_t id = 0;
    Drop drop{"(none)"};
    std::thread database([&] {
        try
        {
            id = intern();
        }
        catch (const Drop &caught)
        {
            drop = caught;
        }
        finished = true;
    });
    // The intern has met the hold and retried at least once.
    while (g_waits < 2 && !finished)
        std::this_thread::yield();
    const bool waitedForHolder = !finished;
    release = true;
    holder.join();
    database.join();
    if (!waitedForHolder || std::strcmp(drop.message, "(none)") != 0)
        std::fprintf(stderr, "%s: the intern did not wait for the holder (ERR_DROP: %s)\n", what, drop.message);
    CHECK(waitedForHolder);
    CHECK(std::strcmp(drop.message, "(none)") == 0);
    CHECK(id != 0 && id == SL_FindString(std::string(expected).c_str()));
}

template <typename Intern>
void RunWhileHeldHere(const char *what, Hold hold, Intern intern)
{
    g_waits = 0;
    Acquire(hold);
    Drop drop{"(none)"};
    try
    {
        intern();
    }
    catch (const Drop &caught)
    {
        drop = caught;
    }
    Release(hold);
    if (std::strcmp(drop.message, "Database user-4 stream intern failed") != 0 || g_waits != 0)
        std::fprintf(stderr, "%s: ERR_DROP '%s' after %d waits\n", what, drop.message, g_waits.load());
    CHECK(std::strcmp(drop.message, "Database user-4 stream intern failed") == 0);
    CHECK(g_waits == 0);
}

// Run alone (the poison is process-wide). A standalone window poisons on a
// thread that survives its ERR_DROP, as the main thread does, and keeps the
// facade serializer for good. The database thread's next intern must fail at
// once, not wait for a holder that never lets go.
void TestPoisonedWindowIsTerminal()
{
    db::registry_ownership::SetRegistryOwnershipCoordinatorBoundaryForTesting(0, 0, 0, 0, 2, 2);
    Drop drop{"(none)"};
    try
    {
        StreamIntern("poisoned-here");
    }
    catch (const Drop &caught)
    {
        drop = caught;
    }
    CHECK(std::strcmp(drop.message, "Database user-4 stream intern failed") == 0);
    CHECK(Sys_HoldsFastCriticalSection()); // the poisoned window kept the serializer

    g_waits = 0;
    Drop databaseDrop{"(none)"};
    std::thread database([&] {
        try
        {
            StreamIntern("after-poison");
        }
        catch (const Drop &caught)
        {
            databaseDrop = caught;
        }
    });
    database.join();
    CHECK(std::strcmp(databaseDrop.message, "Database user-4 stream intern failed") == 0);
    CHECK(g_waits == 0);
}

std::array<std::recursive_mutex, CRITSECT_COUNT> g_criticalSections;
} // namespace

// Engine boundary (weak).
#define WEAK __attribute__((weak))

WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    Drop drop{};
    va_list args;
    va_start(args, fmt);
    // Flawfinder: ignore -- the engine's literal formats into a bounded, terminated buffer.
    std::vsnprintf(drop.message, sizeof(drop.message), fmt, args);
    va_end(args);
    throw drop;
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    std::_Exit(1);
}
WEAK void DB_LoadXFileData(uint8_t *pos, uint32_t size)
{
    if (!pos || !size || size > g_file.size() - g_read)
        Com_Error(ERR_DROP, "Fast-file ended unexpectedly");
    std::copy_n(g_file.data() + g_read, size, pos);
    g_read += size;
    if (DB_MarkStreamRangeMaterialized(pos, size) != db::relocation::Status::Ok)
        Com_Error(ERR_DROP, "Cannot record fast-file output range");
}
WEAK void Sys_EnterCriticalSection(int section) { g_criticalSections.at(static_cast<size_t>(section)).lock(); }
WEAK void Sys_LeaveCriticalSection(int section) { g_criticalSections.at(static_cast<size_t>(section)).unlock(); }
WEAK void Sys_Sleep(uint32_t)
{
    ++g_waits;
    std::this_thread::yield();
}
WEAK void Com_Memset(void *dest, const int val, const size_t count) { std::memset(dest, val, count); }

int main(int argc, char **argv)
{
    // A regression that spins (or deadlocks) fails here rather than hanging ctest.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::fprintf(stderr, "watchdog: a stream intern neither finished nor failed in 10 s\n");
        std::_Exit(1);
    }).detach();

    SL_Init();
    CHECK(db::zone_runtime::TryInitializeZoneRuntimeTable(&db::zone_runtime::ProductionZoneRuntimeTable())
          == db::zone_runtime::ZoneRuntimeTableStatus::Success);
    g_zoneMemory.blocks[4] = {g_virtual, sizeof(g_virtual)};
    DB_InitStreams(&g_zoneMemory);
    DB_PushStreamPos(4); // as DB_LoadXFile leaves it for the asset list
    CHECK(StreamIntern("first-name") == SL_FindString("first-name")); // nothing held

    if (argc > 1 && std::strcmp(argv[1], "poisoned-window") == 0)
        TestPoisonedWindowIsTerminal();
    else if (argc > 1 && std::strcmp(argv[1], "self-hold") == 0)
    {
        RunWhileHeldHere("write hold on this thread", Hold::Write, [] { StreamIntern("self-write"); });
        RunWhileHeldHere("read hold on this thread", Hold::Read, [] { OffsetIntern(); });
        CHECK(StreamIntern("after-self") == SL_FindString("after-self")); // nothing left held
    }
    else
    {
        RunWhileHeldElsewhere("write hold", Hold::Write, "held-write", [] { return StreamIntern("held-write"); });
        RunWhileHeldElsewhere("read hold", Hold::Read, "first-name", OffsetIntern);
        RunWhileHeldElsewhere("session", Hold::Session, "held-session", [] { return StreamIntern("held-session"); });
        RunWhileHeldElsewhere("session, offset", Hold::Session, "first-name", OffsetIntern);
    }
    return g_failures == 0 ? 0 : 1;
}
