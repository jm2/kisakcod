// phys_brush_contact_tests.cpp: physics bodies touching world geometry at
// 64-bit (NOW row 22, #242 items 1 and 2).
//
// The brush callbacks decoded their contexts (InputOutput, BrushBrushData,
// BrushTrimeshData) as ILP32 words and byte offsets, and the post-step
// jitter reset wrote through an ILP32 alias of physGlob. Each check drives
// the production code at 64-bit and fails on the old code.
//
// One source, two executables (PHYS_TEST_SUBJECT), so the collision checks
// do not link phys_ode.cpp:
//   1: phys_world_collision and the brush colliders: CM_TestGeomInLeaf with
//      a box, a brush and a brushmodel resting 0.5 units deep in a world
//      brush, and a brushmodel resting on a terrain triangle.
//   2: phys_ode: dxPostProcessIslands resets each world's jitter regions.
// The engine boundary is weak: the engine TUs replace the stubs they define.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <physics/phys_local.h>
#include <qcommon/qcommon.h>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
}  // namespace

#define WEAK __attribute__((weak))

// An engine assert fails the test: the fixed code never asserts here.
WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", filename ? filename : "?", line);
    std::exit(3);
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}

#if PHYS_TEST_SUBJECT == 1

WEAK clipMap_t cm;
WEAK const dvar_t *phys_drawCollisionWorld;
WEAK const dvar_t *phys_drawCollisionObj;
WEAK char *va(const char *, ...) { return const_cast<char *>(""); }
// ODE's box-vs-terrain collider; no check collides a box with terrain.
WEAK int dCollideBoxTriangleList(const uint16_t *, const float (*)[3], int, const float *, const float *,
    const float *, const float *, int, dContactGeom *, int)
{
    std::exit(4);
}

