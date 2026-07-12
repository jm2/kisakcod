#include "r_workercmds.h"
#include <qcommon/mem_track.h>
#include <qcommon/threads.h>
#include "r_scene.h"
#include "r_staticmodelcache.h"
#include "r_dobj_skin.h"
#include <universal/profile.h>
#include "r_workercmds_common.h"
#include "r_shadowcookie.h"
#include "r_spotshadow.h"
#include "r_dpvs.h"
#include <EffectsCore/fx_system.h>
#include "r_model_skin.h"
#include "r_dvars.h"
#include "rb_logfile.h"
#include "r_setstate_d3d.h"
#include "r_model_pose.h"
#include "r_worker_queue_atomic.h"

#include <cstddef>
#include <cstdint>
#include <setjmp.h>
#include <type_traits>

namespace worker_atomic = gfx::worker_queue_atomic;

static volatile int32_t g_waitTypeMainThread = -1;

GfxSceneEntity *g_GfxEntityBoundsBuf[256];
GfxSceneEntity *g_SkinGfxEntityBuf[1024];
FxCmd g_UpdateFxNonDependentBuf[1];
FxCmd g_UpdateFxRemainingBuf[1];
SkinCachedStaticModelCmd g_skinCachedStaticModelBuf[512];
SkinXModelCmd g_SkinXModelBuf[1024];
DpvsStaticCellCmd g_dpvsCellStaticBuf[256];
DpvsDynamicCellCmd g_dpvsCellSceneEntBuf[512];
DpvsDynamicCellCmd g_dpvsCellDynModelBuf[512];
DpvsDynamicCellCmd g_dpvsCellDynBrushBuf[512];
DpvsEntityCmd g_dpvsEntityBuf[2048];
FxCmd g_UpdateFxSpotLightBuf[1];
FxGenerateVertsCmd g_GenerateFxVertsBuf[2];
FxCmd g_GenerateMarkVertsBuf[1];
SceneEntCmd g_addSceneEntBuf[1];
GfxSpotShadowEntCmd g_spotShadowEntBuf[256];
ShadowCookieCmd g_shadowCookieBuf[1];

WorkerCmds g_workerCmds[WRKCMD_COUNT];

