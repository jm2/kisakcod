// Link stubs for the MSVC RNG test (tests/msvc_rand_shim_tests.cpp), which
// links the production universal/com_math.cpp. Only the environment the rest
// of that TU references is stubbed; the RNG under test is not. The engine
// headers declare each stub, so signature drift fails the build.

#include <universal/assertive.h>
#include <universal/com_angle.h>
#include <qcommon/mem_track.h>
#include <universal/com_math.h>
#include <universal/q_shared.h>

#include <cstdarg>
#include <cstdlib>

// The RNG tests never reach an engine assert; one that fires is a test bug.
void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

void track_static_alloc_internal(void *, int, const char *, int)
{
}

char *QDECL va(const char *, ...)
{
    return nullptr;
}

void AngleVectors(const float *, float *, float *, float *)
{
    std::abort();
}

float KISAK_CDECL AngleDelta(float, float)
{
    std::abort();
}
