#pragma once

// cl_cinematic_policy.h: whether a cinematic may take a multiplayer client's
// connection state. CIN_PlayCinematic sets CA_CINEMATIC, and CL_PacketEvent
// drops the server's reliable connect and gamestate stream while the state is
// below CA_CONNECTED, so a cinematic that starts during a connect loses that
// connect (issue #818). Com_Init queues the intro chain (COM_PlayIntroMovies)
// and then runs the startup commands, so a startup +connect has already set
// CA_CONNECTING by the time the queued cinematic executes. A cinematic must
// leave a connecting or connected client's state alone; one started from the
// menu still plays, so the normal intro is kept. D3D-free, so the portable
// tests reach it.

namespace cl_cinematic
{
// connectionState is the client's connstate_t; firstConnectingState is the
// multiplayer CA_CONNECTING. Single player has no connect states and passes
// INT32_MAX, so it never skips.
constexpr bool SkipsForConnect(int connectionState, int firstConnectingState)
{
    return connectionState >= firstConnectingState;
}
} // namespace cl_cinematic
