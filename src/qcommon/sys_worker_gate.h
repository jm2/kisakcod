#pragma once

#include <universal/platform_compat.h>

struct SysWorkerGate;
using SysWorkerGateHandle = SysWorkerGate *;

// Worker gates are controlled by one thread and observed by one worker. A newly
// created gate starts in the Created state. The first activation returns true,
// telling the controller to start the suspended native thread exactly once.
void KISAK_CDECL Sys_WorkerGateCreate(SysWorkerGateHandle *outGate);

// Destruction requires external quiescence. The worker must no longer access
// the gate, which must be either never activated or active without a pending
// pause. Destruction releases both events and resets the caller's handle.
void KISAK_CDECL Sys_WorkerGateDestroy(SysWorkerGateHandle *gate);

bool KISAK_CDECL Sys_WorkerGateActivate(SysWorkerGateHandle gate);

// A true request result means the controller must wake the worker from any
// unrelated command wait and then call Sys_WorkerGateWaitPaused. False means
// the worker has not started or is already parked.
bool KISAK_CDECL Sys_WorkerGateRequestPause(SysWorkerGateHandle gate);
void KISAK_CDECL Sys_WorkerGateWaitPaused(SysWorkerGateHandle gate);

// The worker calls this only at points where it owns no partially processed
// command. It returns immediately while active, or acknowledges and blocks for
// a requested pause until the controller activates the gate again.
void KISAK_CDECL Sys_WorkerGatePausePoint(SysWorkerGateHandle gate);

bool KISAK_CDECL Sys_WorkerGateIsActive(SysWorkerGateHandle gate);
