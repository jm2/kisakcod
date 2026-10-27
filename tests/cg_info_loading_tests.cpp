// cg_info_loading_tests.cpp: CG_LoadingString (production cgame/cg_info.cpp)
// must not dereference a null loading string. The null case clears isLoading
// and still advances the load screen.

#include <cstdio>

#include <cgame/cg_local.h>
#include <cgame_mp/cg_local_mp.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
int g_loadScreenUpdates = 0;

#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
} // namespace

cg_s cgArray[1];

WEAK void __cdecl SCR_UpdateLoadScreen() { ++g_loadScreenUpdates; }
WEAK void __cdecl Com_Printf(int, const char *, ...) {}

int main()
{
    CG_LoadingString(0, nullptr);
    CHECK(cgArray[0].isLoading == 0);
    CHECK(g_loadScreenUpdates == 1);

    CG_LoadingString(0, "loading");
    CHECK(cgArray[0].isLoading != 0);
    CHECK(g_loadScreenUpdates == 2);

    if (g_failures == 0)
        std::printf("loading string null handling: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
