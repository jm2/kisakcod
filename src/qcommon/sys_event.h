#pragma once

#include <cstdint>

#include <universal/platform_compat.h>

struct SysEvent;
using SysEventHandle = SysEvent *;

// Event handles retain the engine's pointer-to-handle call shape. Creation
// requires a null handle. Destruction requires external quiescence, destroys
// the native event, and resets the caller's handle to null.
void KISAK_CDECL Sys_CreateEvent(
    bool manualReset,
    bool initialState,
    SysEventHandle *event);
void KISAK_CDECL Sys_DestroyEvent(SysEventHandle *event);
void KISAK_CDECL Sys_SetEvent(SysEventHandle *event);
void KISAK_CDECL Sys_ResetEvent(SysEventHandle *event);
void KISAK_CDECL Sys_WaitForSingleObject(SysEventHandle *event);

// Returns true only when signaled and false only on timeout. UINT32_MAX is
// reserved for the non-timeout wait API and is rejected here.
bool KISAK_CDECL Sys_WaitForSingleObjectTimeout(
    SysEventHandle *event,
    std::uint32_t msec);
