// fx_draw_worker_error_tests.cpp: FX verts are generated on the renderer
// worker thread, where Com_Error longjmps to R_WorkerThread's jmp_buf and
// Com_ErrorAbort -> Sys_Error kills the process. FX_DrawTrail's fork-added
// checks must therefore record the first failure and return; the main thread
// converts it to ERR_DROP once after joining the worker. An oversized trail is
// not corruption: retail let R_ReserveCodeMeshVerts fail and skipped it.
//
// The test calls the production FX_DrawTrail directly: spawning the real
// R_WorkerThread would require Sys_* thread/error plumbing that this target
// does not link. The recording site, the pool/handle walk and the converter are
// all production code.

#include <EffectsCore/fx_system.h>

#include <gfx_d3d/r_drawsurf.h>
#include <gfx_d3d/r_scene.h>

#include <qcommon/com_error.h>
#include <qcommon/threads.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The converter only exists on the fixed tree. Declaring it here (identical to
// fx_system.h's declaration) lets this test compile against the base tree, and
// the weak reference keeps it linkable there so it fails at the
// Com_Error/abort instead of at link time.
void __cdecl FX_ConvertPendingDrawFatal();
#pragma weak FX_ConvertPendingDrawFatal

namespace
{
int g_failures;
int g_comErrorCalls;
char g_comErrorMessage[256];
bool g_mainThread;
int g_reserveVerts;
int g_reserveCalls;
bool g_drawSurfCalled;

FxSystemBuffers g_buffers;
FxSystem g_system;
FxEffect g_effect;
FxEffectDef g_effectDef;
FxElemDef g_elemDef;
FxTrailDef g_trailDef;
FxTrailVertex g_trailVerts[64];
std::uint16_t g_trailInds[6] = {0, 1, 2, 0, 2, 3};
int g_args[8][4];
GfxPackedVertex g_verts[64];

void Check(const bool ok, const char *const what)
{
    if (!ok) { ++g_failures; std::fprintf(stderr, "FAIL %s\n", what); }
}

void ResetCounters()
{
    g_comErrorCalls = 0; g_comErrorMessage[0] = '\0';
    g_reserveVerts = 0; g_reserveCalls = 0;
    g_drawSurfCalled = false;
}

std::uint16_t TrailElemHandle(const std::size_t index)
{
    return static_cast<std::uint16_t>(index * (sizeof(FxTrailElem) / FxTrailElem::HANDLE_SCALE));
}

void SetupOversizedTrail()
{
    g_system.effects = g_buffers.effects;
    g_system.elems = g_buffers.elems;
    g_system.trails = g_buffers.trails;
    g_system.trailElems = g_buffers.trailElems;
    g_elemDef.elemType = FX_ELEM_TYPE_TRAIL;
    g_elemDef.visualCount = 1;
    g_elemDef.trailDef = &g_trailDef;
    g_trailDef.repeatDist = 1;
    g_trailDef.vertCount = static_cast<std::int32_t>(sizeof(g_trailVerts) / sizeof(g_trailVerts[0]));
    g_trailDef.verts = g_trailVerts;
    g_trailDef.indCount = static_cast<std::int32_t>(sizeof(g_trailInds) / sizeof(g_trailInds[0]));
    g_trailDef.inds = g_trailInds;
    g_effectDef.elemDefCountLooping = 1;
    g_effectDef.elemDefs = &g_elemDef;
    g_effect.def = &g_effectDef;
    g_effect.firstTrailHandle = FX_INVALID_HANDLE;
    for (std::size_t index = 0; index < MAX_TRAIL_ELEMS; ++index)
    {
        FxTrailElem &elem = g_buffers.trailElems[index].item;
        elem.nextTrailElemHandle = index + 1 == MAX_TRAIL_ELEMS ? FX_INVALID_HANDLE : TrailElemHandle(index + 1);
        elem.msecBegin = 0;
    }
}

void RunOversized()
{
    SetupOversizedTrail();
    ResetCounters();
    FxDrawState draw{};
    draw.system = &g_system;
    draw.effect = &g_effect;
    FxTrail trail{};
    trail.firstElemHandle = TrailElemHandle(0);

    FX_DrawTrail(&g_system, &draw, &trail);

    Check(g_comErrorCalls == 0, "oversized trail does not raise");
    Check(g_reserveCalls == 1, "oversized trail asks the code mesh once");
    Check(
        g_reserveVerts == static_cast<int>(MAX_TRAIL_ELEMS) * g_trailDef.vertCount,
        "every trail element is counted before the reservation");
    Check(!g_drawSurfCalled, "a failed reservation emits no draw surface");
    g_mainThread = true;
    if (FX_ConvertPendingDrawFatal)
        FX_ConvertPendingDrawFatal();
    Check(g_comErrorCalls == 0, "oversized trail records no fatal");
}

void RunCorrupt()
{
    ResetCounters();
    g_mainThread = false;
    FX_DrawTrail(nullptr, nullptr, nullptr);
    Check(g_comErrorCalls == 0, "worker-path corruption does not call Com_Error");

    g_mainThread = true;
    if (!FX_ConvertPendingDrawFatal)
    {
        Check(false, "main-thread converter is linked");
        return;
    }
    FX_ConvertPendingDrawFatal();
    Check(g_comErrorCalls == 1, "the main thread converts the failure once");
    Check(
        std::strstr(g_comErrorMessage, "missing trail draw state") != nullptr,
        "the first recorded reason is kept");
    FX_ConvertPendingDrawFatal();
    Check(g_comErrorCalls == 1, "the recorded failure is consumed");
}
}

