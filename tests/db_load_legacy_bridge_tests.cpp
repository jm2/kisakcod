#include <database/db_load_legacy_bridge.h>
#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_facade.h>
#include <qcommon/com_error.h>
#include <qcommon/sys_memory.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>
#include <script/scr_string_transaction.h>
#include <script/scr_stringlist.cpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

FastCriticalSection db_hashCritSect{};

namespace
{
using db::load_legacy_bridge::DbLoadLegacyBridge;
using db::load_legacy_bridge::LegacyBridgeStatus;
using db::load_legacy_bridge::LegacyBridgeStringId;
using db::registry_ownership::RegistryOwnershipStatus;

constexpr std::uint32_t kDatabaseUserMask = 4;
constexpr std::uint32_t kShutdownUserMask = 8;

std::array<std::recursive_mutex, CRITSECT_COUNT> g_criticalSections{};
thread_local std::array<std::uint32_t, CRITSECT_COUNT>
    g_criticalSectionDepth{};

int g_failures = 0;

[[nodiscard]] bool Check(
    const bool condition,
    const char *const message) noexcept
{
    if (!condition)
    {
        std::fprintf(
            stderr,
            "db_load_legacy_bridge test failed: %s\n",
            message);
        ++g_failures;
    }
    return condition;
}

[[nodiscard]] bool TestHappyPathRoundTrip() noexcept
{
    SL_Init();
    bool ok = true;
    constexpr char firstName[] = "legacy-bridge-round-trip";
    LegacyBridgeStringId interned{};
    ok = Check(
        DbLoadLegacyBridge::TryInternUser4String(firstName, &interned)
            == LegacyBridgeStatus::Success,
        "bridge did not admit the seed user-4 name")
        && ok;
    ok = Check(
        interned.stringId != 0,
        "bridge published a zero stringId for the seeded user-4 name")
        && ok;
    ok = Check(
        interned.canonicalName != nullptr,
        "bridge published a null canonical name for the seeded user-4 name")
        && ok;
    if (!ok)
    {
        SL_Shutdown();
        return false;
    }

    constexpr char repeatedName[] = "legacy-bridge-round-trip";
    LegacyBridgeStringId repeated{};
    ok = Check(
        DbLoadLegacyBridge::TryInternUser4String(repeatedName, &repeated)
            == LegacyBridgeStatus::Success,
        "bridge did not idempotently intern the same user-4 name")
        && ok;
    ok = Check(
        repeated.stringId == interned.stringId,
        "bridge did not return the same stringId for an idempotent intern")
        && ok;
    if (!ok)
    {
        SL_Shutdown();
        return false;
    }

    ok = Check(
        DbLoadLegacyBridge::TryAddUser4(interned.stringId)
            == LegacyBridgeStatus::Success,
        "bridge did not admit the seeded user-4 reference")
        && ok;
    if (!ok)
    {
        SL_Shutdown();
        return false;
    }

    ok = Check(
        DbLoadLegacyBridge::TryAddUser4(0u)
            == LegacyBridgeStatus::InvalidArgument,
        "bridge accepted a zero stringId into user-4")
        && ok;
    ok = Check(
        DbLoadLegacyBridge::TryAddUser4(0x10000u)
            == LegacyBridgeStatus::InvalidArgument,
        "bridge accepted an out-of-range stringId into user-4")
        && ok;

    ok = Check(
        DbLoadLegacyBridge::TryTransferUsers4To8()
            == LegacyBridgeStatus::Success,
        "bridge did not transfer the user-4 -> user-8 reference set")
        && ok;

    ok = Check(
        DbLoadLegacyBridge::TryShutdownUser8()
            == LegacyBridgeStatus::Success,
        "bridge did not shutdown the user-8 reference set")
        && ok;

    SL_ShutdownSystem(kDatabaseUserMask);
    SL_Shutdown();
    return ok;
}

[[nodiscard]] bool TestInternValidationGuards() noexcept
{
    SL_Init();
    bool ok = true;
    ok = Check(
        DbLoadLegacyBridge::TryInternUser4String(nullptr, nullptr)
            == LegacyBridgeStatus::InvalidArgument,
        "bridge admitted a null name through TryInternUser4String")
        && ok;

    LegacyBridgeStringId outString{};
    constexpr char unterminated[] = { 'a', 'b', 'c' };
    ok = Check(
        DbLoadLegacyBridge::TryInternUser4StringOfSize(
            unterminated,
            static_cast<std::uint32_t>(sizeof(unterminated)),
            &outString)
            == LegacyBridgeStatus::InvalidArgument,
        "bridge admitted a non-null-terminated byte payload")
        && ok;

    constexpr std::uint32_t kOversize = 65532u;
    std::array<char, kOversize> oversize{};
    std::memset(oversize.data(), 'a', kOversize - 1u);
    oversize[kOversize - 1u] = '\0';
    ok = Check(
        DbLoadLegacyBridge::TryInternUser4StringOfSize(
            oversize.data(),
            kOversize,
            &outString)
            == LegacyBridgeStatus::InvalidArgument,
        "bridge admitted a payload larger than the registry max bytes")
        && ok;

    SL_ShutdownSystem(kDatabaseUserMask);
    SL_Shutdown();
    return ok;
}

[[nodiscard]] bool TestBackToBackCyclesDoNotPoison() noexcept
{
    SL_Init();
    bool ok = true;
    for (int i = 0; i < 4 && ok; ++i)
    {
        char name[64];
        const int written = std::snprintf(
            name, sizeof(name), "legacy-bridge-cycle-%d", i);
        if (written <= 0
            || static_cast<std::size_t>(written) >= sizeof(name))
        {
            (void)Check(false, "test could not synthesize a name for back-to-back cycle");
            ok = false;
            break;
        }
        LegacyBridgeStringId interned{};
        ok = Check(
            DbLoadLegacyBridge::TryInternUser4String(name, &interned)
                == LegacyBridgeStatus::Success,
            "bridge did not admit a synthesized user-4 name across cycles")
            && ok;
        if (!ok)
            break;
        ok = Check(
            DbLoadLegacyBridge::TryAddUser4(interned.stringId)
                == LegacyBridgeStatus::Success,
            "bridge did not admit a synthesized user-4 reference across cycles")
            && ok;
        if (!ok)
            break;
        ok = Check(
            DbLoadLegacyBridge::TryTransferUsers4To8()
                == LegacyBridgeStatus::Success,
            "bridge did not transfer across cycles")
            && ok;
        if (!ok)
            break;
        ok = Check(
            DbLoadLegacyBridge::TryShutdownUser8()
                == LegacyBridgeStatus::Success,
            "bridge did not shutdown across cycles")
            && ok;
    }
    SL_ShutdownSystem(kDatabaseUserMask);
    SL_Shutdown();
    return ok;
}
} // namespace

