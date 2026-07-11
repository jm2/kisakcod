#include <qcommon/sys_worker_gate.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

#include <qcommon/sys_event.h>

namespace
{
enum class SysWorkerGateState : std::uint32_t
{
    Created,
    Running,
    PauseRequested,
    Parked,
};

[[noreturn]] void Sys_WorkerGateFailFast()
{
    std::abort();
}
}

struct SysWorkerGate
{
    std::atomic<SysWorkerGateState> state;
    SysEventHandle resumeEvent;
    SysEventHandle parkedEvent;
};

namespace
{
SysWorkerGate *Sys_GetWorkerGate(const SysWorkerGateHandle gate)
{
    if (!gate)
        Sys_WorkerGateFailFast();
    return gate;
}
}

void KISAK_CDECL Sys_WorkerGateCreate(SysWorkerGateHandle *const outGate)
{
    if (!outGate || *outGate)
        Sys_WorkerGateFailFast();

    SysWorkerGate *const gate = new (std::nothrow) SysWorkerGate{
        SysWorkerGateState::Created,
        nullptr,
        nullptr};
    if (!gate)
        Sys_WorkerGateFailFast();

    Sys_CreateEvent(false, false, &gate->resumeEvent);
    Sys_CreateEvent(false, false, &gate->parkedEvent);
    *outGate = gate;
}

void KISAK_CDECL Sys_WorkerGateDestroy(SysWorkerGateHandle *const gateHandle)
{
    if (!gateHandle)
        Sys_WorkerGateFailFast();

    SysWorkerGate *const gate = Sys_GetWorkerGate(*gateHandle);
    const SysWorkerGateState state = gate->state.load(std::memory_order_seq_cst);
    if (state != SysWorkerGateState::Created
        && state != SysWorkerGateState::Running)
    {
        Sys_WorkerGateFailFast();
    }

    Sys_DestroyEvent(&gate->parkedEvent);
    Sys_DestroyEvent(&gate->resumeEvent);
    delete gate;
    *gateHandle = nullptr;
}

bool KISAK_CDECL Sys_WorkerGateActivate(const SysWorkerGateHandle gateHandle)
{
    SysWorkerGate *const gate = Sys_GetWorkerGate(gateHandle);

    for (;;)
    {
        SysWorkerGateState state = gate->state.load(std::memory_order_seq_cst);
        switch (state)
        {
        case SysWorkerGateState::Created:
            if (gate->state.compare_exchange_weak(
                    state,
                    SysWorkerGateState::Running,
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst))
            {
                return true;
            }
            break;

        case SysWorkerGateState::Running:
            return false;

        case SysWorkerGateState::PauseRequested:
            Sys_WorkerGateFailFast();

        case SysWorkerGateState::Parked:
            if (gate->state.compare_exchange_weak(
                    state,
                    SysWorkerGateState::Running,
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst))
            {
                Sys_SetEvent(&gate->resumeEvent);
                return false;
            }
            break;

        default:
            Sys_WorkerGateFailFast();
        }
    }
}

bool KISAK_CDECL Sys_WorkerGateRequestPause(
    const SysWorkerGateHandle gateHandle)
{
    SysWorkerGate *const gate = Sys_GetWorkerGate(gateHandle);

    for (;;)
    {
        SysWorkerGateState state = gate->state.load(std::memory_order_seq_cst);
        switch (state)
        {
        case SysWorkerGateState::Created:
        case SysWorkerGateState::Parked:
            return false;

        case SysWorkerGateState::Running:
            if (gate->state.compare_exchange_weak(
                    state,
                    SysWorkerGateState::PauseRequested,
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst))
            {
                return true;
            }
            break;

        case SysWorkerGateState::PauseRequested:
            return true;

        default:
            Sys_WorkerGateFailFast();
        }
    }
}

void KISAK_CDECL Sys_WorkerGateWaitPaused(
    const SysWorkerGateHandle gateHandle)
{
    SysWorkerGate *const gate = Sys_GetWorkerGate(gateHandle);

    for (;;)
    {
        switch (gate->state.load(std::memory_order_seq_cst))
        {
        case SysWorkerGateState::Created:
        case SysWorkerGateState::Parked:
            return;

        case SysWorkerGateState::PauseRequested:
            // Always recheck the state after waking. An acknowledgement can
            // remain signaled when an earlier waiter observed Parked directly.
            Sys_WaitForSingleObject(&gate->parkedEvent);
            break;

        case SysWorkerGateState::Running:
        default:
            Sys_WorkerGateFailFast();
        }
    }
}

void KISAK_CDECL Sys_WorkerGatePausePoint(
    const SysWorkerGateHandle gateHandle)
{
    SysWorkerGate *const gate = Sys_GetWorkerGate(gateHandle);

    for (;;)
    {
        SysWorkerGateState state = gate->state.load(std::memory_order_seq_cst);
        switch (state)
        {
        case SysWorkerGateState::Running:
            return;

        case SysWorkerGateState::PauseRequested:
            if (gate->state.compare_exchange_weak(
                    state,
                    SysWorkerGateState::Parked,
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst))
            {
                // Publish Parked before acknowledging it. From this point the
                // worker cannot return to command processing without activation.
                Sys_SetEvent(&gate->parkedEvent);
            }
            break;

        case SysWorkerGateState::Parked:
            // Resume events are directional. Recheck state after every wake so
            // a stale signal cannot release a later pause generation.
            Sys_WaitForSingleObject(&gate->resumeEvent);
            break;

        case SysWorkerGateState::Created:
        default:
            Sys_WorkerGateFailFast();
        }
    }
}

bool KISAK_CDECL Sys_WorkerGateIsActive(
    const SysWorkerGateHandle gateHandle)
{
    const SysWorkerGate *const gate = Sys_GetWorkerGate(gateHandle);
    return gate->state.load(std::memory_order_seq_cst)
        == SysWorkerGateState::Running;
}
