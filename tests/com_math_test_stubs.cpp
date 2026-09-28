// Link stubs for the engine-owned MSVC RNG test target.
//
// The parity tests link src/universal/com_math.cpp directly — the production
// TU that now hosts the engine's rand/srand stream. That translation unit
// also carries the whole com_math surface (matrix helpers, Cull*FromCone,
// UnitQuatToAngles, ...), which references the engine's environment: the
// assert handler, the va() formatter, AngleVectors, and the static-allocation
// tracker. None of that participates in RNG generation, so it is provided
// here as inert stubs — the production code under test (the RNG itself) is
// NOT stubbed, only its unused surroundings. This mirrors
// tests/net_chan_process_test_stubs.cpp.
//
// The engine headers are included directly so signature drift between these
// stubs and the real declarations fails the build instead of linking wrong.

#include <universal/assertive.h>
#include <universal/com_angle.h>
#include <qcommon/mem_track.h>
#include <universal/com_math.h>
#include <universal/q_shared.h>

#include <cstdarg>
#include <cstdlib>

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    // A failed assert in a code path the RNG tests never drive is a test
    // bug, not a contract under measurement; abort so it cannot pass silently.
    std::abort();
}

void track_static_alloc_internal(void *, int, const char *, int)
{
}

// com_math.cpp calls va() only inside assert/diagnostic paths. The engine's
// real va() cycles a ring of static buffers in q_shared.cpp, which is not
// linked here; returning a constant keeps the diagnostic path inert. The
// signature is q_shared.h's `char* QDECL va(const char*, ...)` so drift
// fails the build rather than linking wrong.
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
