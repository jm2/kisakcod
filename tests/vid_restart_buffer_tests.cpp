// vid_restart_buffer_tests.cpp: CL_Vid_Restart_f's archive buffers across an
// ERR_DROP (#277). The production CL_Vid_Restart_f (client_mp/cl_main_mp.cpp);
// CL_ArchiveClientState longjmps out the way Com_Error does, and the next
// restart must free what the interrupted one allocated.
//
// The rest of the engine boundary is weak, and --gc-sections drops the code
// no check reaches.
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <client_mp/client_mp.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <universal/com_memory.h>
#include <universal/memfile.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

int g_outstanding = 0;
bool g_failArchive = false;
std::jmp_buf g_error;
dvar_s g_off{};
} // namespace

WEAK char *__cdecl Z_VirtualAlloc(int size, const char *, int)
{
    ++g_outstanding;
    return static_cast<char *>(std::calloc(1, size));
}
WEAK void __cdecl Z_VirtualFree(void *ptr)
{
    if (ptr)
        --g_outstanding;
    std::free(ptr);
}
WEAK void __cdecl CL_ArchiveClientState(int, MemoryFile *)
{
    if (g_failArchive)
        std::longjmp(g_error, 1); // what Com_Error(ERR_DROP) does
}

// Globals the restart path reads.
const dvar_t *com_sv_running;
const dvar_t *com_dedicated;
const dvar_t *cl_paused = &g_off;
const dvar_t *useFastFile = &g_off;
int com_expectedHunkUsage;
float g_console_char_height;
int32_t g_console_field_width;
ScreenPlacement scrPlaceFull;
ScreenPlacement scrPlaceFullUnsafe;
ScreenPlacement scrPlaceView[1];
field_t g_consoleField;

// The rest of the client, renderer and file system: no-ops.
WEAK void CL_CreateDevGui() {}
WEAK void CL_DestroyDevGui() {}
WEAK const char *CL_GetConfigString(int, unsigned int) { return ""; }
WEAK clientConnection_t *CL_GetLocalClientConnection(int)
{
    static clientConnection_t clc;
    return &clc;
}
WEAK void CL_InitCGame(int) {}
WEAK void CL_InitUI() {}
WEAK void CL_ShutdownCGame(int) {}
WEAK int32_t CL_ShutdownUI() { return 0; }
WEAK void Cmd_AddCommandInternal(const char *, void (*)(), cmd_function_s *) {}
WEAK int Cmd_Argc() { return 0; }
WEAK const char *Cmd_Argv(int) { return ""; }
WEAK void Cmd_RemoveCommand(const char *) {}
WEAK void CM_Shutdown() {}
WEAK void Com_Error(errorParm_t, const char *, ...) { std::longjmp(g_error, 1); }
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_Restart() {}
WEAK void Com_ShutdownWorld() {}
WEAK int Com_sprintf(char *dest, unsigned int size, const char *, ...)
{
    if (size)
        dest[0] = 0;
    return 0;
}
WEAK void Com_SyncThreads() {}
WEAK void Con_InitClientAssets() {}
WEAK void DB_LoadXAssets(XZoneInfo *, unsigned int, int) {}
WEAK int DB_ModFileExists() { return 0; }
WEAK void DB_ResetZoneSize(int) {}
WEAK void DB_SyncXAssets() {}
WEAK void DB_UpdateDebugZone() {}
WEAK void DevGui_AddCommand(const char *, char *) {}
WEAK void DevGui_AddDvar(const char *, const dvar_s *) {}
WEAK void DevGui_Init() {}
WEAK void DevGui_OpenMenu(const char *) {}
WEAK void DevGui_Shutdown() {}
WEAK const dvar_s *Dvar_FindVar(const char *) { return nullptr; }
WEAK const dvar_s *Dvar_RegisterBool(const char *, bool, unsigned short, const char *) { return &g_off; }
WEAK const dvar_s *Dvar_RegisterInt(const char *, int, DvarLimits, unsigned short, const char *) { return &g_off; }
WEAK void Dvar_SetInt(dvar_s *, int) {}
WEAK int FS_ConditionalRestart(int, int) { return 0; }
WEAK const char *FS_ReferencedIwdPureChecksums() { return ""; }
WEAK const char *Info_ValueForKey(const char *, const char *) { return ""; }
WEAK void I_strncat(char *, int, const char *) {}
WEAK void I_strncpyz(char *dest, const char *, int size)
{
    if (size)
        dest[0] = 0;
}
WEAK void LiveStorage_ReadStats() {}
WEAK Material *Material_RegisterHandle(const char *, int) { return nullptr; }
WEAK void MemFile_InitForReading(MemoryFile *file, int size, unsigned char *buffer, bool)
{
    file->buffer = buffer;
    file->bufferSize = size;
}
WEAK void MemFile_InitForWriting(MemoryFile *file, int size, unsigned char *buffer, bool, bool)
{
    file->buffer = buffer;
    file->bufferSize = size;
    file->bytesUsed = 16;
}
WEAK void MemFile_MoveToSegment(MemoryFile *, int) {}
WEAK void MemFile_StartSegment(MemoryFile *, int) {}
WEAK void MSG_WriteReliableCommandToBuffer(const char *, char *, int) {}
WEAK void MyAssertHandler(const char *, int, int, const char *, ...) {}
WEAK void Phys_Shutdown() {}
WEAK void R_BeginRegistration(vidConfig_t *) {}
WEAK void R_ConfigureRenderer(const GfxConfiguration *) {}
WEAK Font_s *R_RegisterFont(const char *, int) { return nullptr; }
WEAK void R_Shutdown(int) {}
WEAK void R_SyncRenderThread() {}
WEAK void ScrPlace_SetupUnsafeViewport(ScreenPlacement *, int, int, int, int) {}
WEAK void ScrPlace_SetupViewport(ScreenPlacement *, int, int, int, int) {}
WEAK void SEH_UpdateLanguageInfo() {}
WEAK void SND_StopSounds(snd_stopsounds_arg_t) {}
WEAK void StatMon_Reset() {}
WEAK void track_shutdown(int) {}
WEAK char *va(const char *, ...)
{
    static char buffer[8];
    buffer[0] = 0;
    return buffer;
}

int main()
{
    com_sv_running = &g_off;
    com_dedicated = &g_off;
    clientUIActives[0].cgameInitialized = 1;
    clientUIActives[0].connectionState = CA_DISCONNECTED;

    // An archive that fails leaves its staging buffer allocated...
    g_failArchive = true;
    if (!setjmp(g_error))
        CL_Vid_Restart_f();
    CHECK(g_outstanding == 1);

    // ...and the next restart frees it, then frees its own.
    g_failArchive = false;
    CL_Vid_Restart_f();
    CHECK(g_outstanding == 0);

    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("vid_restart buffers: ok");
    return 0;
}
