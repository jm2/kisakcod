// Calls the silent Miles/Bink stubs (win32/win_media_stubs.cpp) through the
// same mss.h/bink.h declarations that the engine compiles against, so the
// test links the way KisakCOD-mp links on Win64. Each check is the result an
// engine caller relies on to switch sound or cinematics off cleanly:
// - MSS_Startup tests AIL_startup() != 0.
// - The driver code prints AIL_last_error() with %s.
// - Sample and stream polling waits until the status reports SMP_DONE.
// - R_Cinematic_BinkOpen fails on a null HBINK, and R_Cinematic_CheckBinkError,
//   run before every playback, asserts that BinkGetError() is null or empty.

#include <cstdio>
#include <cstring>

#include <msslib/mss.h>
#include <binklib/bink.h>

namespace
{
int g_failures = 0;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
}

int main()
{
    Check(AIL_startup() == 0, "AIL_startup reports failure");
    const char *milesError = AIL_last_error();
    Check(milesError && std::strlen(milesError) > 0, "AIL_last_error returns a printable message");
    Check(AIL_open_digital_driver(44100, 16, 2, 0) == nullptr, "AIL_open_digital_driver opens no driver");
    Check(AIL_allocate_sample_handle(nullptr) == nullptr, "AIL_allocate_sample_handle allocates nothing");
    Check(AIL_open_stream(nullptr, "sound/music.mp3", 0) == nullptr, "AIL_open_stream opens no stream");
    Check(AIL_sample_status(nullptr) == SMP_DONE, "AIL_sample_status reports done");
    Check(AIL_stream_status(nullptr) == SMP_DONE, "AIL_stream_status reports done");
    AIL_set_redist_directory("miles");
    AIL_shutdown();

    Check(BinkSetSoundSystem(BinkOpenMiles, 0) == 0, "BinkSetSoundSystem installs no sound system");
    Check(BinkOpen("video/default.bik", 0) == nullptr, "BinkOpen opens no movie");
    const char *binkError = BinkGetError();
    Check(!binkError || binkError[0] == '\0', "BinkGetError reports no error");

    if (g_failures)
        return 1;
    std::printf("win-media-stubs: all checks passed\n");
    return 0;
}
