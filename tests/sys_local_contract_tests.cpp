// sys_local portable-surface contracts (the win_local.h split).
//
// P0: qcommon/sys_local.h must stay the ONE portable spelling of the engine's
// system-layer surface (events, console, paths, input entry points, voice).
// The split out of win32/win_local.h exists so shared and headless translation
// units stop failing inside the Win32 header at 64-bit
// (docs/design/PLATFORM_POSIX.md); a regression that reintroduces Win32-only
// declarations or loses the portable ones breaks lin64/a64 headless closure.
//
// Two kinds of contract are pinned here.
//
// 1. DECLARATIONS. Every moved entry point must exist in the portable header
//    with its production signature. The checks take function pointers in
//    unevaluated contexts, so they verify the declarations without requiring
//    the Win32-only implementations to link on a POSIX host.
//
// 2. EXECUTION. The tests link src/universal/win_common.cpp -- the production
//    implementation of the portable Sys_* path entry points -- and call through
//    the declarations for real. The link itself proves the split kept the
//    declarations and definitions in agreement (a mistyped moved declaration
//    fails at link time), and the runtime checks exercise the engine code.
//    sysEvent_t and SysInfo are instantiated and round-tripped: they are the
//    structs every consumer of the header depends on.
//
// What this does NOT prove: the Win32-only side of win_local.h still compiles
// (the census's win32 target is the Windows control), or the 64-bit sizes of
// sysEvent_t/SysInfo (RUNTIME_SIZE pins are a separate queue item).

