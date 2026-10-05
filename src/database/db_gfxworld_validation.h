#pragma once

// The world's AABB-tree rules that read only the loaded world, shared by the
// 32-bit loader (db_load.cpp) and the 64-bit world loader. Moved from
// db_load.cpp; templates over the world and cell types, which each caller
// supplies (the 64-bit loader reaches them through the generated mirrors, so
// this header includes no renderer header).

#include <database/db_validation.h>
#include <qcommon/com_error.h>

#include <cstdint>
#include <cstdlib>

namespace db::gfxworld_validation
{
template <typename World, typename Cell>
inline bool DB_ValidateWorldAabbCell(
    const World *world,
    const Cell *cell)
{
    if (!world || !cell
        || !db::validation::WorldAabbTreePresenceValid(
            cell->aabbTree != nullptr,
            cell->aabbTreeCount))
    {
        Com_Error(ERR_DROP, "Invalid fast-file world AABB pointer/count");
        return false;
    }

    std::uint8_t *nodeDepths = cell->aabbTreeCount
        ? static_cast<std::uint8_t *>(
            std::malloc(static_cast<std::size_t>(cell->aabbTreeCount)))
        : nullptr;
    if (cell->aabbTreeCount && !nodeDepths)
    {
        Com_Error(ERR_DROP, "Could not allocate world AABB validation state");
        return false;
    }
    const db::validation::WorldAabbTopologyStatus status =
        db::validation::ValidateWorldAabbTopology(
            cell->aabbTree,
            cell->aabbTreeCount,
            world->dpvs.staticSurfaceCount,
            world->dpvs.staticSurfaceCountNoDecal,
            nodeDepths,
            static_cast<std::uint64_t>(cell->aabbTreeCount));
    std::free(nodeDepths);
    if (status != db::validation::WorldAabbTopologyStatus::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file world AABB topology: %s",
            db::validation::WorldAabbTopologyStatusName(status));
        return false;
    }
    return true;
}

template <typename World>
inline bool DB_ValidateWorldAabbTrees(const World *world)
{
    if (!world
        || !db::validation::PointerCountConsistent(
            world->cells != nullptr,
            world->dpvsPlanes.cellCount))
    {
        Com_Error(ERR_DROP, "Invalid fast-file world cell pointer/count");
        return false;
    }
    if (world->surfaceCount < 0
        || !db::validation::WorldAabbSurfacePartitionsValid(
            world->dpvs.staticSurfaceCount,
            world->dpvs.staticSurfaceCountNoDecal,
            static_cast<std::uint32_t>(world->surfaceCount))
        || world->modelCount <= 0
        || !world->models
        || world->models[0].startSurfIndex != 0
        || world->models[0].surfaceCount
            != world->dpvs.staticSurfaceCount
        || world->models[0].surfaceCountNoDecal
            != world->dpvs.staticSurfaceCountNoDecal
        || world->dpvs.smodelCount
            > db::validation::kMaxWorldAabbStaticModels)
    {
        Com_Error(ERR_DROP, "Invalid fast-file world static-surface counts");
        return false;
    }

    std::int32_t totalNodeCount = 0;
    std::int32_t maximumCellNodeCount = 0;
    for (std::int32_t cellIndex = 0;
        cellIndex < world->dpvsPlanes.cellCount;
        ++cellIndex)
    {
        const auto &cell = world->cells[cellIndex];
        if (!db::validation::WorldAabbTreePresenceValid(
                cell.aabbTree != nullptr,
                cell.aabbTreeCount))
        {
            Com_Error(
                ERR_DROP,
                "Invalid fast-file world AABB pointer/count in cell %d",
                cellIndex);
            return false;
        }

        std::int32_t nextTotal = 0;
        if (!db::validation::CheckedCountSum(
                totalNodeCount,
                cell.aabbTreeCount,
                &nextTotal))
        {
            Com_Error(ERR_DROP, "Invalid fast-file aggregate world AABB count");
            return false;
        }
        totalNodeCount = nextTotal;
        if (cell.aabbTreeCount > maximumCellNodeCount)
            maximumCellNodeCount = cell.aabbTreeCount;
    }

    std::uint8_t *nodeDepths = maximumCellNodeCount
        ? static_cast<std::uint8_t *>(
            std::malloc(static_cast<std::size_t>(maximumCellNodeCount)))
        : nullptr;
    if (maximumCellNodeCount && !nodeDepths)
    {
        Com_Error(ERR_DROP, "Could not allocate world AABB validation state");
        return false;
    }
    const std::uint64_t sortedSurfaceCount =
        static_cast<std::uint64_t>(world->dpvs.staticSurfaceCount)
        + world->dpvs.staticSurfaceCountNoDecal;
    std::uint8_t *surfaceCoverage = sortedSurfaceCount
        ? static_cast<std::uint8_t *>(
            std::calloc(static_cast<std::size_t>(sortedSurfaceCount), 1))
        : nullptr;
    if (sortedSurfaceCount && !surfaceCoverage)
    {
        std::free(nodeDepths);
        Com_Error(ERR_DROP, "Could not allocate world AABB surface coverage");
        return false;
    }

    for (std::int32_t cellIndex = 0;
        cellIndex < world->dpvsPlanes.cellCount;
        ++cellIndex)
    {
        const auto &cell = world->cells[cellIndex];
        const db::validation::WorldAabbTopologyStatus status =
            db::validation::ValidateWorldAabbTopology(
                cell.aabbTree,
                cell.aabbTreeCount,
                world->dpvs.staticSurfaceCount,
                world->dpvs.staticSurfaceCountNoDecal,
                nodeDepths,
                static_cast<std::uint64_t>(maximumCellNodeCount));
        if (status != db::validation::WorldAabbTopologyStatus::Ok)
        {
            std::free(nodeDepths);
            std::free(surfaceCoverage);
            Com_Error(
                ERR_DROP,
                "Invalid fast-file world AABB topology in cell %d: %s",
                cellIndex,
                db::validation::WorldAabbTopologyStatusName(status));
            return false;
        }
        if (cell.aabbTreeCount)
        {
            const auto &root = cell.aabbTree[0];
            if (!db::validation::MarkUniqueCoverageSpan(
                    surfaceCoverage,
                    sortedSurfaceCount,
                    root.startSurfIndex,
                    root.surfaceCount)
                || !db::validation::MarkUniqueCoverageSpan(
                    surfaceCoverage,
                    sortedSurfaceCount,
                    root.startSurfIndexNoDecal,
                    root.surfaceCountNoDecal))
            {
                std::free(nodeDepths);
                std::free(surfaceCoverage);
                Com_Error(ERR_DROP, "Overlapping fast-file world AABB root surfaces");
                return false;
            }
        }
    }
    std::free(nodeDepths);
    if (!db::validation::CoverageComplete(
            surfaceCoverage,
            sortedSurfaceCount))
    {
        std::free(surfaceCoverage);
        Com_Error(ERR_DROP, "Fast-file world AABB roots leave uncovered surfaces");
        return false;
    }
    std::free(surfaceCoverage);
    return true;
}
} // namespace db::gfxworld_validation