namespace
{
constexpr uint32_t kWorkerBatchCount = 10u;
constexpr uint32_t kWorkerMaxPayloadSize = 192u;
constexpr uint32_t kOutstandingCountLimit = UINT32_MAX;

using WorkerBusyPredicate = bool(__cdecl *)();

bool R_IsWorkerCmdTypeValid(const WorkerCmdType type)
{
    const int32_t value = static_cast<int32_t>(type);
    return value >= 0 && value < static_cast<int32_t>(WRKCMD_COUNT);
}

bool R_IsWorkerCmdDescriptorValid(const WorkerCmds &cmd)
{
    return cmd.buf
        && cmd.dataSize > 0u
        && cmd.dataSize <= kWorkerMaxPayloadSize
        && cmd.bufSize > 0u
        && cmd.bufCount > 0u
        && cmd.bufSize % cmd.dataSize == 0u
        && cmd.bufSize / cmd.dataSize == cmd.bufCount;
}

bool R_WorkerQueuePending(const WorkerCmds &cmd)
{
    // One word owns the complete submission lifetime, including queued and
    // inline execution, so wait/dependency predicates need no split snapshot.
    return Sys_AtomicLoad(&cmd.outstandingCount) != 0u;
}

void R_WorkerQueueInvariantFailure(const char *const detail)
{
    iassert(0);
    Com_Error(ERR_FATAL, "Renderer worker queue invariant failed: %s", detail);
}

bool R_ReleaseWorkerGuard(volatile uint32_t *const guard)
{
    const bool released = worker_atomic::ReleaseGuard(guard);
    if (!released)
        R_WorkerQueueInvariantFailure("guard ownership");
    return released;
}

template <WorkerCmdType Command, class T, std::size_t Count>
bool R_BindWorkerCmdBuffer(T (&buffer)[Count])
{
    static_assert(std::is_same_v<T, WorkerCmdPayloadT<Command>>);
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(Count > 0u);

    constexpr std::size_t nativeDataSize = sizeof(T);
    constexpr std::size_t nativeBufferSize = sizeof(buffer);
    if (nativeDataSize > kWorkerMaxPayloadSize
        || nativeBufferSize > UINT32_MAX)
    {
        return false;
    }

    WorkerCmds &cmd = g_workerCmds[Command];
    cmd.dataSize = static_cast<uint32_t>(nativeDataSize);
    cmd.buf = reinterpret_cast<uint8_t *>(buffer);
    cmd.bufSize = static_cast<uint32_t>(nativeBufferSize);
    cmd.bufCount = static_cast<uint32_t>(Count);
    Sys_AtomicStore(&cmd.startPos, 0u);
    Sys_AtomicStore(&cmd.endPos, 0u);
    Sys_AtomicStore(&cmd.producerGuard, 0u);
    Sys_AtomicStore(&cmd.inSize, 0u);
    Sys_AtomicStore(&cmd.outSize, 0u);
    Sys_AtomicStore(&cmd.consumerGuard, 0u);
    Sys_AtomicStore(&cmd.outstandingCount, 0u);
    return R_IsWorkerCmdDescriptorValid(cmd);
}

bool R_FXNonDependentOrSpotLightPending()
{
    return R_FXSpotLightPending() || R_FXNonDependentPending();
}

bool R_EndFenceBusy()
{
    return R_EndFencePending() != 0;
}

const WorkerBusyPredicate g_cmdOutputBusy[WRKCMD_COUNT] =
{
    nullptr,
    nullptr,
    &R_FXNonDependentOrSpotLightPending,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &R_EndFenceBusy,
    &R_EndFenceBusy,
    &R_EndFenceBusy,
    &R_EndFenceBusy,
};
} // namespace

bool __cdecl R_FXSpotLightPending()
{
    return R_WorkerQueuePending(g_workerCmds[WRKCMD_UPDATE_FX_SPOT_LIGHT]);
}

bool __cdecl R_FXNonDependentPending()
{
    return R_WorkerQueuePending(g_workerCmds[WRKCMD_UPDATE_FX_NON_DEPENDENT]);
}

void __cdecl TRACK_r_workercmds()
{
    TRACK_STATIC_ARR(g_GfxEntityBoundsBuf, 18);
    TRACK_STATIC_ARR(g_SkinGfxEntityBuf, 18);
    TRACK_STATIC_ARR(g_UpdateFxNonDependentBuf, 18);
    TRACK_STATIC_ARR(g_UpdateFxRemainingBuf, 18);
    TRACK_STATIC_ARR(g_skinCachedStaticModelBuf, 18);
    TRACK_STATIC_ARR(g_SkinXModelBuf, 18);
    TRACK_STATIC_ARR(g_dpvsCellStaticBuf, 18);
    TRACK_STATIC_ARR(g_dpvsCellSceneEntBuf, 18);
    TRACK_STATIC_ARR(g_dpvsCellDynModelBuf, 18);
    TRACK_STATIC_ARR(g_dpvsCellDynBrushBuf, 18);
    TRACK_STATIC_ARR(g_dpvsEntityBuf, 18);
    TRACK_STATIC_ARR(g_UpdateFxSpotLightBuf, 18);
    TRACK_STATIC_ARR(g_GenerateFxVertsBuf, 18);
    TRACK_STATIC_ARR(g_GenerateMarkVertsBuf, 18);
    TRACK_STATIC_ARR(g_addSceneEntBuf, 18);
    TRACK_STATIC_ARR(g_spotShadowEntBuf, 18);
    TRACK_STATIC_ARR(g_shadowCookieBuf, 18);
    TRACK_STATIC_ARR(g_workerCmds, 18);
}