namespace
{
constexpr int kSolid = 1;
constexpr int kFloorFlags = 0x100000;
constexpr int kTerrainFlags = 0x200000;
constexpr float kDepth = 0.5f;   // how deep each body sits in the floor
constexpr float kHalf = 4.0f;    // the body is an 8-unit cube

dvar_t g_off;
dmaterial_t g_materials[2];
// Around each axial side (0 -X, 1 +X, 2 -Y, 3 +Y, 4 -Z, 5 +Z), the four
// sides it meets, in winding order: for axis a, b = a + 1 and c = a + 2.
uint8_t g_cubeAdjacency[24] = {2, 4, 3, 5, 2, 4, 3, 5, 4, 0, 5, 1, 4, 0, 5, 1, 0, 2, 1, 3, 0, 2, 1, 3};
cbrush_t g_brushes[2];  // [0] the world floor, [1] the body's own brush
uint16_t g_floorBrush[1] = {0};
uint16_t g_bodyBrush[1] = {1};
cLeafBrushNode_s g_nodes[3];  // node 0 is the "no node" index
cmodel_t g_bodyModel;
CollisionAabbTree g_aabbTree;
CollisionPartition g_partition;
uint16_t g_triIndices[3] = {0, 2, 1};
float g_verts[3][3] = {{-64.0f, -64.0f, 0.0f}, {64.0f, -64.0f, 0.0f}, {0.0f, 64.0f, 0.0f}};
int g_partitionStamps[1];
dContactGeomExt g_contacts[16];

bool Near(float a, float b) { return std::fabs(a - b) < 1.0e-3f; }

void Cube(cbrush_t *brush, float minZ, float maxZ, float half)
{
    *brush = {};
    for (int i = 0; i < 2; ++i)
    {
        brush->mins[i] = -half;
        brush->maxs[i] = half;
    }
    brush->mins[2] = minZ;
    brush->maxs[2] = maxZ;
    brush->contents = kSolid;
    brush->baseAdjacentSide = g_cubeAdjacency;
    for (int side = 0; side < 6; ++side)
    {
        brush->firstAdjacentSideOffsets[side & 1][side >> 1] = static_cast<int16_t>(4 * side);
        brush->edgeCount[side & 1][side >> 1] = 4;
    }
}

void LeafNode(cLeafBrushNode_s *node, uint16_t *brushes)
{
    *node = {};
    node->leafBrushCount = 1;
    node->contents = kSolid;
    node->data.leaf.brushes = brushes;
}

void BuildWorld()
{
    g_off.current.enabled = false;
    phys_drawCollisionWorld = &g_off;
    phys_drawCollisionObj = &g_off;

    std::strcpy(g_materials[0].material, "floor");
    g_materials[0].surfaceFlags = kFloorFlags;
    g_materials[0].contentFlags = kSolid;
    std::strcpy(g_materials[1].material, "terrain");
    g_materials[1].surfaceFlags = kTerrainFlags;
    g_materials[1].contentFlags = kSolid;

    Cube(&g_brushes[0], -16.0f, 0.0f, 64.0f);  // axial material 0: floor
    Cube(&g_brushes[1], -kHalf, kHalf, kHalf);
    LeafNode(&g_nodes[1], g_floorBrush);
    LeafNode(&g_nodes[2], g_bodyBrush);

    g_bodyModel = {};
    for (int i = 0; i < 3; ++i)
    {
        g_bodyModel.mins[i] = -kHalf;
        g_bodyModel.maxs[i] = kHalf;
    }
    g_bodyModel.radius = kHalf * 1.7320508f;
    g_bodyModel.leaf.leafBrushNode = 2;

    g_aabbTree = {};
    g_aabbTree.halfSize[0] = g_aabbTree.halfSize[1] = 64.0f;
    g_aabbTree.halfSize[2] = 1.0f;
    g_aabbTree.materialIndex = 1;  // terrain
    g_partition = {};
    g_partition.triCount = 1;

    cm.materials = g_materials;
    cm.numMaterials = 2;
    cm.brushes = g_brushes;
    cm.leafbrushNodes = g_nodes;
    cm.aabbTrees = &g_aabbTree;
    cm.partitions = &g_partition;
    cm.partitionCount = 1;
    cm.triIndices = g_triIndices;
    cm.verts = g_verts;
}

cLeaf_t Leaf(bool terrain)
{
    cLeaf_t leaf{};
    leaf.brushContents = terrain ? 0 : kSolid;
    leaf.terrainContents = terrain ? kSolid : 0;
    leaf.collAabbCount = terrain ? 1 : 0;
    leaf.leafBrushNode = 1;
    for (int i = 0; i < 3; ++i)
    {
        leaf.mins[i] = -128.0f;
        leaf.maxs[i] = 128.0f;
    }
    return leaf;
}

// A body centred kHalf - kDepth above the floor, unrotated, as
// dCollideWorldGeom describes it.
objInfo Body(PhysicsGeomType type)
{
    objInfo input{};
    input.clipMask = kSolid;
    input.type = type;
    input.threadInfo.checkcount.global = 1;
    input.threadInfo.checkcount.partitions = g_partitionStamps;
    input.pos[2] = kHalf - kDepth;
    for (int i = 0; i < 3; ++i)
    {
        input.R[i][i] = input.RTransposed[i][i] = 1.0f;
        input.bodyCenter[i] = input.pos[i];
        input.bounds[0][i] = input.pos[i] - (kHalf * 1.7320508f + 1.0f);
        input.bounds[1][i] = input.pos[i] + (kHalf * 1.7320508f + 1.0f);
    }
    input.radius = (kHalf * 1.7320508f + 1.0f) * 1.7320508f;
    if (type == PHYS_GEOM_BOX)
        input.u.sideExtents[0] = input.u.sideExtents[1] = input.u.sideExtents[2] = kHalf;
    else if (type == PHYS_GEOM_BRUSH)
        input.u.brush = &g_brushes[1];
    else
        input.u.brushModel = &g_bodyModel;
    return input;
}

// Runs the leaf test: one contact per bottom corner of the body, kDepth
// deep, with the touched surface's flags. The world is o1 in
// dCollideWorldGeom, so the normal points from the body into the world
// (moving o1 along it by the depth separates the pair).
void CheckContacts(const char *what, PhysicsGeomType type, bool terrain, int flags)
{
    std::memset(g_contacts, 0, sizeof(g_contacts));
    g_partitionStamps[0] = 0;
    Results results{g_contacts, 0, 16, static_cast<int>(sizeof(dContactGeomExt))};
    cLeaf_t leaf = Leaf(terrain);
    const objInfo input = Body(type);
    CM_TestGeomInLeaf(&leaf, &input, &results);

    const int before = g_failures;
    unsigned corners = 0;
    CHECK(results.contactCount == 4);
    for (int i = 0; i < results.contactCount && i < 16; ++i)
    {
        const dContactGeom &c = g_contacts[i].contact;
        CHECK(Near(c.normal[0], 0.0f) && Near(c.normal[1], 0.0f) && Near(c.normal[2], -1.0f));
        CHECK(Near(c.depth, kDepth));
        CHECK(Near(std::fabs(c.pos[0]), kHalf) && Near(std::fabs(c.pos[1]), kHalf));
        CHECK(c.pos[2] >= -kDepth - 1.0e-3f && c.pos[2] <= 1.0e-3f);
        CHECK(g_contacts[i].surfFlags == flags);
        corners |= 1u << ((c.pos[0] > 0.0f) + 2 * (c.pos[1] > 0.0f));
    }
    CHECK(corners == 0xFu);
    if (g_failures != before)
        std::fprintf(stderr, "%s: %d contacts\n", what, results.contactCount);
}
}  // namespace

