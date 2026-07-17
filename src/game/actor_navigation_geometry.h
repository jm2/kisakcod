#pragma once

namespace ActorNavigationGeometry
{
inline float DistanceSquared3D(const float *point, const float *origin)
{
    const float dx = point[0] - origin[0];
    const float dy = point[1] - origin[1];
    const float dz = point[2] - origin[2];
    return dz * dz + (dx * dx + dy * dy);
}

inline bool IsWithinDistance3D(const float *point, const float *origin, float distanceSquared)
{
    return DistanceSquared3D(point, origin) < distanceSquared;
}

inline bool IsOutsideHalfPlanes2D(
    const float *point,
    const float (*normals)[2],
    const float *distances,
    int planeCount)
{
    for (int planeIndex = 0; planeIndex < planeCount; ++planeIndex)
    {
        const float projectedDistance =
            point[0] * normals[planeIndex][0] + point[1] * normals[planeIndex][1];
        if (projectedDistance > distances[planeIndex])
            return true;
    }

    return false;
}

inline bool IsInsideCylinder(
    const float *point,
    const float *origin,
    float radiusSquared,
    float halfHeightSquared)
{
    const float dz = point[2] - origin[2];
    if (dz * dz > halfHeightSquared)
        return false;

    const float dx = point[0] - origin[0];
    const float dy = point[1] - origin[1];
    return dx * dx + dy * dy <= radiusSquared;
}
} // namespace ActorNavigationGeometry