#include <qcommon/sys_local.h>
#include <qcommon/sys_filesystem.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace
{
int fail(const char *what)
{
    std::fprintf(stderr, "sys_local contract test failed: %s\n", what);
    return 1;
}

// --- 1. Declarations (unevaluated: no link dependency) ----------------------
// KISAK_CDECL is the portable spelling of the original __cdecl; on MSVC x86
// the calling convention is part of the function type, so the expected
// spellings use the same shim the declarations compile with.

static_assert(std::is_same_v<decltype(&IN_MouseEvent), void (KISAK_CDECL *)(int)>);
static_assert(std::is_same_v<decltype(&IN_Frame), void (KISAK_CDECL *)()>);
static_assert(std::is_same_v<decltype(&IN_IsTalkKeyHeld), bool (KISAK_CDECL *)()>);
static_assert(std::is_same_v<decltype(&Sys_ShowConsole), void (KISAK_CDECL *)()>);
static_assert(std::is_same_v<decltype(&Sys_IsLANAddress), bool (KISAK_CDECL *)(netadr_t)>);
static_assert(std::is_same_v<decltype(&Sys_IsLANAddress_IgnoreSubnet), bool (KISAK_CDECL *)(netadr_t)>);
static_assert(std::is_same_v<decltype(&Sys_GetPacket), qboolean (KISAK_CDECL *)(netadr_t *, msg_t *)>);
static_assert(std::is_same_v<decltype(&Sys_GetBroadcastPacket), qboolean (KISAK_CDECL *)(msg_t *)>);
static_assert(std::is_same_v<decltype(&Conbuf_AppendText), void (KISAK_CDECL *)(const char *)>);
static_assert(std::is_same_v<decltype(&Conbuf_AppendTextInMainThread), void (KISAK_CDECL *)(const char *)>);
static_assert(std::is_same_v<decltype(&Sys_QueEvent), void (KISAK_CDECL *)(std::uint32_t, sysEventType_t, int, int, int, void *)>);
static_assert(std::is_same_v<decltype(&Sys_GetEvent), sysEvent_t *(KISAK_CDECL *)(sysEvent_t *)>);
static_assert(std::is_same_v<decltype(&Sys_OutOfMemErrorInternal), void (KISAK_CDECL *)(const char *, int)>);
static_assert(std::is_same_v<decltype(&Sys_Print), void (KISAK_CDECL *)(const char *)>);
static_assert(std::is_same_v<decltype(&Sys_Mkdir), void (KISAK_CDECL *)(const char *)>);
static_assert(std::is_same_v<decltype(&Sys_RemoveDirTree), bool (KISAK_CDECL *)(const char *)>);
static_assert(std::is_same_v<decltype(&Sys_CountFileList), int (KISAK_CDECL *)(char **)>);
static_assert(std::is_same_v<decltype(&Sys_ListFiles), char **(KISAK_CDECL *)(const char *, const char *, const char *, int *, int)>);
static_assert(std::is_same_v<decltype(&Sys_Cwd), char *(KISAK_CDECL *)()>);
static_assert(std::is_same_v<decltype(&Sys_DefaultCDPath), const char *(KISAK_CDECL *)()>);
static_assert(std::is_same_v<decltype(&Sys_DefaultInstallPath), char *(KISAK_CDECL *)()>);
static_assert(std::is_same_v<decltype(&Voice_Init), bool (KISAK_CDECL *)()>);
static_assert(std::is_same_v<decltype(&Voice_Shutdown), void (KISAK_CDECL *)()>);
static_assert(std::is_same_v<decltype(&Voice_IsClientTalking), bool (KISAK_CDECL *)(std::uint32_t)>);

// --- 2. Struct contracts ----------------------------------------------------

bool CheckSysEventRoundTrip()
{
    int payload[4] = {1, 2, 3, 4};
    sysEvent_t event{};
    event.evTime = 123;
    event.evType = SE_KEY;
    event.evValue = 456;
    event.evValue2 = 789;
    event.evPtrLength = static_cast<int>(sizeof(payload));
    event.evPtr = payload;

    if (event.evTime != 123 || event.evType != SE_KEY || event.evValue != 456
        || event.evValue2 != 789
        || event.evPtrLength != static_cast<int>(sizeof(payload))
        || event.evPtr != payload)
    {
        return false;
    }
    // The pointer member is what the event queue hands to consumers; it must
    // survive the round trip as a real address, not a truncated one.
    return *static_cast<int *>(event.evPtr) == 1;
}

bool CheckSysInfoRoundTrip()
{
    // __declspec(align(8)) is MSVC-only; every other target takes the members'
    // natural (>= 8) alignment because C++ forbids requesting less than the
    // long double members demand on LP64. The portable contract is "at least
    // the 8 bytes the ILP32 layout pinned".
    if (alignof(SysInfo) < 8)
        return false;

    SysInfo info{};
    info.cpuGHz = 2.5L;
    info.configureGHz = 3.0L;
    info.logicalCpuCount = 8;
    info.physicalCpuCount = 4;
    info.sysMB = 16384;
    info.SSE = true;
    info.gpuDescription[0] = 'g';
    info.cpuVendor[0] = 'v';
    info.cpuName[0] = 'n';

    return info.cpuGHz == 2.5L && info.configureGHz == 3.0L
        && info.logicalCpuCount == 8 && info.physicalCpuCount == 4
        && info.sysMB == 16384 && info.SSE
        && info.gpuDescription[0] == 'g' && info.cpuVendor[0] == 'v'
        && info.cpuName[0] == 'n';
}

bool CheckEventQueueConstants()
{
    // Sys_QueEvent's ring buffer (win32/win_main.cpp) indexes with
    // eventHead & MASK_QUED_EVENTS; the mask contract is what makes that
    // wraparound correct.
    if (MAX_QUED_EVENTS <= 0)
        return false;
    if (MASK_QUED_EVENTS != MAX_QUED_EVENTS - 1)
        return false;
    for (int i = 0; i < MAX_QUED_EVENTS * 2; ++i)
    {
        if (((i & MASK_QUED_EVENTS) & ~MASK_QUED_EVENTS) != 0)
            return false;
    }
    return true;
}

// --- 3. Engine execution through the moved declarations ---------------------
// These call src/universal/win_common.cpp, the production implementation the
// declarations describe. A declaration that drifted from the definition fails
// the link of this target.

bool CheckEnginePathSurface()
{
    // Sys_DefaultCDPath is the engine's empty optical-drive path.
    const char *const cdPath = Sys_DefaultCDPath();
    if (!cdPath || cdPath[0] != '\0')
        return false;

    const char *const installPath = Sys_DefaultInstallPath();
    if (!installPath)
        return false;

    char *const cwd = Sys_Cwd();
    if (!cwd || cwd[0] == '\0')
        return false;

    char first[] = "first";
    char second[] = "second";
    char *listWithTwo[3] = {first, second, nullptr};
    char *emptyList[1] = {nullptr};
    if (Sys_CountFileList(listWithTwo) != 2)
        return false;
    if (Sys_CountFileList(emptyList) != 0)
        return false;
    if (Sys_CountFileList(nullptr) != 0)
        return false;

    return true;
}

bool CheckEngineMkdirSurface()
{
    char *const cwd = Sys_Cwd();
    if (!cwd || cwd[0] == '\0')
        return false;

    char fullPath[1024];
    std::snprintf(
        fullPath,
        sizeof(fullPath),
        "%s/kisakcod-sys-local-test-mkdir",
        cwd);

    // Clean slate, then the declaration and definition must agree or this
    // call never links. The removal has to actually leave fullPath absent: a
    // stale pre-existing directory would otherwise make a no-op Sys_Mkdir look
    // like a successful create. So the listing must report Error (nothing
    // there) before the create and Complete after it, and cleanup runs on the
    // failure paths as well so a failing check does not leak the directory.
    (void)Sys_FileSystemRemoveTree(fullPath);

    std::vector<SysFileSystemDirectoryEntry> entries;
    if (Sys_FileSystemListDirectory(fullPath, 16, &entries)
        != SysFileSystemListStatus::Error)
    {
        (void)Sys_FileSystemRemoveTree(fullPath);
        return false;
    }

    Sys_Mkdir(fullPath);

    if (Sys_FileSystemListDirectory(fullPath, 16, &entries)
        != SysFileSystemListStatus::Complete)
    {
        (void)Sys_FileSystemRemoveTree(fullPath);
        return false;
    }

    // Best-effort cleanup: the platform service owns real recursive removal.
    (void)Sys_FileSystemRemoveTree(fullPath);
    return true;
}
} // namespace

int main()
{
    if (!CheckSysEventRoundTrip())
        return fail("sysEvent_t field round trip");
    if (!CheckSysInfoRoundTrip())
        return fail("SysInfo field round trip / alignment");
    if (!CheckEventQueueConstants())
        return fail("event queue mask constants");
    if (!CheckEnginePathSurface())
        return fail("engine path entry points (win_common.cpp)");
    if (!CheckEngineMkdirSurface())
        return fail("engine Sys_Mkdir entry point (win_common.cpp)");
    return 0;
}