int __cdecl R_ProcessWorkerCmdsWithTimeoutInternal(int(__cdecl *timeout)())
{
    if (!timeout || timeout())
        return 1;

    Sys_ResetWorkerCmdEvent();
    int processed;
    do
    {
        processed = 0;
        const WorkerCmdType waitType = static_cast<WorkerCmdType>(
            Sys_AtomicLoad(&g_waitTypeMainThread));
        if (R_IsWorkerCmdTypeValid(waitType))
        {
            while (R_ProcessWorkerCmd(waitType))
            {
                if (timeout())
                    return 1;
                processed = 1;
            }
        }
        for (int32_t typeIndex = 0;
             typeIndex < static_cast<int32_t>(WRKCMD_COUNT);
             ++typeIndex)
        {
            const WorkerCmdType type = static_cast<WorkerCmdType>(typeIndex);
            while (R_ProcessWorkerCmd(type))
            {
                if (timeout())
                    return 1;
                processed = 1;
            }
        }
        if (timeout())
            return 1;
    } while (processed);
    return 0;
}

void __cdecl R_ProcessWorkerCmdsWithTimeout(int(__cdecl *timeout)(), int forever)
{
    if (!timeout)
    {
        iassert(timeout);
        return;
    }
    while (!timeout() && !R_ProcessWorkerCmdsWithTimeoutInternal(timeout) && forever)
    {
        PROF_SCOPED("WaitForWorkerCmd");
        Sys_WaitForWorkerCmd();
    }
}

void __cdecl R_WaitWorkerCmdsOfType(WorkerCmdType type)
{
    iassert(Sys_IsMainThread());
    iassert(R_IsWorkerCmdTypeValid(type));
    if (!R_IsWorkerCmdTypeValid(type))
        return;

    Sys_AtomicStore(&g_waitTypeMainThread, static_cast<int32_t>(type));
    if (!R_WorkerCmdsFinished())
    {
        R_NotifyWorkerCmdType(type);
        KISAK_NULLSUB();
        R_ProcessWorkerCmdsWithTimeout(R_WorkerCmdsFinished, 1);
    }
    Sys_AtomicStore(&g_waitTypeMainThread, -1);
}

void __cdecl R_NotifyWorkerCmdType(WorkerCmdType type)
{
    iassert(R_IsWorkerCmdTypeValid(type));
    if (R_IsWorkerCmdTypeValid(type))
        Sys_SetWorkerCmdEvent();
}

int __cdecl R_WorkerCmdsFinished()
{
    const WorkerCmdType waitType = static_cast<WorkerCmdType>(
        Sys_AtomicLoad(&g_waitTypeMainThread));
    return R_IsWorkerCmdTypeValid(waitType)
        && !R_WorkerQueuePending(g_workerCmds[waitType]);
}

void __cdecl R_ProcessWorkerCmds()
{
    Sys_ResetWorkerCmdEvent();
    int processed;
    do
    {
        processed = 0;
        const WorkerCmdType waitType = static_cast<WorkerCmdType>(
            Sys_AtomicLoad(&g_waitTypeMainThread));
        if (R_IsWorkerCmdTypeValid(waitType))
        {
            while (R_ProcessWorkerCmd(waitType))
                processed = 1;
        }
        for (int32_t typeIndex = 0;
             typeIndex < static_cast<int32_t>(WRKCMD_COUNT);
             ++typeIndex)
        {
            const WorkerCmdType type = static_cast<WorkerCmdType>(typeIndex);
            while (R_ProcessWorkerCmd(type))
                processed = 1;
        }
    } while (processed);
}

