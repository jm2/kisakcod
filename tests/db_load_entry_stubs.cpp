// db_load_entry_stubs: the engine-service no-op tier for the
// db_load_entry production enrollment (ki-458h / #125).
//
// The enrolled production units (db_registry.cpp, db_stream_load.cpp,
// db_relocation.cpp, the zone-runtime cohort and the fx-fastfile
// subject object libraries) reference the surrounding engine through
// narrow seams. A unit harness cannot host the whole engine, so this
// TU supplies the seams the admission and stream-relocation cases do
// not traverse, at their PRODUCTION signatures and GLOBAL scope:
//
//   - single-threaded database/render thread coordination (the
//     harness runs the registry on one thread; every ownership
//     handshake answers "ready / main thread / no suspension");
//   - the zone-lifecycle callbacks outside the tested entry points
//     (world/sound/dobj load-save, geometry buffer recovery);
//   - the render/material/image invalidation fan-out behind
//     DB_SyncLostDevice and asset removal;
//   - the game-side asset consumers the registry notifies;
//   - the harness-owned singletons the registry externs (cm,
//     comWorld, gameWorldSp) and the dvar pointers it reads.
//
// Every stub is a failure to REACH, not a behavior: the enrolled
// cases assert on the production registry, stream and resolver
// behavior, and a stub that records or terminates makes an
// unexpected traversal loud. The controlled provider behind
// DB_LoadXFileData and the printf/assert family live in
// db_load_entry_test.cpp instead.

#include <database/database.h>
#include <database/db_zone_memory.h>
#include <qcommon/threads.h>
#include <qcommon/cmd.h>
#include <gfx_d3d/r_rendercmds.h>
#include <universal/q_shared.h>

#include <game/g_bsp.h>
#include <qcommon/qcommon.h>
#include <qcommon/com_bsp.h>

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include <universal/msvc_printf_shim.h>

#include "db_load_entry_harness.hpp"

// ---------------------------------------------------------------------------
// Harness singletons the production registry externs. The real
// defining translation units (cm_load.cpp, com_bsp.cpp, g_bsp.cpp)
// are not part of this enrollment; the registry only needs the
// storage to exist.
// ---------------------------------------------------------------------------
clipMap_t cm;
ComWorld comWorld;
GameWorldSp gameWorldSp;
int com_missingAssetOpenFailed;

int fs_numServerReferencedFFs;
const char *fs_serverReferencedFFNames[32];

XAsset *varXAsset;

// The dvar pointers the registry and stream code read. Fast-file
// mode is always enabled (the harness dvar record lives in
// db_load_entry_harness.hpp); the rest are never dereferenced on the
// enrolled paths, so null is a loud failure if one ever is.
static dvar_t s_placeholderDvar;
const dvar_t *com_sv_running = &s_placeholderDvar;
const dvar_t *loc_warnings;
const dvar_t *loc_warningsAsErrors;
const dvar_s *fs_basepath;
const dvar_s *fs_gameDirVar;

// ---------------------------------------------------------------------------
// Single-threaded coordination. The numbered critical sections are
// the production lock pairs; with one thread they are no-ops, which
// keeps the production call order intact. Every ownership handshake
// answers "ready / main thread / no suspension pending" so the
// production wait loops fall straight through.
// ---------------------------------------------------------------------------
void Sys_EnterCriticalSection(int section) { (void)section; }
void Sys_LeaveCriticalSection(int section) { (void)section; }

void Sys_WakeDatabase()
{
    ++db_load_entry_harness::State().wakeCount;
}

void Sys_WakeDatabase2()
{
    ++db_load_entry_harness::State().wake2Count;
}

void Sys_NotifyDatabase()
{
    ++db_load_entry_harness::State().notifyCount;
}

void Sys_SyncDatabase()
{
    ++db_load_entry_harness::State().syncCount;
}

bool Sys_IsMainThread() { return true; }

bool Sys_IsDatabaseThread() { return false; }

bool Sys_IsDatabaseReady() { return true; }

bool Sys_IsDatabaseReady2() { return true; }

bool Sys_IsRenderThread() { return false; }

bool Sys_HaveSuspendedDatabaseThread(ThreadOwner owner)
{
    (void)owner;
    return false;
}

