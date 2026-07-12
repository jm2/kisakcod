#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <universal/kisak_abi.h>
#include <universal/platform_compat.h>

#include "r_material.h"
#include "rb_backend.h"
#include "fxprimitives.h"

enum WorkerCmdType : __int32
{
    WRKCMD_FIRST_FRONTEND = 0x0,
    WRKCMD_UPDATE_FX_SPOT_LIGHT = 0x0,
    WRKCMD_UPDATE_FX_NON_DEPENDENT = 0x1,
    WRKCMD_UPDATE_FX_REMAINING = 0x2,
    WRKCMD_DPVS_CELL_STATIC = 0x3,
    WRKCMD_DPVS_CELL_SCENE_ENT = 0x4,
    WRKCMD_DPVS_CELL_DYN_MODEL = 0x5,
    WRKCMD_DPVS_CELL_DYN_BRUSH = 0x6,
    WRKCMD_DPVS_ENTITY = 0x7,
    WRKCMD_ADD_SCENE_ENT = 0x8,
    WRKCMD_SPOT_SHADOW_ENT = 0x9,
    WRKCMD_SHADOW_COOKIE = 0xA,
    WRKCMD_BOUNDS_ENT_DELAYED = 0xB,
    WRKCMD_SKIN_ENT_DELAYED = 0xC,
    WRKCMD_GENERATE_FX_VERTS = 0xD,
    WRKCMD_GENERATE_MARK_VERTS = 0xE,
    WRKCMD_SKIN_CACHED_STATICMODEL = 0xF,
    WRKCMD_SKIN_XMODEL = 0x10,
    WRKCMD_COUNT = 0x11,
};
inline WorkerCmdType &operator++(WorkerCmdType &e) {
    e = static_cast<WorkerCmdType>(static_cast<int>(e) + 1);
    return e;
}
inline WorkerCmdType &operator++(WorkerCmdType &e, int i)
{
    ++e;
    return e;
}

struct WorkerCmds // sizeof=0x80
{                                       // ...
    volatile uint32_t startPos;
    volatile uint32_t endPos;
    volatile uint32_t producerGuard;
    volatile uint32_t inSize;           // reserved, published, or executing
    volatile uint32_t outSize;          // published and not yet claimed
    uint32_t dataSize;
    uint8_t *buf;
    uint32_t bufSize;
    uint32_t bufCount;
    uint32_t pad[21];
    volatile uint32_t consumerGuard;
    volatile uint32_t outstandingCount; // submitting, queued, or executing
};
RUNTIME_SIZE(WorkerCmds, 0x80, 0x88);
RUNTIME_OFFSET(WorkerCmds, consumerGuard, 0x78, 0x7C);
RUNTIME_OFFSET(WorkerCmds, outstandingCount, 0x7C, 0x80);

struct DpvsStaticCellCmd;
struct DpvsDynamicCellCmd;
struct DpvsEntityCmd;
struct SceneEntCmd;
struct GfxSpotShadowEntCmd;
struct ShadowCookieCmd;
struct GfxSceneEntity;
struct FxGenerateVertsCmd;
struct SkinCachedStaticModelCmd;
struct SkinXModelCmd;

template <WorkerCmdType Command>
struct WorkerCmdPayload;

#define KISAK_WORKER_CMD_PAYLOAD(command, payload) \
    template <> struct WorkerCmdPayload<command> { using type = payload; }

KISAK_WORKER_CMD_PAYLOAD(WRKCMD_UPDATE_FX_SPOT_LIGHT, FxCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_UPDATE_FX_NON_DEPENDENT, FxCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_UPDATE_FX_REMAINING, FxCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_DPVS_CELL_STATIC, DpvsStaticCellCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_DPVS_CELL_SCENE_ENT, DpvsDynamicCellCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_DPVS_CELL_DYN_MODEL, DpvsDynamicCellCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_DPVS_CELL_DYN_BRUSH, DpvsDynamicCellCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_DPVS_ENTITY, DpvsEntityCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_ADD_SCENE_ENT, SceneEntCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_SPOT_SHADOW_ENT, GfxSpotShadowEntCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_SHADOW_COOKIE, ShadowCookieCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_BOUNDS_ENT_DELAYED, GfxSceneEntity *);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_SKIN_ENT_DELAYED, GfxSceneEntity *);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_GENERATE_FX_VERTS, FxGenerateVertsCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_GENERATE_MARK_VERTS, FxCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_SKIN_CACHED_STATICMODEL, SkinCachedStaticModelCmd);
KISAK_WORKER_CMD_PAYLOAD(WRKCMD_SKIN_XMODEL, SkinXModelCmd);

#undef KISAK_WORKER_CMD_PAYLOAD

template <WorkerCmdType Command>
using WorkerCmdPayloadT = typename WorkerCmdPayload<Command>::type;

namespace gfx::worker_cmd_detail
{
void AddWorkerCmd(
    WorkerCmdType type,
    const void *data,
    std::size_t dataSize);
}

template <WorkerCmdType Command>
inline void R_AddWorkerCmd(const WorkerCmdPayloadT<Command> &payload)
{
    static_assert(std::is_trivially_copyable_v<WorkerCmdPayloadT<Command>>);
    gfx::worker_cmd_detail::AddWorkerCmd(
        Command,
        &payload,
        sizeof(payload));
}

bool __cdecl R_FXSpotLightPending();
bool __cdecl R_FXNonDependentPending();
void __cdecl TRACK_r_workercmds();
void __cdecl R_WaitWorkerCmdsOfType(WorkerCmdType type);
void __cdecl R_NotifyWorkerCmdType(WorkerCmdType type);
int __cdecl R_WorkerCmdsFinished();
void __cdecl R_ProcessWorkerCmds();
int __cdecl R_ProcessWorkerCmd(WorkerCmdType type);
void __cdecl R_ProcessWorkerCmdInternal(WorkerCmdType type, void *data);
void R_InitWorkerThreads();
int R_InitWorkerCmds();
void KISAK_CDECL R_WorkerThread(uint32_t threadContext);
void __cdecl R_UpdateActiveWorkerThreads();
void __cdecl R_WaitFrontendWorkerCmds();
int __cdecl R_FinishedWorkerCmds();
void __cdecl R_WaitWorkerCmds();
void __cdecl R_ProcessWorkerCmdsWithTimeout(int(__cdecl *timeout)(), int forever);