int __cdecl R_ProcessWorkerCmd(WorkerCmdType type)
{
    iassert(R_IsWorkerCmdTypeValid(type));
    if (!R_IsWorkerCmdTypeValid(type))
        return 0;

    WorkerCmds &cmd = g_workerCmds[type];
    if (!R_IsWorkerCmdDescriptorValid(cmd))
    {
        R_WorkerQueueInvariantFailure("invalid descriptor during consume");
        return 0;
    }

    if (!worker_atomic::TryAcquireGuard(&cmd.consumerGuard))
    {
        if (Sys_AtomicLoad(&cmd.consumerGuard) > 1u)
            R_WorkerQueueInvariantFailure("corrupt consumer guard");
        return 0;
    }

    const uint32_t readyCount = Sys_AtomicLoad(&cmd.outSize);
    if (readyCount == 0u)
    {
        (void)R_ReleaseWorkerGuard(&cmd.consumerGuard);
        return 0;
    }
    if (readyCount > cmd.bufCount)
    {
        R_WorkerQueueInvariantFailure("published count exceeds capacity");
        return 0;
    }

    const WorkerBusyPredicate busyPredicate = g_cmdOutputBusy[type];
    if (busyPredicate && busyPredicate())
    {
        (void)R_ReleaseWorkerGuard(&cmd.consumerGuard);
        return 0;
    }

    uint32_t count = 0u;
    const uint32_t maximum = busyPredicate ? 1u : kWorkerBatchCount;
    if (!worker_atomic::TryClaimUpTo(
            &cmd.outSize,
            maximum,
            cmd.bufCount,
            &count))
    {
        R_WorkerQueueInvariantFailure("ready claim failed");
        return 0;
    }

    const std::size_t totalBytes =
        static_cast<std::size_t>(count) * cmd.dataSize;
    alignas(std::max_align_t) uint8_t data[
        kWorkerBatchCount * kWorkerMaxPayloadSize];
    if (count > cmd.bufCount || totalBytes > sizeof(data))
    {
        R_WorkerQueueInvariantFailure("dequeue scratch range");
        return 0;
    }

    const uint32_t startPos = Sys_AtomicLoad(&cmd.startPos);
    if (startPos >= cmd.bufCount)
    {
        R_WorkerQueueInvariantFailure("read cursor outside ring");
        return 0;
    }

    const uint32_t firstCount = count < cmd.bufCount - startPos
        ? count
        : cmd.bufCount - startPos;
    const uint32_t secondCount = count - firstCount;
    memcpy(
        data,
        &cmd.buf[static_cast<std::size_t>(startPos) * cmd.dataSize],
        static_cast<std::size_t>(firstCount) * cmd.dataSize);
    if (secondCount != 0u)
    {
        memcpy(
            &data[static_cast<std::size_t>(firstCount) * cmd.dataSize],
            cmd.buf,
            static_cast<std::size_t>(secondCount) * cmd.dataSize);
    }

    uint32_t claimedStart = UINT32_MAX;
    const bool advanced = worker_atomic::TryAdvanceCursor(
        &cmd.startPos,
        count,
        cmd.bufCount,
        &claimedStart);
    if (!advanced || claimedStart != startPos)
    {
        R_WorkerQueueInvariantFailure("read cursor advance");
        return 0;
    }

    if (!R_ReleaseWorkerGuard(&cmd.consumerGuard))
        return 0;
    KISAK_NULLSUB();
    for (uint32_t index = 0u; index < count; ++index)
    {
        R_ProcessWorkerCmdInternal(
            type,
            &data[static_cast<std::size_t>(index) * cmd.dataSize]);
    }

    if (!worker_atomic::TrySubtractBounded(
            &cmd.inSize,
            count,
            cmd.bufCount))
    {
        R_WorkerQueueInvariantFailure("queued capacity completion");
        return 0;
    }
    if (!worker_atomic::TrySubtractBounded(
            &cmd.outstandingCount,
            count,
            kOutstandingCountLimit))
    {
        R_WorkerQueueInvariantFailure("outstanding completion");
        return 0;
    }
    Sys_SetWorkerCmdEvent();
    return 1;
}

