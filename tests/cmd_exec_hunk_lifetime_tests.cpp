// cmd_exec_hunk_lifetime_tests.cpp: an exec'd file must stay alive when one of
// its commands clears hunk temp memory.  FS_ReadFile normally returns hunk
// temp memory; a map command inside the file frees that memory while the exec
// buffer is still being read.  The real Cmd_ExecFromDisk, Cbuf_ExecuteBuffer,
// Hunk_AllocateTempMemory, Hunk_FreeTempMemory and Hunk_ClearTempMemory run
// here against a synthetic cfg and synthetic virtual-memory backend.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <strings.h>

#include <qcommon/cmd.h>
#include <qcommon/com_error.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_memory.h>
#include <universal/assertive.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>

namespace
{
int g_failures;
int g_reads;
int g_frees;
int g_clearRuns;
int g_markerRuns;
int g_nestedRuns;

cmd_function_s g_clearCommand;
cmd_function_s g_markerCommand;
cmd_function_s g_nestedCommand;

char g_vaText[1];
void *g_hunkReservation;
std::size_t g_hunkReservationSize;

struct SyntheticFile
{
    const char *path;
    const char *text;
};

const SyntheticFile g_files[] = {
    {"outer.cfg", "exec-hunk-clear\nexec-nested\nexec-marker\n"},
    {"nested.cfg", "exec-marker\n"},
};

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

const char *FindFile(const char *const path)
{
    for (const SyntheticFile &file : g_files)
    {
        if (std::strcmp(file.path, path) == 0)
            return file.text;
    }
    return nullptr;
}

void __cdecl ClearHunk_f()
{
    ++g_clearRuns;
    Hunk_ClearTempMemory();
}

void __cdecl Marker_f()
{
    ++g_markerRuns;
}

void __cdecl NestedExec_f()
{
    ++g_nestedRuns;
    Check(Cmd_ExecFromDisk(0, 0, "nested.cfg") == 1, "nested cfg exec returns success");
}
} // namespace

// Stand-ins for the engine's file system and virtual-memory platform layer.
// The cfg itself is allocated by the real hunk temp allocator, as FS_ReadFile
// does, and decommit zeroes the pages so the stale hunk header is observable.
int __cdecl FS_ReadFile(const char *const path, void **const buffer)
{
    const char *const text = FindFile(path);
    if (!text)
    {
        if (buffer)
            *buffer = nullptr;
        return -1;
    }

    const std::size_t length = std::char_traits<char>::length(text);
    char *const copy = reinterpret_cast<char *>(Hunk_AllocateTempMemory(static_cast<int32_t>(length + 1), "cmd exec test cfg"));
    std::copy_n(text, length + 1, copy);
    *buffer = copy;
    ++g_reads;
    return static_cast<int>(length);
}

void __cdecl FS_FreeFile(char *const buffer)
{
    ++g_frees;
    Hunk_FreeTempMemory(buffer);
}

std::size_t KISAK_CDECL Sys_VirtualMemoryPageSize()
{
    return 4096;
}

void *KISAK_CDECL Sys_VirtualMemoryReserve(const std::size_t size)
{
    const std::size_t pageSize = Sys_VirtualMemoryPageSize();
    g_hunkReservationSize = (size + pageSize - 1) & ~(pageSize - 1);
    g_hunkReservation = std::aligned_alloc(pageSize, g_hunkReservationSize);
    if (g_hunkReservation)
        std::memset(g_hunkReservation, 0, g_hunkReservationSize);
    return g_hunkReservation;
}

bool KISAK_CDECL Sys_VirtualMemoryCommit(void *, std::size_t)
{
    return true;
}

bool KISAK_CDECL Sys_VirtualMemoryDecommit(void *const address, const std::size_t size)
{
    std::memset(address, 0, size);
    return true;
}

bool KISAK_CDECL Sys_VirtualMemoryRelease(void *const address)
{
    if (address != g_hunkReservation)
        return false;
    std::free(address);
    g_hunkReservation = nullptr;
    return true;
}

void __cdecl Com_Printf(int, const char *, ...) {}
void __cdecl Com_PrintError(int, const char *, ...) {}

void __cdecl Com_Error(const errorParm_t code, const char *const fmt, ...)
{
    std::fprintf(stderr, "engine error %d: %s\n", static_cast<int>(code), fmt);
    std::_Exit(2);
}

void __cdecl MyAssertHandler(const char *const filename, const int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", filename ? filename : "?", line);
    std::_Exit(3);
}

void __cdecl Com_Memset(void *const dest, const int value, const std::size_t count)
{
    std::memset(dest, value, count);
}

void __cdecl Sys_OutOfMemErrorInternal(const char *, int) { std::_Exit(4); }
void __cdecl track_temp_alloc(int, int, int, const char *) {}
void __cdecl track_temp_free(int, int, const char *) {}
void __cdecl track_PrintAllInfo() {}
void __cdecl Sys_EnterCriticalSection(int) {}
void __cdecl Sys_LeaveCriticalSection(int) {}
bool __cdecl Sys_IsMainThread() { return true; }
bool __cdecl Sys_IsRenderThread() { return false; }
void __cdecl SV_WaitServer() {}
void __cdecl Scr_MonitorCommand(const char *) {}
int __cdecl Dvar_Command() { return 0; }
int __cdecl SV_GameCommand() { return 0; }
int __cdecl I_stricmp(const char *const lhs, const char *const rhs) { return strcasecmp(lhs, rhs); }

char *__cdecl va(const char *, ...)
{
    return g_vaText;
}

const dvar_t *com_sv_running;
int com_inServerFrame;

int main()
{
    s_hunkTotal = 64 * 1024;
    s_hunkData = static_cast<unsigned char *>(Sys_VirtualMemoryReserve(static_cast<std::size_t>(s_hunkTotal)));
    if (!s_hunkData)
    {
        std::fprintf(stderr, "FAIL reserve test hunk\n");
        return 1;
    }

    Cmd_ComErrorCleanup();
    Cbuf_Init();
    Cmd_AddCommandInternal("exec-hunk-clear", ClearHunk_f, &g_clearCommand);
    Cmd_AddCommandInternal("exec-marker", Marker_f, &g_markerCommand);
    Cmd_AddCommandInternal("exec-nested", NestedExec_f, &g_nestedCommand);

    Check(Cmd_ExecFromDisk(0, 0, "outer.cfg") == 1, "cfg exec returns success");
    Check(g_clearRuns == 1, "the command before the clear ran");
    Check(g_nestedRuns == 1, "nested exec after the clear ran");
    Check(g_markerRuns == 2, "commands after the clear still ran");
    Check(g_reads == 2 && g_frees == 2, "both cfg temp buffers were released");
    Check(Cmd_ExecFromDisk(0, 0, "missing.cfg") == 0, "a missing cfg returns failure");
    Check(g_markerRuns == 2, "a missing cfg executes nothing");
    Hunk_CheckTempMemoryClear();

    (void)Sys_VirtualMemoryRelease(s_hunkData);

    if (g_failures)
    {
        std::fprintf(stderr, "cmd exec hunk lifetime: FAILED\n");
        return 1;
    }
    std::printf("cmd exec hunk lifetime: ok\n");
    return 0;
}
