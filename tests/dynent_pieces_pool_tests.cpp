// SPDX-License-Identifier: GPL-3.0
//
// dynent_pieces_pool_tests.cpp - lifetime of the breakable-piece pool
// (issue #260). Drives the production commit/shutdown helpers in
// DynEntity_pieces.cpp with fake dxBody pointers, so the sidecar bookkeeping
// is checked without an ODE world:
//   1. a full pool refuses the 101st piece, as retail 1.8 does
//      (0x0048C423 cmp numPieces,0x64), and the count is never reset;
//   2. shutdown releases every slot and destroys the bodies;
//   3. tokens recorded before shutdown never resolve to a later body.

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

// Retail keeps numPieces for the life of the process, so each test starts
// from an empty pool by hand.
void ResetPool()
{
    DynEntPieces_Shutdown();
    numPieces = 0;
    g_destroyedCount = 0;
    g_wrongWorld = false;
}

bool TestCommitStopsAtRetailCap()
{
    ResetPool();
    for (int32_t index = 0; index < kPieceCapacity; ++index)
    {
        if (!DynEntPieces_CommitSlotLocked(Body(index), nullptr).committed)
            return false;
        if (numPieces != index + 1 || !g_breakablePieces[index].active)
            return false;
        if (phys_obj_id::ReadResolve<dxBody>(
                g_breakablePieceBodySidecar, g_breakablePieces[index].physObjId)
            != Body(index))
        {
            return false;
        }
    }

    // The 101st piece is refused and nothing is evicted.
    if (DynEntPieces_CommitSlotLocked(Body(kPieceCapacity), nullptr).committed)
        return false;
    if (numPieces != kPieceCapacity || g_destroyedCount != 0)
        return false;
    if (phys_obj_id::ReadResolve<dxBody>(
            g_breakablePieceBodySidecar, g_breakablePieces[0].physObjId)
        != Body(0))
    {
        return false;
    }

    // Shutdown frees every body but keeps the count, so the next map still
    // spawns no pieces, as in retail.
    DynEntPieces_Shutdown();
    if (g_wrongWorld || g_destroyedCount != kPieceCapacity)
        return false;
    if (numPieces != kPieceCapacity)
        return false;
    return !DynEntPieces_CommitSlotLocked(Body(kPieceCapacity + 1), nullptr).committed;
}

bool TestShutdownReleasesEverySlot()
{
    ResetPool();

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
    if (numPieces != kPieceCapacity)
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
    ResetPool();

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
    if (numPieces != kPieceCapacity)
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
    if (!TestCommitStopsAtRetailCap())
        return Fail("commit/retail cap");
    if (!TestShutdownReleasesEverySlot())
        return Fail("shutdown/stale token");
    if (!TestClShutdownReleasesPieces())
        return Fail("DynEntCl_Shutdown wiring");
    std::printf("dynent pieces pool tests passed\n");
    return 0;
}