void KISAK_CDECL Sys_EnterCriticalSection(const int criticalSection)
{
    if (criticalSection < 0 || criticalSection >= CRITSECT_COUNT)
        std::abort();
    const std::size_t index = static_cast<std::size_t>(criticalSection);
    g_criticalSections[index].lock();
    ++g_criticalSectionDepth[index];
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int criticalSection)
{
    if (criticalSection < 0 || criticalSection >= CRITSECT_COUNT)
        std::abort();
    const std::size_t index = static_cast<std::size_t>(criticalSection);
    if (g_criticalSectionDepth[index] == 0)
        std::abort();
    --g_criticalSectionDepth[index];
    g_criticalSections[index].unlock();
}

std::size_t KISAK_CDECL Sys_VirtualMemoryPageSize()
{
    return 64;
}

void *KISAK_CDECL Sys_VirtualMemoryReserve(const std::size_t size)
{
    static std::array<std::uint8_t, 0x08000000> g_pmemBacking{};
    return size == 0x08000000u
        ? static_cast<void *>(g_pmemBacking.data())
        : nullptr;
}

bool KISAK_CDECL Sys_VirtualMemoryCommit(
    void *const address, const std::size_t size)
{
    static std::array<std::uint8_t, 0x08000000> g_pmemBacking{};
    return address == g_pmemBacking.data()
        && size == 0x08000000u;
}

bool KISAK_CDECL Sys_VirtualMemoryDecommit(
    void *const, const std::size_t)
{
    return false;
}

bool KISAK_CDECL Sys_VirtualMemoryRelease(void *const address)
{
    static std::array<std::uint8_t, 0x08000000> g_pmemBacking{};
    return address == g_pmemBacking.data();
}

void KISAK_CDECL Sys_Sleep(const std::uint32_t)
{
}

void KISAK_CDECL Com_Memset(
    void *const destination, const int value, const std::size_t count)
{
    std::memset(destination, value, count);
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

void KISAK_CDECL Com_Error(errorParm_t, const char *, ...)
{
    std::abort();
}

void KISAK_CDECL Com_Printf(int, const char *, ...)
{
}

double KISAK_CDECL ConvertToMB(const int bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void KISAK_CDECL Sys_OutOfMemErrorInternal(const char *, int)
{
    std::abort();
}

char *KISAK_CDECL va(const char *, ...)
{
    static thread_local char empty[1]{};
    return empty;
}

int main()
{
    const bool ok = TestInternValidationGuards()
        && TestHappyPathRoundTrip()
        && TestBackToBackCyclesDoNotPoison();
    if (g_failures != 0)
        return 1;
    return ok ? 0 : 1;
}
