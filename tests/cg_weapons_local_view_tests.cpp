// cg_weapons_local_view_tests.cpp: the MP local-view predicate in
// production cgame/cg_weapons.cpp. A player entity is the local view entity
// only when otherFlags selects a view mode and the entity number matches the
// local player's client number.

#include <cstdio>
#include <cstring>

#include <bgame/bg_public.h>
#include <cgame/cg_local.h>
#include <cgame_mp/cg_local_mp.h>
#include <universal/com_math.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
snapshot_s g_snapshot{};

#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

void SetSnapshot(int otherFlags, int clientNum)
{
    std::memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot.ps.otherFlags = otherFlags;
    g_snapshot.ps.clientNum = clientNum;
    cgArray[0].nextSnap = &g_snapshot;
}
} // namespace

cg_s cgArray[1];
centity_s cg_entitiesArray[1][MAX_GENTITIES];

dvar_s g_tracerChance{};
const dvar_t *cg_tracerChance = &g_tracerChance;

WEAK char __cdecl CL_IsLocalClientActive(int32_t) { return 1; }
WEAK WeaponDef *__cdecl BG_GetWeaponDef(uint32_t) { return nullptr; }
WEAK int __cdecl Kisak_rand() { return 0; }
WEAK void MyAssertHandler(const char *, int, int, const char *, ...) {}

int main()
{
    g_tracerChance.current.value = 1.0f;

    SetSnapshot(0, 5);
    CHECK(CG_GetLocalClientGlobalsForEnt(0, 5) == nullptr);
    CHECK(ShouldSpawnTracer(0, 5));

    SetSnapshot(6, 5);
    CHECK(CG_GetLocalClientGlobalsForEnt(0, 5) == &cgArray[0]);
    CHECK(!ShouldSpawnTracer(0, 5));

    SetSnapshot(2, 5);
    CHECK(CG_GetLocalClientGlobalsForEnt(0, 5) == &cgArray[0]);

    SetSnapshot(6, 6);
    CHECK(CG_GetLocalClientGlobalsForEnt(0, 5) == nullptr);

    SetSnapshot(0, 6);
    CHECK(ShouldSpawnTracer(0, 5));

    if (g_failures == 0)
        std::printf("weapon local-view gating: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
