#include <database/db_validation.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;

void Expect(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct TestWorldAabbNode
{
    float mins[3] = {};
    float maxs[3] = {};
    std::uint16_t childCount = 0;
    std::uint16_t surfaceCount = 0;
    std::uint16_t startSurfIndex = 0;
    std::uint16_t surfaceCountNoDecal = 0;
    std::uint16_t startSurfIndexNoDecal = 0;
    std::uint16_t smodelIndexCount = 0;
    std::int32_t childrenOffset = 0;
};

struct TestDiskWorldAabbNode
{
    std::uint32_t firstSurface = 0;
    std::uint32_t surfaceCount = 0;
    std::uint32_t childCount = 0;
};

struct TestSurfaceCollisionAabb
{
    std::uint16_t mins[3] = {};
    std::uint16_t maxs[3] = {};
};

struct TestSurfaceCollisionNode
{
    TestSurfaceCollisionAabb aabb;
    std::uint16_t childBeginIndex = 0;
    std::uint16_t childCount = 0;
};

struct TestSurfaceCollisionLeaf
{
    std::uint16_t triangleBeginIndex = 0;
};

struct TestRigidVertList
{
    std::uint16_t vertCount = 0;
    std::uint16_t triOffset = 0;
    std::uint16_t triCount = 0;
    const void *collisionTree = nullptr;
    std::uint16_t boneOffset = 0;
};

struct TestSurfaceCacheLayout
{
    std::uint16_t vertCount = 0;
    std::uint16_t triCount = 0;
    std::uint16_t baseVertIndex = 0;
    std::uint16_t baseTriIndex = 0;
};

struct TestPackedVertexPayload
{
    float xyz[3] = {};
    float binormalSign = 1.0f;
};

struct TestAnimMatPayload
{
    float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float trans[3] = {};
    float transWeight = 2.0f;
};

struct TestBoneInfoPayload
{
    float bounds[2][3] = {};
    float offset[3] = {};
    float radiusSquared = 0.0f;
};

struct TestSurfaceCollisionTree
{
    std::array<TestSurfaceCollisionNode, 4> nodes = {};
    std::array<TestSurfaceCollisionLeaf, 4> leafs = {};
};

struct TestBrushPlane
{
    float normal[3] = {};
    float dist = 0.0f;
};

struct TestBrushSide
{
    TestBrushPlane *plane = nullptr;
    std::uint32_t materialNum = 0;
    std::int16_t firstAdjacentSideOffset = 0;
    std::uint8_t edgeCount = 0;
};

struct TestBrushWrapper
{
    float mins[3] = {};
    float maxs[3] = {};
    std::uint32_t numsides = 0;
    TestBrushSide *sides = nullptr;
    std::int16_t axialMaterialNum[2][3] = {};
    std::uint8_t *baseAdjacentSide = nullptr;
    std::int16_t firstAdjacentSideOffsets[2][3] = {};
    std::uint8_t edgeCount[2][3] = {};
    std::int32_t totalEdgeCount = 0;
    TestBrushPlane *planes = nullptr;
};

struct TestPhysMass
{
    float centerOfMass[3] = {};
    float momentsOfInertia[3] = {};
    float productsOfInertia[3] = {};
};

struct TestPhysGeomInfo
{
    TestBrushWrapper *brush = nullptr;
    std::int32_t type = 0;
    float orientation[3][3] = {};
    float offset[3] = {};
    float halfLengths[3] = {};
};

struct TestPhysGeomList
{
    std::uint32_t count = 0;
    TestPhysGeomInfo *geoms = nullptr;
    TestPhysMass mass = {};
};

struct TestPhysicsFixture
{
    std::array<TestBrushPlane, 2> planes = {};
    std::array<TestBrushSide, 2> sides = {};
    std::array<std::uint8_t, 8> adjacency = {};
    TestBrushWrapper brush = {};
    std::array<TestPhysGeomInfo, 3> geoms = {};
    TestPhysGeomList list = {};
};

struct TestClipPlane
{
    float normal[3] = {};
    float dist = 0.0f;
    std::uint8_t type = 0;
    std::uint8_t signbits = 0;
    std::uint8_t pad[2] = {};
};

struct TestClipBrushSide
{
    TestClipPlane *plane = nullptr;
    std::uint32_t materialNum = 0;
    std::int16_t firstAdjacentSideOffset = 0;
    std::uint8_t edgeCount = 0;
};

struct TestClipBrush
{
    float mins[3] = {};
    std::int32_t contents = 0;
    float maxs[3] = {};
    std::uint32_t numsides = 0;
    TestClipBrushSide *sides = nullptr;
    std::int16_t axialMaterialNum[2][3] = {};
    std::uint8_t *baseAdjacentSide = nullptr;
    std::int16_t firstAdjacentSideOffsets[2][3] = {};
    std::uint8_t edgeCount[2][3] = {};
};

struct TestClipMaterial
{
    std::array<std::uint8_t, 72> bytes = {};
};

struct TestClipMap
{
    std::int32_t planeCount = 0;
    TestClipPlane *planes = nullptr;
    std::uint32_t numMaterials = 0;
    TestClipMaterial *materials = nullptr;
    std::uint32_t numBrushSides = 0;
    TestClipBrushSide *brushsides = nullptr;
    std::uint32_t numBrushEdges = 0;
    std::uint8_t *brushEdges = nullptr;
    std::uint16_t numBrushes = 0;
    TestClipBrush *brushes = nullptr;
};

struct TestClipMapFixture
{
    std::array<TestClipPlane, 4> planes = {};
    std::array<TestClipMaterial, 3> materials = {};
    std::array<TestClipBrushSide, 4> sides = {};
    std::array<std::uint8_t, 5> edges = {};
    std::array<TestClipBrush, 2> brushes = {};
    TestClipMap map = {};
};

struct TestPathLink
{
    float fDist = 0.0f;
    std::uint16_t nodeNum = 0;
};

struct TestPathTree;

struct TestPathTreeLeaf
{
    std::int32_t nodeCount = 0;
    std::uint16_t *nodes = nullptr;
};

struct TestPathTreeInfo
{
    TestPathTree *child[2] = {};
    TestPathTreeLeaf s = {};
};

struct TestPathTree
{
    std::int32_t axis = -1;
    float dist = 0.0f;
    TestPathTreeInfo u = {};
};

struct TestPathNodeConstant
{
    std::int32_t type = 0;
    float vOrigin[3] = {};
    float fAngle = 0.0f;
    float forward[2] = {};
    float fRadius = 0.0f;
    float minUseDistSq = 0.0f;
    std::int16_t wOverlapNode[2] = {-1, -1};
    std::int16_t wChainParent = -1;
};

struct TestPathNode
{
    TestPathNodeConstant constant = {};
};

struct TestPathBaseNode
{
    std::uint8_t value = 0;
};

struct TestPathData
{
    std::uint32_t nodeCount = 0;
    TestPathNode *nodes = nullptr;
    TestPathBaseNode *basenodes = nullptr;
    std::uint32_t chainNodeCount = 0;
    std::uint16_t *chainNodeForNode = nullptr;
    std::uint16_t *nodeForChainNode = nullptr;
    std::int32_t visBytes = 0;
    std::uint8_t *pathVis = nullptr;
    std::int32_t nodeTreeCount = 0;
    TestPathTree *nodeTree = nullptr;
};

struct TestPathTreeFixture
{
    std::array<TestPathTree, 7> trees = {};
    std::array<std::uint16_t, 6> pathNodeIndices = {};
};

void PopulateValidPathTreeFixture(TestPathTreeFixture *fixture)
{
    *fixture = {};
    fixture->trees[0].axis = 0;
    fixture->trees[0].dist = 4.0f;
    fixture->trees[0].u.child[0] = &fixture->trees[4];
    fixture->trees[0].u.child[1] = &fixture->trees[1];
    fixture->trees[1].axis = 1;
    fixture->trees[1].dist = -2.0f;
    fixture->trees[1].u.child[0] = &fixture->trees[5];
    fixture->trees[1].u.child[1] = &fixture->trees[6];
    fixture->trees[4].axis = 2;
    fixture->trees[4].dist = 1.0f;
    fixture->trees[4].u.child[0] = &fixture->trees[2];
    fixture->trees[4].u.child[1] = &fixture->trees[3];

    constexpr std::uint32_t leafTreeIndices[] = {2, 3, 5, 6};
    constexpr std::uint32_t leafOffsets[] = {0, 2, 3, 4};
    constexpr std::uint32_t leafCounts[] = {2, 1, 1, 2};
    for (std::uint32_t pathNodeIndex = 0;
        pathNodeIndex < fixture->pathNodeIndices.size();
        ++pathNodeIndex)
    {
        fixture->pathNodeIndices[pathNodeIndex] =
            static_cast<std::uint16_t>(pathNodeIndex);
    }
    for (std::uint32_t leaf = 0; leaf < 4; ++leaf)
    {
        TestPathTree &tree = fixture->trees[leafTreeIndices[leaf]];
        tree.axis = -1;
        tree.u.s.nodeCount = static_cast<std::int32_t>(leafCounts[leaf]);
        tree.u.s.nodes = &fixture->pathNodeIndices[leafOffsets[leaf]];
    }
}

void PopulatePathTreeDepthFixture(
    std::uint32_t leafDepth,
    std::vector<TestPathTree> *trees,
    std::vector<std::uint16_t> *pathNodeIndices)
{
    trees->assign(leafDepth * 2u - 1u, TestPathTree{});
    pathNodeIndices->resize(leafDepth);
    const std::uint32_t internalCount = leafDepth - 1u;
    for (std::uint32_t internal = 0;
        internal < internalCount;
        ++internal)
    {
        TestPathTree &tree = (*trees)[internal];
        tree.axis = static_cast<std::int32_t>(internal % 3u);
        tree.dist = static_cast<float>(internal);
        tree.u.child[0] = internal + 1u < internalCount
            ? &(*trees)[internal + 1u]
            : &(*trees)[internalCount];
        tree.u.child[1] = &(*trees)[internalCount + 1u + internal];
    }
    for (std::uint32_t leaf = 0; leaf < leafDepth; ++leaf)
    {
        (*pathNodeIndices)[leaf] = static_cast<std::uint16_t>(leaf);
        TestPathTree &tree = (*trees)[internalCount + leaf];
        tree.axis = -1;
        tree.u.s.nodeCount = 1;
        tree.u.s.nodes = &(*pathNodeIndices)[leaf];
    }
}

struct TestGfxAabbTree
{
    std::uint8_t value = 0;
};

struct TestGfxImage
{
    std::uint8_t value = 0;
};

struct TestGfxTexture
{
    std::uint32_t value = 0;
};

struct TestGfxCullGroup
{
    float mins[3] = {};
    float maxs[3] = {};
    std::int32_t surfaceCount = 0;
    std::int32_t startSurfIndex = -1;
};

struct TestGfxReflectionProbe
{
    float origin[3] = {};
    TestGfxImage *reflectionImage = nullptr;
};

struct TestGfxCell;

struct TestDpvsPlane
{
    float coeffs[4] = {};
    std::uint8_t side[3] = {};
};

struct TestGfxPortal
{
    TestDpvsPlane plane = {};
    TestGfxCell *cell = nullptr;
    float (*vertices)[3] = nullptr;
    std::uint8_t vertexCount = 0;
    float hullAxis[2][3] = {};
};

struct TestGfxCell
{
    float mins[3] = {};
    float maxs[3] = {};
    std::int32_t aabbTreeCount = 0;
    TestGfxAabbTree *aabbTree = nullptr;
    std::int32_t portalCount = 0;
    TestGfxPortal *portals = nullptr;
    std::int32_t cullGroupCount = 0;
    std::int32_t *cullGroups = nullptr;
    std::uint8_t reflectionProbeCount = 0;
    std::uint8_t *reflectionProbes = nullptr;
};

struct TestGfxWorldDpvsPlanes
{
    std::int32_t cellCount = 0;
};

struct TestGfxWorldDpvs
{
    std::uint32_t staticSurfaceCount = 0;
    std::uint32_t staticSurfaceCountNoDecal = 0;
    std::uint16_t *sortedSurfIndex = nullptr;
    TestGfxCullGroup *cullGroups = nullptr;
};

struct TestGfxWorld
{
    std::int32_t surfaceCount = 0;
    std::int32_t cullGroupCount = 0;
    std::uint32_t reflectionProbeCount = 0;
    TestGfxReflectionProbe *reflectionProbes = nullptr;
    TestGfxTexture *reflectionProbeTextures = nullptr;
    TestGfxWorldDpvsPlanes dpvsPlanes = {};
    TestGfxCell *cells = nullptr;
    TestGfxWorldDpvs dpvs = {};
};

struct TestGfxWorldFixture
{
    std::array<TestGfxAabbTree, 4> aabbTrees = {};
    std::array<TestGfxPortal, 4> portals = {};
    float vertices[4][4][3] = {};
    std::array<std::int32_t, 4> cellCullGroups = {};
    std::array<std::uint8_t, 4> cellReflectionProbes = {};
    std::array<TestGfxCell, 4> cells = {};
    std::array<TestGfxCullGroup, 3> cullGroups = {};
    std::array<TestGfxReflectionProbe, 3> reflectionProbes = {};
    std::array<TestGfxImage, 3> reflectionImages = {};
    std::array<TestGfxTexture, 3> reflectionProbeTextures = {};
    std::array<std::uint16_t, 3> sortedSurfIndex = {};
    TestGfxWorld world = {};
};

void PopulateValidGfxWorldFixture(TestGfxWorldFixture *fixture)
{
    *fixture = {};
    constexpr std::uint32_t targetCells[] = {1, 0, 3, 3};
    for (std::uint32_t index = 0;
        index < fixture->cells.size();
        ++index)
    {
        TestGfxCell &cell = fixture->cells[index];
        for (std::uint32_t axis = 0; axis < 3; ++axis)
        {
            cell.mins[axis] = -4.0f - static_cast<float>(index);
            cell.maxs[axis] = 4.0f + static_cast<float>(index);
        }
        cell.aabbTreeCount = 1;
        cell.aabbTree = &fixture->aabbTrees[index];
        cell.portalCount = 1;
        cell.portals = &fixture->portals[index];
        cell.cullGroupCount = 1;
        cell.cullGroups = &fixture->cellCullGroups[index];
        fixture->cellCullGroups[index] =
            static_cast<std::int32_t>(index % fixture->cullGroups.size());
        cell.reflectionProbeCount = 1;
        cell.reflectionProbes = &fixture->cellReflectionProbes[index];
        fixture->cellReflectionProbes[index] =
            static_cast<std::uint8_t>(
                index % fixture->reflectionProbes.size());

        TestGfxPortal &portal = fixture->portals[index];
        portal.cell = &fixture->cells[targetCells[index]];
        portal.plane.coeffs[0] = 1.0f;
        portal.plane.coeffs[1] = -1.0f;
        portal.plane.coeffs[3] = 2.0f;
        portal.plane.side[0] = 12;
        portal.plane.side[1] = 4;
        portal.plane.side[2] = 8;
        portal.vertices = fixture->vertices[index];
        portal.vertexCount = 4;
        portal.hullAxis[0][0] = 1.0f;
        portal.hullAxis[1][1] = 1.0f;
        const float z = static_cast<float>(index);
        portal.vertices[0][0] = -1.0f;
        portal.vertices[0][1] = -1.0f;
        portal.vertices[0][2] = z;
        portal.vertices[1][0] = 1.0f;
        portal.vertices[1][1] = -1.0f;
        portal.vertices[1][2] = z;
        portal.vertices[2][0] = 1.0f;
        portal.vertices[2][1] = 1.0f;
        portal.vertices[2][2] = z;
        portal.vertices[3][0] = -1.0f;
        portal.vertices[3][1] = 1.0f;
        portal.vertices[3][2] = z;
    }

    fixture->world.cullGroupCount =
        static_cast<std::int32_t>(fixture->cullGroups.size());
    fixture->world.surfaceCount =
        static_cast<std::int32_t>(fixture->sortedSurfIndex.size());
    fixture->world.reflectionProbeCount =
        static_cast<std::uint32_t>(fixture->reflectionProbes.size());
    fixture->world.reflectionProbes = fixture->reflectionProbes.data();
    fixture->world.reflectionProbeTextures =
        fixture->reflectionProbeTextures.data();
    fixture->world.dpvsPlanes.cellCount =
        static_cast<std::int32_t>(fixture->cells.size());
    fixture->world.cells = fixture->cells.data();
    fixture->world.dpvs.staticSurfaceCount =
        static_cast<std::uint32_t>(fixture->sortedSurfIndex.size());
    fixture->world.dpvs.staticSurfaceCountNoDecal = 0;
    fixture->world.dpvs.sortedSurfIndex =
        fixture->sortedSurfIndex.data();
    fixture->world.dpvs.cullGroups = fixture->cullGroups.data();
    for (std::uint32_t index = 0;
        index < fixture->cullGroups.size();
        ++index)
    {
        TestGfxCullGroup &group = fixture->cullGroups[index];
        for (std::uint32_t axis = 0; axis < 3; ++axis)
        {
            group.mins[axis] = -8.0f - static_cast<float>(index);
            group.maxs[axis] = 8.0f + static_cast<float>(index);
        }
        group.surfaceCount = 1;
        group.startSurfIndex = static_cast<std::int32_t>(index);
        fixture->sortedSurfIndex[index] =
            static_cast<std::uint16_t>(index);

        TestGfxReflectionProbe &probe = fixture->reflectionProbes[index];
        probe.origin[0] = static_cast<float>(index);
        probe.origin[1] = static_cast<float>(index) + 1.0f;
        probe.origin[2] = static_cast<float>(index) + 2.0f;
        probe.reflectionImage = &fixture->reflectionImages[index];
    }
}

void PopulateValidClipMapFixture(TestClipMapFixture *fixture)
{
    *fixture = {};
    fixture->planes[0].normal[0] = 1.0f;
    fixture->planes[0].type = 0;
    fixture->planes[1].normal[0] = -1.0f;
    fixture->planes[1].type = 3;
    fixture->planes[1].signbits = 1;
    fixture->planes[2].normal[1] = 1.0f;
    fixture->planes[2].type = 1;

    TestClipBrush &first = fixture->brushes[0];
    first.mins[0] = first.mins[1] = first.mins[2] = -2.0f;
    first.maxs[0] = first.maxs[1] = first.maxs[2] = 2.0f;
    first.contents = 1;
    first.numsides = 2;
    first.sides = fixture->sides.data();
    first.baseAdjacentSide = fixture->edges.data();
    first.edgeCount[0][0] = 1;
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            if (axis != 0 || direction != 0)
                first.firstAdjacentSideOffsets[direction][axis] = 1;
        }
    }
    fixture->sides[0].plane = &fixture->planes[0];
    fixture->sides[0].materialNum = 1;
    fixture->sides[0].firstAdjacentSideOffset = 1;
    fixture->sides[0].edgeCount = 1;
    fixture->sides[1].plane = &fixture->planes[0];
    fixture->sides[1].firstAdjacentSideOffset = 2;
    fixture->edges[0] = 0;
    fixture->edges[1] = 7;

    TestClipBrush &second = fixture->brushes[1];
    second.mins[0] = second.mins[1] = second.mins[2] = -1.0f;
    second.maxs[0] = second.maxs[1] = second.maxs[2] = 1.0f;
    second.contents = 2;
    second.numsides = 1;
    second.sides = &fixture->sides[2];
    second.baseAdjacentSide = &fixture->edges[2];
    fixture->sides[2].plane = &fixture->planes[2];
    fixture->sides[2].firstAdjacentSideOffset = 0;
    fixture->sides[2].edgeCount = 1;
    fixture->edges[2] = 6;

    fixture->map.planeCount = 3;
    fixture->map.planes = fixture->planes.data();
    fixture->map.numMaterials = 2;
    fixture->map.materials = fixture->materials.data();
    fixture->map.numBrushSides = 3;
    fixture->map.brushsides = fixture->sides.data();
    fixture->map.numBrushEdges = 3;
    fixture->map.brushEdges = fixture->edges.data();
    fixture->map.numBrushes = 2;
    fixture->map.brushes = fixture->brushes.data();
}

TestClipBrush ValidClipMapBoxBrush(bool sourceSentinel)
{
    TestClipBrush brush = {};
    brush.contents = -1;
    const float maximumFloat = (std::numeric_limits<float>::max)();
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        brush.mins[axis] = sourceSentinel ? maximumFloat : -1.0f;
        brush.maxs[axis] = sourceSentinel ? -maximumFloat : 1.0f;
        for (std::uint32_t direction = 0; direction < 2; ++direction)
            brush.axialMaterialNum[direction][axis] = -1;
    }
    return brush;
}

