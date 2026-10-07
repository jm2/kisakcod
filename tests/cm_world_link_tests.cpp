// cm_world_link_tests.cpp: the world-sector link lists in the production
// cm_world.cpp, at 64-bit. CM_AddEntityToNode and CM_AddStaticModelToNode
// keep each sector's entities and static models sorted through 1-based
// "next" links. The decompiled code reached an entity's link as a
// configstrings offset that holds only for the x86 server_t, and cast a
// sector's first static-model link to a cStaticModel_s.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <qcommon/qcommon.h>
#include <server_mp/server_mp.h>
#include <xanim/xanim.h>

server_t sv;
clipMap_t cm;

void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file ? file : "?", line, fmt ? fmt : "");
    std::exit(3);
}
void __cdecl Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "unexpected Com_Error: %s\n", fmt ? fmt : "");
    std::exit(2);
}

namespace
{
int g_failures = 0;
void Check(bool ok, const char *what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

bool ConfigstringsUntouched()
{
    for (const auto value : sv.configstrings)
    {
        if (value)
            return false;
    }
    return true;
}

cStaticModel_s g_models[8];
} // namespace

int main()
{
    // Entities 5, 2 and 9 (0-based 4, 1, 8) into sector 3, out of order.
    for (int index : {4, 1, 8})
        CM_AddEntityToNode(&sv.svEntities[index], 3);
    Check(sv.svEntities[1].worldSector == 3 && sv.svEntities[4].worldSector == 3 && sv.svEntities[8].worldSector == 3,
          "each entity records its sector");
    Check(sv.svEntities[1].nextEntityInWorldSector == 5 && sv.svEntities[4].nextEntityInWorldSector == 9
              && sv.svEntities[8].nextEntityInWorldSector == 0,
          "the sector's entities link in entity order");
    Check(ConfigstringsUntouched(), "linking writes no configstring");

    // Static models 6, 0 and 3 into sector 2.
    cm.staticModelList = g_models;
    cm.numStaticModels = 8;
    for (int index : {6, 0, 3})
        CM_AddStaticModelToNode(&g_models[index], 2);
    Check(g_models[0].writable.nextModelInWorldSector == 4 && g_models[3].writable.nextModelInWorldSector == 7
              && g_models[6].writable.nextModelInWorldSector == 0,
          "the sector's static models link in model order");

    if (!g_failures)
        std::printf("cm world links: all checks passed\n");
    return g_failures ? 1 : 0;
}