void __cdecl R_ProcessWorkerCmdInternal(WorkerCmdType type, void *data)
{
    R_NotifyWorkerCmdType(type);
    switch (type)
    {
    case WRKCMD_UPDATE_FX_SPOT_LIGHT:
        R_ProcessCmd_UpdateFxSpotLight((FxCmd *)data);
        break;
    case WRKCMD_UPDATE_FX_NON_DEPENDENT:
        R_ProcessCmd_UpdateFxNonDependent((FxCmd *)data);
        break;
    case WRKCMD_UPDATE_FX_REMAINING:
        R_ProcessCmd_UpdateFxRemaining((FxCmd *)data);
        break;
    case WRKCMD_DPVS_CELL_STATIC:
        R_AddCellStaticSurfacesInFrustumCmd((DpvsStaticCellCmd *)data);
        break;
    case WRKCMD_DPVS_CELL_SCENE_ENT:
        R_AddCellSceneEntSurfacesInFrustumCmd(
            static_cast<const DpvsDynamicCellCmd *>(data));
        break;
    case WRKCMD_DPVS_CELL_DYN_MODEL:
        R_AddCellDynModelSurfacesInFrustumCmd((const DpvsDynamicCellCmd *)data);
        break;
    case WRKCMD_DPVS_CELL_DYN_BRUSH:
        R_AddCellDynBrushSurfacesInFrustumCmd((const DpvsDynamicCellCmd *)data);
        break;
    case WRKCMD_DPVS_ENTITY:
        R_AddEntitySurfacesInFrustumCmd(
            static_cast<const DpvsEntityCmd *>(data));
        break;
    case WRKCMD_ADD_SCENE_ENT:
        R_AddAllSceneEntSurfacesCamera(
            static_cast<const SceneEntCmd *>(data)->viewInfo);
        break;
    case WRKCMD_SPOT_SHADOW_ENT:
        R_AddSpotShadowEntCmd((const GfxSpotShadowEntCmd *)data);
        break;
    case WRKCMD_SHADOW_COOKIE:
        R_GenerateShadowCookiesCmd((ShadowCookieCmd *)data);
        break;
    case WRKCMD_BOUNDS_ENT_DELAYED:
        R_UpdateGfxEntityBoundsCmd((GfxSceneEntity **)data);
        break;
    case WRKCMD_SKIN_ENT_DELAYED:
        R_SkinGfxEntityCmd((GfxSceneEntity **)data);
        break;
    case WRKCMD_GENERATE_FX_VERTS:
        if (!dx.deviceLost)
            FX_GenerateVerts((FxGenerateVertsCmd *)data);
        break;
    case WRKCMD_GENERATE_MARK_VERTS:
        if (!dx.deviceLost)
            FX_GenerateMarkVertsForWorld(((FxCmd *)data)->localClientNum);
        break;
    case WRKCMD_SKIN_CACHED_STATICMODEL:
        R_SkinCachedStaticModelCmd((SkinCachedStaticModelCmd *)data);
        break;
    case WRKCMD_SKIN_XMODEL:
        R_SkinXModelCmd(static_cast<const SkinXModelCmd *>(data));
        break;
    default:
        if (!alwaysfails)
        {
            iassert(0);
            //MyAssertHandler(".\\r_workercmds.cpp", 635, 0, "unhandled case");
        }
        break;
    }
}

void R_InitWorkerThreads()
{
    uint32_t workerThreadIndexa; // [esp+0h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    if (sys_smp_allowed->current.enabled)
    {
        for (workerThreadIndexa = 0; workerThreadIndexa < 2; ++workerThreadIndexa)
        {
            if (!Sys_SpawnWorkerThread(R_WorkerThread, workerThreadIndexa))
                Com_Error(ERR_FATAL, "Failed to create thread");
        }
    }
}

