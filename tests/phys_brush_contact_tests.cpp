// phys_brush_contact_tests.cpp: physics bodies touching world geometry at
// 64-bit (NOW row 22, #242 items 1 and 2), the frame context the
// collision callbacks read (#243 item 1) and ODE's full contact group
// (#238 item 1).
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
//      brush, and a brushmodel resting on a terrain triangle; and
//      Phys_TestAgainstEntities stopping at the caller's contact capacity
//      (#243 item 2).
//   2: phys_ode: dxPostProcessIslands resets each world's jitter regions.
//   3: phys_ode: Phys_RunFrame hands the inter-body near callback a
//      FrameInfo, as it does the world collision (#243 item 1).
//   4: ODE's ode.cpp: a contact group filled to capacity empties without
//      an ODE assert (#238 item 1).
//   5: ODE's error layer: the handlers Phys_Init installs keep a failed
//      internal assertion from aborting the process and route a hard ODE
//      error through Com_Error (#238 item 2).
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
int g_comWarnings = 0;
int g_comDPrints = 0;
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
WEAK void Com_PrintWarning(int, const char *, ...) { ++g_comWarnings; }
WEAK void Com_DPrintf(int, const char *, ...) { ++g_comDPrints; }

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

    g_materials[0].surfaceFlags = kFloorFlags;
    g_materials[0].contentFlags = kSolid;
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

// Phys_TestAgainstEntities' boundary: three entities in the area, and a
// capsule-box test that always touches, as dCollideSpheres writes one
// contact whatever room is left.
WEAK gentity_s g_entities[MAX_GENTITIES];
WEAK int CM_AreaEntities(const float *, const float *, int *entityList, int maxcount, int)
{
    for (int i = 0; i < 3 && i < maxcount; ++i)
        entityList[i] = i;
    return 3;
}
namespace
{
int g_capsuleCalls = 0;
}  // namespace
WEAK int ODE_CollideCapsuleBox(const float *, const float *, float *, const float *, const float *, float, float,
    int maxc, dContactGeom *contact, int)
{
    ++g_capsuleCalls;
    CHECK(maxc > 0);
    contact->depth = 1.0f;
    return 1;
}

// Two contacts of room and three touching entities: two contacts, each with
// no surface flags, and nothing written past them.
void CheckEntityCapacity()
{
    objInfo input{};
    std::memset(g_contacts, 0x77, sizeof(g_contacts));
    Results results{g_contacts, 0, 2, static_cast<int>(sizeof(dContactGeomExt))};
    Phys_TestAgainstEntities(&input, &results);
    CHECK(results.contactCount == 2);
    CHECK(g_capsuleCalls == 2);
    CHECK(g_contacts[0].surfFlags == 0 && g_contacts[1].surfFlags == 0);
    CHECK(g_contacts[2].surfFlags == 0x77777777);
}