bool __cdecl Sys_IsMainThread() { return g_mainThread; }

void __cdecl Com_Error(const errorParm_t code, const char *const fmt, ...)
{
    ++g_comErrorCalls;
    std::va_list args; va_start(args, fmt);
    std::vsnprintf(g_comErrorMessage, sizeof(g_comErrorMessage), fmt, args);
    va_end(args);
    std::fprintf(stderr, "Com_Error(%d): %s\n", static_cast<int>(code), g_comErrorMessage);
}

[[noreturn]] void KISAK_CDECL FX_InvalidPoolHandle(const void *, const std::uint32_t)
{
    std::abort();
}

bool __cdecl FX_IsTrailElemAllocated(FxSystem *, const FxTrailElem *const elem) { return elem != nullptr; }
bool __cdecl FX_IsTrailAllocated(FxSystem *, const FxTrail *const trail) { return trail != nullptr; }

FxElemVisuals __cdecl FX_GetElemVisuals(const FxElemDef *, int32_t)
{
    FxElemVisuals visuals{};
    return visuals;
}

void __cdecl FX_GetOrientation(
    const FxElemDef *,
    const FxSpatialFrame *,
    const FxSpatialFrame *,
    int32_t,
    orientation_t *const orient)
{
    std::memset(orient, 0, sizeof(*orient));
    orient->axis[0][0] = 1.0f; orient->axis[1][1] = 1.0f; orient->axis[2][2] = 1.0f;
}

void __cdecl FX_OrientationPosToWorldPos(const orientation_t *, const float *const pos, float *const out)
{
    out[0] = pos[0]; out[1] = pos[1]; out[2] = pos[2];
}

void __cdecl FX_AddVisBlocker(FxSystem *, const float *, float, float) {}

char __cdecl R_ReserveCodeMeshVerts(const int count, uint16_t *const baseVertex)
{
    ++g_reserveCalls; g_reserveVerts += count; *baseVertex = 0;
    return 0;
}

char __cdecl R_ReserveCodeMeshIndices(int, r_double_index_t **const indicesOut) { *indicesOut = nullptr; return 0; }
char __cdecl R_ReserveCodeMeshArgs(int, uint32_t *const argOffsetOut) { *argOffsetOut = 0; return 0; }
float (*__cdecl R_GetCodeMeshArgs(uint32_t))[4] { return reinterpret_cast<float (*)[4]>(g_args); }
GfxPackedVertex *__cdecl R_GetCodeMeshVerts(uint16_t) { return g_verts; }

void __cdecl R_AddCodeMeshDrawSurf(
    Material *,
    r_double_index_t *,
    uint32_t,
    uint32_t,
    uint32_t,
    const char *)
{
    g_drawSurfCalled = true;
}

int main(const int argc, char **const argv)
{
    if (argc == 1 || std::strcmp(argv[1], "oversized") == 0)
        RunOversized();
    if (argc == 1 || std::strcmp(argv[1], "corrupt") == 0)
        RunCorrupt();
    if (g_failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("fx_draw_worker_error: ok");
    return 0;
}
