// cg_bg_callbacks_tests.cpp: the DObj callback CG_Init hands to cgameGlob->bgs
// (cgame_mp/cg_main_mp.cpp CG_BgCreateDObj) has bgs_t's own type. Called
// through it, as the bgame code calls it, it forwards to CG_CreateDObj, which
// creates the client DObj and parks the entity's origin. Before #676 CG_Init
// cast CG_CreateDObj (a uint32_t handle) to that type, which UBSan's
// -fsanitize=function reports on the first player DObj; #674 fixed the
// server's twin.
//
// The engine boundary is weak: --gc-sections drops what the check never
// reaches.

#include <cstdio>
#include <cstdlib>

#include <cgame_mp/cg_local_mp.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

DObjModel_s *g_dobjModels = nullptr;
int g_dobjNumModels = 0;
uint32_t g_dobjHandle = 0;
int g_dobjLocalClientNum = -1;
} // namespace

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK DObj_s *__cdecl Com_ClientDObjCreate(DObjModel_s *dobjModels, uint16_t numModels, XAnimTree_s *, uint32_t handle,
                                          int localClientNum)
{
    g_dobjModels = dobjModels;
    g_dobjNumModels = numModels;
    g_dobjHandle = handle;
    g_dobjLocalClientNum = localClientNum;
    return nullptr;
}

int main()
{
    bgs_t bgs{};
    bgs.CreateDObj = CG_BgCreateDObj;
    DObjModel_s models[3]{};
    clientInfo_t ci{};
    ci.clientNum = 7;
    bgs.CreateDObj(models, 3, nullptr, 40, 0, &ci);
    CHECK(g_dobjModels == models);
    CHECK(g_dobjNumModels == 3);
    CHECK(g_dobjHandle == 40u);
    CHECK(g_dobjLocalClientNum == 0);
    for (int axis = 0; axis < 3; ++axis)
        CHECK(cg_entityOriginArray[0][7][axis] == 131072.0f); // parked until the first snapshot

    if (g_failures == 0)
        std::printf("cg bg callbacks: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
