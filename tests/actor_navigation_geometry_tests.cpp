#include <game/actor_navigation_geometry.h>

#include <cstdio>

namespace
{
int failures;

void Expect(bool condition, const char *description)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", description);
        ++failures;
    }
}

void TestDistanceAndStrictGoalBoundary()
{
    const float origin[3] = {10.0f, -3.0f, 4.0f};
    const float point[3] = {13.0f, -7.0f, 9.0f};

    Expect(
        ActorNavigationGeometry::DistanceSquared3D(point, origin) == 50.0f,
        "3D distance includes nonzero X, Y, and Z deltas");
    Expect(
        !ActorNavigationGeometry::IsWithinDistance3D(point, origin, 50.0f),
        "goal distance excludes its exact boundary");
    Expect(
        ActorNavigationGeometry::IsWithinDistance3D(point, origin, 50.25f),
        "goal distance accepts points strictly inside the boundary");

    const float verticalBoundary[3] = {10.0f, -3.0f, 9.0f};
    Expect(
        !ActorNavigationGeometry::IsWithinDistance3D(verticalBoundary, origin, 25.0f),
        "goal distance uses the vertical component");
}

void TestHalfPlaneFiltering()
{
    const float normals[2][2] = {{1.0f, 0.0f}, {0.0f, -1.0f}};
    const float distances[2] = {10.0f, 4.0f};
    const float boundary[3] = {10.0f, -4.0f, 99.0f};
    const float outsideX[3] = {10.25f, -4.0f, 99.0f};
    const float outsideY[3] = {10.0f, -4.25f, 99.0f};

    Expect(
        !ActorNavigationGeometry::IsOutsideHalfPlanes2D(boundary, normals, distances, 2),
        "half-plane filtering includes exact boundaries");
    Expect(
        ActorNavigationGeometry::IsOutsideHalfPlanes2D(outsideX, normals, distances, 2),
        "half-plane filtering rejects an X violation");
    Expect(
        ActorNavigationGeometry::IsOutsideHalfPlanes2D(outsideY, normals, distances, 2),
        "half-plane filtering rejects a Y violation");
    Expect(
        !ActorNavigationGeometry::IsOutsideHalfPlanes2D(outsideX, normals, distances, 0),
        "an empty half-plane set does not reject nodes");
}

void TestCylinderInclusion()
{
    const float origin[3] = {10.0f, -20.0f, 30.0f};
    const float center[3] = {10.0f, -20.0f, 30.0f};
    const float boundary[3] = {13.0f, -16.0f, 33.0f};
    const float outsideRadius[3] = {13.25f, -16.0f, 30.0f};
    const float outsideHeight[3] = {10.0f, -20.0f, 33.25f};

    Expect(
        ActorNavigationGeometry::IsInsideCylinder(center, origin, 25.0f, 9.0f),
        "a nonzero cylinder origin contains its center");
    Expect(
        ActorNavigationGeometry::IsInsideCylinder(boundary, origin, 25.0f, 9.0f),
        "cylinder inclusion accepts radial and vertical boundaries");
    Expect(
        !ActorNavigationGeometry::IsInsideCylinder(outsideRadius, origin, 25.0f, 9.0f),
        "cylinder inclusion rejects points outside the radius");
    Expect(
        !ActorNavigationGeometry::IsInsideCylinder(outsideHeight, origin, 25.0f, 9.0f),
        "cylinder inclusion measures height from origin.z");
}
} // namespace

int main()
{
    TestDistanceAndStrictGoalBoundary();
    TestHalfPlaneFiltering();
    TestCylinderInclusion();

    if (failures == 0)
        std::puts("actor navigation geometry tests passed");

    return failures == 0 ? 0 : 1;
}
