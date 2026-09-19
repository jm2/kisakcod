#pragma once

enum dlStatus_t : __int32
{                                       // ...
    DL_CONTINUE = 0x0,
    DL_DONE = 0x1,
    DL_FAILED = 0x2,
};

void __cdecl DL_CancelDownload();
void __cdecl DL_InitDownload();
int __cdecl DL_BeginDownload(char *localName, char *remoteName);
int __cdecl DL_DownloadLoop();
bool __cdecl DL_InProgress();
bool __cdecl DL_DLIsMotd();

// Bytes persisted so far in the active download; the client copies this
// into the legacy progress meter each frame (the retail transport fed the
// same counter from its library callback). Meaningful only while
// DL_InProgress(); always safe to call.
int __cdecl DL_BytesRead();