void PopulateValidPhysicsFixture(TestPhysicsFixture *fixture)
{
    *fixture = {};
    fixture->brush.mins[0] = -1.0f;
    fixture->brush.mins[1] = -2.0f;
    fixture->brush.mins[2] = -3.0f;
    fixture->brush.maxs[0] = 1.0f;
    fixture->brush.maxs[1] = 2.0f;
    fixture->brush.maxs[2] = 3.0f;
    fixture->brush.numsides =
        static_cast<std::uint32_t>(fixture->sides.size());
    fixture->brush.sides = fixture->sides.data();
    fixture->brush.baseAdjacentSide = fixture->adjacency.data();
    fixture->brush.totalEdgeCount =
        static_cast<std::int32_t>(fixture->adjacency.size());
    fixture->brush.planes = fixture->planes.data();

    std::uint8_t edgeIndex = 0;
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            fixture->brush.firstAdjacentSideOffsets[direction][axis] =
                edgeIndex;
            fixture->brush.edgeCount[direction][axis] = 1;
            fixture->adjacency[edgeIndex] = edgeIndex;
            ++edgeIndex;
        }
    }
    for (std::size_t sideIndex = 0;
        sideIndex < fixture->sides.size();
        ++sideIndex)
    {
        fixture->planes[sideIndex].normal[sideIndex] = 1.0f;
        fixture->planes[sideIndex].dist =
            1.0f + static_cast<float>(sideIndex);
        fixture->sides[sideIndex].plane = &fixture->planes[sideIndex];
        fixture->sides[sideIndex].firstAdjacentSideOffset = edgeIndex;
        fixture->sides[sideIndex].edgeCount = 1;
        fixture->adjacency[edgeIndex] = edgeIndex;
        ++edgeIndex;
    }

    fixture->geoms[0].type = 0;
    fixture->geoms[0].brush = &fixture->brush;
    fixture->geoms[1].type = 1;
    fixture->geoms[1].halfLengths[0] = 1.0f;
    fixture->geoms[1].halfLengths[1] = 2.0f;
    fixture->geoms[1].halfLengths[2] = 3.0f;
    fixture->geoms[2].type = 4;
    fixture->geoms[2].halfLengths[0] = 2.0f;
    fixture->geoms[2].halfLengths[1] = 4.0f;
    for (TestPhysGeomInfo &geom : fixture->geoms)
    {
        geom.orientation[0][0] = 1.0f;
        geom.orientation[1][1] = 1.0f;
        geom.orientation[2][2] = 1.0f;
    }

    fixture->list.count =
        static_cast<std::uint32_t>(fixture->geoms.size());
    fixture->list.geoms = fixture->geoms.data();
    fixture->list.mass.centerOfMass[0] = 0.5f;
    fixture->list.mass.momentsOfInertia[0] = 1.0f;
    fixture->list.mass.momentsOfInertia[1] = 2.0f;
    fixture->list.mass.momentsOfInertia[2] = 3.0f;
    fixture->list.mass.productsOfInertia[0] = 0.25f;
}

TestSurfaceCollisionTree ValidTestSurfaceCollisionTree()
{
    TestSurfaceCollisionTree tree = {};
    tree.nodes[0].childBeginIndex = 1;
    tree.nodes[0].childCount = 2;
    tree.nodes[1].childBeginIndex = 0;
    tree.nodes[1].childCount = UINT16_C(0x8002);
    tree.nodes[2].childBeginIndex = 3;
    tree.nodes[2].childCount = 1;
    tree.nodes[3].childBeginIndex = 2;
    tree.nodes[3].childCount = UINT16_C(0x8002);
    tree.leafs[0].triangleBeginIndex = 2;
    tree.leafs[1].triangleBeginIndex = UINT16_C(0x8003);
    tree.leafs[2].triangleBeginIndex = 5;
    tree.leafs[3].triangleBeginIndex = UINT16_C(0x8006);
    return tree;
}

template <std::size_t Count>
void SetTestWorldAabbChildren(
    std::array<TestWorldAabbNode, Count> *nodes,
    std::size_t parent,
    std::size_t firstChild,
    std::uint16_t childCount)
{
    (*nodes)[parent].childCount = childCount;
    (*nodes)[parent].childrenOffset = static_cast<std::int32_t>(
        (firstChild - parent) * sizeof(TestWorldAabbNode));
}

std::array<TestWorldAabbNode, 4> ValidTestWorldAabbTree()
{
    std::array<TestWorldAabbNode, 4> nodes = {};
    nodes[0].surfaceCount = 4;
    nodes[0].surfaceCountNoDecal = 2;
    nodes[0].startSurfIndexNoDecal = 4;
    nodes[1].surfaceCount = 2;
    nodes[1].surfaceCountNoDecal = 1;
    nodes[1].startSurfIndexNoDecal = 4;
    nodes[2].startSurfIndex = 2;
    nodes[2].surfaceCount = 2;
    nodes[2].surfaceCountNoDecal = 1;
    nodes[2].startSurfIndexNoDecal = 5;
    nodes[3].surfaceCount = 2;
    nodes[3].surfaceCountNoDecal = 1;
    nodes[3].startSurfIndexNoDecal = 4;
    SetTestWorldAabbChildren(&nodes, 0, 1, 2);
    SetTestWorldAabbChildren(&nodes, 1, 3, 1);
    return nodes;
}
}