int main()
{
    CheckEntityCapacity();
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

namespace
{
dxWorld g_worlds[PHYS_WORLD_COUNT];
Jitter g_regions[PHYS_WORLD_COUNT][5];
}  // namespace

// dxPostProcessIslands ends each world's step: explosion jitter regions
// (Phys_AddJitterRegion) last exactly one step, and the regions themselves
// and the other worlds' state are left alone.
int main()
{
    for (int w = 0; w < PHYS_WORLD_COUNT; ++w)
    {
        for (int i = 0; i < 5; ++i)
        {
            g_regions[w][i] = {{1.0f * w, 2.0f, 1.0f * i}, 4.0f, 9.0f, 2.0f, 3.0f, 0.5f, 1.5f};
            physGlob.worldData[w].jitterRegions[i] = g_regions[w][i];
        }
        physGlob.world[w] = &g_worlds[w];
        physGlob.worldData[w].numJitterRegions = 3;
    }
    for (int w = 0; w < PHYS_WORLD_COUNT; ++w)
    {
        dxPostProcessIslands(static_cast<PhysWorld>(w));
        CHECK(physGlob.worldData[w].numJitterRegions == 0);
    }
    for (int w = 0; w < PHYS_WORLD_COUNT; ++w)
        CHECK(std::memcmp(physGlob.worldData[w].jitterRegions, g_regions[w], sizeof(g_regions[w])) == 0);
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}

#elif PHYS_TEST_SUBJECT == 3

extern bool physInited;
extern const dvar_t *phys_dumpcontacts;
extern const dvar_t *phys_mcv;
extern const dvar_t *phys_mcv_ragdoll;
extern const dvar_t *phys_csl;
extern const dvar_t *phys_qsi;
void __cdecl ODE_CollideSimpleSpaceWithGeomNoAABBTest(dxSpace *space, dxGeom *geom, void *data);

// Phys_RunFrame's ODE calls. The space collisions only record the context
// they were handed; the step does nothing.
WEAK void Dvar_SetBool(dvar_s *, bool) {}
WEAK void dWorldSetCFM(dWorldID, dReal) {}
WEAK void dWorldSetERP(dWorldID, dReal) {}
WEAK void dWorldSetContactMaxCorrectingVel(dWorldID, dReal) {}
WEAK void dWorldSetContactSurfaceLayer(dWorldID, dReal) {}
WEAK void dWorldSetQuickStepNumIterations(dWorldID, int) {}
WEAK void dWorldSetAutoDisableLinearThreshold(dWorldID, dReal) {}
WEAK void dWorldSetAutoDisableAngularThreshold(dWorldID, dReal) {}
WEAK void dWorldSetAutoDisableTime(dWorldID, dReal) {}
WEAK void dWorldSetGravity(dWorldID, dReal, dReal, dReal) {}
WEAK void dWorldQuickStep(dWorldID, dReal) {}
WEAK void dGeomEnable(dGeomID) {}
WEAK void dGeomDisable(dGeomID) {}
// Phys_NearCallback's calls: the recording collisions never run it.
[[noreturn]] static void NoPairs() { std::exit(4); }
WEAK void *dBodyGetData(dBodyID) { NoPairs(); }
WEAK dBodyID dGeomGetBody(dGeomID) { NoPairs(); }
WEAK int dAreConnectedExcluding(dBodyID, dBodyID, int) { NoPairs(); }
WEAK int dCollide(dGeomID, dGeomID, int, dContactGeom *, int) { NoPairs(); }
WEAK void Phys_ReduceContacts(dxBody *, const ContactList *, ContactList *) { NoPairs(); }
WEAK void Phys_CreateJointForEachContact(ContactList *, dxBody *, dxBody *, const dSurfaceParameters *, PhysWorld)
{
    NoPairs();
}

namespace
{
// The two words a FrameInfo holds, as each collision saw them.
int g_pairContext[2] = {-1, -1};
int g_worldContext[2] = {-1, -1};
int g_pairCalls = 0;
int g_worldCalls = 0;
dxWorld g_world;
dvar_t g_on;
dvar_t g_off;
}  // namespace

WEAK void dSpaceCollide(dSpaceID, void *data, dNearCallback *)
{
    ++g_pairCalls;
    std::memcpy(g_pairContext, data, sizeof(g_pairContext));
}
WEAK void ODE_CollideSimpleSpaceWithGeomNoAABBTest(dxSpace *, dxGeom *, void *data)
{
    ++g_worldCalls;
    std::memcpy(g_worldContext, data, sizeof(g_worldContext));
}

// With phys_interBodyCollision on, the body-vs-body collision reads the same
// {localClientNum, worldIndex} as the body-vs-world one.
int main()
{
    g_on.current.enabled = true;
    physInited = true;
    for (const dvar_t **dvar : {&phys_dumpcontacts, &phys_cfm, &phys_erp, &phys_mcv, &phys_mcv_ragdoll, &phys_csl,
                                &phys_qsi, &phys_autoDisableLinear, &phys_autoDisableAngular, &phys_autoDisableTime,
                                &phys_visibleTris, &phys_gravity})
        *dvar = &g_off;
    phys_interBodyCollision = &g_on;
    physGlob.world[PHYS_WORLD_FX] = &g_world;
    physGlob.worldGeom = reinterpret_cast<dxGeom *>(&g_world);
    Phys_RunFrame(0, PHYS_WORLD_FX, 0.05f);
    CHECK(g_pairCalls == 1);
    CHECK(g_worldCalls == 1);
    CHECK(g_pairContext[0] == 0 && g_pairContext[1] == PHYS_WORLD_FX);
    CHECK(g_worldContext[0] == 0 && g_worldContext[1] == PHYS_WORLD_FX);
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}


#elif PHYS_TEST_SUBJECT == 4

#include <physics/ode/joint.h>

namespace
{
int g_odeAsserts = 0;
dxWorld g_world;
dxJointGroup g_group;
dSurfaceParameters g_surface;
dContactGeom g_contact;
}  // namespace

// ODE's asserts report through dDebug, which ends the process; count them.
WEAK extern "C" void dDebug(int, const char *msg, ...)
{
    std::fprintf(stderr, "ODE assert: %s\n", msg ? msg : "?");
    ++g_odeAsserts;
}
// A contact joint's own setup is joint.cpp's; this check only counts joints.
WEAK void jointInit(dxJoint *) {}
// ODE's global spaces and geoms are built at startup; nothing here frees
// pooled memory or takes a lock.
WEAK bool Pool_Free(poolstorage_t, pooldata_t *, void *) noexcept { std::exit(4); }
WEAK void Sys_EnterCriticalSection(int) {}
WEAK void Sys_LeaveCriticalSection(int) {}

// dJointCreateContact fills a group to ODE_WORLD_MAX_JOINT_COUNT and then
// refuses; emptying the full group asserts nothing and detaches every joint.
int main()
{
    int created = 0;
    while (dJointCreateContact(&g_world, &g_group, &g_surface, &g_contact))
        ++created;
    CHECK(created == ODE_WORLD_MAX_JOINT_COUNT);
    CHECK(g_group.num == ODE_WORLD_MAX_JOINT_COUNT);
    dJointGroupEmpty(&g_group);
    CHECK(g_odeAsserts == 0);
    CHECK(g_group.num == 0);
    CHECK(g_world.nj == 0);
    CHECK(g_world.firstjoint == nullptr);
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}

#elif PHYS_TEST_SUBJECT == 5

#include <physics/ode/joint.h>
#include <physics/ode/quickstep.h>
#include <physics/ode/util.h>

// #243 item 3: ODE_BreakupIslandIfTooBig trimmed an island body by body and
// stopped only when the joint list got small. Two-body joints never satisfy
// ShouldNotRemoveJoint for one removed body alone, so a 15th body plus 74
// joints ran *bodyCount through 0 and wrote body[-1]->tag (and beyond) in
// Release, where iassert(*bodyCount > 0) is compiled out.
//
// Each check passes the body list one past a canary slot, so an indexing
// underflow lands on a body this test owns and fails in every build, not only
// under ASan.

// dxBody's dMass member default-constructs through dMassSetZero; the checks
// never read a mass, so an empty definition keeps the link to util.cpp small.
void dMassSetZero(dMass *) {}

namespace
{
constexpr int kTooManyBodies = ISLAND_MAX_BODY_COUNT + 1;
constexpr int kFirstShapeParallel = ISLAND_MAX_JOINT_COUNT - 1;
constexpr int kFirstShapeJoints = kFirstShapeParallel + 3;
constexpr int kSecondShapeBodies = 13;
constexpr int kSecondShapeJoints = ISLAND_MAX_JOINT_COUNT + 1;

void TwoBodyJoint(dxJoint *joint, dxBody *a, dxBody *b)
{
    *joint = {};
    joint->tag = 1;
    joint->node[0].body = a;
    joint->node[1].body = b;
}

// A world-attached joint (node[1].body == 0) leaves the island with its body.
void OneBodyJoint(dxJoint *joint, dxBody *a)
{
    TwoBodyJoint(joint, a, nullptr);
}

// 15 bodies, 73 joints between body[0] and body[1], and three joints the
// canary owns. Nothing in the body list can remove those 73 from the list, so
// the old loop tagged body[0], then the canary as body[-1]; the canary's
// joints are what stop it there instead of walking further out of the array.
void OneBodyTooMany()
{
    dxBody canary = {};
    dxBody bodies[kTooManyBodies] = {};
    dxBody *slot[kTooManyBodies + 2] = {};
    dxJoint joints[kFirstShapeJoints] = {};
    dxJoint *list[kFirstShapeJoints] = {};

    canary.tag = 1;
    slot[0] = &canary;
    for (int i = 0; i < kTooManyBodies; ++i)
    {
        bodies[i].tag = 1;
        slot[i + 1] = &bodies[i];
    }

    for (int i = 0; i < kFirstShapeParallel; ++i)
    {
        TwoBodyJoint(&joints[i], &bodies[0], &bodies[1]);
        list[i] = &joints[i];
    }
    for (int i = kFirstShapeParallel; i < kFirstShapeJoints; ++i)
    {
        OneBodyJoint(&joints[i], &canary);
        list[i] = &joints[i];
    }

    int bodyCount = kTooManyBodies;
    int jointCount = kFirstShapeJoints;
    ODE_BreakupIslandIfTooBig(&slot[1], &bodyCount, list, &jointCount);

    CHECK(bodyCount == 1);
    CHECK(canary.tag == 1);
    CHECK(bodies[0].tag == 1);
    CHECK(jointCount <= ISLAND_MAX_JOINT_COUNT);
    for (int i = 0; i < jointCount; ++i)
        CHECK(list[i]->tag == 1);
}

// 13 bodies stay inside the body cap, but 73 joints between body[0] and
// body[1] keep the list too long. Only removing both body[12] and body[11]
// drops the two joints that connect them, and that needs the removal
// predicate to see every body removed so far rather than just the last one.
void JointsAcrossRemovedBodies()
{
    dxBody bodies[kSecondShapeBodies] = {};
    dxBody *slot[kSecondShapeBodies] = {};
    dxJoint joints[kSecondShapeJoints] = {};
    dxJoint *list[kSecondShapeJoints] = {};

    for (int i = 0; i < kSecondShapeBodies; ++i)
    {
        bodies[i].tag = 1;
        slot[i] = &bodies[i];
    }

    constexpr int kParallel = ISLAND_MAX_JOINT_COUNT - 1;
    for (int i = 0; i < kParallel; ++i)
    {
        TwoBodyJoint(&joints[i], &bodies[0], &bodies[1]);
        list[i] = &joints[i];
    }
    for (int i = kParallel; i < kSecondShapeJoints; ++i)
    {
        TwoBodyJoint(&joints[i], &bodies[11], &bodies[12]);
        list[i] = &joints[i];
    }

    int bodyCount = kSecondShapeBodies;
    int jointCount = kSecondShapeJoints;
    ODE_BreakupIslandIfTooBig(slot, &bodyCount, list, &jointCount);

    CHECK(bodyCount == 11);
    CHECK(jointCount == kParallel);
}

}  // namespace

int main()
{
    OneBodyTooMany();
    JointsAcrossRemovedBodies();
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}

#elif PHYS_TEST_SUBJECT == 6

#include <ode/error.h>
#include <setjmp.h>

namespace
{
int g_odeErrors = 0;
jmp_buf g_errorJump;
}  // namespace

// The engine's error path does not return; Com_Error longjmps out of it.
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error: %s\n", fmt ? fmt : "?");
    ++g_odeErrors;
    longjmp(g_errorJump, 1);
}

int main()
{
    // Master never installs a handler, so this symbol does not exist there
    // and the check does not link; at head it is what Phys_Init calls.
    Phys_InstallOdeErrorHandlers();
    CHECK(dGetErrorHandler() != nullptr);
    CHECK(dGetDebugHandler() != nullptr);
    CHECK(dGetMessageHandler() != nullptr);

    // A message is chatter: it must reach the debug log and return.
    dMessage(d_ERR_UASSERT, "message %d", 7);
    CHECK(g_comDPrints == 1);

    // A failed internal assertion must be reported and must NOT abort the
    // process (error.cpp aborts when no handler is installed).
    dDebug(d_ERR_IASSERT, "assertion %d", 3);
    CHECK(g_comWarnings == 1);

    // A hard ODE error must go through Com_Error, not exit(1).
    if (setjmp(g_errorJump) == 0)
    {
        dError(d_ERR_UASSERT, "fatal %d", 9);
        std::fprintf(stderr, "dError returned instead of dropping\n");
        ++g_failures;
    }
    else
    {
        CHECK(g_odeErrors == 1);
    }

    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else
        std::fprintf(stderr, "ode error policy: all checks passed\n");
    return g_failures ? 1 : 0;
}

#endif
