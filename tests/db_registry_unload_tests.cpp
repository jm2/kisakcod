// db_registry_unload_tests.cpp: the db_registry unload sequence (quit, and the
// zone unload of a map change) against the real bridge, facade, coordinator
// and script-string registry.
//
// DB_ShutdownXAssets moved user-4 names through bridge calls while it held
// db_hashCritSect. Each call's registry window takes that hash itself, so it
// refused with Busy, and the ERR_DROP then raised under the hash never
// returned: Com_ErrorCleanup's localized-message lookup waits for the hash.
// Every orderly quit of a server past Com_Init hung there. On that code the
// quit checks fail: its Com_Error reaches the stub below with the hash held.
//
// The production TUs at 64-bit with the Linux headless server's defines, as in
// game_mp_hazard_tests.cpp: the engine boundary is weak stubs, and
// --gc-sections drops the code no check reaches.

#include <database/database.h>
#include <database/db_load_legacy_bridge.h>
#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_table.h>
#include <game/g_bsp.h>
#include <qcommon/com_bsp.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <script/scr_stringlist.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <strings.h>

extern FastCriticalSection db_hashCritSect;

namespace
{
using db::load_legacy_bridge::DbLoadLegacyBridge;
using db::load_legacy_bridge::LegacyBridgeStatus;

int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

int g_reports = 0;               // Com_PrintError calls
bool g_reportedUnderHash = false; // one of them while db_hashCritSect was held

// A name a zone load interned for its assets: user 4 only.
void InternZoneName(const char *name)
{
    db::load_legacy_bridge::LegacyBridgeStringId id{};
    CHECK(DbLoadLegacyBridge::TryInternUser4String(name, &id) == LegacyBridgeStatus::Success);
    CHECK(SL_FindString(name) == id.stringId);
}

// Quit after Com_Init. With every zone unloaded, the user-8 shutdown frees the
// zones' names, and the quit returns with the hash released.
void TestQuitFreesZoneNames()
{
    InternZoneName("unload-quit-name");
    DB_ShutdownXAssets();
    CHECK(!Sys_IsWriteLocked(&db_hashCritSect));
    CHECK(SL_FindString("unload-quit-name") == 0);
    CHECK(g_reports == 0);
}

// A registry call fails inside the session: a mark of an id no string has.
// Nothing is raised under the hash, the user-8 shutdown is skipped so the
// transferred name survives, and the failure comes back once the hash is free.
void TestFailureInSessionKeepsNames()
{
    InternZoneName("unload-kept-name");
    DbLoadLegacyBridge::BeginSession();
    CHECK(Sys_IsWriteLocked(&db_hashCritSect));
    uint16_t unallocated = 0xFFF0;
    Mark_ScriptStringCustom(&unallocated);
    DB_FreeUnusedResources();
    CHECK(DbLoadLegacyBridge::FinishSession() == LegacyBridgeStatus::OwnershipMismatch);
    CHECK(!Sys_IsWriteLocked(&db_hashCritSect));
    CHECK(SL_FindString("unload-kept-name") != 0);
}

// Two unloads with a poisoned registry boundary, so no registry window opens:
// each completes and reports the failure after releasing the hash.
void TestUnloadReportsAfterRelease()
{
    db::registry_ownership::SetRegistryOwnershipCoordinatorBoundaryForTesting(0, 0, 0, 0, 2, 2);
    DB_ShutdownXAssets();
    DB_ShutdownXAssets();
    CHECK(g_reports == 2);
    CHECK(!g_reportedUnderHash);
    CHECK(!Sys_IsWriteLocked(&db_hashCritSect));
}

std::array<std::recursive_mutex, CRITSECT_COUNT> g_criticalSections;
}  // namespace

// Engine boundary (weak).
#define WEAK __attribute__((weak))

WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error%s: %s\n",
                 Sys_IsWriteLocked(&db_hashCritSect) ? " with db_hashCritSect held (hangs in production)" : "", fmt);
    std::exit(1);
}
WEAK void Com_PrintError(int, const char *, ...)
{
    ++g_reports;
    g_reportedUnderHash = g_reportedUnderHash || Sys_IsWriteLocked(&db_hashCritSect);
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    std::exit(1);
}
WEAK void Sys_Error(const char *fmt, ...)
{
    std::fprintf(stderr, "Sys_Error: %s\n", fmt);
    std::exit(1);
}
WEAK void Sys_EnterCriticalSection(int section) { g_criticalSections.at(static_cast<size_t>(section)).lock(); }
WEAK void Sys_LeaveCriticalSection(int section) { g_criticalSections.at(static_cast<size_t>(section)).unlock(); }
WEAK void Sys_Sleep(uint32_t) {}
WEAK bool Sys_IsMainThread() { return true; }
WEAK bool Sys_IsDatabaseReady2() { return true; }
WEAK void Sys_DatabaseCompleted2() {}
WEAK void Sys_SyncDatabase() {}
WEAK void Com_Memset(void *dest, const int val, const size_t count) { std::memset(dest, val, count); }
WEAK int I_stricmp(const char *s0, const char *s1) { return strcasecmp(s0, s1); }
WEAK void Z_Free(void *, int) {}
WEAK void PMem_Free(const char *, uint32_t) {}
// Reached only through asset tables; no zone or asset exists in these checks.
WEAK clipMap_t cm;
WEAK ComWorld comWorld;
WEAK GameWorldMp gameWorldMp;
WEAK XAsset *varXAsset;
WEAK void CM_Unload() {}
WEAK void Com_UnloadWorld() {}
WEAK void BG_FillInAllWeaponItems() {}
WEAK void Mark_XAsset() {}
WEAK void DB_SaveDObjs() {}
WEAK void DB_LoadDObjs() {}
WEAK void DB_ReleaseGeometryBuffers(XZoneMemory *) {}
WEAK const char *DB_GetXAssetHeaderName(int32_t, const XAssetHeader *) { return ""; }
WEAK const char *DB_GetXAssetName(const XAsset *) { return ""; }
WEAK void DB_SetXAssetName(XAsset *, const char *) {}
WEAK int32_t DB_GetXAssetTypeSize(int32_t) { return 0; }
WEAK const char *DB_GetXAssetTypeName(uint32_t) { return ""; }

int main()
{
    SL_Init();
    // DB_Init's order: the zone runtime table exists before the first zone.
    CHECK(db::zone_runtime::TryInitializeZoneRuntimeTable(&db::zone_runtime::ProductionZoneRuntimeTable())
          == db::zone_runtime::ZoneRuntimeTableStatus::Success);
    TestQuitFreesZoneNames();
    TestFailureInSessionKeepsNames();
    TestUnloadReportsAfterRelease(); // last: the poison is process-wide
    return g_failures == 0 ? 0 : 1;
}
