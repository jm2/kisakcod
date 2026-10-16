#pragma once

// cl_cinematic_policy.h: whether a cinematic may take a multiplayer client's
// connection state. CIN_PlayCinematic sets CA_CINEMATIC, which drops a connect
// in flight: Com_Init queues the intro chain (COM_PlayIntroMovies) and then
// runs a startup +devmap at once. With real Bink the intro plays and ends
// back in the menu, as retail does. A build without Bink (no 64-bit Bink
// exists; 64-bit clients link silent stubs) fails the open at once, so the
// intro only strands the connect. Those builds skip a cinematic while the
// client connects or plays. D3D-free, so the portable tests reach it.

#include <universal/kisak_abi.h>

namespace cl_cinematic
{
// Whether this build plays cinematics: Win32 links the real Bink library,
// and every 64-bit client links the silent media stubs, whose open fails. The
// width stands in for "links the stubs" because no compile definition marks
// the Win64 stub build. Revisit when the native clients gain FFmpeg
// cinematics (CLIENT.md): those 64-bit builds will play them, and must not skip.
inline constexpr bool kBuildPlaysBink = !KISAK_ARCH_64BIT;

// connectionState is the client's connstate_t; firstConnectingState is the
// multiplayer CA_CONNECTING. Single player has no connect states and passes
// none it can reach.
constexpr bool SkipsForConnect(int connectionState, int firstConnectingState, bool buildPlaysBink = kBuildPlaysBink)
{
    return !buildPlaysBink && connectionState >= firstConnectingState;
}
} // namespace cl_cinematic
