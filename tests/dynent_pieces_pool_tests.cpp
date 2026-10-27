// SPDX-License-Identifier: GPL-3.0
//
// dynent_pieces_pool_tests.cpp - lifetime of the breakable-piece pool
// (issue #260). Drives the production commit/shutdown helpers in
// DynEntity_pieces.cpp with fake dxBody pointers, so the sidecar bookkeeping
// is checked without an ODE world:
//   1. a full pool recycles slots instead of refusing every later piece;
//   2. recycling releases the old binding and returns its body to destroy;
//   3. shutdown releases every slot, destroys the bodies and resets the pool;
//   4. tokens recorded before shutdown never resolve to a later body.

#include <DynEntity/DynEntity_client.h>

#include <universal/phys_obj_id.h>

#include <cstdint>
#include <cstdio>

namespace
{
constexpr int32_t kPieceCapacity = 100;
constexpr int32_t kBodyCount = 400;

alignas(16) unsigned char g_bodyStorage[kBodyCount][16];
dxBody *g_destroyedBodies[kBodyCount];
int32_t g_destroyedCount;
bool g_wrongWorld;

dxBody *Body(int32_t index)
{
    return reinterpret_cast<dxBody *>(g_bodyStorage[index]);
}

int Fail(const char *const message)
{
    std::fprintf(stderr, "dynent pieces pool test failed: %s\n", message);
    return 1;
}

bool TestCommitRecyclesFullPool()
{
    DynEntPieces_Shutdown();
    g_destroyedCount = 0;
    g_wrongWorld = false;

    phys_obj_id::BodyToken tokens[kPieceCapacity];
    for (int32_t index = 0; index < kPieceCapacity; ++index)
    {
        const DynEntPieceSlotCommit commit =
            DynEntPieces_CommitSlotLocked(Body(index), nullptr);
        if (!commit.committed || commit.retiredBody)
            return false;
        if (numPieces != index + 1 || !g_breakablePieces[index].active)
            return false;
        tokens[index] = g_breakablePieces[index].physObjId;
        if (phys_obj_id::ReadResolve<dxBody>(
                g_breakablePieceBodySidecar, tokens[index])
            != Body(index))
        {
            return false;
        }
    }

    // 60 more pieces must recycle slots 0..59, each returning the old body
    // and invalidating the token recorded for that slot.
    for (int32_t piece = kPieceCapacity; piece < kPieceCapacity + 60; ++piece)
    {
        const int32_t slot = piece - kPieceCapacity;
        const DynEntPieceSlotCommit commit =
            DynEntPieces_CommitSlotLocked(Body(piece), nullptr);
        if (!commit.committed || commit.retiredBody != Body(slot))
            return false;
        if (phys_obj_id::ReadResolve<dxBody>(
                g_breakablePieceBodySidecar, tokens[slot])
            != nullptr)
        {
            return false;
        }
        tokens[slot] = g_breakablePieces[slot].physObjId;
        if (phys_obj_id::ReadResolve<dxBody>(
                g_breakablePieceBodySidecar, tokens[slot])
            != Body(piece))
        {
            return false;
        }
        if (numPieces != kPieceCapacity)
            return false;
    }

    DynEntPieces_Shutdown();
    if (g_wrongWorld || g_destroyedCount != kPieceCapacity)
        return false;
    if (numPieces != 0)
        return false;
    for (int32_t slot = 0; slot < kPieceCapacity; ++slot)
    {
        if (g_breakablePieces[slot].active
            || g_breakablePieceBodySidecar.IsBound(
                static_cast<phys_obj_id::OwnerIndex>(slot)))
        {
            return false;
        }
    }
    return true;
}

bool TestShutdownReleasesEverySlot()
{
    DynEntPieces_Shutdown();
    g_destroyedCount = 0;
    g_wrongWorld = false;

    phys_obj_id::BodyToken tokens[kPieceCapacity];
    for (int32_t slot = 0; slot < kPieceCapacity; ++slot)
    {
        const phys_obj_id::TokenResult bind =
            g_breakablePieceBodySidecar.Bind(
                static_cast<phys_obj_id::OwnerIndex>(slot), Body(200 + slot));
        if (!bind)
            return false;
        tokens[slot] = bind.token;
        g_breakablePieces[slot].physObjId = bind.token;
        g_breakablePieces[slot].model = nullptr;
        g_breakablePieces[slot].lightingHandle = 0;
        g_breakablePieces[slot].active = true;
    }
    numPieces = kPieceCapacity;

    DynEntPieces_Shutdown();
    if (g_wrongWorld || g_destroyedCount != kPieceCapacity)
        return false;
    if (numPieces != 0)
        return false;
    for (int32_t slot = 0; slot < kPieceCapacity; ++slot)
    {
        if (g_breakablePieces[slot].active
            || g_breakablePieces[slot].physObjId
                != phys_obj_id::INVALID_BODY_TOKEN)
        {
            return false;
        }
        if (g_breakablePieceBodySidecar.IsBound(
                static_cast<phys_obj_id::OwnerIndex>(slot)))
        {
            return false;
        }
        // The token recorded before the map change must never resolve to a
        // later body, even after the slot is bound again by the next map.
        if (phys_obj_id::ReadResolve<dxBody>(
                g_breakablePieceBodySidecar, tokens[slot])
            != nullptr)
        {
            return false;
        }
        if (g_destroyedBodies[slot] != Body(200 + slot))
            return false;
    }

    // A second shutdown is a no-op (map_restart calls it twice around init).
    DynEntPieces_Shutdown();
    return !g_wrongWorld && g_destroyedCount == kPieceCapacity;
}

bool TestClShutdownReleasesPieces()
{
    DynEntPieces_Shutdown();
    g_destroyedCount = 0;
    g_wrongWorld = false;

    phys_obj_id::BodyToken tokens[kPieceCapacity];
    for (int32_t slot = 0; slot < kPieceCapacity; ++slot)
    {
        const phys_obj_id::TokenResult bind =
            g_breakablePieceBodySidecar.Bind(
                static_cast<phys_obj_id::OwnerIndex>(slot), Body(300 + slot));
        if (!bind)
            return false;
        tokens[slot] = bind.token;
        g_breakablePieces[slot].physObjId = bind.token;
        g_breakablePieces[slot].model = nullptr;
        g_breakablePieces[slot].lightingHandle = 0;
        g_breakablePieces[slot].active = true;
    }
    numPieces = kPieceCapacity;

    // The engine shutdown path must call the piece shutdown before the
    // caller reaches Phys_Shutdown.
    DynEntCl_Shutdown(0);
    if (g_wrongWorld || g_destroyedCount != kPieceCapacity)
        return false;
    if (numPieces != 0)
        return false;
    for (int32_t slot = 0; slot < kPieceCapacity; ++slot)
    {
        if (phys_obj_id::ReadResolve<dxBody>(
                g_breakablePieceBodySidecar, tokens[slot])
            != nullptr)
        {
            return false;
        }
    }
    return true;
}
} // namespace

uint16_t __cdecl DynEnt_GetEntityCount(DynEntityCollType)
{
    return 0;
}

DynEntityClient *__cdecl DynEnt_GetClientEntity(uint16_t, DynEntityDrawType)
{
    return nullptr;
}

int32_t __cdecl CL_GetLocalClientActiveCount()
{
    return 1;
}

void KISAK_CDECL Sys_EnterCriticalSection(int)
{
}

void KISAK_CDECL Sys_LeaveCriticalSection(int)
{
}

void __cdecl Phys_ObjDestroy(PhysWorld worldIndex, dxBody *id)
{
    if (worldIndex != PHYS_WORLD_FX)
    {
        g_wrongWorld = true;
        return;
    }
    if (g_destroyedCount < kBodyCount)
        g_destroyedBodies[g_destroyedCount++] = id;
}

int main()
{
    if (!TestCommitRecyclesFullPool())
        return Fail("commit/recycle");
    if (!TestShutdownReleasesEverySlot())
        return Fail("shutdown/stale token");
    if (!TestClShutdownReleasesPieces())
        return Fail("DynEntCl_Shutdown wiring");
    std::printf("dynent pieces pool tests passed\n");
    return 0;
}