int main()
{
    BuildWorld();
    CheckContacts("box on brush", PHYS_GEOM_BOX, false, kFloorFlags);
    CheckContacts("brush on brush", PHYS_GEOM_BRUSH, false, kFloorFlags);
    CheckContacts("brushmodel on brush", PHYS_GEOM_BRUSHMODEL, false, kFloorFlags);
    CheckContacts("brushmodel on terrain", PHYS_GEOM_BRUSHMODEL, true, kTerrainFlags);
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}

#elif PHYS_TEST_SUBJECT == 2

WEAK int g_phys_msecStep[3];
WEAK int g_phys_minMsecStep[3];
WEAK int g_phys_maxMsecStep[3];
WEAK void dJointGroupEmpty(dJointGroupID) {}
// The worlds have no bodies, so nothing reaches ODE's body and geom calls.
[[noreturn]] static void NoBodies() { std::exit(4); }
WEAK void *dBodyGetData(dBodyID) { NoBodies(); }
WEAK const dReal *dBodyGetPosition(dBodyID) { NoBodies(); }
WEAK int dBodyIsEnabled(dBodyID) { NoBodies(); }
WEAK dGeomID dGeomGetBodyNext(dGeomID) { NoBodies(); }
WEAK int dGeomGetClass(dGeomID) { NoBodies(); }
WEAK void dGeomMoved(dGeomID) { NoBodies(); }
WEAK dGeomID dGeomTransformGetGeom(dGeomID) { NoBodies(); }
WEAK dxWorld *ODE_BodyGetWorld(dxBody *) { NoBodies(); }

// dxPostProcessIslands ends each world's step: explosion jitter regions
// (Phys_AddJitterRegion) last exactly one step, and the regions themselves
// and the other worlds' state are left alone.
int main()
{
    static dxWorld worlds[PHYS_WORLD_COUNT];
    static Jitter regions[PHYS_WORLD_COUNT][5];
    for (int w = 0; w < PHYS_WORLD_COUNT; ++w)
    {
        for (Jitter &jitter : regions[w])
            jitter = {{1.0f * w, 2.0f, 3.0f}, 4.0f, 9.0f, 2.0f, 3.0f, 0.5f, 1.5f};
        physGlob.world[w] = &worlds[w];
        physGlob.worldData[w].numJitterRegions = 3;
        std::memcpy(physGlob.worldData[w].jitterRegions, regions[w], sizeof(regions[w]));
    }
    for (int w = 0; w < PHYS_WORLD_COUNT; ++w)
    {
        dxPostProcessIslands(static_cast<PhysWorld>(w));
        CHECK(physGlob.worldData[w].numJitterRegions == 0);
    }
    for (int w = 0; w < PHYS_WORLD_COUNT; ++w)
        CHECK(std::memcmp(physGlob.worldData[w].jitterRegions, regions[w], sizeof(regions[w])) == 0);
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}

#endif
