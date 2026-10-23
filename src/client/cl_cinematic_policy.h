#pragma once

// cl_cinematic_policy.h: whether a cinematic may take a multiplayer client's
// connection state. CIN_PlayCinematic sets CA_CINEMATIC, which drops a connect
// in flight: Com_Init queues the intro chain (COM_PlayIntroMovies) and then
// runs a startup +devmap at once. With real Bink the intro plays and ends
// back in the menu, as retail does. A build without Bink would fail the open
// at once, so the intro would only strand the connect; such a build skips a
// cinematic while the client connects or plays. D3D-free, so the portable
// tests reach it.

namespace cl_cinematic
{
// Whether this build plays cinematics: every client decodes Bink through
// FFmpeg (gfx_d3d/r_bink_ffmpeg.cpp), so a shipped build never skips. The
// tests still check the rule for a build without Bink.
inline constexpr bool kBuildPlaysBink = true;

// connectionState is the client's connstate_t; firstConnectingState is the
// multiplayer CA_CONNECTING. Single player has no connect states and passes
// none it can reach.
constexpr bool SkipsForConnect(int connectionState, int firstConnectingState, bool buildPlaysBink = kBuildPlaysBink)
{
    return !buildPlaysBink && connectionState >= firstConnectingState;
}
} // namespace cl_cinematic
