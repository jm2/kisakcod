// posix_voice.cpp: the silent POSIX voice backend, in place of
// win32/win_voice.cpp's DirectSound capture and playback. It implements the
// Voice_* surface of qcommon/sys_local.h so the client links and voice chat is
// off: nothing records or plays, incoming voice is dropped and no client ever
// talks. A real backend (OpenAL Soft capture and playback) replaces it later.

#include <qcommon/sys_local.h>

bool __cdecl Voice_SendVoiceData()
{
    return false;
}

// win_voice.cpp returns 0 too; callers ignore the result.
bool __cdecl Voice_Init()
{
    return false;
}

void __cdecl Voice_Shutdown()
{
}

double __cdecl Voice_GetVoiceLevel()
{
    return 0.0;
}

void __cdecl Voice_Playback()
{
}

int __cdecl Voice_GetLocalVoiceData()
{
    return 0;
}

void __cdecl Voice_IncomingVoiceData(unsigned __int8 talker, unsigned __int8 *data, int packetDataSize)
{
    (void)talker;
    (void)data;
    (void)packetDataSize;
}

bool __cdecl Voice_IsClientTalking(uint32_t clientNum)
{
    (void)clientNum;
    return false;
}

// No microphone: recording never starts, so there is nothing to stop.
char __cdecl Voice_StartRecording()
{
    return 0;
}

char __cdecl Voice_StopRecording()
{
    return 0;
}