void Sys_SuspendDatabaseThread(ThreadOwner owner) { (void)owner; }

void Sys_ResumeDatabaseThread(ThreadOwner owner) { (void)owner; }

char Sys_SpawnDatabaseThread(SysThreadContextEntry function)
{
    (void)function;
    return 1;
}

unsigned int Sys_Milliseconds()
{
    return ++db_load_entry_harness::State().milliseconds;
}

void ProfLoad_Begin(const char *name) { (void)name; }

void ProfLoad_End() {}

void DB_MediaReleaseThreadOwnership() {}

bool DB_MediaIsInRemoteScreenUpdate() { return false; }

bool DB_IsRenderThreadRemoteScreenUpdate() { return false; }

void Sys_DatabaseCompleted() {}
void Sys_DatabaseCompleted2() {}
void Sys_WaitStartDatabase() {}

void *Sys_GetValue(int value)
{
    (void)value;
    return nullptr;
}

const char *Sys_DefaultInstallPath() { return "."; }

void Sys_Sleep(unsigned int milliseconds) { (void)milliseconds; }

void Sys_Error(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    buffer[sizeof(buffer) - 1] = '\0';
    va_end(args);
    std::printf("Sys_Error: %s\n", buffer);
    std::_Exit(4);
}

void Sys_VirtualMemoryCommit(void *address, unsigned int size)
{
    (void)address;
    (void)size;
}

void Sys_VirtualMemoryRelease(void *address) { (void)address; }

void *Sys_VirtualMemoryReserve(unsigned int size)
{
    (void)size;
    return nullptr;
}

void Com_SyncThreads() {}

// The production abort path after Com_Error: unreachable under the
// record-and-return protocol; reaching it is a hard failure.
void Com_ErrorAbort()
{
    std::printf("Com_ErrorAbort reached\n");
    std::_Exit(4);
}

// ---------------------------------------------------------------------------
// Dvar and command registration seams: the registry registers no
// commands on the enrolled paths; the string dvar registration
// returns the placeholder record.
// ---------------------------------------------------------------------------
const dvar_t *Dvar_RegisterString(const char *dvarName,
                                  const char *value,
                                  unsigned short flags,
                                  const char *description)
{
    (void)value;
    (void)flags;
    (void)description;
    s_placeholderDvar.name = dvarName;
    return &s_placeholderDvar;
}

void Cmd_AddCommandInternal(const char *name, void (*function)(),
                            cmd_function_s *allocdata)
{
    (void)name;
    (void)function;
    (void)allocdata;
}

const char *Cmd_Argv(int arg)
{
    (void)arg;
    return "";
}

// ---------------------------------------------------------------------------
// Zone lifecycle outside the tested entry points. DB_LoadXFile and
// its internal are the full-zone load path; the enrolled admission
// and relocation cases never start one, so they record-and-return.
// ---------------------------------------------------------------------------
void DB_LoadXFile(const char *path, void *f, const char *filename,
                  XZoneMemory *zoneMem, void (*interrupt)(),
                  uint8_t *buf, int32_t allocType)
{
    (void)path;
    (void)f;
    (void)filename;
    (void)zoneMem;
    (void)interrupt;
    (void)buf;
    (void)allocType;
}

void DB_LoadXFileInternal() {}

bool DB_IsMinimumFastFileLoaded() { return true; }

void DB_ResetZoneSize(int32_t trackLoadProgress)
{
    (void)trackLoadProgress;
}

void DB_LoadSounds() {}
void DB_SaveSounds() {}
void DB_LoadDObjs() {}
void DB_SaveDObjs() {}

void DB_RecoverGeometryBuffers(XZoneMemory *zoneMemory)
{
    (void)zoneMemory;
}

void DB_ReleaseGeometryBuffers(XZoneMemory *zoneMemory)
{
    (void)zoneMemory;
}

void Com_UnloadWorld() {}

void CM_Unload() {}

void Mark_XAsset() {}

void track_static_alloc_internal(void *address, int size,
                                 const char *name, int type)
{
    (void)address;
    (void)size;
    (void)name;
    (void)type;
}