int main()
{
    Expect(
        db::validation::AssetOutputCapacityValid(0),
        "zero asset output capacity accepted");
    Expect(
        !db::validation::AssetOutputCapacityValid(-1),
        "negative asset output capacity rejected");
    Expect(
        db::validation::AssetOutputCountCanIncrement(INT32_MAX - 1),
        "asset output count below int32 maximum can increment");
    Expect(
        !db::validation::AssetOutputCountCanIncrement(INT32_MAX),
        "asset output count cannot overflow int32");
    Expect(
        db::validation::AssetOutputWriteAllowed(true, 4, 5),
        "asset output writes the last in-capacity entry");
    Expect(
        !db::validation::AssetOutputWriteAllowed(true, 5, 5),
        "asset output rejects one-past-capacity writes");
    Expect(
        !db::validation::AssetOutputWriteAllowed(false, 0, 0),
        "count-only asset enumeration performs no output writes");

    Expect(
        db::validation::SoundFileHeaderValid(1, 1),
        "loaded sound-file header accepted");
    Expect(
        db::validation::SoundFileHeaderValid(2, 0),
        "missing streamed sound-file header accepted");
    Expect(
        !db::validation::SoundFileHeaderValid(0, 1),
        "unknown sound-file type rejected");
    Expect(
        !db::validation::SoundFileHeaderValid(3, 1),
        "out-of-range sound-file type rejected");
    Expect(
        !db::validation::SoundFileHeaderValid(1, 2),
        "invalid sound-file existence flag rejected");
    const float finitePieceOffset[] = {1.0f, -2.0f, 3.0f};
    const float nanPieceOffset[] = {
        0.0f,
        (std::numeric_limits<float>::quiet_NaN)(),
        0.0f};
    Expect(
        db::validation::XModelPieceRuntimeValid(
            true,
            finitePieceOffset),
        "usable model-piece runtime dependencies accepted");
    Expect(
        !db::validation::XModelPieceRuntimeValid(
            false,
            finitePieceOffset),
        "missing model-piece model rejected");
    Expect(
        !db::validation::XModelPieceRuntimeValid(
            true,
            nanPieceOffset),
        "non-finite model-piece offset rejected");

    {
        const std::uint16_t validSurfaceIndices[] = {
            0, 1, 2,
            2, 3, 4,
            1, 2, 3,
            4, 3, 0,
        };
        const std::uint16_t invalidSurfaceIndices[] = {0, 1, 5};
        Expect(
            db::validation::XSurfaceTriangleIndicesValid(
                validSurfaceIndices,
                4,
                5),
            "surface triangle indices inside the vertex array accepted");
        Expect(
            !db::validation::XSurfaceTriangleIndicesValid(
                invalidSurfaceIndices,
                1,
                5),
            "surface triangle index at the vertex-array end rejected");
        Expect(
            db::validation::XSurfaceTriangleIndicesValid(nullptr, 0, 0),
            "empty surface triangle-index array accepted");
        Expect(
            !db::validation::XSurfaceTriangleIndicesValid(nullptr, 1, 5),
            "missing nonempty surface triangle-index array rejected");
        Expect(
            !db::validation::XSurfaceTriangleIndicesValid(
                nullptr,
                (std::numeric_limits<std::uint32_t>::max)() / 3u + 1u,
                1),
            "surface triangle-index count multiplication overflow rejected");

        Expect(
            db::validation::XSurfaceTriangleCountValid(2)
                && db::validation::XSurfaceTriangleCountValid(8),
            "builder-padded even surface triangle counts accepted");
        Expect(
            !db::validation::XSurfaceTriangleCountValid(0)
                && !db::validation::XSurfaceTriangleCountValid(1)
                && !db::validation::XSurfaceTriangleCountValid(3),
            "empty and unpadded surface triangle counts rejected");

        std::array<TestPackedVertexPayload, 2> packedVertices = {};
        packedVertices[0].xyz[0] = -8.0f;
        packedVertices[1].xyz[2] = 12.0f;
        packedVertices[1].binormalSign = -1.0f;
        Expect(
            db::validation::XSurfaceVertexPayloadValid(
                packedVertices.data(),
                static_cast<std::uint32_t>(packedVertices.size())),
            "finite canonical packed surface vertices accepted");
        auto invalidPackedVertices = packedVertices;
        invalidPackedVertices[0].xyz[1] =
            std::numeric_limits<float>::quiet_NaN();
        Expect(
            !db::validation::XSurfaceVertexPayloadValid(
                invalidPackedVertices.data(),
                static_cast<std::uint32_t>(invalidPackedVertices.size())),
            "non-finite packed surface position rejected");
        invalidPackedVertices[0] = packedVertices[0];
        invalidPackedVertices[0].binormalSign = 0.0f;
        Expect(
            !db::validation::XSurfaceVertexPayloadValid(
                invalidPackedVertices.data(),
                static_cast<std::uint32_t>(invalidPackedVertices.size())),
            "noncanonical packed surface binormal sign rejected");
        Expect(
            !db::validation::XSurfaceVertexPayloadValid<
                TestPackedVertexPayload>(nullptr, 1),
            "missing packed surface vertex payload rejected");

        const TestSurfaceCacheLayout validCacheSurfaces[] = {
            {3, 2, 0, 0},
            {2, 4, 3, 2},
        };
        std::uint32_t lodVertexCount = 99u;
        std::uint32_t lodTriangleCount = 99u;
        Expect(
            db::validation::XModelLodSurfaceCacheLayoutValid(
                validCacheSurfaces,
                2,
                &lodVertexCount,
                &lodTriangleCount)
                && lodVertexCount == 5u && lodTriangleCount == 6u,
            "exact cumulative LOD surface-cache bases accepted");
        auto invalidCacheSurfaces = std::array<TestSurfaceCacheLayout, 2>{
            validCacheSurfaces[0],
            validCacheSurfaces[1],
        };
        invalidCacheSurfaces[1].baseVertIndex = 4;
        Expect(
            !db::validation::XModelLodSurfaceCacheLayoutValid(
                invalidCacheSurfaces.data(),
                static_cast<std::uint32_t>(invalidCacheSurfaces.size()),
                &lodVertexCount,
                &lodTriangleCount),
            "gapped LOD surface vertex base rejected");
        invalidCacheSurfaces[1] = validCacheSurfaces[1];
        invalidCacheSurfaces[1].baseTriIndex = 4;
        Expect(
            !db::validation::XModelLodSurfaceCacheLayoutValid(
                invalidCacheSurfaces.data(),
                static_cast<std::uint32_t>(invalidCacheSurfaces.size()),
                &lodVertexCount,
                &lodTriangleCount),
            "gapped LOD surface triangle base rejected");
        invalidCacheSurfaces[1] = validCacheSurfaces[1];
        invalidCacheSurfaces[1].triCount = 3;
        Expect(
            !db::validation::XModelLodSurfaceCacheLayoutValid(
                invalidCacheSurfaces.data(),
                static_cast<std::uint32_t>(invalidCacheSurfaces.size()),
                &lodVertexCount,
                &lodTriangleCount),
            "unpadded LOD cache surface rejected");
        Expect(
            !db::validation::XModelLodSurfaceCacheLayoutValid<
                TestSurfaceCacheLayout>(
                nullptr,
                0,
                &lodVertexCount,
                &lodTriangleCount)
                && lodVertexCount == 0u && lodTriangleCount == 0u,
            "missing LOD cache surface array rejected with zero outputs");

        Expect(
            db::validation::XModelStaticCacheLayoutValid(0, 0, 5, 6),
            "canonical disabled static-model cache metadata accepted");
        Expect(
            db::validation::XModelStaticCacheLayoutValid(0, 255, 5, 6),
            "disabled static-model cache ignores inactive allocation metadata");
        Expect(
            db::validation::XModelStaticCacheLayoutValid(1, 4, 16, 20)
                && db::validation::XModelStaticCacheLayoutValid(4, 9, 512, 682),
            "bounded static-model cache groups and capacities accepted");
        Expect(
            !db::validation::XModelStaticCacheLayoutValid(5, 4, 1, 2)
                && !db::validation::XModelStaticCacheLayoutValid(1, 3, 1, 2)
                && !db::validation::XModelStaticCacheLayoutValid(1, 10, 1, 2),
            "static-model cache group and shift bounds enforced");
        Expect(
            !db::validation::XModelStaticCacheLayoutValid(1, 4, 17, 2)
                && !db::validation::XModelStaticCacheLayoutValid(1, 4, 1, 22),
            "undersized static-model vertex and index cache allocations rejected");

        Expect(
            db::validation::XModelLodDistanceFollows(100.0f, 100.0f)
                && db::validation::XModelLodDistanceFollows(100.0f, 200.0f),
            "nondecreasing finite model LOD distances accepted");
        Expect(
            !db::validation::XModelLodDistanceFollows(200.0f, 100.0f)
                && !db::validation::XModelLodDistanceFollows(
                    100.0f,
                    std::numeric_limits<float>::infinity()),
            "decreasing and non-finite model LOD distances rejected");

        std::array<TestAnimMatPayload, 2> basePose = {};
        basePose[1].quat[3] = 2.0f;
        basePose[1].transWeight = 0.5f;
        basePose[1].trans[0] = 4.0f;
        Expect(
            db::validation::XModelBasePoseValid(
                basePose.data(),
                static_cast<std::uint32_t>(basePose.size())),
            "finite canonical non-unit base-pose quaternion accepted");
        auto invalidBasePose = basePose;
        invalidBasePose[0].trans[1] =
            std::numeric_limits<float>::infinity();
        Expect(
            !db::validation::XModelBasePoseValid(
                invalidBasePose.data(),
                static_cast<std::uint32_t>(invalidBasePose.size())),
            "non-finite base-pose translation rejected");
        invalidBasePose[0] = basePose[0];
        invalidBasePose[0].quat[3] = 0.0f;
        Expect(
            !db::validation::XModelBasePoseValid(
                invalidBasePose.data(),
                static_cast<std::uint32_t>(invalidBasePose.size())),
            "zero-length base-pose quaternion rejected");
        invalidBasePose[0] = basePose[0];
        invalidBasePose[0].transWeight = 1.0f;
        Expect(
            !db::validation::XModelBasePoseValid(
                invalidBasePose.data(),
                static_cast<std::uint32_t>(invalidBasePose.size())),
            "noncanonical base-pose quaternion weight rejected");
        invalidBasePose[0] = basePose[0];
        invalidBasePose[0].quat[0] = 1000.0f;
        Expect(
            !db::validation::XModelBasePoseValid(
                invalidBasePose.data(),
                static_cast<std::uint32_t>(invalidBasePose.size())),
            "base-pose transform unsafe for packed-basis conversion rejected");

        std::array<TestBoneInfoPayload, 2> boneInfo = {};
        for (TestBoneInfoPayload &info : boneInfo)
        {
            for (std::uint32_t axis = 0u; axis < 3u; ++axis)
            {
                info.bounds[0][axis] = -1.0f;
                info.bounds[1][axis] = 1.0f;
            }
            info.radiusSquared = 3.0f;
        }
        Expect(
            db::validation::XModelBoneInfoValid(
                boneInfo.data(),
                static_cast<std::uint32_t>(boneInfo.size())),
            "canonical finite model bone bounds accepted");
        auto invalidBoneInfo = boneInfo;
        invalidBoneInfo[0].bounds[0][0] = 2.0f;
        Expect(
            !db::validation::XModelBoneInfoValid(
                invalidBoneInfo.data(),
                static_cast<std::uint32_t>(invalidBoneInfo.size())),
            "reversed model bone bounds rejected");
        invalidBoneInfo[0] = boneInfo[0];
        invalidBoneInfo[0].offset[0] = 0.5f;
        Expect(
            !db::validation::XModelBoneInfoValid(
                invalidBoneInfo.data(),
                static_cast<std::uint32_t>(invalidBoneInfo.size())),
            "model bone offset inconsistent with bounds rejected");
        invalidBoneInfo[0] = boneInfo[0];
        invalidBoneInfo[0].radiusSquared = 2.0f;
        Expect(
            !db::validation::XModelBoneInfoValid(
                invalidBoneInfo.data(),
                static_cast<std::uint32_t>(invalidBoneInfo.size())),
            "model bone radius inconsistent with bounds rejected");

        const float modelMins[3] = {-4.0f, -3.0f, -2.0f};
        const float modelMaxs[3] = {4.0f, 3.0f, 2.0f};
        Expect(
            db::validation::XModelBoundsValid(modelMins, modelMaxs, 6.0f),
            "finite ordered model bounds accepted");
        const float reversedModelMaxs[3] = {-5.0f, 3.0f, 2.0f};
        Expect(
            !db::validation::XModelBoundsValid(
                modelMins,
                reversedModelMaxs,
                6.0f)
                && !db::validation::XModelBoundsValid(
                    modelMins,
                    modelMaxs,
                    -1.0f),
            "reversed model bounds and negative radius rejected");

        const std::int16_t mixedSkinCounts[4] = {1, 2, 3, 4};
        std::uint32_t blendElementCount = 99u;
        Expect(
            db::validation::XSurfaceSkinningLayoutValid(
                mixedSkinCounts,
                true,
                10,
                true,
                &blendElementCount)
                && blendElementCount == 50u,
            "exact deformed surface skinning buckets accepted");
        std::int16_t invalidSkinCounts[4] = {-1, 2, 3, 4};
        Expect(
            !db::validation::XSurfaceSkinningLayoutValid(
                invalidSkinCounts,
                true,
                8,
                true,
                &blendElementCount),
            "negative surface skinning bucket rejected");
        invalidSkinCounts[0] = 1;
        Expect(
            !db::validation::XSurfaceSkinningLayoutValid(
                invalidSkinCounts,
                true,
                9,
                true,
                &blendElementCount),
            "surface skinning bucket total mismatch rejected");
        Expect(
            !db::validation::XSurfaceSkinningLayoutValid(
                mixedSkinCounts,
                false,
                10,
                true,
                &blendElementCount),
            "missing deformed surface blend records rejected");
        const std::int16_t emptySkinCounts[4] = {};
        Expect(
            db::validation::XSurfaceSkinningLayoutValid(
                emptySkinCounts,
                false,
                5,
                false,
                &blendElementCount)
                && blendElementCount == 0u,
            "canonical rigid surface skinning metadata accepted");

        const std::int16_t oneEachSkinCounts[4] = {1, 1, 1, 1};
        std::array<std::uint16_t, 16> blendRecords = {
            0,
            0, 64, 100,
            0, 64, 100, 128, 200,
            0, 64, 100, 128, 200, 192, 300,
        };
        const std::uint32_t fourBonePartBits[4] = {
            UINT32_C(0xF0000000), 0, 0, 0};
        Expect(
            db::validation::XSurfaceBlendRecordsValid(
                blendRecords.data(),
                oneEachSkinCounts,
                4,
                fourBonePartBits),
            "bounded weighted-surface bone records accepted");
        auto invalidBlendRecords = blendRecords;
        invalidBlendRecords[2] = 65;
        Expect(
            !db::validation::XSurfaceBlendRecordsValid(
                invalidBlendRecords.data(),
                oneEachSkinCounts,
                4,
                fourBonePartBits),
            "misaligned weighted-surface bone offset rejected");
        invalidBlendRecords = blendRecords;
        invalidBlendRecords[14] = 256;
        Expect(
            !db::validation::XSurfaceBlendRecordsValid(
                invalidBlendRecords.data(),
                oneEachSkinCounts,
                4,
                fourBonePartBits),
            "weighted-surface bone offset at model end rejected");
        invalidBlendRecords = blendRecords;
        invalidBlendRecords[11] = 40000;
        invalidBlendRecords[13] = 30000;
        Expect(
            !db::validation::XSurfaceBlendRecordsValid(
                invalidBlendRecords.data(),
                oneEachSkinCounts,
                4,
                fourBonePartBits),
            "weighted-surface explicit weight overflow rejected");
        const std::uint32_t missingBonePartBits[4] = {
            UINT32_C(0xE0000000), 0, 0, 0};
        Expect(
            !db::validation::XSurfaceBlendRecordsValid(
                blendRecords.data(),
                oneEachSkinCounts,
                4,
                missingBonePartBits),
            "weighted-surface bone absent from part bits rejected");
        const std::uint32_t outOfRangePartBits[4] = {
            UINT32_C(0xF8000000), 0, 0, 0};
        Expect(
            !db::validation::XSurfaceBlendRecordsValid(
                blendRecords.data(),
                oneEachSkinCounts,
                4,
                outOfRangePartBits),
            "surface part bit beyond model bone count rejected");

        db::validation::XModelPointerPresence modelPointers;
        modelPointers.name = true;
        modelPointers.boneNames = true;
        modelPointers.partClassification = true;
        modelPointers.baseMatrices = true;
        modelPointers.surfaces = true;
        modelPointers.materials = true;
        modelPointers.boneInfo = true;
        Expect(
            db::validation::XModelHeaderLayoutValid(
                1, 1, 2, 1, 0, modelPointers),
            "root-only model pointer contract accepted");
        Expect(
            !db::validation::XModelHeaderLayoutValid(
                1, 1, 2, 5, 0, modelPointers),
            "model LOD count beyond its fixed array rejected");
        modelPointers.parentList = true;
        modelPointers.quaternions = true;
        Expect(
            !db::validation::XModelHeaderLayoutValid(
                2, 1, 2, 1, 0, modelPointers),
            "non-root model without translations rejected");
        modelPointers.translations = true;
        Expect(
            db::validation::XModelHeaderLayoutValid(
                2, 1, 2, 1, 1, modelPointers),
            "complete non-root model pointer contract accepted");

        const std::uint32_t twoBonePartBits[4] = {
            UINT32_C(0xC0000000), 0, 0, 0};
        Expect(
            db::validation::XModelLodLayoutValid(
                0, 0, 2, 2, 0, 1000.0f, 2, twoBonePartBits),
            "bounded model LOD surface and bone ranges accepted");
        Expect(
            !db::validation::XModelLodLayoutValid(
                0, 1, 2, 2, 0, 1000.0f, 2, twoBonePartBits),
            "model LOD surface span beyond aggregate rejected");
        Expect(
            !db::validation::XModelLodLayoutValid(
                0,
                0,
                2,
                2,
                1,
                std::numeric_limits<float>::quiet_NaN(),
                2,
                twoBonePartBits),
            "mismatched and non-finite model LOD metadata rejected");
        const std::uint8_t validPartClassifications[2] = {0u, 0x12u};
        const std::uint8_t invalidPartClassifications[2] = {0u, 0x13u};
        Expect(
            db::validation::XModelPartClassificationsValid(
                validPartClassifications,
                2),
            "bounded model part classifications accepted");
        Expect(
            !db::validation::XModelPartClassificationsValid(
                invalidPartClassifications,
                2),
            "model part classification beyond the priority map rejected");

        const auto collisionTree = ValidTestSurfaceCollisionTree();
        const TestRigidVertList validRigidLists[] = {
            {3, 0, 2, &collisionTree},
            {2, 2, 2, &collisionTree},
        };
        auto validRigidSkinLists = std::array<TestRigidVertList, 2>{
            validRigidLists[0],
            validRigidLists[1],
        };
        validRigidSkinLists[0].boneOffset = 0;
        validRigidSkinLists[1].boneOffset = 64;
        Expect(
            db::validation::XSurfaceRigidSkinningValid(
                validRigidSkinLists.data(),
                validRigidSkinLists.size(),
                5,
                4,
                fourBonePartBits),
            "bounded rigid-surface bone partitions accepted");
        validRigidSkinLists[1].boneOffset = 65;
        Expect(
            !db::validation::XSurfaceRigidSkinningValid(
                validRigidSkinLists.data(),
                validRigidSkinLists.size(),
                5,
                4,
                fourBonePartBits),
            "misaligned rigid-surface bone offset rejected");
        Expect(
            db::validation::XSurfaceRigidPartitionValid(
                validRigidLists,
                2,
                5,
                4,
                validSurfaceIndices),
            "ordered rigid vertex and triangle partitions accepted");
        Expect(
            db::validation::XSurfaceRigidPartitionValid<TestRigidVertList>(
                nullptr,
                0,
                0,
                0,
                nullptr),
            "empty rigid partition covers an empty surface");

        auto invalidRigidLists = std::array<TestRigidVertList, 2>{
            validRigidLists[0],
            validRigidLists[1],
        };
        invalidRigidLists[0].vertCount = 0;
        Expect(
            !db::validation::XSurfaceRigidPartitionValid(
                invalidRigidLists.data(),
                invalidRigidLists.size(),
                5,
                4,
                validSurfaceIndices),
            "zero-length rigid vertex partition rejected");
        invalidRigidLists = {validRigidLists[0], validRigidLists[1]};
        invalidRigidLists[1].triCount = 0;
        Expect(
            !db::validation::XSurfaceRigidPartitionValid(
                invalidRigidLists.data(),
                invalidRigidLists.size(),
                5,
                4,
                validSurfaceIndices),
            "zero-length rigid triangle partition rejected");
        invalidRigidLists = {validRigidLists[0], validRigidLists[1]};
        invalidRigidLists[1].triOffset = 3;
        Expect(
            !db::validation::XSurfaceRigidPartitionValid(
                invalidRigidLists.data(),
                invalidRigidLists.size(),
                5,
                4,
                validSurfaceIndices),
            "gapped rigid triangle partition rejected");
        invalidRigidLists = {validRigidLists[0], validRigidLists[1]};
        invalidRigidLists[1].vertCount = 1;
        Expect(
            !db::validation::XSurfaceRigidPartitionValid(
                invalidRigidLists.data(),
                invalidRigidLists.size(),
                5,
                4,
                validSurfaceIndices),
            "incomplete rigid vertex coverage rejected");
        invalidRigidLists = {validRigidLists[0], validRigidLists[1]};
        invalidRigidLists[1].collisionTree = nullptr;
        Expect(
            !db::validation::XSurfaceRigidPartitionValid(
                invalidRigidLists.data(),
                invalidRigidLists.size(),
                5,
                4,
                validSurfaceIndices),
            "rigid partition without a collision tree rejected");
        Expect(
            !db::validation::XSurfaceRigidPartitionValid<TestRigidVertList>(
                nullptr,
                1,
                5,
                4,
                validSurfaceIndices),
            "missing rigid partition array rejected");

        const std::uint16_t paddedSurfaceIndices[] = {
            0, 1, 2,
            2, 3, 4,
            1, 2, 3,
            3, 3, 3,
        };
        const TestRigidVertList paddedRigidLists[] = {
            {3, 0, 2, &collisionTree},
            {2, 2, 1, &collisionTree},
        };
        Expect(
            db::validation::XSurfaceRigidPartitionValid(
                paddedRigidLists,
                2,
                5,
                4,
                paddedSurfaceIndices),
            "source-generated trailing surface padding triangle accepted");
        auto invalidPaddingIndices = std::array<std::uint16_t, 12>{};
        for (std::size_t index = 0; index < invalidPaddingIndices.size(); ++index)
            invalidPaddingIndices[index] = paddedSurfaceIndices[index];
        invalidPaddingIndices[11] = 2;
        Expect(
            !db::validation::XSurfaceRigidPartitionValid(
                paddedRigidLists,
                2,
                5,
                4,
                invalidPaddingIndices.data()),
            "malformed trailing surface padding triangle rejected");
        const TestRigidVertList shortRigidLists[] = {
            {3, 0, 1, &collisionTree},
            {2, 1, 1, &collisionTree},
        };
        Expect(
            !db::validation::XSurfaceRigidPartitionValid(
                shortRigidLists,
                2,
                5,
                4,
                paddedSurfaceIndices),
            "more than one uncovered surface triangle rejected");
        const std::uint16_t oddPaddingIndices[] = {
            0, 1, 2,
            2, 3, 4,
            4, 4, 4,
        };
        Expect(
            !db::validation::XSurfaceRigidPartitionValid(
                shortRigidLists,
                2,
                5,
                3,
                oddPaddingIndices),
            "odd-count surface cannot claim a source padding triangle");
    }

    {
        const auto validCollisionTree = ValidTestSurfaceCollisionTree();
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                validCollisionTree.nodes.data(),
                validCollisionTree.nodes.size(),
                validCollisionTree.leafs.data(),
                validCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::Ok,
            "valid surface collision topology accepted");
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology<
                TestSurfaceCollisionNode,
                TestSurfaceCollisionLeaf>(
                nullptr,
                0,
                nullptr,
                0,
                0,
                0,
                0)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTree,
            "empty surface collision tree rejected before root access");
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                validCollisionTree.nodes.data(),
                db::validation::kMaxXSurfaceCollisionEntries + 1u,
                validCollisionTree.leafs.data(),
                validCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTree,
            "unaddressable surface collision node count rejected");

        auto invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.nodes[2].aabb.mins[1] = 2;
        invalidCollisionTree.nodes[2].aabb.maxs[1] = 1;
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidBounds,
            "inverted surface collision node bounds rejected");
        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.nodes[1].childCount = 0;
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidChildSpan,
            "zero surface collision child count rejected");
        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.nodes[1].childCount = UINT16_C(0x8000);
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidChildSpan,
            "zero decoded surface collision leaf count rejected");
        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.nodes[0].childBeginIndex = 3;
        invalidCollisionTree.nodes[0].childCount = 2;
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidChildSpan,
            "overflowing surface collision node span rejected");
        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.nodes[1].childBeginIndex = 3;
        invalidCollisionTree.nodes[1].childCount = UINT16_C(0x8002);
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidChildSpan,
            "overflowing surface collision leaf span rejected");

        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.nodes[1].childBeginIndex = 3;
        invalidCollisionTree.nodes[1].childCount = 1;
        invalidCollisionTree.nodes[2].childBeginIndex = 3;
        invalidCollisionTree.nodes[2].childCount = 1;
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTopology,
            "multiply-owned surface collision node rejected");
        const std::array<TestSurfaceCollisionNode, 2> cyclicNodes = {{
            {{}, 1, 1},
            {{}, 0, 1},
        }};
        const std::array<TestSurfaceCollisionLeaf, 1> cyclicLeafs = {{{2}}};
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                cyclicNodes.data(),
                cyclicNodes.size(),
                cyclicLeafs.data(),
                cyclicLeafs.size(),
                2,
                1,
                3)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTopology,
            "surface collision node cycle rejected");
        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.nodes[0].childCount = 1;
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTopology,
            "unreachable surface collision entries rejected");
        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.nodes[3].childCount = UINT16_C(0x8001);
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTopology,
            "unreachable surface collision leaf rejected");

        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.leafs[0].triangleBeginIndex = 1;
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTriangleSpan,
            "collision leaf triangle below its rigid partition rejected");
        invalidCollisionTree = ValidTestSurfaceCollisionTree();
        invalidCollisionTree.leafs[3].triangleBeginIndex = UINT16_C(0x8007);
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                invalidCollisionTree.nodes.data(),
                invalidCollisionTree.nodes.size(),
                invalidCollisionTree.leafs.data(),
                invalidCollisionTree.leafs.size(),
                2,
                6,
                8)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTriangleSpan,
            "two-triangle collision leaf beyond its rigid partition rejected");
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                validCollisionTree.nodes.data(),
                validCollisionTree.nodes.size(),
                validCollisionTree.leafs.data(),
                validCollisionTree.leafs.size(),
                2,
                6,
                7)
                == db::validation::XSurfaceCollisionTopologyStatus::InvalidTriangleSpan,
            "rigid collision partition beyond its surface rejected");

        std::array<TestSurfaceCollisionNode, 127> maximumQueueNodes = {};
        std::array<TestSurfaceCollisionLeaf, 63> maximumQueueLeafs = {};
        maximumQueueNodes[0].childBeginIndex = 1;
        maximumQueueNodes[0].childCount = 63;
        for (std::uint32_t index = 0; index < 63; ++index)
        {
            maximumQueueNodes[1u + index].childBeginIndex =
                static_cast<std::uint16_t>(64u + index);
            maximumQueueNodes[1u + index].childCount = 1;
            maximumQueueNodes[64u + index].childBeginIndex =
                static_cast<std::uint16_t>(index);
            maximumQueueNodes[64u + index].childCount = UINT16_C(0x8001);
            maximumQueueLeafs[index].triangleBeginIndex =
                static_cast<std::uint16_t>(index);
        }
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                maximumQueueNodes.data(),
                maximumQueueNodes.size(),
                maximumQueueLeafs.data(),
                maximumQueueLeafs.size(),
                0,
                63,
                63)
                == db::validation::XSurfaceCollisionTopologyStatus::Ok,
            "surface collision node-range queue accepts 63 pending ranges");

        std::array<TestSurfaceCollisionNode, 129> overflowingQueueNodes = {};
        std::array<TestSurfaceCollisionLeaf, 64> overflowingQueueLeafs = {};
        overflowingQueueNodes[0].childBeginIndex = 1;
        overflowingQueueNodes[0].childCount = 64;
        for (std::uint32_t index = 0; index < 64; ++index)
        {
            overflowingQueueNodes[1u + index].childBeginIndex =
                static_cast<std::uint16_t>(65u + index);
            overflowingQueueNodes[1u + index].childCount = 1;
            overflowingQueueNodes[65u + index].childBeginIndex =
                static_cast<std::uint16_t>(index);
            overflowingQueueNodes[65u + index].childCount = UINT16_C(0x8001);
            overflowingQueueLeafs[index].triangleBeginIndex =
                static_cast<std::uint16_t>(index);
        }
        Expect(
            db::validation::ValidateXSurfaceCollisionTopology(
                overflowingQueueNodes.data(),
                overflowingQueueNodes.size(),
                overflowingQueueLeafs.data(),
                overflowingQueueLeafs.size(),
                0,
                64,
                64)
                == db::validation::XSurfaceCollisionTopologyStatus::
                    NodeRangeQueueCapacityExceeded,
            "surface collision topology rejects 64 pending node ranges");
    }

    Expect(
        db::validation::SpeakerMapExpectedSpeakerCount(0) == 2u
            && db::validation::SpeakerMapExpectedSpeakerCount(1) == 6u,
        "speaker-map output configurations use their fixed capacities");
    Expect(
        db::validation::SpeakerMapExpectedSpeakerCount(2) == 0u,
        "unknown speaker-map output configuration rejected");
    Expect(
        db::validation::SpeakerMapEntryValid(1, 1, 2, 2, 0.25f, 0.75f),
        "valid stereo speaker-map entry accepted");
    Expect(
        !db::validation::SpeakerMapEntryValid(1, 2, 2, 2, 0.25f, 0.75f),
        "misidentified speaker-map entry rejected");
    Expect(
        !db::validation::SpeakerMapEntryValid(1, 1, 3, 2, 0.25f, 0.75f),
        "oversized speaker-map level count rejected");
    Expect(
        !db::validation::SpeakerMapEntryValid(1, 1, 2, 2, -0.1f, 0.75f),
        "negative speaker-map level rejected");
    Expect(
        !db::validation::SpeakerMapEntryValid(
            1,
            1,
            2,
            2,
            (std::numeric_limits<float>::quiet_NaN)(),
            0.75f),
        "non-finite speaker-map level rejected");

    Expect(
        db::validation::WorldAabbTreePresenceValid(false, 0),
        "empty world AABB tree is represented by a null pointer");
    Expect(
        db::validation::WorldAabbTreePresenceValid(true, 1),
        "nonempty world AABB tree requires a pointer");
    Expect(
        !db::validation::WorldAabbTreePresenceValid(false, 1),
        "missing nonempty world AABB tree rejected");
    Expect(
        !db::validation::WorldAabbTreePresenceValid(true, 0),
        "present zero-node world AABB tree rejected");
    Expect(
        db::validation::UnsignedSpanWithinPartition(4, 2, 4, 2),
        "world AABB surface span may end exactly at its partition limit");
    Expect(
        !db::validation::UnsignedSpanWithinPartition(5, 2, 4, 2),
        "world AABB surface span cannot exceed its partition limit");
    Expect(
        db::validation::OptionalUnsignedSpanWithinPartition(65535, 0, 0, 10),
        "empty brush-model surface span permits its uint16 sentinel start");
    Expect(
        db::validation::WorldAabbSurfacePartitionsValid(50000, 40000, 50000),
        "world AABB no-decal partition may end beyond a uint16 start index");
    Expect(
        !db::validation::WorldAabbSurfacePartitionsValid(65537, 0, 65537),
        "world AABB static partition must have a representable nonempty start");
    Expect(
        !db::validation::WorldAabbSurfacePartitionsValid(65536, 1, 65536),
        "nonempty world AABB no-decal partition requires a uint16 start");
    std::uint8_t surfaceCoverage[4] = {};
    Expect(
        db::validation::MarkUniqueCoverageSpan(surfaceCoverage, 4, 0, 2)
            && db::validation::MarkUniqueCoverageSpan(surfaceCoverage, 4, 2, 2)
            && db::validation::CoverageComplete(surfaceCoverage, 4),
        "disjoint world AABB root spans cover their full partition");
    surfaceCoverage[0] = 0;
    surfaceCoverage[1] = 0;
    surfaceCoverage[2] = 0;
    surfaceCoverage[3] = 0;
    Expect(
        db::validation::MarkUniqueCoverageSpan(surfaceCoverage, 4, 0, 2)
            && !db::validation::MarkUniqueCoverageSpan(surfaceCoverage, 4, 1, 2),
        "overlapping world AABB root spans rejected");
    surfaceCoverage[0] = 0;
    surfaceCoverage[1] = 0;
    surfaceCoverage[2] = 0;
    surfaceCoverage[3] = 0;
    Expect(
        db::validation::MarkUniqueCoverageSpan(surfaceCoverage, 4, 1, 3)
            && !db::validation::CoverageComplete(surfaceCoverage, 4),
        "gapped world AABB root coverage rejected");

    std::uint8_t aabbDepths[65] = {};
    const auto validAabbTree = ValidTestWorldAabbTree();
    Expect(
        db::validation::ValidateWorldAabbTopology(
            validAabbTree.data(),
            validAabbTree.size(),
            4,
            2,
            aabbDepths,
            sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::Ok,
        "valid branched world AABB topology accepted");
    Expect(
        db::validation::ValidateWorldAabbTopology<TestWorldAabbNode>(
            nullptr,
            0,
            0,
            0,
            nullptr,
            0)
            == db::validation::WorldAabbTopologyStatus::Ok,
        "empty world AABB topology accepted without scratch storage");
    Expect(
        db::validation::ValidateWorldAabbTopology(
            validAabbTree.data(),
            validAabbTree.size(),
            4,
            2,
            aabbDepths,
            3)
            == db::validation::WorldAabbTopologyStatus::InvalidWorkBuffer,
        "undersized world AABB topology scratch storage rejected");
    Expect(
        db::validation::ValidateWorldAabbTopology(
            validAabbTree.data(),
            validAabbTree.size(),
            4,
            2,
            aabbDepths,
            sizeof(aabbDepths),
            128)
            == db::validation::WorldAabbTopologyStatus::InvalidWorkBuffer,
        "world AABB depth budget reserves its work-state content bit");

    auto invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[0].childrenOffset += 1;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidChildSpan,
        "misaligned world AABB child offset rejected");
    invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[0].childrenOffset = -static_cast<std::int32_t>(
        sizeof(TestWorldAabbNode));
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidChildSpan,
        "backward world AABB child offset rejected");
    invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[0].childrenOffset = 4 * sizeof(TestWorldAabbNode);
    invalidAabbTree[0].childCount = 1;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidChildSpan,
        "one-past world AABB child block rejected");
    invalidAabbTree = ValidTestWorldAabbTree();
    SetTestWorldAabbChildren(&invalidAabbTree, 1, 2, 1);
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            != db::validation::WorldAabbTopologyStatus::Ok,
        "multiply-owned world AABB child rejected");
    invalidAabbTree = ValidTestWorldAabbTree();
    SetTestWorldAabbChildren(&invalidAabbTree, 0, 2, 1);
    invalidAabbTree[1].childCount = 0;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            != db::validation::WorldAabbTopologyStatus::Ok,
        "disconnected world AABB node rejected");
    invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[2].startSurfIndex = 0;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidSurfaceRange,
        "world AABB child surface ranges must partition their parent");

    invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[2].startSurfIndex = 3;
    invalidAabbTree[2].surfaceCount = 2;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidSurfaceRange,
        "out-of-range world AABB surface span rejected");
    invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[2].startSurfIndexNoDecal = 3;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidSurfaceRange,
        "world AABB no-decal span outside its partition rejected");
    invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[2].surfaceCountNoDecal = 3;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 3, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidSurfaceRange,
        "world AABB no-decal count cannot exceed its full surface count");

    invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[2].mins[0] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidBounds,
        "NaN world AABB bound rejected");
    invalidAabbTree = ValidTestWorldAabbTree();
    invalidAabbTree[2].mins[0] = 1.0f;
    invalidAabbTree[2].maxs[0] = 0.0f;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invalidAabbTree.data(), 4, 4, 2, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidBounds,
        "inverted nonempty world AABB bound rejected");
    std::array<TestWorldAabbNode, 2> invertedInternalAabb = {};
    SetTestWorldAabbChildren(&invertedInternalAabb, 0, 1, 1);
    invertedInternalAabb[0].surfaceCount = 1;
    invertedInternalAabb[0].mins[0] = 1.0f;
    invertedInternalAabb[0].maxs[0] = 0.0f;
    invertedInternalAabb[1].surfaceCount = 1;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invertedInternalAabb.data(),
            invertedInternalAabb.size(),
            1,
            0,
            aabbDepths,
            sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::InvalidBounds,
        "inverted world AABB parent with nonempty descendants rejected");
    invertedInternalAabb[0].surfaceCount = 0;
    invertedInternalAabb[1].surfaceCount = 0;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            invertedInternalAabb.data(),
            invertedInternalAabb.size(),
            0,
            0,
            aabbDepths,
            sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::Ok,
        "fully empty world AABB subtree permits sentinel bounds");
    TestWorldAabbNode emptyAabbLeaf = {};
    emptyAabbLeaf.mins[0] = 1.0f;
    emptyAabbLeaf.maxs[0] = 0.0f;
    emptyAabbLeaf.childrenOffset = INT32_MIN;
    Expect(
        db::validation::ValidateWorldAabbTopology(
            &emptyAabbLeaf, 1, 0, 0, aabbDepths, sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::Ok,
        "empty world AABB leaf permits sentinel bounds and ignores its child offset");

    std::array<TestWorldAabbNode, 64> maximumDepthAabb = {};
    for (std::size_t index = 0; index + 1 < maximumDepthAabb.size(); ++index)
        SetTestWorldAabbChildren(&maximumDepthAabb, index, index + 1, 1);
    Expect(
        db::validation::ValidateWorldAabbTopology(
            maximumDepthAabb.data(),
            maximumDepthAabb.size(),
            0,
            0,
            aabbDepths,
            sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::Ok,
        "world AABB topology at the recursion-depth budget accepted");
    std::array<TestWorldAabbNode, 65> excessiveDepthAabb = {};
    for (std::size_t index = 0; index + 1 < excessiveDepthAabb.size(); ++index)
        SetTestWorldAabbChildren(&excessiveDepthAabb, index, index + 1, 1);
    Expect(
        db::validation::ValidateWorldAabbTopology(
            excessiveDepthAabb.data(),
            excessiveDepthAabb.size(),
            0,
            0,
            aabbDepths,
            sizeof(aabbDepths))
            == db::validation::WorldAabbTopologyStatus::DepthLimitExceeded,
        "world AABB topology beyond the recursion-depth budget rejected");

    std::array<TestDiskWorldAabbNode, 4> validDiskAabbForest = {{
        {0, 4, 2},
        {0, 2, 0},
        {2, 2, 0},
        {4, 1, 0},
    }};
    std::uint8_t diskAabbRoots[65] = {};
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            validDiskAabbForest.data(),
            validDiskAabbForest.size(),
            5,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::Ok
            && diskAabbRoots[0] == 1
            && diskAabbRoots[1] == 0
            && diskAabbRoots[2] == 0
            && diskAabbRoots[3] == 1,
        "valid implicit BSP AABB forest identifies exact roots");
    auto invalidDiskAabbForest = validDiskAabbForest;
    invalidDiskAabbForest[0].childCount = 4;
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            invalidDiskAabbForest.data(),
            invalidDiskAabbForest.size(),
            5,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::InvalidChildSpan,
        "implicit BSP AABB child block beyond the lump rejected");
    invalidDiskAabbForest = validDiskAabbForest;
    invalidDiskAabbForest[2].firstSurface = 4;
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            invalidDiskAabbForest.data(),
            invalidDiskAabbForest.size(),
            5,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::InvalidSurfaceRange,
        "implicit BSP AABB surface span beyond the world rejected");
    invalidDiskAabbForest = validDiskAabbForest;
    invalidDiskAabbForest[2].firstSurface = 0;
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            invalidDiskAabbForest.data(),
            invalidDiskAabbForest.size(),
            5,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::InvalidSurfaceRange,
        "implicit BSP AABB child surface ranges must partition their parent");
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            validDiskAabbForest.data(),
            validDiskAabbForest.size(),
            5,
            diskAabbRoots,
            3)
            == db::validation::WorldAabbTopologyStatus::InvalidWorkBuffer,
        "implicit BSP AABB root scratch capacity enforced");
    std::array<TestDiskWorldAabbNode, 2> excessiveLeafReferences = {{
        {0, 3, 0},
        {2, 3, 0},
    }};
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            excessiveLeafReferences.data(),
            excessiveLeafReferences.size(),
            5,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::InvalidSurfaceRange,
        "implicit BSP AABB aggregate leaf references cannot overflow no-decal output");
    const std::array<TestDiskWorldAabbNode, 1> excessiveCombinedPartitions = {{
        {0, UINT32_C(50000), 0},
    }};
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            excessiveCombinedPartitions.data(),
            excessiveCombinedPartitions.size(),
            50000,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::Ok,
        "implicit BSP AABB leaves may cover a large representable static partition");
    const std::array<TestDiskWorldAabbNode, 1> unrepresentableDiskSurfaceCount = {{
        {0, UINT32_C(65536), 0},
    }};
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            unrepresentableDiskSurfaceCount.data(),
            unrepresentableDiskSurfaceCount.size(),
            65536,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::InvalidSurfaceRange,
        "implicit BSP AABB surface counts must fit their uint16 runtime field");
    const std::array<TestDiskWorldAabbNode, 7> nestedDiskAabbForest = {{
        {0, 3, 2},
        {0, 2, 2},
        {2, 1, 1},
        {0, 1, 0},
        {1, 1, 0},
        {2, 1, 0},
        {3, 1, 0},
    }};
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            nestedDiskAabbForest.data(),
            nestedDiskAabbForest.size(),
            4,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::Ok
            && diskAabbRoots[0] == 1
            && diskAabbRoots[6] == 1,
        "implicit BSP AABB traversal returns from descendants to reserved siblings");
    std::array<TestDiskWorldAabbNode, 64> maximumDepthDiskAabbTree = {};
    for (std::size_t index = 0;
        index + 1 < maximumDepthDiskAabbTree.size();
        ++index)
    {
        maximumDepthDiskAabbTree[index].childCount = 1;
    }
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            maximumDepthDiskAabbTree.data(),
            maximumDepthDiskAabbTree.size(),
            0,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::Ok,
        "implicit BSP AABB topology at the reconstruction depth budget accepted");
    std::array<TestDiskWorldAabbNode, 65> deepDiskAabbTree = {};
    for (std::size_t index = 0; index + 1 < deepDiskAabbTree.size(); ++index)
        deepDiskAabbTree[index].childCount = 1;
    Expect(
        db::validation::ValidateImplicitWorldAabbForest(
            deepDiskAabbTree.data(),
            deepDiskAabbTree.size(),
            0,
            diskAabbRoots,
            sizeof(diskAabbRoots))
            == db::validation::WorldAabbTopologyStatus::DepthLimitExceeded,
        "implicit BSP AABB reconstruction depth is bounded before recursion");

    Expect(db::validation::CanInternString(1), "empty terminated string can be interned");
    Expect(db::validation::CanInternString(65531), "maximum script-memory string can be interned");
    Expect(!db::validation::CanInternString(0), "zero-byte string extent rejected");
    Expect(!db::validation::CanInternString(65532), "script-memory allocation ceiling enforced");
    Expect(db::validation::PointerCountConsistent(false, 0), "null zero-count span is consistent");
    Expect(db::validation::PointerCountConsistent(true, 0), "present zero-count span is consistent");
    Expect(db::validation::PointerCountConsistent(true, 1), "present nonempty span is consistent");
    Expect(!db::validation::PointerCountConsistent(false, 1), "missing nonempty span rejected");
    Expect(!db::validation::PointerCountConsistent(false, -1), "negative span count rejected");
    Expect(!db::validation::PointerCountConsistent(true, -1), "present negative span count rejected");
    Expect(db::validation::CountInRange(96, 96, 65536), "minimum font glyph count accepted");
    Expect(db::validation::CountInRange(65536, 96, 65536), "maximum font glyph count accepted");
    Expect(!db::validation::CountInRange(95, 96, 65536), "short font glyph table rejected");
    Expect(!db::validation::CountInRange(65537, 96, 65536), "oversized font glyph table rejected");
    Expect(!db::validation::CountInRange(1, 2, 1), "invalid count range rejected");
    Expect(db::validation::OptionalMirroredCountInRange(0, 0, 2, 16), "empty mirrored graph accepted");
    Expect(db::validation::OptionalMirroredCountInRange(2, 2, 2, 16), "minimum mirrored graph accepted");
    Expect(db::validation::OptionalMirroredCountInRange(16, 16, 2, 16), "maximum mirrored graph accepted");
    Expect(!db::validation::OptionalMirroredCountInRange(1, 1, 2, 16), "short mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(17, 17, 2, 16), "oversized mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(4, 5, 2, 16), "mismatched mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(-1, -1, 2, 16), "negative mirrored graph rejected");

    const float minimumGraph[][2] = {{0.0f, 0.2f}, {1.0f, 0.4f}};
    const float validGraph[][2] = {{0.0f, 0.2f}, {0.5f, 0.8f}, {1.0f, 0.4f}};
    const float missingStart[][2] = {{0.1f, 0.2f}, {1.0f, 0.4f}};
    const float missingEnd[][2] = {{0.0f, 0.2f}, {0.9f, 0.4f}};
    const float duplicateX[][2] = {{0.0f, 0.2f}, {0.5f, 0.8f}, {0.5f, 0.4f}, {1.0f, 0.3f}};
    const float decreasingX[][2] = {{0.0f, 0.2f}, {0.7f, 0.8f}, {0.5f, 0.4f}, {1.0f, 0.3f}};
    const float invalidY[][2] = {{0.0f, -0.1f}, {1.0f, 0.4f}};
    const float nanGraph[][2] = {
        {0.0f, 0.2f},
        {(std::numeric_limits<float>::quiet_NaN)(), 0.8f},
        {1.0f, 0.4f}};
    const float infiniteGraph[][2] = {
        {0.0f, 0.2f},
        {0.5f, (std::numeric_limits<float>::infinity)()},
        {1.0f, 0.4f}};
    float maximumGraph[16][2] = {};
    for (std::uint32_t index = 0; index < 16; ++index)
    {
        maximumGraph[index][0] = static_cast<float>(index) / 15.0f;
        maximumGraph[index][1] = 0.5f;
    }
    Expect(db::validation::NormalizedGraphKnots(minimumGraph, 2), "two-knot graph accepted");
    Expect(db::validation::NormalizedGraphKnots(validGraph, 3), "normalized graph accepted");
    Expect(db::validation::NormalizedGraphKnots(maximumGraph, 16), "sixteen-knot graph accepted");
    Expect(!db::validation::NormalizedGraphKnots(nullptr, 3), "missing graph rejected");
    Expect(!db::validation::NormalizedGraphKnots(validGraph, 1), "single-knot graph rejected");
    Expect(!db::validation::NormalizedGraphKnots(missingStart, 2), "graph missing zero endpoint rejected");
    Expect(!db::validation::NormalizedGraphKnots(missingEnd, 2), "graph missing one endpoint rejected");
    Expect(!db::validation::NormalizedGraphKnots(duplicateX, 4), "duplicate graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(decreasingX, 4), "decreasing graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(invalidY, 2), "out-of-range graph value rejected");
    Expect(!db::validation::NormalizedGraphKnots(nanGraph, 3), "NaN graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(infiniteGraph, 3), "infinite graph value rejected");

    Expect(db::validation::MaterialVertexRoutingValid(0, 0), "minimum material vertex route accepted");
    Expect(db::validation::MaterialVertexRoutingValid(8, 11), "maximum material vertex route accepted");
    Expect(!db::validation::MaterialVertexRoutingValid(9, 0), "invalid material vertex source rejected");
    Expect(!db::validation::MaterialVertexRoutingValid(0, 12), "invalid material vertex destination rejected");
    Expect(db::validation::CountInRange(1, 1, 12), "minimum material vertex route count accepted");
    Expect(db::validation::CountInRange(12, 1, 12), "maximum unique material vertex routes accepted");
    Expect(!db::validation::CountInRange(0, 1, 12), "empty material vertex route table rejected");
    Expect(!db::validation::CountInRange(13, 1, 12), "oversized material vertex route table rejected");
    Expect(db::validation::CountInRange(1, 1, 4), "minimum material technique pass count accepted");
    Expect(db::validation::CountInRange(4, 1, 4), "maximum material technique pass count accepted");
    Expect(!db::validation::CountInRange(0, 1, 4), "empty material technique rejected");
    Expect(!db::validation::CountInRange(5, 1, 4), "oversized material technique rejected");
    std::uint32_t techniqueBytes = UINT32_MAX;
    Expect(
        db::validation::MaterialTechniqueDiskBytes(1, &techniqueBytes)
            && techniqueBytes == 28,
        "one-pass material technique disk extent accepted");
    Expect(
        db::validation::MaterialTechniqueDiskBytes(4, &techniqueBytes)
            && techniqueBytes == 88,
        "four-pass material technique disk extent accepted");
    Expect(
        !db::validation::MaterialTechniqueDiskBytes(0, &techniqueBytes)
            && techniqueBytes == 0,
        "empty material technique disk extent rejected");
    techniqueBytes = UINT32_MAX;
    Expect(
        !db::validation::MaterialTechniqueDiskBytes(5, &techniqueBytes)
            && techniqueBytes == 0,
        "oversized material technique disk extent rejected");
    Expect(
        !db::validation::MaterialTechniqueDiskBytes(1, nullptr),
        "null material technique disk extent output rejected");

    uint32_t textureTableBytes = UINT32_C(0xFFFFFFFF);
    Expect(
        db::validation::MaterialTextureTableDiskBytes(1, &textureTableBytes)
            && textureTableBytes == disk32::kMaterialTextureDefBytes,
        "single-entry material texture-table extent accepted");
    Expect(
        db::validation::MaterialTextureTableDiskBytes(255, &textureTableBytes)
            && textureTableBytes == 255 * disk32::kMaterialTextureDefBytes,
        "maximum material texture-table extent accepted");
    Expect(
        !db::validation::MaterialTextureTableDiskBytes(0, &textureTableBytes)
            && textureTableBytes == 0,
        "empty completed material texture table rejected");
    Expect(
        !db::validation::MaterialTextureTableDiskBytes(256, &textureTableBytes)
            && textureTableBytes == 0,
        "oversized material texture table rejected");
    Expect(
        !db::validation::MaterialTextureTableDiskBytes(1, nullptr),
        "null material texture-table extent output rejected");
    Expect(
        db::validation::MaterialTextureHeaderValid(1, 0, true, 'a', 'z'),
        "minimum material texture header accepted");
    Expect(
        db::validation::MaterialTextureHeaderValid(23, 11, true, '$', 'p'),
        "maximum decoded material sampler and semantic accepted");
    Expect(
        !db::validation::MaterialTextureHeaderValid(0, 0, true, 'a', 'z'),
        "material texture without a filter rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(8, 0, true, 'a', 'z'),
        "material texture mipmap state without a filter rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(25, 0, true, 'a', 'z'),
        "out-of-range decoded material sampler rejected");
    Expect(
        db::validation::MaterialTextureHeaderValid(1, 3, true, 'a', 'z'),
        "reserved in-range material texture semantic accepted");
    Expect(
        !db::validation::MaterialTextureHeaderValid(1, 12, true, 'a', 'z'),
        "out-of-range material texture semantic rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(1, 0, false, 'a', 'z'),
        "material texture without a payload rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(1, 0, true, 0, 'z'),
        "material texture without a name start rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(1, 0, true, 'a', 0),
        "material texture without a name end rejected");

    struct HashEntry
    {
        uint32_t nameHash;
    };
    const HashEntry sortedHashes[] = {
        {1},
        {2},
        {UINT32_MAX}};
    const HashEntry duplicateHashes[] = {{1}, {1}};
    const HashEntry descendingHashes[] = {{2}, {1}};
    Expect(
        db::validation::StrictlyIncreasingNameHashes<HashEntry>(nullptr, 0),
        "empty material name-hash table accepted");
    Expect(
        !db::validation::StrictlyIncreasingNameHashes<HashEntry>(nullptr, 1),
        "missing material name-hash table rejected");
    Expect(
        db::validation::StrictlyIncreasingNameHashes(sortedHashes, 3),
        "strictly increasing material name hashes accepted");
    Expect(
        !db::validation::StrictlyIncreasingNameHashes(duplicateHashes, 2),
        "duplicate material name hashes rejected");
    Expect(
        !db::validation::StrictlyIncreasingNameHashes(descendingHashes, 2),
        "descending material name hashes rejected");
    Expect(
        db::validation::SortedNameHashContains(sortedHashes, 3, 1),
        "first sorted material name hash found");
    Expect(
        db::validation::SortedNameHashContains(sortedHashes, 3, 2),
        "middle sorted material name hash found");
    Expect(
        db::validation::SortedNameHashContains(sortedHashes, 3, UINT32_MAX),
        "last sorted material name hash found");
    Expect(
        !db::validation::SortedNameHashContains(sortedHashes, 3, 0),
        "material name hash below the table rejected");
    Expect(
        !db::validation::SortedNameHashContains(sortedHashes, 3, 3),
        "missing material name hash between entries rejected");
    Expect(
        !db::validation::SortedNameHashContains<HashEntry>(nullptr, 0, 1),
        "material name hash lookup rejects an empty table");
    Expect(
        db::validation::FindSortedNameHash(sortedHashes, 3, 2)
            == &sortedHashes[1],
        "sorted material name-hash lookup returns the matching entry");
    Expect(
        db::validation::FindSortedNameHash(sortedHashes, 3, 1)
            == &sortedHashes[0],
        "sorted material name-hash lookup returns the first entry");
    Expect(
        db::validation::FindSortedNameHash(sortedHashes, 3, UINT32_MAX)
            == &sortedHashes[2],
        "sorted material name-hash lookup returns the last entry");
    Expect(
        db::validation::FindSortedNameHash(sortedHashes, 3, 3) == nullptr,
        "sorted material name-hash lookup returns null for a missing entry");
    Expect(
        db::validation::FindSortedNameHash<HashEntry>(nullptr, 0, 1)
            == nullptr,
        "sorted material name-hash lookup rejects a null table");

    uint32_t materialTableCount = 0;
    Expect(
        db::validation::CheckedMaterialTableCountSum(
            250,
            5,
            &materialTableCount)
            && materialTableCount == 255,
        "maximum layered material table count accepted");
    Expect(
        !db::validation::CheckedMaterialTableCountSum(
            255,
            1,
            &materialTableCount),
        "layered material table count overflow rejected");
    Expect(
        !db::validation::CheckedMaterialTableCountSum(
            256,
            0,
            &materialTableCount),
        "already-oversized layered material table count rejected");
    Expect(
        !db::validation::CheckedMaterialTableCountSum(
            0,
            0,
            nullptr),
        "missing layered material table count result rejected");

    Expect(
        db::validation::MaterialTechniqueStateSpanValid(false, 0, 255, 0),
        "absent material technique uses the state sentinel");
    Expect(
        db::validation::MaterialTechniqueStateSpanValid(true, 4, 132, 136),
        "maximum material technique state span accepted");
    Expect(
        db::validation::MaterialTechniqueStateSpanValid(true, 4, 0, 4),
        "material technique state span at the table start accepted");
    Expect(
        !db::validation::MaterialTechniqueStateSpanValid(true, 1, 0, 137),
        "oversized material state table rejected");
    Expect(
        !db::validation::MaterialTechniqueStateSpanValid(true, 1, 255, 4),
        "present material technique cannot use the state sentinel");
    Expect(
        !db::validation::MaterialTechniqueStateSpanValid(true, 0, 0, 4),
        "material technique without a pass rejected from state mapping");
    Expect(
        !db::validation::MaterialTechniqueStateSpanValid(true, 5, 0, 5),
        "material technique with too many passes rejected from state mapping");
    Expect(
        !db::validation::MaterialTechniqueStateSpanValid(true, 1, 4, 4),
        "material technique state entry at table end rejected");
    Expect(
        !db::validation::MaterialTechniqueStateSpanValid(
            true,
            4,
            UINT32_MAX,
            4),
        "overflow-shaped material technique state entry rejected");
    Expect(
        !db::validation::MaterialTechniqueStateSpanValid(false, 0, 0, 0),
        "absent material technique requires the state sentinel");

    Expect(
        db::validation::MaterialRemapSlotValid(false, 0, false, 0),
        "both empty material remap slots accepted");
    Expect(
        db::validation::MaterialRemapSlotValid(true, 4, false, 0),
        "empty remapped material technique slot accepted");
    Expect(
        db::validation::MaterialRemapSlotValid(true, 4, true, 4),
        "equal material remap pass counts accepted");
    Expect(
        db::validation::MaterialRemapSlotValid(true, 4, true, 1),
        "smaller material remap pass span accepted");
    Expect(
        !db::validation::MaterialRemapSlotValid(false, 0, true, 1),
        "additional remapped material technique rejected");
    Expect(
        !db::validation::MaterialRemapSlotValid(true, 1, true, 4),
        "expanded material remap pass span rejected");
    Expect(
        !db::validation::MaterialRemapSlotValid(true, 0, true, 0),
        "present zero-pass material remap slots rejected");
    Expect(
        !db::validation::MaterialRemapSlotValid(true, 5, true, 5),
        "present oversized material remap slots rejected");

    constexpr uint32_t validRgbBlend = UINT32_C(10)
        | (UINT32_C(10) << 4)
        | (UINT32_C(5) << 8);
    constexpr uint32_t validSeparateBlend = validRgbBlend
        | (UINT32_C(10) << 16)
        | (UINT32_C(10) << 20)
        | (UINT32_C(5) << 24);
    Expect(
        db::validation::MaterialStateBitsDecodeSafe(0),
        "disabled material blend state accepted");
    Expect(
        db::validation::MaterialStateBitsDecodeSafe(validRgbBlend),
        "maximum material RGB blend indexes accepted");
    Expect(
        db::validation::MaterialStateBitsDecodeSafe(validSeparateBlend),
        "maximum separate-alpha blend indexes accepted");
    Expect(
        !db::validation::MaterialStateBitsDecodeSafe(UINT32_C(6) << 8),
        "out-of-range material RGB blend operation rejected");
    Expect(
        !db::validation::MaterialStateBitsDecodeSafe(
            (UINT32_C(11)) | (UINT32_C(1) << 8)),
        "out-of-range material RGB source blend rejected");
    Expect(
        !db::validation::MaterialStateBitsDecodeSafe(
            (UINT32_C(11) << 4) | (UINT32_C(1) << 8)),
        "out-of-range material RGB destination blend rejected");
    Expect(
        !db::validation::MaterialStateBitsDecodeSafe(UINT32_C(1) << 24),
        "alpha blend without RGB blend rejected");
    Expect(
        !db::validation::MaterialStateBitsDecodeSafe(
            (UINT32_C(1) << 8) | (UINT32_C(6) << 24)),
        "out-of-range material alpha blend operation rejected");
    Expect(
        !db::validation::MaterialStateBitsDecodeSafe(
            (UINT32_C(1) << 8)
            | (UINT32_C(11) << 16)
            | (UINT32_C(1) << 24)),
        "out-of-range material alpha source blend rejected");
    Expect(
        !db::validation::MaterialStateBitsDecodeSafe(
            (UINT32_C(1) << 8)
            | (UINT32_C(11) << 20)
            | (UINT32_C(1) << 24)),
        "out-of-range material alpha destination blend rejected");

    Expect(db::validation::WaterGridValid(4, 4), "minimum water FFT grid accepted");
    Expect(db::validation::WaterGridValid(8, 8), "power-of-two water FFT grid accepted");
    Expect(db::validation::WaterGridValid(64, 64), "maximum water FFT grid accepted");
    Expect(!db::validation::WaterGridValid(4, 8), "non-square water FFT grid rejected");
    Expect(!db::validation::WaterGridValid(3, 3), "undersized water FFT grid rejected");
    Expect(!db::validation::WaterGridValid(65, 65), "oversized water FFT grid rejected");
    Expect(!db::validation::WaterGridValid(6, 6), "non-power-of-two water FFT grid rejected");
    Expect(!db::validation::WaterGridValid(-4, -4), "negative water FFT grid rejected");

    Expect(
        db::validation::WaterParametersValid(
            1.0f,
            1.0f,
            800.0f,
            10.0f,
            1.0f,
            0.0f,
            0.5f),
        "finite positive water parameters accepted");
    Expect(
        !db::validation::WaterParametersValid(
            0.0f,
            1.0f,
            800.0f,
            10.0f,
            1.0f,
            0.0f,
            0.5f),
        "zero water world length rejected");
    Expect(
        !db::validation::WaterParametersValid(
            1.0f,
            1.0f,
            800.0f,
            10.0f,
            0.0f,
            0.0f,
            0.5f),
        "zero water wind direction rejected");
    Expect(
        !db::validation::WaterParametersValid(
            1.0f,
            1.0f,
            800.0f,
            10.0f,
            (std::numeric_limits<float>::quiet_NaN)(),
            1.0f,
            0.5f),
        "NaN water parameter rejected");
    Expect(
        !db::validation::WaterParametersValid(
            1.0f,
            1.0f,
            (std::numeric_limits<float>::infinity)(),
            10.0f,
            1.0f,
            0.0f,
            0.5f),
        "infinite water parameter rejected");

    struct ComplexPair
    {
        float real;
        float imag;
    };
    const ComplexPair finiteComplex[] = {{0.0f, 1.0f}, {-2.0f, 3.0f}};
    const ComplexPair nanComplex[] = {
        {0.0f, (std::numeric_limits<float>::quiet_NaN)()}};
    const float finiteFrequencies[] = {0.0f, 1.0f, 100.0f};
    const float negativeFrequencies[] = {0.0f, -1.0f};
    const float infiniteFrequencies[] = {
        0.0f,
        (std::numeric_limits<float>::infinity)()};
    Expect(
        db::validation::FiniteComplexArray(finiteComplex, 2),
        "finite water complex samples accepted");
    Expect(
        !db::validation::FiniteComplexArray(nanComplex, 1),
        "non-finite water complex sample rejected");
    Expect(
        db::validation::FiniteComplexArray<ComplexPair>(nullptr, 0),
        "empty water complex sample array accepted");
    Expect(
        !db::validation::FiniteComplexArray<ComplexPair>(nullptr, 1),
        "missing water complex sample array rejected");
    Expect(
        db::validation::FiniteNonnegativeFloatArray(finiteFrequencies, 3),
        "finite nonnegative water frequencies accepted");
    Expect(
        !db::validation::FiniteNonnegativeFloatArray(negativeFrequencies, 2),
        "negative water frequency rejected");
    Expect(
        !db::validation::FiniteNonnegativeFloatArray(infiniteFrequencies, 2),
        "infinite water frequency rejected");
    Expect(
        db::validation::FiniteNonnegativeFloatArray(nullptr, 0),
        "empty water frequency array accepted");
    Expect(
        !db::validation::FiniteNonnegativeFloatArray(nullptr, 1),
        "missing water frequency array rejected");
    const float finiteConstants[] = {0.0f, -1.0f, 2.0f, 3.0f};
    const float nanConstants[] = {
        0.0f,
        (std::numeric_limits<float>::quiet_NaN)()};
    const float infiniteConstants[] = {
        0.0f,
        (std::numeric_limits<float>::infinity)()};
    Expect(
        db::validation::FiniteFloatArray(finiteConstants, 4),
        "finite water code constants accepted");
    Expect(
        !db::validation::FiniteFloatArray(nanConstants, 2),
        "non-finite water code constant rejected");
    Expect(
        !db::validation::FiniteFloatArray(infiniteConstants, 2),
        "infinite material constant rejected");
    Expect(
        db::validation::FiniteFloatArray(nullptr, 0),
        "empty finite-float array accepted");
    Expect(
        !db::validation::FiniteFloatArray(nullptr, 1),
        "missing finite-float array rejected");
    Expect(
        db::validation::WaterPicmipDimension(8, 0) == 8,
        "water picmip zero preserves its dimension");
    Expect(
        db::validation::WaterPicmipDimension(8, 1) == 4,
        "water picmip one halves its dimension");
    Expect(
        db::validation::WaterPicmipDimension(4, 1) == 4,
        "water picmip preserves the minimum dimension");
    Expect(
        db::validation::WaterPicmipDimension(64, 1) == 32,
        "water picmip halves the maximum dimension");
    Expect(
        db::validation::WaterPicmipDimension(8, 2) == 0,
        "invalid water picmip rejected");
    Expect(
        db::validation::WaterPicmipDimension(6, 1) == 0,
        "non-power-of-two water picmip rejected");

    int amplitudeGrid[64];
    int frequencyGrid[64];
    for (int index = 0; index < 64; ++index)
    {
        amplitudeGrid[index] = index;
        frequencyGrid[index] = 1000 + index;
    }
    Expect(
        db::validation::DownsampleWaterGridInPlace(amplitudeGrid, 8, 4),
        "water amplitude grid downsamples in place");
    Expect(
        db::validation::DownsampleWaterGridInPlace(frequencyGrid, 8, 4),
        "water frequency grid downsamples in place");
    const int expectedPicmipIndices[] = {
        0, 2, 4, 6,
        16, 18, 20, 22,
        32, 34, 36, 38,
        48, 50, 52, 54};
    for (int index = 0; index < 16; ++index)
    {
        Expect(
            amplitudeGrid[index] == expectedPicmipIndices[index],
            "water amplitude picmip selects the expected source sample");
        Expect(
            frequencyGrid[index] == 1000 + expectedPicmipIndices[index],
            "water frequency picmip selects the matching source sample");
    }
    int minimumGrid[16];
    for (int index = 0; index < 16; ++index)
        minimumGrid[index] = index;
    Expect(
        db::validation::DownsampleWaterGridInPlace(minimumGrid, 4, 4),
        "minimum water grid accepts identity picmip");
    Expect(
        minimumGrid[15] == 15,
        "identity water picmip preserves samples");
    Expect(
        !db::validation::DownsampleWaterGridInPlace(minimumGrid, 4, 8),
        "water grid upsampling rejected");
    Expect(
        !db::validation::DownsampleWaterGridInPlace<int>(nullptr, 8, 4),
        "missing water grid rejected before picmip");
    Expect(
        !db::validation::DownsampleWaterGridInPlace(minimumGrid, 6, 4),
        "non-power-of-two source grid rejected before picmip");

    int maximumGrid[4096];
    for (int index = 0; index < 4096; ++index)
        maximumGrid[index] = index;
    Expect(
        db::validation::DownsampleWaterGridInPlace(maximumGrid, 64, 32),
        "maximum water grid downsamples one picmip level");
    Expect(
        maximumGrid[0] == 0
            && maximumGrid[1] == 2
            && maximumGrid[32] == 128
            && maximumGrid[1023] == 4030,
        "maximum water grid selects the expected source samples");

    Expect(
        db::validation::MaterialShaderLoadDefValid(true, 2, 0),
        "minimum shader load definition accepted");
    Expect(
        db::validation::MaterialShaderLoadDefValid(true, UINT16_MAX, 1),
        "maximum shader load definition accepted");
    Expect(
        !db::validation::MaterialShaderLoadDefValid(false, 2, 0),
        "missing shader program rejected");
    Expect(
        !db::validation::MaterialShaderLoadDefValid(true, 1, 0),
        "one-DWORD shader program rejected");
    Expect(
        !db::validation::MaterialShaderLoadDefValid(
            true,
            UINT32_C(65536),
            0),
        "oversized shader program rejected");
    Expect(
        !db::validation::MaterialShaderLoadDefValid(true, 2, 2),
        "unknown shader renderer rejected");

    using db::validation::D3D9ShaderStage;
    const std::uint32_t validVertexShader[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x02000001),
        UINT32_C(0x800F0000),
        UINT32_C(0x90E40000),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t validPixelShader[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x0001FFFE),
        UINT32_C(0x0000FFFF),
        UINT32_C(0x0000FFFF)};
    Expect(
        db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            D3D9ShaderStage::Vertex,
            0),
        "bounded shader-model-2 vertex bytecode accepted");
    Expect(
        db::validation::D3D9ShaderBytecodeValid(
            validPixelShader,
            4,
            D3D9ShaderStage::Pixel,
            1),
        "bounded shader-model-3 pixel bytecode and comment accepted");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            D3D9ShaderStage::Pixel,
            0),
        "vertex bytecode rejected for pixel stage");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validPixelShader,
            4,
            D3D9ShaderStage::Vertex,
            1),
        "pixel bytecode rejected for vertex stage");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            D3D9ShaderStage::Vertex,
            1),
        "shader-model-2 bytecode rejected for renderer 1");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validPixelShader,
            4,
            D3D9ShaderStage::Pixel,
            0),
        "shader-model-3 bytecode rejected for renderer 0");

    const std::uint32_t invalidMajor[] = {
        UINT32_C(0xFFFE0400),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t invalidMinor[] = {
        UINT32_C(0xFFFE0201),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t shaderMissingEnd[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x00000000)};
    const std::uint32_t earlyEnd[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x0000FFFF),
        UINT32_C(0x00000000)};
    const std::uint32_t truncatedInstruction[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x03000001),
        UINT32_C(0x800F0000),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t truncatedComment[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x0002FFFE),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t nonExactEnd[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x0100FFFF),
        UINT32_C(0x00000000),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t reservedInstruction[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x80000000),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t reservedComment[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x8000FFFE),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t unknownOpcode[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x00000031),
        UINT32_C(0x0000FFFF)};
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            invalidMajor,
            2,
            D3D9ShaderStage::Vertex,
            0),
        "unsupported shader model rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            invalidMinor,
            2,
            D3D9ShaderStage::Vertex,
            0),
        "unsupported shader minor version rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            shaderMissingEnd,
            2,
            D3D9ShaderStage::Vertex,
            0),
        "shader missing END token rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            earlyEnd,
            3,
            D3D9ShaderStage::Vertex,
            0),
        "shader trailing data after END rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            truncatedInstruction,
            4,
            D3D9ShaderStage::Vertex,
            0),
        "truncated shader instruction rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            truncatedComment,
            3,
            D3D9ShaderStage::Pixel,
            1),
        "truncated shader comment rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            nonExactEnd,
            4,
            D3D9ShaderStage::Vertex,
            0),
        "END opcode with instruction bits rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            reservedInstruction,
            3,
            D3D9ShaderStage::Pixel,
            1),
        "reserved shader instruction bits rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            reservedComment,
            3,
            D3D9ShaderStage::Pixel,
            1),
        "reserved shader comment bit rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            unknownOpcode,
            3,
            D3D9ShaderStage::Pixel,
            1),
        "unknown shader opcode rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            nullptr,
            2,
            D3D9ShaderStage::Vertex,
            0),
        "null shader bytecode rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            static_cast<D3D9ShaderStage>(2),
            0),
        "unknown shader stage rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            D3D9ShaderStage::Vertex,
            2),
        "unknown shader renderer rejected by bytecode validation");
    Expect(db::validation::MaterialVertexRoutingFollows(0, 1, 0, 2), "ordered material vertex destination accepted");
    Expect(db::validation::MaterialVertexRoutingFollows(0, 11, 1, 0), "ordered material vertex source accepted");
    Expect(!db::validation::MaterialVertexRoutingFollows(1, 0, 0, 11), "decreasing material vertex route rejected");
    Expect(!db::validation::MaterialVertexRoutingFollows(1, 1, 1, 1), "duplicate material vertex route rejected");
    Expect(db::validation::MaterialPassLayoutValid(16, 16, 32, true, 0), "maximum material argument layout accepted");
    Expect(db::validation::MaterialPassLayoutValid(16, 16, 29, true, 7), "material layout with custom samplers accepted");
    Expect(!db::validation::MaterialPassLayoutValid(0, 0, 0, false, 0), "empty material argument layout rejected");
    Expect(!db::validation::MaterialPassLayoutValid(0, 0, 0, true, 0), "present empty material argument layout rejected");
    Expect(!db::validation::MaterialPassLayoutValid(1, 0, 0, false, 0), "missing material argument array rejected");
    Expect(!db::validation::MaterialPassLayoutValid(32, 32, 1, true, 0), "oversized material argument layout rejected");
    Expect(!db::validation::MaterialPassLayoutValid(16, 16, 32, true, 1), "custom sampler beyond argument limit rejected");
    Expect(!db::validation::MaterialPassLayoutValid(0, 0, 0, false, 8), "unknown custom sampler flag rejected");

    using db::validation::MaterialArgumentSegment;
    Expect(db::validation::MaterialArgumentTypeAllowedInSegment(3, MaterialArgumentSegment::PerPrimitive), "per-primitive code constant accepted");
    Expect(!db::validation::MaterialArgumentTypeAllowedInSegment(4, MaterialArgumentSegment::PerPrimitive), "per-primitive sampler rejected");
    Expect(db::validation::MaterialArgumentTypeAllowedInSegment(3, MaterialArgumentSegment::PerObject), "per-object code constant accepted");
    Expect(db::validation::MaterialArgumentTypeAllowedInSegment(4, MaterialArgumentSegment::PerObject), "per-object sampler accepted");
    Expect(!db::validation::MaterialArgumentTypeAllowedInSegment(5, MaterialArgumentSegment::PerObject), "per-object pixel constant rejected");
    Expect(db::validation::MaterialArgumentTypeAllowedInSegment(7, MaterialArgumentSegment::Stable), "stable literal accepted");
    Expect(!db::validation::MaterialArgumentTypeAllowedInSegment(8, MaterialArgumentSegment::Stable), "unknown material argument rejected");

    Expect(db::validation::MaterialArgumentShapeValid(0, 31, 0, 0, 0), "named vertex constant accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(1, 32, 0, 0, 0), "vertex constant destination rejected");
    Expect(db::validation::MaterialArgumentShapeValid(2, 15, 0, 0, 0), "named pixel sampler accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(2, 16, 0, 0, 0), "named pixel sampler destination rejected");
    Expect(db::validation::MaterialArgumentShapeValid(3, 31, 57, 0, 1), "scalar code vertex constant accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(3, 31, 57, 1, 1), "scalar code vertex row rejected");
    Expect(db::validation::MaterialArgumentShapeValid(3, 28, 89, 0, 4), "code vertex matrix accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(3, 29, 89, 0, 4), "vertex destination span rejected");
    Expect(!db::validation::MaterialArgumentShapeValid(3, 28, 90, 0, 4), "vertex code source rejected");
    Expect(!db::validation::MaterialArgumentShapeValid(3, 28, 89, 1, 4), "vertex matrix row span rejected");
    Expect(db::validation::MaterialArgumentShapeValid(4, 15, 26, 0, 0), "code pixel sampler accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(4, 15, 27, 0, 0), "pixel sampler source rejected");
    Expect(db::validation::MaterialArgumentShapeValid(5, 255, 50, 0, 1), "code pixel constant accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(5, 256, 50, 0, 1), "pixel constant destination rejected");
    Expect(!db::validation::MaterialArgumentShapeValid(5, 255, 51, 0, 1), "pixel constant source rejected");
    Expect(db::validation::MaterialArgumentShapeValid(6, 255, 0, 0, 0), "named pixel constant accepted");
    Expect(db::validation::MaterialArgumentShapeValid(7, 255, 0, 0, 0), "literal pixel constant accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(8, 0, 0, 0, 0), "unknown material argument shape rejected");
    Expect(db::validation::MaterialCodeConstantAllowedInSegment(50, MaterialArgumentSegment::Stable), "stable code constant accepted");
    Expect(!db::validation::MaterialCodeConstantAllowedInSegment(51, MaterialArgumentSegment::Stable), "non-stable code constant rejected");
    Expect(db::validation::MaterialCodeConstantAllowedInSegment(51, MaterialArgumentSegment::PerObject), "per-object code constant accepted");
    Expect(!db::validation::MaterialCodeConstantAllowedInSegment(57, MaterialArgumentSegment::PerObject), "per-primitive constant in object segment rejected");
    Expect(db::validation::MaterialCodeConstantAllowedInSegment(89, MaterialArgumentSegment::PerPrimitive), "per-primitive code constant accepted");
    Expect(!db::validation::MaterialCodeConstantAllowedInSegment(90, MaterialArgumentSegment::PerPrimitive), "out-of-range code constant rejected");
    Expect(db::validation::MaterialCodeSamplerAllowedInSegment(0, MaterialArgumentSegment::Stable), "stable code sampler accepted");
    Expect(db::validation::MaterialCodeSamplerAllowedInSegment(25, MaterialArgumentSegment::PerObject), "per-object code sampler accepted");
    Expect(!db::validation::MaterialCodeSamplerAllowedInSegment(4, MaterialArgumentSegment::Stable), "custom code sampler in stable args rejected");
    Expect(!db::validation::MaterialCodeSamplerAllowedInSegment(26, MaterialArgumentSegment::PerObject), "custom code sampler in object args rejected");
    Expect(!db::validation::MaterialCodeSamplerAllowedInSegment(0, MaterialArgumentSegment::PerPrimitive), "code sampler in primitive args rejected");

    const std::uint16_t validIndices[] = {0, 2, 3};
    const std::uint16_t invalidIndices[] = {0, 4};
    Expect(db::validation::AllU16Below(validIndices, 3, 4), "bounded uint16 indices accepted");
    Expect(!db::validation::AllU16Below(invalidIndices, 2, 4), "out-of-range uint16 index rejected");
    Expect(db::validation::AllU16Below(nullptr, 0, 0), "empty uint16 index list accepted");
    Expect(!db::validation::AllU16Below(nullptr, 1, 4), "missing uint16 index list rejected");

    std::int32_t brushSideBytes = -1;
    std::int32_t brushPlaneBytes = -1;
    std::int32_t brushAdjacencyBytes = -1;
    Expect(
        db::validation::BrushWrapperLayoutValid(
            true,
            true,
            2,
            true,
            8,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes)
            && brushSideBytes == 24
            && brushPlaneBytes == 40
            && brushAdjacencyBytes == 8,
        "bounded brush child layout accepted");
    Expect(
        db::validation::BrushWrapperLayoutValid(
            true,
            true,
            db::validation::kMaxBrushNonaxialSides,
            false,
            0,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes)
            && brushSideBytes == 312
            && brushPlaneBytes == 520,
        "largest source-compatible brush side layout accepted");
    Expect(
        db::validation::BrushWrapperLayoutValid(
            false,
            false,
            0,
            true,
            db::validation::kMaxBrushAdjacencyEntries,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes)
            && brushAdjacencyBytes
                == db::validation::kMaxBrushAdjacencyEntries,
        "largest source-compatible brush adjacency layout accepted");
    Expect(
        db::validation::BrushWrapperLayoutValid(
            false,
            false,
            0,
            false,
            0,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "empty brush child layout accepted");
    Expect(
        db::validation::BrushWrapperLayoutValid(
            true,
            true,
            0,
            true,
            0,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "present empty brush child spans accepted");
    Expect(
        !db::validation::BrushWrapperLayoutValid(
            false,
            true,
            1,
            true,
            8,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "missing nonempty brush sides rejected");
    Expect(
        !db::validation::BrushWrapperLayoutValid(
            true,
            false,
            1,
            true,
            8,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "missing nonempty brush planes rejected");
    Expect(
        !db::validation::BrushWrapperLayoutValid(
            true,
            true,
            1,
            false,
            1,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "missing nonempty brush adjacency rejected");
    Expect(
        !db::validation::BrushWrapperLayoutValid(
            true,
            true,
            1,
            true,
            -1,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "negative brush adjacency count rejected");
    Expect(
        !db::validation::BrushWrapperLayoutValid(
            true,
            true,
            db::validation::kMaxBrushNonaxialSides + 1,
            true,
            1,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "brush side count beyond the source builder limit rejected");
    Expect(
        !db::validation::BrushWrapperLayoutValid(
            true,
            true,
            1,
            true,
            db::validation::kMaxBrushAdjacencyEntries + 1,
            &brushSideBytes,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "brush adjacency count beyond the source builder limit rejected");
    Expect(
        !db::validation::BrushWrapperLayoutValid(
            true,
            true,
            1,
            true,
            1,
            nullptr,
            &brushPlaneBytes,
            &brushAdjacencyBytes),
        "brush layout requires every extent output");

    Expect(
        db::validation::BrushAdjacencySpanValid(3, 5, 8),
        "brush adjacency span ending at the extent accepted");
    Expect(
        db::validation::BrushAdjacencySpanValid(0, 0, 0),
        "empty brush adjacency span accepted");
    Expect(
        !db::validation::BrushAdjacencySpanValid(-1, 1, 8),
        "negative brush adjacency offset rejected");
    Expect(
        !db::validation::BrushAdjacencySpanValid(9, 0, 8),
        "brush adjacency offset beyond the extent rejected");
    Expect(
        !db::validation::BrushAdjacencySpanValid(7, 2, 8),
        "brush adjacency span beyond the extent rejected");

    std::array<TestBrushPlane,
        db::validation::kMaxBrushNonaxialSides> maximumBrushPlanes = {};
    std::array<TestBrushSide,
        db::validation::kMaxBrushNonaxialSides> maximumBrushSides = {};
    std::array<std::uint8_t,
        db::validation::kMaxBrushAdjacencyEntries> maximumBrushAdjacency = {};
    TestBrushWrapper maximumBrush = {};
    maximumBrush.numsides = db::validation::kMaxBrushNonaxialSides;
    maximumBrush.sides = maximumBrushSides.data();
    maximumBrush.baseAdjacentSide = maximumBrushAdjacency.data();
    maximumBrush.totalEdgeCount =
        db::validation::kMaxBrushAdjacencyEntries;
    maximumBrush.planes = maximumBrushPlanes.data();
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            const std::uint32_t axialSide = axis * 2 + direction;
            maximumBrush.firstAdjacentSideOffsets[direction][axis] =
                static_cast<std::int16_t>(
                    axialSide * db::validation::kMaxBrushWindingEdges);
            maximumBrush.edgeCount[direction][axis] =
                static_cast<std::uint8_t>(
                    db::validation::kMaxBrushWindingEdges);
        }
    }
    for (std::size_t sideIndex = 0;
        sideIndex < maximumBrushSides.size();
        ++sideIndex)
    {
        maximumBrushSides[sideIndex].plane = &maximumBrushPlanes[sideIndex];
        maximumBrushSides[sideIndex].firstAdjacentSideOffset =
            static_cast<std::int16_t>(
                (sideIndex + 6) * db::validation::kMaxBrushWindingEdges);
        maximumBrushSides[sideIndex].edgeCount =
            static_cast<std::uint8_t>(
                db::validation::kMaxBrushWindingEdges);
        maximumBrushPlanes[sideIndex].normal[sideIndex % 3] = 1.0f;
    }
    for (std::size_t edgeIndex = 0;
        edgeIndex < maximumBrushAdjacency.size();
        ++edgeIndex)
    {
        maximumBrushAdjacency[edgeIndex] =
            static_cast<std::uint8_t>(edgeIndex % 32);
    }
    Expect(
        db::validation::BrushWrapperRuntimeValid(maximumBrush),
        "largest source-compatible brush runtime graph accepted");
    maximumBrushAdjacency.back() = 32;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(maximumBrush),
        "largest brush graph still enforces its final adjacency identity");

    TestPhysicsFixture physics = {};
    PopulateValidPhysicsFixture(&physics);
    std::uint32_t brushMaterialIndex = UINT32_MAX;
    Expect(
        db::validation::BrushMaterialIndex(
            &physics.brush,
            0,
            1,
            &brushMaterialIndex)
            && brushMaterialIndex == 0,
        "bounded axial brush material accepted");
    Expect(
        db::validation::BrushMaterialIndex(
            &physics.brush,
            6,
            1,
            &brushMaterialIndex)
            && brushMaterialIndex == 0,
        "bounded nonaxial brush material accepted");
    Expect(
        !db::validation::BrushMaterialIndex(
            &physics.brush,
            8,
            1,
            &brushMaterialIndex),
        "out-of-range brush side rejected before material access");
    Expect(
        !db::validation::BrushMaterialIndex(
            &physics.brush,
            0,
            0,
            &brushMaterialIndex),
        "missing brush material table rejected");
    Expect(
        !db::validation::BrushMaterialIndex(
            &physics.brush,
            0,
            1,
            nullptr),
        "brush material lookup requires an output");
    physics.brush.axialMaterialNum[0][0] = -1;
    Expect(
        !db::validation::BrushMaterialIndex(
            &physics.brush,
            0,
            1,
            &brushMaterialIndex),
        "negative axial brush material rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.brush.axialMaterialNum[0][0] = 1;
    Expect(
        !db::validation::BrushMaterialIndex(
            &physics.brush,
            0,
            1,
            &brushMaterialIndex),
        "oversized axial brush material rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.sides[0].materialNum = 1;
    Expect(
        !db::validation::BrushMaterialIndex(
            &physics.brush,
            6,
            1,
            &brushMaterialIndex),
        "oversized nonaxial brush material rejected");
    PopulateValidPhysicsFixture(&physics);
    Expect(
        db::validation::BrushWrapperRuntimeValid(physics.brush),
        "valid brush runtime graph accepted");
    Expect(
        db::validation::PhysGeomListRuntimeValid(physics.list),
        "valid brush, box, and cylinder geometry list accepted");

    PopulateValidPhysicsFixture(&physics);
    physics.brush.mins[0] = physics.brush.maxs[0];
    Expect(
        db::validation::BrushWrapperRuntimeValid(physics.brush),
        "zero-width but ordered brush bound accepted");
    PopulateValidPhysicsFixture(&physics);
    physics.brush.mins[0] = physics.brush.maxs[0] + 1.0f;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "inverted brush bound rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.brush.maxs[1] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "non-finite brush bound rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.sides[1].plane = &physics.planes[0];
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "brush side pointing at the wrong owned plane rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.planes[0].normal[2] =
        (std::numeric_limits<float>::infinity)();
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "non-finite brush plane rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.brush.axialMaterialNum[1][2] = -1;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "nonzero axial model-brush material rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.sides[0].materialNum = 1;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "nonzero nonaxial model-brush material rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.brush.firstAdjacentSideOffsets[0][0] = -1;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "negative axial adjacency offset rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.brush.firstAdjacentSideOffsets[1][2] = 8;
    physics.brush.edgeCount[1][2] = 1;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "axial adjacency span beyond the table rejected");
    PopulateValidPhysicsFixture(&physics);
    std::array<std::uint8_t, 13> longAxialAdjacency = {};
    physics.brush.baseAdjacentSide = longAxialAdjacency.data();
    physics.brush.totalEdgeCount =
        static_cast<std::int32_t>(longAxialAdjacency.size());
    physics.brush.edgeCount[0][1] =
        static_cast<std::uint8_t>(
            db::validation::kMaxBrushWindingEdges + 1);
    physics.brush.firstAdjacentSideOffsets[0][1] = 0;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "axial winding beyond the source edge limit rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.sides[0].firstAdjacentSideOffset = -1;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "negative nonaxial adjacency offset rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.sides[1].firstAdjacentSideOffset = 8;
    physics.sides[1].edgeCount = 1;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "nonaxial adjacency span beyond the table rejected");
    PopulateValidPhysicsFixture(&physics);
    std::array<std::uint8_t, 20> longSideAdjacency = {};
    physics.brush.baseAdjacentSide = longSideAdjacency.data();
    physics.brush.totalEdgeCount =
        static_cast<std::int32_t>(longSideAdjacency.size());
    physics.sides[0].edgeCount =
        static_cast<std::uint8_t>(
            db::validation::kMaxBrushWindingEdges + 1);
    physics.sides[0].firstAdjacentSideOffset = 0;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "nonaxial winding beyond the source edge limit rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.adjacency[7] = 8;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "adjacent side outside the axial and nonaxial side set rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.brush.baseAdjacentSide = nullptr;
    Expect(
        !db::validation::BrushWrapperRuntimeValid(physics.brush),
        "missing nonempty brush adjacency table rejected");

    PopulateValidPhysicsFixture(&physics);
    physics.brush.totalEdgeCount = 0;
    physics.brush.baseAdjacentSide = nullptr;
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            physics.brush.firstAdjacentSideOffsets[direction][axis] = 0;
            physics.brush.edgeCount[direction][axis] = 0;
        }
    }
    for (TestBrushSide &side : physics.sides)
    {
        side.firstAdjacentSideOffset = 0;
        side.edgeCount = 0;
    }
    Expect(
        db::validation::BrushWrapperRuntimeValid(physics.brush),
        "brush with consistently empty edge spans accepted");

    PopulateValidPhysicsFixture(&physics);
    physics.brush.numsides = 0;
    physics.brush.sides = nullptr;
    physics.brush.planes = nullptr;
    physics.brush.totalEdgeCount = 6;
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            const std::uint32_t index = axis * 2 + direction;
            physics.brush.firstAdjacentSideOffsets[direction][axis] =
                static_cast<std::int16_t>(index);
            physics.brush.edgeCount[direction][axis] = 1;
            physics.adjacency[index] = static_cast<std::uint8_t>(index);
        }
    }
    Expect(
        db::validation::BrushWrapperRuntimeValid(physics.brush),
        "axial-only brush graph accepted");

    std::int32_t physGeomBytes = -1;
    Expect(
        db::validation::PhysGeomListLayoutValid(
            true,
            3,
            &physGeomBytes)
            && physGeomBytes == 204,
        "bounded physics geometry list accepted");
    Expect(
        !db::validation::PhysGeomListLayoutValid(
            false,
            0,
            &physGeomBytes),
        "empty physics geometry list rejected");
    Expect(
        !db::validation::PhysGeomListLayoutValid(
            false,
            1,
            &physGeomBytes),
        "missing nonempty physics geometry list rejected");
    Expect(
        !db::validation::PhysGeomListLayoutValid(
            true,
            (std::numeric_limits<std::uint32_t>::max)(),
            &physGeomBytes),
        "oversized physics geometry list rejected");
    Expect(
        !db::validation::PhysGeomListLayoutValid(true, 1, nullptr),
        "physics geometry layout requires an extent output");

    PopulateValidPhysicsFixture(&physics);
    physics.geoms[0].type = 1;
    Expect(
        !db::validation::PhysGeomInfoRuntimeValid(physics.geoms[0]),
        "primitive geometry carrying a brush rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.geoms[0].brush = nullptr;
    Expect(
        !db::validation::PhysGeomInfoRuntimeValid(physics.geoms[0]),
        "brush geometry missing its brush rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.geoms[1].type = 2;
    Expect(
        !db::validation::PhysGeomInfoRuntimeValid(physics.geoms[1]),
        "unknown physics geometry type rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.geoms[1].halfLengths[2] = 0.0f;
    Expect(
        !db::validation::PhysGeomInfoRuntimeValid(physics.geoms[1]),
        "non-positive used box dimension rejected");
    PopulateValidPhysicsFixture(&physics);
    Expect(
        physics.geoms[2].halfLengths[2] == 0.0f
            && db::validation::PhysGeomInfoRuntimeValid(physics.geoms[2]),
        "unused zero cylinder dimension accepted");
    physics.geoms[2].halfLengths[0] = -1.0f;
    Expect(
        !db::validation::PhysGeomInfoRuntimeValid(physics.geoms[2]),
        "non-positive cylinder half-height rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.geoms[1].orientation[2][0] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::PhysGeomInfoRuntimeValid(physics.geoms[1]),
        "non-finite primitive orientation rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.geoms[1].offset[1] =
        (std::numeric_limits<float>::infinity)();
    Expect(
        !db::validation::PhysGeomInfoRuntimeValid(physics.geoms[1]),
        "non-finite primitive offset rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.geoms[1].halfLengths[0] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::PhysGeomInfoRuntimeValid(physics.geoms[1]),
        "non-finite primitive dimensions rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.list.mass.productsOfInertia[2] =
        (std::numeric_limits<float>::infinity)();
    Expect(
        !db::validation::PhysGeomListRuntimeValid(physics.list),
        "non-finite physics mass rejected");
    PopulateValidPhysicsFixture(&physics);
    physics.brush.planes[0].dist =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::PhysGeomListRuntimeValid(physics.list),
        "invalid nested brush rejects the physics geometry list");

    TestPhysGeomList emptyPhysList = {};
    Expect(
        !db::validation::PhysGeomListRuntimeValid(emptyPhysList),
        "empty physics geometry runtime list rejected");

    TestClipMapFixture clipMap = {};
    PopulateValidClipMapFixture(&clipMap);
    db::validation::ClipMapBrushLayoutExtents clipExtents = {};
    Expect(
        db::validation::ClipMapBrushLayoutValid(
            clipMap.map,
            &clipExtents)
            && clipExtents.planeBytes == 60
            && clipExtents.materialBytes == 144
            && clipExtents.brushSideBytes == 36
            && clipExtents.brushEdgeBytes == 3
            && clipExtents.brushBytes == 160,
        "checked clipmap brush global extents accepted");
    Expect(
        db::validation::ClipMapBrushLayoutValid(
            true, 65536,
            true, 1,
            false, 0,
            false, 0,
            false, 0,
            &clipExtents)
            && clipExtents.planeBytes == 1310720,
        "maximum source clipmap plane count accepted");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 0,
            true, 1,
            false, 0,
            false, 0,
            false, 0,
            &clipExtents),
        "empty clipmap plane table rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 65537,
            true, 1,
            false, 0,
            false, 0,
            false, 0,
            &clipExtents),
        "clipmap plane count beyond 65536 rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            false, 1,
            true, 1,
            false, 0,
            false, 0,
            false, 0,
            &clipExtents),
        "missing required clipmap plane table rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            false, 0,
            false, 0,
            false, 0,
            false, 0,
            &clipExtents),
        "empty clipmap material table rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            false, 1,
            false, 0,
            false, 0,
            false, 0,
            &clipExtents),
        "missing nonempty clipmap material table rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            true,
                static_cast<std::uint64_t>(INT32_MAX) / 72u + 1u,
            false, 0,
            false, 0,
            false, 0,
            &clipExtents),
        "overflowing clipmap material extent rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            true, 1,
            true,
                static_cast<std::uint64_t>(INT32_MAX) / 12u + 1u,
            false, 0,
            false, 0,
            &clipExtents),
        "overflowing clipmap brush-side extent rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            true, 1,
            false, 0,
            true, static_cast<std::uint64_t>(INT32_MAX) + 1u,
            false, 0,
            &clipExtents),
        "overflowing clipmap brush-edge extent rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            true, 1,
            false, 0,
            false, 0,
            true,
                static_cast<std::uint64_t>(INT32_MAX) / 80u + 1u,
            &clipExtents),
        "overflowing clipmap brush extent rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            true, 1,
            false, 1,
            false, 0,
            false, 0,
            &clipExtents),
        "missing nonempty clipmap brush-side table rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            true, 1,
            false, 0,
            false, 1,
            false, 0,
            &clipExtents),
        "missing nonempty clipmap brush-edge table rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            true, 1,
            false, 0,
            false, 0,
            false, 1,
            &clipExtents),
        "missing nonempty clipmap brush table rejected");
    Expect(
        !db::validation::ClipMapBrushLayoutValid(
            true, 1,
            true, 1,
            false, 0,
            false, 0,
            false, 0,
            nullptr),
        "clipmap brush layout requires extent output");

    Expect(
        db::validation::ClipMapPlaneValid(clipMap.planes[0]),
        "consistent positive axial clipmap plane accepted");
    Expect(
        db::validation::ClipMapPlaneValid(clipMap.planes[1]),
        "consistent negative nonaxial clipmap plane accepted");
    clipMap.planes[0].type = 3;
    Expect(
        !db::validation::ClipMapPlaneValid(clipMap.planes[0]),
        "clipmap plane type inconsistent with its normal rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.planes[1].signbits = 0;
    Expect(
        !db::validation::ClipMapPlaneValid(clipMap.planes[1]),
        "clipmap plane sign bits inconsistent with its normal rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.planes[2].dist =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::ClipMapPlaneValid(clipMap.planes[2]),
        "non-finite clipmap plane rejected");

    PopulateValidClipMapFixture(&clipMap);
    std::uint64_t clipElementIndex = UINT64_MAX;
    Expect(
        db::validation::ExactArrayElementIndex(
            clipMap.planes.data(),
            3,
            &clipMap.planes[2],
            &clipElementIndex)
            && clipElementIndex == 2,
        "exact clipmap array element membership accepted");
    const auto misalignedClipPlane = reinterpret_cast<TestClipPlane *>(
        reinterpret_cast<std::uintptr_t>(clipMap.planes.data()) + 1u);
    Expect(
        !db::validation::ExactArrayElementIndex(
            clipMap.planes.data(),
            3,
            misalignedClipPlane,
            &clipElementIndex),
        "misaligned interior clipmap array pointer rejected");
    Expect(
        !db::validation::ExactArrayElementIndex(
            clipMap.planes.data(),
            3,
            &clipMap.planes[3],
            &clipElementIndex),
        "one-past clipmap array pointer rejected");
    Expect(
        !db::validation::ExactArrayElementIndex(
            clipMap.planes.data(),
            3,
            &clipMap.planes[0],
            nullptr),
        "exact clipmap membership requires index output");

    std::array<TestClipBrushSide,
        db::validation::kMaxClipMapBrushNonaxialSides + 1>
        maximumClipSides = {};
    TestClipBrush maximumClipBrush = {};
    maximumClipBrush.numsides =
        db::validation::kMaxClipMapBrushNonaxialSides;
    maximumClipBrush.sides = maximumClipSides.data();
    std::uint32_t clipBrushEdgeCount = UINT32_MAX;
    Expect(
        db::validation::ClipMapBrushAdjacencyExtentValid(
            maximumClipBrush,
            &clipBrushEdgeCount)
            && clipBrushEdgeCount == 0,
        "250-side clipmap brush adjacency accepted");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.brushes[0].baseAdjacentSide =
        reinterpret_cast<std::uint8_t *>(static_cast<std::uintptr_t>(1));
    Expect(
        db::validation::ClipMapBrushAdjacencyPrefixExtent(
            clipMap.brushes[0],
            &clipBrushEdgeCount)
            && clipBrushEdgeCount == 2,
        "clipmap adjacency extent derivation does not read an unresolved token");
    maximumClipBrush.numsides =
        db::validation::kMaxClipMapBrushNonaxialSides + 1;
    Expect(
        !db::validation::ClipMapBrushAdjacencyExtentValid(
            maximumClipBrush,
            &clipBrushEdgeCount),
        "251-side clipmap brush adjacency rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.brushes[0].firstAdjacentSideOffsets[0][0] = -1;
    Expect(
        !db::validation::ClipMapBrushAdjacencyExtentValid(
            clipMap.brushes[0],
            &clipBrushEdgeCount),
        "negative clipmap brush adjacency prefix rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.sides[0].firstAdjacentSideOffset = 2;
    Expect(
        !db::validation::ClipMapBrushAdjacencyExtentValid(
            clipMap.brushes[0],
            &clipBrushEdgeCount),
        "gap in clipmap brush adjacency prefix rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.sides[0].firstAdjacentSideOffset = 0;
    Expect(
        !db::validation::ClipMapBrushAdjacencyExtentValid(
            clipMap.brushes[0],
            &clipBrushEdgeCount),
        "overlap in clipmap brush adjacency prefix rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.edges[0] = 8;
    Expect(
        !db::validation::ClipMapBrushAdjacencyExtentValid(
            clipMap.brushes[0],
            &clipBrushEdgeCount),
        "clipmap brush adjacency side identity at the limit rejected");

    PopulateValidClipMapFixture(&clipMap);
    Expect(
        db::validation::ClipMapBrushGraphValid(clipMap.map),
        "complete clipmap brush graph with a shared plane accepted");
    clipMap.map.numBrushSides = 4;
    clipMap.brushes[1].sides = &clipMap.sides[3];
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "gap between clipmap brush side slices rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.brushes[1].sides = &clipMap.sides[1];
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "overlap between clipmap brush side slices rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.map.numBrushSides = 4;
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "leftover global clipmap brush side rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.map.numBrushEdges = 4;
    clipMap.brushes[1].baseAdjacentSide = &clipMap.edges[3];
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "gap between clipmap brush edge slices rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.brushes[1].baseAdjacentSide = &clipMap.edges[1];
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "overlap between clipmap brush edge slices rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.map.numBrushEdges = 4;
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "leftover global clipmap brush edge rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.brushes[0].axialMaterialNum[0][0] = 2;
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "out-of-range axial clipmap brush material rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.sides[0].materialNum = 2;
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "out-of-range nonaxial clipmap brush material rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.brushes[0].mins[0] = 3.0f;
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "inverted ordinary clipmap brush bounds rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.brushes[0].maxs[1] =
        (std::numeric_limits<float>::infinity)();
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "non-finite ordinary clipmap brush bounds rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.planes[0].signbits = 4;
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "invalid plane metadata rejects the complete clipmap brush graph");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.sides[0].plane = misalignedClipPlane;
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "misaligned clipmap brush plane reference rejected");
    PopulateValidClipMapFixture(&clipMap);
    clipMap.sides[0].plane = &clipMap.planes[3];
    Expect(
        !db::validation::ClipMapBrushGraphValid(clipMap.map),
        "one-past clipmap brush plane reference rejected");

    TestClipBrush clipBox = ValidClipMapBoxBrush(true);
    Expect(
        db::validation::ClipMapBoxBrushValid(clipBox),
        "source FLT_MAX clipmap box-brush sentinel accepted");
    clipBox = ValidClipMapBoxBrush(false);
    Expect(
        db::validation::ClipMapBoxBrushValid(clipBox),
        "ordered runtime clipmap box-brush bounds accepted");
    clipBox.numsides = 1;
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush with a nonaxial side count rejected");
    clipBox = ValidClipMapBoxBrush(false);
    clipBox.sides = clipMap.sides.data();
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush with a side pointer rejected");
    clipBox = ValidClipMapBoxBrush(false);
    clipBox.baseAdjacentSide = clipMap.edges.data();
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush with an adjacency pointer rejected");
    clipBox = ValidClipMapBoxBrush(false);
    clipBox.firstAdjacentSideOffsets[1][2] = 1;
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush with a nonzero adjacency offset rejected");
    clipBox = ValidClipMapBoxBrush(false);
    clipBox.edgeCount[0][1] = 1;
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush with a nonzero adjacency count rejected");
    clipBox = ValidClipMapBoxBrush(false);
    clipBox.axialMaterialNum[0][0] = 0;
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush with a material rejected");
    clipBox = ValidClipMapBoxBrush(false);
    clipBox.contents = 0;
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush without sentinel contents rejected");
    clipBox = ValidClipMapBoxBrush(false);
    clipBox.maxs[2] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush with a non-finite bound rejected");
    clipBox = ValidClipMapBoxBrush(false);
    clipBox.mins[0] = 2.0f;
    Expect(
        !db::validation::ClipMapBoxBrushValid(clipBox),
        "box brush with nonsentinel inverted bounds rejected");

    std::int32_t pathVisibilityBytes = -1;
    Expect(
        db::validation::PathVisibilityBytes(0, &pathVisibilityBytes)
            && pathVisibilityBytes == 0
            && db::validation::PathVisibilityBytes(6, &pathVisibilityBytes)
            && pathVisibilityBytes == 4
            && db::validation::PathVisibilityBytes(
                db::validation::kMaxPathNodes,
                &pathVisibilityBytes)
            && pathVisibilityBytes == 8387584,
        "path visibility uses the complete directed-pair bit matrix");
    Expect(
        !db::validation::PathVisibilityBytes(
            db::validation::kMaxPathNodes + 1u,
            &pathVisibilityBytes)
            && !db::validation::PathVisibilityBytes(1, nullptr),
        "oversized path visibility layouts are rejected");

    TestPathNode pathNodes[6] = {};
    TestPathBaseNode pathBaseNodes[6] = {};
    std::uint16_t pathChainNodeForNode[6] = {0, 1, 2, 3, 4, 5};
    std::uint16_t pathNodeForChainNode[6] = {0, 1, 2, 3, 4, 5};
    std::uint8_t pathVisibility[4] = {};
    TestPathTreeFixture pathTree = {};
    PopulateValidPathTreeFixture(&pathTree);
    Expect(
        db::validation::PathNodeTypeValid(0)
            && db::validation::PathNodeTypeValid(19)
            && !db::validation::PathNodeTypeValid(-1)
            && !db::validation::PathNodeTypeValid(20),
        "path node types must remain safe runtime table and bit indices");
    TestPathData pathData = {};
    pathData.nodeCount = 6;
    pathData.nodes = pathNodes;
    pathData.basenodes = pathBaseNodes;
    pathData.chainNodeForNode = pathChainNodeForNode;
    pathData.nodeForChainNode = pathNodeForChainNode;
    pathData.visBytes = 4;
    pathData.pathVis = pathVisibility;
    pathData.nodeTreeCount =
        static_cast<std::int32_t>(pathTree.trees.size());
    pathData.nodeTree = pathTree.trees.data();
    db::validation::PathDataLayoutExtents pathExtents = {};
    Expect(
        db::validation::PathDataLayoutValid(pathData, &pathExtents)
            && pathExtents.nodeBytes == 768
            && pathExtents.baseNodeBytes == 96
            && pathExtents.chainMapBytes == 12
            && pathExtents.visibilityBytes == 4
            && pathExtents.treeBytes == 112,
        "path-data child extents use fixed disk32 schemas");
    pathData.pathVis = nullptr;
    pathData.visBytes = 0;
    Expect(
        db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "explicit no-cache path visibility form accepted");
    pathData = {};
    Expect(
        db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "empty path data accepted");

    pathData.nodeCount = 6;
    pathData.nodes = pathNodes;
    pathData.basenodes = pathBaseNodes;
    pathData.chainNodeForNode = pathChainNodeForNode;
    pathData.nodeForChainNode = pathNodeForChainNode;
    pathData.visBytes = 4;
    pathData.pathVis = pathVisibility;
    pathData.nodeTreeCount = 7;
    pathData.nodeTree = pathTree.trees.data();
    pathData.basenodes = nullptr;
    Expect(
        !db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "path data requires runtime base-node storage");
    pathData.basenodes = pathBaseNodes;
    pathData.chainNodeCount = 1;
    pathData.chainNodeForNode = nullptr;
    pathData.nodeForChainNode = nullptr;
    Expect(
        !db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "nonempty path data requires both chain maps");
    pathData.chainNodeForNode = pathChainNodeForNode;
    Expect(
        !db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "one-sided path chain maps rejected");
    pathData.nodeForChainNode = pathNodeForChainNode;
    Expect(
        db::validation::PathDataLayoutValid(pathData, &pathExtents)
            && pathExtents.chainMapBytes == 12,
        "paired path chain maps use the full node count");
    pathData.chainNodeCount = 7;
    Expect(
        !db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "path chain count beyond all path nodes rejected");
    pathData.chainNodeCount = 0;
    pathData.visBytes = 3;
    Expect(
        !db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "undersized path visibility cache rejected");
    pathData.visBytes = 4;
    pathData.nodeTreeCount = 12;
    Expect(
        !db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "path tree larger than a full binary tree rejected");
    pathData.nodeTreeCount = -1;
    Expect(
        !db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "negative path-tree count rejected");
    pathData.nodeTreeCount = 7;
    pathData.nodeCount = db::validation::kMaxPathNodes + 1u;
    Expect(
        !db::validation::PathDataLayoutValid(pathData, &pathExtents),
        "path node count above the engine limit rejected");
    Expect(
        !db::validation::PathDataLayoutValid(pathData, nullptr),
        "path-data layout requires extent output");

    Expect(
        db::validation::PathNodesRuntimeValid(pathNodes, 6)
            && db::validation::PathNodesRuntimeValid<TestPathNode>(
                nullptr,
                0),
        "finite path-node constants with sentinel references accepted");
    pathNodes[0].constant.type = 20;
    Expect(
        !db::validation::PathNodesRuntimeValid(pathNodes, 6),
        "path-node type at the runtime table limit rejected");
    pathNodes[0] = {};
    pathNodes[0].constant.wOverlapNode[0] = 6;
    Expect(
        !db::validation::PathNodesRuntimeValid(pathNodes, 6),
        "path-node overlap index at the node limit rejected");
    pathNodes[0] = {};
    pathNodes[0].constant.wOverlapNode[0] = -2;
    Expect(
        !db::validation::PathNodesRuntimeValid(pathNodes, 6),
        "noncanonical negative path-node overlap rejected");
    pathNodes[0] = {};
    pathNodes[0].constant.wOverlapNode[1] = 1;
    Expect(
        !db::validation::PathNodesRuntimeValid(pathNodes, 6),
        "sparse path-node overlap slots rejected");
    pathNodes[0] = {};
    pathNodes[0].constant.wOverlapNode[0] = 0;
    Expect(
        !db::validation::PathNodesRuntimeValid(pathNodes, 6),
        "self-referential path-node overlap rejected");
    pathNodes[0] = {};
    pathNodes[0].constant.wChainParent = 6;
    Expect(
        !db::validation::PathNodesRuntimeValid(pathNodes, 6),
        "path-node chain parent at the node limit rejected");
    pathNodes[0] = {};
    pathNodes[0].constant.wChainParent = 1;
    pathNodes[1].constant.wChainParent = 0;
    Expect(
        !db::validation::PathNodesRuntimeValid(pathNodes, 6),
        "cyclic path-node chain parents rejected");
    pathNodes[0] = {};
    pathNodes[1] = {};
    pathNodes[0].constant.vOrigin[1] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::PathNodesRuntimeValid(pathNodes, 6),
        "non-finite path-node geometry rejected");
    pathNodes[0] = {};
    Expect(
        !db::validation::PathNodesRuntimeValid<TestPathNode>(nullptr, 1),
        "path-node runtime validation requires owned storage");
    std::vector<TestPathNode> deepPathChain(256);
    for (std::size_t nodeIndex = 1;
        nodeIndex < deepPathChain.size();
        ++nodeIndex)
    {
        deepPathChain[nodeIndex].constant.wChainParent =
            static_cast<std::int16_t>(nodeIndex - 1u);
    }
    Expect(
        db::validation::PathNodesRuntimeValid(
            deepPathChain.data(),
            deepPathChain.size()),
        "path chain at the parent-depth limit accepted");
    deepPathChain.emplace_back();
    deepPathChain.back().constant.wChainParent = 255;
    Expect(
        !db::validation::PathNodesRuntimeValid(
            deepPathChain.data(),
            deepPathChain.size()),
        "path chain beyond the parent-depth limit rejected");

    Expect(
        db::validation::PathChainMapsRuntimeValid(
            pathChainNodeForNode,
            pathNodeForChainNode,
            6,
            2)
            && db::validation::PathChainMapsRuntimeValid(
                nullptr,
                nullptr,
                0,
                0),
        "inverse path-chain permutations accepted");
    pathNodeForChainNode[5] = 6;
    Expect(
        !db::validation::PathChainMapsRuntimeValid(
            pathChainNodeForNode,
            pathNodeForChainNode,
            6,
            2),
        "out-of-range inverse path-chain index rejected");
    pathNodeForChainNode[5] = 5;
    pathNodeForChainNode[0] = 1;
    pathNodeForChainNode[1] = 0;
    Expect(
        !db::validation::PathChainMapsRuntimeValid(
            pathChainNodeForNode,
            pathNodeForChainNode,
            6,
            2),
        "inconsistent inverse path-chain permutation rejected");
    pathNodeForChainNode[0] = 0;
    pathNodeForChainNode[1] = 1;
    std::uint16_t scrambledChainNodeForNode[6] = {1, 3, 0, 5, 4, 2};
    std::uint16_t scrambledNodeForChainNode[6] = {2, 0, 5, 1, 4, 3};
    Expect(
        db::validation::PathChainMapsRuntimeValid(
            scrambledChainNodeForNode,
            scrambledNodeForChainNode,
            6,
            0)
            && db::validation::PathChainMapsRuntimeValid(
                scrambledChainNodeForNode,
                scrambledNodeForChainNode,
                6,
                3),
        "scrambled inverse chain maps accept empty and nonempty chain prefixes");
    Expect(
        !db::validation::PathChainMapsRuntimeValid(
            nullptr,
            nullptr,
            6,
            0),
        "nonempty path data rejects absent runtime chain maps");

    TestPathLink pathLinks[2] = {{12.0f, 1}, {4.0f, 5}};
    Expect(
        db::validation::PathLinksRuntimeValid(pathLinks, 2, 6)
            && db::validation::PathLinksRuntimeValid<TestPathLink>(
                nullptr,
                0,
                6),
        "finite in-range path links accepted");
    pathLinks[1].nodeNum = 6;
    Expect(
        !db::validation::PathLinksRuntimeValid(pathLinks, 2, 6),
        "path link index at the node limit rejected");
    pathLinks[1].nodeNum = 5;
    pathLinks[0].fDist = -1.0f;
    Expect(
        !db::validation::PathLinksRuntimeValid(pathLinks, 2, 6),
        "negative path-link distance rejected");
    pathLinks[0].fDist =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::PathLinksRuntimeValid(pathLinks, 2, 6),
        "non-finite path-link distance rejected");
    Expect(
        !db::validation::PathLinksRuntimeValid<TestPathLink>(
            nullptr,
            1,
            6)
            && !db::validation::PathLinksRuntimeValid(pathLinks, 0, 6),
        "noncanonical path link pointer/count pairs rejected");

    PopulateValidPathTreeFixture(&pathTree);
    const std::uint32_t testPathTreeStride =
        static_cast<std::uint32_t>(sizeof(TestPathTree));
    Expect(
        db::validation::PathTreeGraphValid(
            pathTree.trees.data(),
            pathTree.trees.size(),
            pathTree.pathNodeIndices.size(),
            testPathTreeStride),
        "scrambled flat path tree accepts forward and backward child references");
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(),
            pathTree.trees.size(),
            pathTree.pathNodeIndices.size(),
            0),
        "path tree rejects a zero serialized stride");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[0].axis = 3;
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "path tree split axis outside XYZ rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[0].dist =
        (std::numeric_limits<float>::infinity)();
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "path tree non-finite split distance rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[0].u.child[0] = nullptr;
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "path tree null child rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[0].u.child[0] = reinterpret_cast<TestPathTree *>(
        reinterpret_cast<std::uintptr_t>(pathTree.trees.data()) + 1u);
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "path tree interior child pointer rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[0].u.child[0] = pathTree.trees.data()
        + pathTree.trees.size();
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "path tree one-past child pointer rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[4].u.child[0] = &pathTree.trees[0];
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "path tree cycle rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[0].u.child[1] = &pathTree.trees[4];
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "shared path-tree child ownership rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[2].u.s.nodeCount = 0;
    pathTree.trees[2].u.s.nodes = nullptr;
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "empty path-tree leaf rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[2].u.s.nodeCount = -1;
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "negative path-tree leaf count rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.trees[0].axis = -1;
    pathTree.trees[0].u.s.nodeCount = 1;
    pathTree.trees[0].u.s.nodes = pathTree.pathNodeIndices.data();
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "orphaned flat path-tree nodes rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.pathNodeIndices[2] = 6;
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "path-tree leaf index at the node limit rejected");
    PopulateValidPathTreeFixture(&pathTree);
    pathTree.pathNodeIndices[2] = 0;
    Expect(
        !db::validation::PathTreeGraphValid(
            pathTree.trees.data(), 7, 6, testPathTreeStride),
        "duplicate path-tree leaf ownership rejected");
    TestPathTree leafRoot = {};
    std::uint16_t leafRootIndex = 0;
    leafRoot.axis = -7;
    leafRoot.u.s.nodeCount = 1;
    leafRoot.u.s.nodes = &leafRootIndex;
    Expect(
        db::validation::PathTreeGraphValid(
            &leafRoot,
            1,
            1,
            static_cast<std::uint32_t>(sizeof(leafRoot))),
        "single negative-axis path-tree leaf root accepted");
    Expect(
        db::validation::PathTreeGraphValid<TestPathTree>(
            nullptr,
            0,
            0,
            testPathTreeStride),
        "empty path tree accepted with empty path data");
    std::vector<TestPathTree> deepPathTrees;
    std::vector<std::uint16_t> deepPathIndices;
    PopulatePathTreeDepthFixture(64, &deepPathTrees, &deepPathIndices);
    Expect(
        db::validation::PathTreeGraphValid(
            deepPathTrees.data(),
            deepPathTrees.size(),
            deepPathIndices.size(),
            testPathTreeStride),
        "path tree at the iterative traversal depth limit accepted");
    PopulatePathTreeDepthFixture(65, &deepPathTrees, &deepPathIndices);
    Expect(
        !db::validation::PathTreeGraphValid(
            deepPathTrees.data(),
            deepPathTrees.size(),
            deepPathIndices.size(),
            testPathTreeStride),
        "path tree beyond the traversal depth limit rejected");

    const std::uintptr_t serializedCellBaseAddress =
        static_cast<std::uintptr_t>(0x1000);
    const auto serializedCellBase = reinterpret_cast<const TestGfxCell *>(
        serializedCellBaseAddress);
    std::uint64_t serializedCellIndex = UINT64_MAX;
    Expect(
        db::validation::SerializedArrayElementIndex(
            serializedCellBase,
            4,
            disk32::kGfxCellBytes,
            serializedCellBase,
            &serializedCellIndex)
            && serializedCellIndex == 0,
        "first serialized world cell accepted");
    Expect(
        db::validation::SerializedArrayElementIndex(
            serializedCellBase,
            4,
            disk32::kGfxCellBytes,
            reinterpret_cast<const TestGfxCell *>(
                serializedCellBaseAddress
                    + disk32::kGfxCellBytes * 2u),
            &serializedCellIndex)
            && serializedCellIndex == 2,
        "forward serialized world cell accepted");
    Expect(
        db::validation::SerializedArrayElementIndex(
            serializedCellBase,
            4,
            disk32::kGfxCellBytes,
            reinterpret_cast<const TestGfxCell *>(
                serializedCellBaseAddress
                    + disk32::kGfxCellBytes * 3u),
            &serializedCellIndex)
            && serializedCellIndex == 3,
        "last serialized world cell accepted");
    Expect(
        db::validation::SerializedArrayElementIndex(
            serializedCellBase,
            4,
            disk32::kGfxCellBytes,
            reinterpret_cast<const TestGfxCell *>(
                serializedCellBaseAddress
                    + disk32::kGfxCellBytes),
            &serializedCellIndex)
            && serializedCellIndex == 1,
        "backward-selected serialized world cell accepted");
    Expect(
        !db::validation::SerializedArrayElementIndex(
            serializedCellBase,
            4,
            disk32::kGfxCellBytes,
            reinterpret_cast<const TestGfxCell *>(
                serializedCellBaseAddress + 1u),
            &serializedCellIndex),
        "interior serialized world cell pointer rejected");
    Expect(
        !db::validation::SerializedArrayElementIndex(
            serializedCellBase,
            4,
            disk32::kGfxCellBytes,
            reinterpret_cast<const TestGfxCell *>(
                serializedCellBaseAddress
                    + disk32::kGfxCellBytes * 4u),
            &serializedCellIndex),
        "one-past serialized world cell pointer rejected");
    Expect(
        !db::validation::SerializedArrayElementIndex(
            serializedCellBase,
            4,
            0,
            serializedCellBase,
            &serializedCellIndex),
        "zero serialized world cell stride rejected");
    const auto overflowingSerializedCellBase =
        reinterpret_cast<const TestGfxCell *>(
            (std::numeric_limits<std::uintptr_t>::max)() - 31u);
    Expect(
        !db::validation::SerializedArrayElementIndex(
            overflowingSerializedCellBase,
            2,
            32,
            overflowingSerializedCellBase,
            &serializedCellIndex),
        "overflowing serialized world cell span rejected");
    Expect(
        !db::validation::SerializedArrayElementIndex(
            serializedCellBase,
            4,
            disk32::kGfxCellBytes,
            serializedCellBase,
            nullptr),
        "serialized world cell membership requires index output");

    std::int32_t gfxCellBytes = -1;
    Expect(
        db::validation::GfxWorldCellLayoutValid(
            true,
            db::validation::kMaxGfxWorldCells,
            &gfxCellBytes)
            && gfxCellBytes == 57344,
        "maximum world cell header table accepted");
    Expect(
        !db::validation::GfxWorldCellLayoutValid(
            true,
            db::validation::kMaxGfxWorldCells + 1,
            &gfxCellBytes),
        "world cell count above the source limit rejected");
    Expect(
        !db::validation::GfxWorldCellLayoutValid(
            false,
            1,
            &gfxCellBytes),
        "missing required world cell table rejected");
    Expect(
        !db::validation::GfxWorldCellLayoutValid(
            true,
            0,
            &gfxCellBytes),
        "empty world cell table rejected");
    Expect(
        !db::validation::GfxWorldCellLayoutValid(true, 1, nullptr),
        "world cell layout requires extent output");
    Expect(
        db::validation::GfxWorldCellBitsValid(1, 16)
            && db::validation::GfxWorldCellBitsValid(128, 16)
            && db::validation::GfxWorldCellBitsValid(129, 32)
            && db::validation::GfxWorldCellBitsValid(1024, 128),
        "world cell bit bytes match the renderer's 128-cell allocation chunks");
    Expect(
        !db::validation::GfxWorldCellBitsValid(0, 0)
            && !db::validation::GfxWorldCellBitsValid(1025, 144)
            && !db::validation::GfxWorldCellBitsValid(128, 32),
        "invalid world cell bit byte counts rejected");
    Expect(
        db::validation::GfxReflectionProbeCountValid(1)
            && db::validation::GfxReflectionProbeCountValid(254),
        "renderer-safe world reflection-probe counts accepted");
    Expect(
        !db::validation::GfxReflectionProbeCountValid(0)
            && !db::validation::GfxReflectionProbeCountValid(255),
        "empty and uint8-wrapping world reflection-probe counts rejected");

    TestGfxWorldFixture gfxWorld = {};
    PopulateValidGfxWorldFixture(&gfxWorld);
    db::validation::GfxCellLayoutExtents gfxCellExtents = {};
    Expect(
        db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents)
            && gfxCellExtents.aabbTreeBytes
                == static_cast<std::int32_t>(
                    disk32::kGfxAabbTreeBytes)
            && gfxCellExtents.portalBytes
                == static_cast<std::int32_t>(
                    disk32::kGfxPortalBytes)
            && gfxCellExtents.cullGroupBytes == 4
            && gfxCellExtents.reflectionProbeBytes == 1,
        "world cell child extents use serialized strides");
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            nullptr),
        "world cell layout requires extent output");
    gfxWorld.cells[0].mins[0] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "non-finite world cell bounds rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].mins[1] = 10.0f;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "inverted world cell bounds rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].aabbTree = nullptr;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "missing world cell AABB table rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].aabbTreeCount = 0;
    gfxWorld.cells[0].aabbTree = nullptr;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "world cell without the runtime-required AABB root rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].portals = nullptr;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "missing world cell portal table rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].cullGroups = nullptr;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "missing world cell cull-group table rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].reflectionProbes = nullptr;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "missing world cell reflection-probe table rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].reflectionProbeCount = 0;
    gfxWorld.cells[0].reflectionProbes = nullptr;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "world cell without a nearest-probe candidate rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].portalCount = 0;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "present empty world cell portal table rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].aabbTreeCount = -1;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "negative world cell AABB count rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].portalCount = INT32_MAX;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "overflowing world cell portal extent rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[0].cullGroupCount = INT32_MAX;
    Expect(
        !db::validation::GfxCellLayoutValid(
            gfxWorld.cells[0],
            &gfxCellExtents),
        "overflowing world cell cull-group extent rejected");

    PopulateValidGfxWorldFixture(&gfxWorld);
    Expect(
        db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "finite four-vertex portal with canonical plane sides accepted");
    gfxWorld.portals[0].cell = nullptr;
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal without a target cell rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].vertexCount =
        db::validation::kMinGfxPortalVertices - 1;
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal below the minimum vertex count rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].vertexCount =
        db::validation::kMaxGfxPortalVertices + 1;
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal above the maximum vertex count rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].vertices = nullptr;
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal without a vertex table rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].plane.coeffs[0] = 0.0f;
    gfxWorld.portals[0].plane.coeffs[1] = 0.0f;
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal with a zero plane normal rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].plane.coeffs[3] =
        (std::numeric_limits<float>::infinity)();
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal with a non-finite plane rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].plane.side[1] = 16;
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal with a noncanonical plane side offset rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].hullAxis[1][2] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal with a non-finite hull axis rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.vertices[0][2][1] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "portal with a non-finite vertex rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    float maximumPortalVertices[
        db::validation::kMaxGfxPortalVertices][3] = {};
    gfxWorld.portals[0].vertices = maximumPortalVertices;
    gfxWorld.portals[0].vertexCount =
        db::validation::kMaxGfxPortalVertices;
    Expect(
        db::validation::GfxPortalRuntimeValid(gfxWorld.portals[0]),
        "maximum-size finite portal vertex table accepted");

    PopulateValidGfxWorldFixture(&gfxWorld);
    const std::uint32_t testCellStride =
        static_cast<std::uint32_t>(sizeof(TestGfxCell));
    Expect(
        db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "directed portal graph accepts forward, backward, cyclic, and self links");
    Expect(
        !db::validation::GfxWorldCellGraphValid(gfxWorld.world, 0),
        "world portal graph rejects a zero cell stride");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.reflectionProbeCount = 0;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph requires the default reflection probe");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.reflectionProbeCount = 255;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects a uint8-wrapping probe count");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.reflectionProbeTextures = nullptr;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph requires reflection texture scratch storage");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.reflectionProbes[1].origin[2] =
        (std::numeric_limits<float>::infinity)();
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects a non-finite reflection probe");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.reflectionProbes[1].reflectionImage = nullptr;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects a probe without an image");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cullGroups[0].mins[0] = 9.0f;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects inverted cull-group bounds");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cullGroups[0].maxs[1] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects non-finite cull-group bounds");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cullGroups[0].surfaceCount = -1;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects a negative cull-group surface count");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cullGroups[0].surfaceCount = 0;
    gfxWorld.cullGroups[0].startSurfIndex = 0;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "empty cull groups require the source-format minus-one start");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cullGroups[0].startSurfIndex = -1;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "nonempty cull groups require a nonnegative surface start");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cullGroups[2].surfaceCount = 2;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "cull-group spans beyond sorted surfaces are rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.dpvs.sortedSurfIndex = nullptr;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph requires the sorted surface table");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.dpvs.staticSurfaceCountNoDecal = 4;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects a no-decal partition beyond static surfaces");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.surfaceCount = 2;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects static surfaces beyond the world total");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.surfaceCount = -1;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects a negative surface total");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.sortedSurfIndex[2] = 3;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects an out-of-range sorted surface index");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].cell = reinterpret_cast<TestGfxCell *>(
        reinterpret_cast<std::uintptr_t>(gfxWorld.cells.data()) + 1u);
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects an interior target cell pointer");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[0].cell = reinterpret_cast<TestGfxCell *>(
        reinterpret_cast<std::uintptr_t>(gfxWorld.cells.data())
            + sizeof(TestGfxCell) * gfxWorld.cells.size());
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects a one-past target cell pointer");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cellCullGroups[0] = -1;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "negative world cell cull-group index rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cellCullGroups[0] = gfxWorld.world.cullGroupCount;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world cell cull-group index at the global limit rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cellReflectionProbes[0] =
        static_cast<std::uint8_t>(gfxWorld.world.reflectionProbeCount);
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world cell reflection-probe index at the global limit rejected");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.dpvs.cullGroups = nullptr;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph requires its global cull-group table");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.reflectionProbes = nullptr;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph requires its global reflection-probe table");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.cells = nullptr;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph requires its global cell table");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.world.cullGroupCount = -1;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "world portal graph rejects a negative global cull-group count");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.cells[2].maxs[0] =
        (std::numeric_limits<float>::quiet_NaN)();
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "invalid child cell rejects the complete world portal graph");
    PopulateValidGfxWorldFixture(&gfxWorld);
    gfxWorld.portals[3].plane.side[0] = 0;
    Expect(
        !db::validation::GfxWorldCellGraphValid(
            gfxWorld.world,
            testCellStride),
        "invalid portal rejects the complete world portal graph");

    std::uint32_t spanBytes = UINT32_MAX;
    Expect(db::validation::CheckedSpanBytes(0, 20, &spanBytes) && spanBytes == 0, "zero span size");
    Expect(db::validation::CheckedSpanBytes(12, 20, &spanBytes) && spanBytes == 240, "direct span size");
    Expect(db::validation::CheckedSpanBytes(UINT64_C(214748364), 20, &spanBytes)
        && spanBytes == UINT32_C(4294967280), "maximum direct span product");
    Expect(!db::validation::CheckedSpanBytes(UINT64_C(214748365), 20, &spanBytes), "direct span overflow rejected");
    Expect(!db::validation::CheckedSpanBytes((std::numeric_limits<std::uint64_t>::max)(), 1, &spanBytes), "oversized direct span count rejected");
    Expect(!db::validation::CheckedSpanBytes(1, 0, &spanBytes), "zero direct span stride rejected");
    Expect(!db::validation::CheckedSpanBytes(1, 1, nullptr), "null direct span result rejected");

    std::int32_t bytes = -1;
    Expect(db::validation::CheckedArrayBytes(0, 8, &bytes) && bytes == 0, "zero array size");
    Expect(db::validation::CheckedArrayBytes(32768, 8, &bytes) && bytes == 262144, "asset array size");
    Expect(!db::validation::CheckedArrayBytes(-1, 8, &bytes), "negative count rejected");
    Expect(!db::validation::CheckedArrayBytes(1, 0, &bytes), "zero stride rejected");
    Expect(!db::validation::CheckedArrayBytes((std::numeric_limits<std::int32_t>::max)(), 8, &bytes), "multiplication overflow rejected");
    Expect(!db::validation::CheckedArrayBytes(1, 8, nullptr), "null byte result rejected");

    Expect(
        db::validation::XModelPiecesLayoutValid(true, true, 1, &bytes)
            && bytes == 16,
        "single model-piece layout accepted");
    Expect(
        db::validation::XModelPiecesLayoutValid(true, true, UINT16_MAX, &bytes)
            && bytes == 1048560,
        "largest source-format model-piece layout accepted");
    Expect(
        db::validation::XModelPiecesLayoutValid(true, true, 0, &bytes)
            && bytes == 0,
        "present empty model-pieces layout accepted");
    Expect(
        db::validation::XModelPiecesLayoutValid(true, false, 0, &bytes)
            && bytes == 0,
        "null empty model-pieces layout accepted");
    Expect(
        !db::validation::XModelPiecesLayoutValid(true, true, -1, &bytes),
        "negative model-pieces count rejected");
    Expect(
        !db::validation::XModelPiecesLayoutValid(true, true, 65536, &bytes),
        "model-pieces count beyond the source format rejected");
    Expect(
        !db::validation::XModelPiecesLayoutValid(false, true, 1, &bytes),
        "nameless model-pieces layout rejected");
    Expect(
        !db::validation::XModelPiecesLayoutValid(true, false, 1, &bytes),
        "missing model-pieces array rejected");
    Expect(
        !db::validation::XModelPiecesLayoutValid(true, true, 1, nullptr),
        "model-pieces layout requires an extent output");

    std::int32_t count = -1;
    Expect(db::validation::CheckedCountSum(12, 30, &count) && count == 42, "count sum");
    Expect(!db::validation::CheckedCountSum(-1, 1, &count), "negative sum operand rejected");
    Expect(!db::validation::CheckedCountSum(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count), "count sum overflow rejected");
    Expect(!db::validation::CheckedCountSum(1, 1, nullptr), "null count sum result rejected");
    Expect(db::validation::CheckedCountProduct(6, 7, &count) && count == 42, "count product");
    Expect(db::validation::CheckedCountProduct(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count product");
    Expect(db::validation::CheckedCountProduct(0, 42, &count) && count == 0, "zero product");
    Expect(!db::validation::CheckedCountProduct(
        0, (std::numeric_limits<std::uint32_t>::max)(), &count), "oversized zero-product operand rejected");
    Expect(!db::validation::CheckedCountProduct(-1, 0, &count), "negative product operand rejected");
    Expect(!db::validation::CheckedCountProduct(
        (std::numeric_limits<std::uint32_t>::max)(), 2, &count), "count product overflow rejected");
    Expect(!db::validation::CheckedCountProduct(1, 1, nullptr), "null count product result rejected");
    Expect(db::validation::CheckedCountDifference(10, 3, &count) && count == 7, "count difference");
    Expect(db::validation::CheckedCountDifference(
        (std::numeric_limits<std::int32_t>::max)(), 0, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count difference");
    Expect(!db::validation::CheckedCountDifference(3, 10, &count), "count subtraction underflow rejected");
    Expect(!db::validation::CheckedCountDifference(
        (std::numeric_limits<std::uint32_t>::max)(),
        (std::numeric_limits<std::uint32_t>::max)(),
        &count), "oversized difference operands rejected");
    Expect(!db::validation::CheckedCountDifference(1, 0, nullptr), "null count difference result rejected");
    Expect(db::validation::CheckedCountCeilDiv(33, 32, &count) && count == 2, "count ceiling division");
    Expect(db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::int32_t>::max)(), 32, &count)
        && count == 67108864, "large count ceiling division");
    Expect(db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count ceiling division");
    Expect(!db::validation::CheckedCountCeilDiv(1, 0, &count), "zero divisor rejected");
    Expect(!db::validation::CheckedCountCeilDiv(1, 1, nullptr), "null ceiling-division result rejected");
    Expect(!db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::uint32_t>::max)(), 32, &count), "oversized dividend rejected");

    Expect(db::validation::CanAppendBytes(0x40000, 0x40000, 0x80000), "double-buffer append");
    Expect(db::validation::CanAppendBytes(0x80000, 0, 0x80000), "zero append at capacity");
    Expect(!db::validation::CanAppendBytes(0x80001, 0, 0x80000), "current bytes over capacity rejected");
    Expect(!db::validation::CanAppendBytes(0x40001, 0x40000, 0x80000), "append over capacity rejected");

    Expect(db::validation::IsAlignmentMask(0), "zero alignment mask");
    Expect(db::validation::IsAlignmentMask(1), "two-byte alignment mask");
    Expect(db::validation::IsAlignmentMask(15), "sixteen-byte alignment mask");
    Expect(!db::validation::IsAlignmentMask(5), "noncontiguous alignment mask rejected");
    Expect(!db::validation::IsAlignmentMask((std::numeric_limits<std::uintptr_t>::max)()), "maximum mask rejected");

    std::uintptr_t aligned = 0;
    Expect(db::validation::AlignUp(0x1001, 15, &aligned) && aligned == 0x1010, "alignment rounds up");
    Expect(db::validation::AlignUp(0x1010, 15, &aligned) && aligned == 0x1010, "aligned value preserved");
    Expect(!db::validation::AlignUp((std::numeric_limits<std::uintptr_t>::max)() - 7, 15, &aligned), "alignment overflow rejected");

    constexpr std::uintptr_t base = 0x1000;
    Expect(db::validation::SpanWithinBlock(base, 0x100, base, 0x100), "whole block span");
    Expect(db::validation::SpanWithinBlock(base, 0x100, base + 0x100, 0), "exact end empty span");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base - 1, 1), "position before block rejected");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base + 0x100, 1), "span past end rejected");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base + 0x80, 0x81), "oversized tail span rejected");

    std::uint32_t remaining = 0;
    Expect(db::validation::RemainingInBlock(base, 0x100, base + 0x40, &remaining) && remaining == 0xC0, "remaining bytes computed");
    Expect(db::validation::RemainingInBlock(base, 0x100, base + 0x100, &remaining) && remaining == 0, "zero bytes at block end");
    Expect(!db::validation::RemainingInBlock(base, 0x100, base + 0x101, &remaining), "position after block rejected");

    return failures == 0 ? 0 : 1;
}
