#pragma once

// posix_sys.h: the POSIX system layer shared by every POSIX entry point (the
// headless server's posix_main.cpp and a client entry): the sysEvent_t queue,
// process lifetime, Sys_Init and the CPU description. Each entry point keeps
// main(), its Sys_GetEvent pump, the clipboard and Sys_In_Restart_f.

#include <cstddef>

#include <qcommon/sys_local.h>

// Takes the oldest queued event; false when the queue is empty. The caller
// does not hold CRITSECT_SYS_EVENT_QUEUE.
bool Posix_DequeueEvent(sysEvent_t *out);
// True when no event is queued; the caller holds CRITSECT_SYS_EVENT_QUEUE.
bool Posix_EventQueueEmpty();
// Frees every queued event's payload (Sys_NormalExit runs it).
void Sys_ShutdownEvents();

// Fills sys_info the way win_main.cpp's Sys_FindInfo does.
void Posix_DetectCpu();
void Posix_PrintWorkingDir();
// Rebuilds WinMain's lpCmdLine from argv: the arguments without the
// executable name, each one that holds whitespace quoted.
void Posix_BuildCommandLine(int argc, char **argv, char *out, std::size_t outSize);

// Each entry point defines it: a headless server has no input to restart.
void Sys_In_Restart_f();