// ---------------------------------------------------------------------------
// Render / material / image invalidation fan-out: the production
// registry notifies the renderer on device loss and asset removal;
// no renderer exists in the harness.
// ---------------------------------------------------------------------------
void R_BeginRemoteScreenUpdate() {}
void R_EndRemoteScreenUpdate() {}
bool R_IsInRemoteScreenUpdate() { return false; }
void R_PushRemoteScreenUpdate(int value) { (void)value; }
int R_PopRemoteScreenUpdate() { return 0; }
void R_ReleaseThreadOwnership() {}
void R_ShutdownStreams() {}
void R_SyncRenderThread() {}
void R_UnloadWorld() {}
void R_ClearAllStaticModelCacheRefs() {}

void RB_ClearPixelShader() {}
void RB_ClearVertexShader() {}
void RB_ClearVertexDecl() {}
void RB_UnbindAllImages() {}

void Material_ClearShaderUploadList() {}
void Material_DirtySort() {}
void Material_DirtyTechniqueSetOverrides() {}
void Material_OverrideTechniqueSets() {}
void Material_OriginalRemapTechniqueSet(MaterialTechniqueSet *techSet)
{
    (void)techSet;
}
void Material_ReleaseTechniqueSet(XAssetHeader header, void *data)
{
    (void)header;
    (void)data;
}
void Material_UploadShaders(MaterialTechniqueSet *techniqueSet)
{
    (void)techniqueSet;
}

bool Image_IsProg(GfxImage *image)
{
    (void)image;
    return false;
}

void Image_Free(GfxImage *image) { (void)image; }

// ---------------------------------------------------------------------------
// Game-side asset consumers and helpers the registry notifies. None
// are traversed by the enrolled cases.
// ---------------------------------------------------------------------------
void BG_FillInAllWeaponItems() {}
void CG_VisionSetMyChanges() {}

void CG_TraceCapsule(trace_t *results, const float *start,
                     const float *mins, const float *maxs,
                     const float *end, int skipNumber, int mask)
{
    (void)results;
    (void)start;
    (void)mins;
    (void)maxs;
    (void)end;
    (void)skipNumber;
    (void)mask;
}

void G_TraceCapsule(trace_t *results, const float *start,
                    const float *mins, const float *maxs,
                    const float *end, int skipNumber, int mask)
{
    (void)results;
    (void)start;
    (void)mins;
    (void)maxs;
    (void)end;
    (void)skipNumber;
    (void)mask;
}

// ---------------------------------------------------------------------------
// File-name and misc helpers: FS_HashFileName is the production hash
// arithmetic the registry's name chains reuse; a real small hash
// keeps chain behavior sane. The extension helper only runs on paths
// the enrolled cases do not traverse.
// ---------------------------------------------------------------------------
int FS_HashFileName(const char *fname, int size)
{
    if (!fname || size <= 0)
        return 0;
    unsigned int hash = 0;
    for (const char *letter = fname; *letter; ++letter)
    {
        char c = *letter;
        if (c == '\\')
            c = '/';
        hash += static_cast<unsigned char>(c) * 119;
        hash = (hash << 5) ^ hash;
    }
    return static_cast<int>(hash % static_cast<unsigned int>(size));
}

void Com_PrintWarning(int channel, const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    buffer[sizeof(buffer) - 1] = '\0';
    va_end(args);
    db_load_entry_harness::RecordedError record;
    record.text = buffer;
    db_load_entry_harness::State().printErrors.push_back(record);
    std::printf("printWarning(%d): %s\n", channel, buffer);
}

// ---------------------------------------------------------------------------
// Profiling seams (intentionally silent in the harness; the stage-2
// tier counts these, the admission tier does not).
// ---------------------------------------------------------------------------
void Profile_Guard(int guard) { (void)guard; }
void Profile_Recover(int guard) { (void)guard; }

// ---------------------------------------------------------------------------
// Network sleep seam: never traversed.
// ---------------------------------------------------------------------------
void NET_Sleep(int milliseconds) { (void)milliseconds; }

const char *Win_GetLanguage() { return "english"; }

// _copyDWord: the production dword-fill helper (common.cpp). The
// harness's registry paths do hit this through Com_Memset's aligned
// fast path, so give it the real loop semantics.
void _copyDWord(uint32_t *dest, const uint32_t constant, const uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        dest[i] = constant;
}