int R_InitWorkerCmds()
{
    Sys_AtomicStore(&g_waitTypeMainThread, -1);
    bool valid = true;
    valid &= R_BindWorkerCmdBuffer<WRKCMD_UPDATE_FX_SPOT_LIGHT>(
        g_UpdateFxSpotLightBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_UPDATE_FX_NON_DEPENDENT>(
        g_UpdateFxNonDependentBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_UPDATE_FX_REMAINING>(
        g_UpdateFxRemainingBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_DPVS_CELL_STATIC>(
        g_dpvsCellStaticBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_DPVS_CELL_SCENE_ENT>(
        g_dpvsCellSceneEntBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_DPVS_CELL_DYN_MODEL>(
        g_dpvsCellDynModelBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_DPVS_CELL_DYN_BRUSH>(
        g_dpvsCellDynBrushBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_DPVS_ENTITY>(g_dpvsEntityBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_ADD_SCENE_ENT>(g_addSceneEntBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_SPOT_SHADOW_ENT>(
        g_spotShadowEntBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_SHADOW_COOKIE>(g_shadowCookieBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_BOUNDS_ENT_DELAYED>(
        g_GfxEntityBoundsBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_SKIN_ENT_DELAYED>(
        g_SkinGfxEntityBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_GENERATE_FX_VERTS>(
        g_GenerateFxVertsBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_GENERATE_MARK_VERTS>(
        g_GenerateMarkVertsBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_SKIN_CACHED_STATICMODEL>(
        g_skinCachedStaticModelBuf);
    valid &= R_BindWorkerCmdBuffer<WRKCMD_SKIN_XMODEL>(g_SkinXModelBuf);
    return valid ? 1 : 0;
}

void KISAK_CDECL R_WorkerThread(uint32_t threadContext)
{
    void *Value; // eax
    const ThreadContext_t context = static_cast<ThreadContext_t>(threadContext);

    Value = Sys_GetValue(2);
    if (setjmp(*(jmp_buf *)Value))
        Com_ErrorAbort();
    Profile_Guard(1);

    while (1)
    {
        Sys_WorkerThreadPausePoint(context);
        {
            PROF_SCOPED("WaitForWorkerCmd");
            Sys_WaitForWorkerCmd();
            Sys_WorkerThreadPausePoint(context);
        }
        {
            PROF_SCOPED("WorkerThread");
            R_ProcessWorkerCmds();
        }
    }
}

void gfx::worker_cmd_detail::AddWorkerCmd(
    WorkerCmdType type,
    const void *data,
    std::size_t dataSize)
{
    iassert(R_IsWorkerCmdTypeValid(type));
    iassert(data);
    if (!R_IsWorkerCmdTypeValid(type) || !data)
        return;

    WorkerCmds &cmd = g_workerCmds[type];
    const bool validDescriptor = R_IsWorkerCmdDescriptorValid(cmd);
    if (!validDescriptor || dataSize != cmd.dataSize)
    {
        R_WorkerQueueInvariantFailure("invalid descriptor or payload size");
        return;
    }

    const bool producerTracked = worker_atomic::TryAddBounded(
        &cmd.outstandingCount,
        1u,
        kOutstandingCountLimit);
    if (!producerTracked)
    {
        R_WorkerQueueInvariantFailure("outstanding submission overflow");
        return;
    }

    if (r_smp_worker->current.enabled && sys_smp_allowed->current.enabled)
    {
        bool queueFull = false;
        while (!worker_atomic::TryAcquireGuard(&cmd.producerGuard))
        {
            if (Sys_AtomicLoad(&cmd.producerGuard) > 1u)
            {
                R_WorkerQueueInvariantFailure("corrupt producer guard");
                return;
            }
            Sys_Sleep(0);
        }

        if (worker_atomic::TryAddBounded(&cmd.inSize, 1u, cmd.bufCount))
        {
            uint32_t endPos = UINT32_MAX;
            if (!worker_atomic::TryAdvanceCursor(
                    &cmd.endPos,
                    1u,
                    cmd.bufCount,
                    &endPos))
            {
                R_WorkerQueueInvariantFailure("write cursor advance");
                return;
            }

            memcpy(
                &cmd.buf[static_cast<std::size_t>(endPos) * cmd.dataSize],
                data,
                cmd.dataSize);
            if (!worker_atomic::TryAddBounded(
                    &cmd.outSize,
                    1u,
                    cmd.bufCount))
            {
                R_WorkerQueueInvariantFailure("ready publication");
                return;
            }
            if (!R_ReleaseWorkerGuard(&cmd.producerGuard))
                return;
            R_NotifyWorkerCmdType(type);
            return;
        }
        else
        {
            const uint32_t inSize = Sys_AtomicLoad(&cmd.inSize);
            if (inSize > cmd.bufCount)
            {
                R_WorkerQueueInvariantFailure("queued capacity claim");
                return;
            }
            queueFull = true;
        }

        if (!R_ReleaseWorkerGuard(&cmd.producerGuard))
            return;
        if (queueFull && type != WRKCMD_SKIN_CACHED_STATICMODEL)
            R_WarnOncePerFrame(R_WARN_WORKER_CMD_SIZE, type);
    }

    {
        PROF_SCOPED("WaitWorkerCmds");
        if (g_cmdOutputBusy[type])
        {
            while (g_cmdOutputBusy[type]())
                Sys_Sleep(1);
        }
    }

    R_ProcessWorkerCmdInternal(type, const_cast<void *>(data));
    const bool releasedInlineProducer = worker_atomic::TrySubtractBounded(
        &cmd.outstandingCount,
        1u,
        kOutstandingCountLimit);
    if (!releasedInlineProducer)
    {
        R_WorkerQueueInvariantFailure("inline outstanding completion");
        return;
    }
    Sys_SetWorkerCmdEvent();
}

void __cdecl R_UpdateActiveWorkerThreads()
{
    char v0; // [esp+3h] [ebp-9h]
    uint32_t i; // [esp+4h] [ebp-8h]
    uint32_t workerIter; // [esp+8h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    for (i = 0; i < 2; ++i)
    {
        if (r_smp_worker_thread[i]->modified)
        {
            v0 = 1;
            goto LABEL_9;
        }
    }
    v0 = 0;
LABEL_9:
    if (v0)
    {
        Com_SyncThreads();
        for (workerIter = 0; workerIter < 2; ++workerIter)
        {
            if (r_smp_worker_thread[workerIter]->modified)
            {
                Dvar_ClearModified((dvar_s*)r_smp_worker_thread[workerIter]);
                Sys_SetWorkerThreadActive(
                    workerIter,
                    r_smp_worker_thread[workerIter]->current.enabled);
            }
        }
        R_ReleaseThreadOwnership();
    }
}

void __cdecl R_WaitFrontendWorkerCmds()
{
    iassert(Sys_IsMainThread());

    PROF_SCOPED("R_WaitFrontendWorkerCmds");
    //KISAK_NULLSUB();

    R_ProcessWorkerCmdsWithTimeout(R_FinishedWorkerCmds, 1);
}

int __cdecl R_FinishedWorkerCmds()
{
    for (int32_t typeIndex = 0;
         typeIndex < static_cast<int32_t>(WRKCMD_COUNT);
         ++typeIndex)
    {
        if (R_WorkerQueuePending(g_workerCmds[typeIndex]))
            return 0;
    }
    return 1;
}

void __cdecl R_WaitWorkerCmds()
{
#ifndef KISAK_SP
    iassert(Sys_IsMainThread());
#endif

    PROF_SCOPED("R_WaitWorkerCmds");
    //KISAK_NULLSUB();

    R_ProcessWorkerCmdsWithTimeout(R_FinishedWorkerCmds, 1);
}