// ---------------------------------------------------------------------------
// Third-tier seams: hunk tracking bookkeeping (the real track_* TU,
// mem_track.cpp, hosts the whole hunk accounting subsystem which a
// unit harness cannot host), animation/model pool frees, platform
// virtual-memory queries and the reflection-probe dvar registration.
// ---------------------------------------------------------------------------
void track_PrintAllInfo() {}
void track_PrintInfo() {}

void track_hunk_ClearToMarkHigh(int mark) { (void)mark; }
void track_hunk_ClearToMarkLow(int mark) { (void)mark; }
void track_hunk_ClearToStart() {}

void track_hunk_alloc(int size, int pos, const char *name, int type)
{
    (void)size; (void)pos; (void)name; (void)type;
}

void track_hunk_allocLow(int size, int pos, const char *name, int type)
{
    (void)size; (void)pos; (void)name; (void)type;
}

void track_set_hunk_size(int size) { (void)size; }

void track_temp_alloc(int size, int hunkSize, int permanent,
                      const char *name)
{
    (void)size; (void)hunkSize; (void)permanent; (void)name;
}

void track_temp_free(int hunkSize, int permanent, const char *name)
{
    (void)hunkSize; (void)permanent; (void)name;
}

void track_temp_high_alloc(int size, int hunkSize, int permanent,
                           const char *name)
{
    (void)size; (void)hunkSize; (void)permanent; (void)name;
}

void track_temp_high_clear(int scope) { (void)scope; }

void track_z_commit(int a, int b)
{
    (void)a; (void)b;
}

int FS_LoadStack() { return 0; }

void Sys_VirtualMemoryDecommit(void *address, unsigned int size)
{
    (void)address; (void)size;
}

unsigned int Sys_VirtualMemoryPageSize() { return 4096u; }

void R_ReflectionProbeRegisterDvars() {}

void XAnimFree(XAnimParts *parts) { (void)parts; }

void XAnimFreeList(XAnim_s *anims) { (void)anims; }

void XModelPartsFree(XModelPartsLoad *load) { (void)load; }

bool r_reflectionProbeGenerate = false;


// ---------------------------------------------------------------------------
// Second-tier seams surfaced by the link: the math/register helpers
// and the sound/model registration lookups. The memory-tree tracker
// is real production code (scr_memorytree.cpp) and is enrolled
// directly in the build, not stubbed.
// ---------------------------------------------------------------------------
#include <universal/com_sndalias.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_model.h>
// The FX archive pre-flight rig enrolls the real production math
// (universal/com_math.cpp), which owns Q_fabs and Vec3Mad; only the
// DB-slice build (build_dbentry.sh) compiles these seams.
#ifndef KISAK_FX_RESTORE_RIG
float Q_fabs(float x) { return x < 0.0f ? -x : x; }

void AngleVectors(const float *angles, float *forward, float *right,
                  float *up)
{
    (void)angles; (void)forward; (void)right; (void)up;
}

void Vec3Mad(const float *vector1, float scalar, const float *vector2,
             float *result)
{
    (void)vector1; (void)scalar; (void)vector2; (void)result;
}
#endif // !KISAK_FX_RESTORE_RIG

snd_alias_list_t *Com_FindSoundAlias(const char *name)
{
    (void)name;
    return nullptr;
}

Material *Material_RegisterHandle(const char *name, int imageTrack)
{
    (void)name; (void)imageTrack;
    return nullptr;
}

XModel *R_RegisterModel(const char *name)
{
    (void)name;
    return nullptr;
}

bool Sys_IsServerThread() { return false; }

void Sys_SetValue(int valueIndex, void *value)
{
    (void)valueIndex; (void)value;
}

#ifdef KISAK_FX_RESTORE_RIG
// Production FX_Register (fx_load_obj.cpp) is enrolled in the FX
// restore rig: the FastFile path resolves the admitted asset through
// the real DB_FindXAssetHeader registration lookup.
#else
const FxEffectDef *FX_Register(const char *name)
{
    (void)name;
    return nullptr;
}
#endif

// Registry-ownership coordinator facade: the real coordinator TU is
// enrolled in the committed CMake target; this mirror build compiles
// it directly (db_registry_ownership_coordinator.cpp), so no stubs
// are needed here.

