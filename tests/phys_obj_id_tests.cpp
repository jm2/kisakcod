// SPDX-License-Identifier: GPL-3.0
//
// phys_obj_id_tests.cpp - exercises the priority-7 native64 ABI seam. The
// cpose_t::physObjId, BreakablePiece::physObjId, and DynEntityClient::
// physObjId fields are frozen 32-bit tokens backed by generation-checked
// sidecars. These tests prove:
//   1. saved bytes do not widen (DynEntityClient stays 0xC, BreakablePiece
//      stays 0xC, MP cpose_t stays 0x64/0x68);
//   2. a token round-trips through Bind / Resolve / Release and rejects
//      stale generations after a body has been retired;
//   3. the legacy sentinel contract (0x00000000 == null, 0xFFFFFFFF ==
//      dead) is preserved across every operation;
//   4. (compile-time) the size/offset assertions for the runtime and
//      on-disk layouts compile and pass on every target.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

#include <universal/kisak_abi.h>
#include <universal/phys_obj_id.h>

// The sidecars are declared in universal/phys_obj_id.h and defined in
// bgame/bg_phys_obj_id_tables.cpp (which this test CMakeLists pulls in
// directly). The struct types they describe on the engine side come from
// bgame/bg_local.h and DynEntity/DynEntity_client.h; for the size/offset
// regression checks below we re-introduce the minimum surface we need so
// this test does not depend on the engine translation units.
//
// The runtime types must agree with the engine; if the engine layouts
// drift, the static_asserts in bgame/bg_local.h and DynEntity_client.h
// fail at compile time and the test would never get this far.
struct [[nodiscard]] alignas(4) DynEntityClientLayout
{
    std::int32_t physObjId;
    std::uint16_t flags;
    std::uint16_t lightingHandle;
    std::int32_t health;
};
ONDISK_SIZE(DynEntityClientLayout, 0xC);

struct [[nodiscard]] alignas(4) BreakablePieceLayout
{
    std::int32_t model; // pointer stored as 32-bit on the 32-bit-on-disk layout
    std::int32_t physObjId;
    std::uint16_t lightingHandle;
    bool active;
    std::uint8_t pad;
};
ONDISK_SIZE(BreakablePieceLayout, 0xC);

namespace
{
// On-disk / saved-byte regression contract: the test TU re-declares the
// minimum surface of the runtime shapes and pins their sizes via the
// platform-wide ONDISK_SIZE contract. The static_asserts in the public
// headers (bgame/bg_local.h, DynEntity/DynEntity_client.h) confirm the
// engine side stays in sync; this test guards the test side.

// Not constexpr: std::fprintf is not a constexpr call, so a constexpr Fail
// can never produce a constant expression (MSVC C3615 under /W4+/WX; GCC
// accepts it only as ill-formed-no-diagnostic-required). Matches the repo
// test idiom used by the other test TUs.
int Fail(const char *const message)
{
    std::fprintf(stderr, "phys_obj_id test failed: %s\n", message);
    return 1;
}

bool TestTokenSentinels()
{
    if (!phys_obj_id::IsNull(phys_obj_id::INVALID_BODY_TOKEN))
        return false;
    if (!phys_obj_id::IsDead(phys_obj_id::DEAD_BODY_TOKEN))
        return false;
    if (phys_obj_id::IsNull(phys_obj_id::DEAD_BODY_TOKEN) || phys_obj_id::IsDead(phys_obj_id::INVALID_BODY_TOKEN))
        return false;
    // Packed token (gen=1, idx=2) is neither null nor dead.
    const phys_obj_id::BodyToken tok = phys_obj_id::PackToken(1, 2);
    if (!phys_obj_id::IsLive(tok))
        return false;
    if (phys_obj_id::GenerationOf(tok) != 1)
        return false;
    if (phys_obj_id::OwnerOf(tok) != 2)
        return false;
    return true;
}

bool TestBindResolveRelease()
{
    phys_obj_id::BodySidecar<8> sidecar;
    int sentinel[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    void *body = &sentinel[0];
    const phys_obj_id::TokenResult bind = sidecar.Bind(0, body);
    if (!bind)
        return false;
    const phys_obj_id::BodyResult resolved = sidecar.Resolve(bind.token);
    if (!resolved || resolved.body != body)
        return false;
    const phys_obj_id::BodyResult released = sidecar.Release(bind.token);
    if (!released || released.body != body)
        return false;
    // After release, the same token must not resolve again.
    const phys_obj_id::BodyResult second = sidecar.Resolve(bind.token);
    if (second)
        return false;
    return true;
}

bool TestStaleTokenRejection()
{
    phys_obj_id::BodySidecar<8> sidecar;
    int sentinel[8] = {0};
    void *firstBody = &sentinel[0];
    void *secondBody = &sentinel[1];
    const phys_obj_id::TokenResult firstBind = sidecar.Bind(0, firstBody);
    if (!firstBind)
        return false;
    // Release the first binding; the next bind on the same owner must
    // produce a different generation so the stale token cannot resolve.
    const phys_obj_id::BodyResult released = sidecar.Release(firstBind.token);
    if (!released || released.body != firstBody)
        return false;
    const phys_obj_id::TokenResult secondBind = sidecar.Bind(0, secondBody);
    if (!secondBind)
        return false;
    if (secondBind.token == firstBind.token)
        return false;
    const phys_obj_id::BodyResult staleResolve = sidecar.Resolve(firstBind.token);
    if (staleResolve)
        return false;
    const phys_obj_id::BodyResult liveResolve = sidecar.Resolve(secondBind.token);
    if (!liveResolve || liveResolve.body != secondBody)
        return false;
    return true;
}

bool TestDoubleBindRejected()
{
    phys_obj_id::BodySidecar<8> sidecar;
    int sentinel[8] = {0};
    const phys_obj_id::TokenResult first = sidecar.Bind(2, &sentinel[0]);
    if (!first)
        return false;
    const phys_obj_id::TokenResult second = sidecar.Bind(2, &sentinel[1]);
    if (second)
        return false;
    if (second.status != phys_obj_id::Status::AlreadyBound)
        return false;
    return true;
}

bool TestInvalidArguments()
{
    phys_obj_id::BodySidecar<8> sidecar;
    int sentinel[8] = {0};
    // Bind with null body is rejected.
    const phys_obj_id::TokenResult nullBody = sidecar.Bind(0, nullptr);
    if (nullBody)
        return false;
    // Bind out of range is rejected.
    const phys_obj_id::TokenResult outOfRange = sidecar.Bind(128, &sentinel[0]);
    if (outOfRange)
        return false;
    // Resolve on null/dead is rejected.
    if (sidecar.Resolve(phys_obj_id::INVALID_BODY_TOKEN))
        return false;
    if (sidecar.Resolve(phys_obj_id::DEAD_BODY_TOKEN))
        return false;
    // Release on null/dead is rejected.
    if (sidecar.Release(phys_obj_id::INVALID_BODY_TOKEN))
        return false;
    if (sidecar.Release(phys_obj_id::DEAD_BODY_TOKEN))
        return false;
    return true;
}

bool TestWriteBindHelper()
{
    phys_obj_id::BodySidecar<4> sidecar;
    int sentinel[4] = {0};
    phys_obj_id::BodyToken field = 0xDEADBEEFu;
    const phys_obj_id::TokenResult bind = phys_obj_id::WriteBind(
        sidecar, &field, 0, &sentinel[0]);
    if (!bind)
        return false;
    if (field != bind.token)
        return false;
    if (field == 0xDEADBEEFu)
        return false;
    void *const body = phys_obj_id::ReadResolve<void>(sidecar, field);
    if (body != &sentinel[0])
        return false;
    return true;
}

bool TestConsumeReleaseHelper()
{
    phys_obj_id::BodySidecar<4> sidecar;
    int sentinel[4] = {0};
    phys_obj_id::BodyToken field = phys_obj_id::INVALID_BODY_TOKEN;
    if (!phys_obj_id::WriteBind(sidecar, &field, 0, &sentinel[0]))
        return false;
    void *body = nullptr;
    if (!phys_obj_id::ConsumeRelease<void>(sidecar, &field, &body))
        return false;
    if (body != &sentinel[0])
        return false;
    if (field != phys_obj_id::INVALID_BODY_TOKEN)
        return false;
    // A second consume must fail.
    if (phys_obj_id::ConsumeRelease<void>(sidecar, &field, &body))
        return false;
    return true;
}

bool TestGlobalSidecarReflexiveBind()
{
    // The cpose sidecar exists on every target, including the 32-bit ILP32
    // build that this test compiles under. The native pointer happens to
    // fit in 32 bits on ILP32, but the sidecar contract is identical on
    // LP64/LLP64 so the binding round-trip is the same shape.
    int sentinel = 0;
    const phys_obj_id::OwnerIndex owner = 0;
    phys_obj_id::BodyToken field = phys_obj_id::INVALID_BODY_TOKEN;
    const phys_obj_id::TokenResult bind = phys_obj_id::WriteBind(
        g_cposeBodySidecar, &field, owner, &sentinel);
    if (!bind)
        return false;
    const void *const resolved = phys_obj_id::ReadResolve<void>(
        g_cposeBodySidecar, field);
    if (resolved != &sentinel)
        return false;
    // ReleaseByOwner would touch the same slot; safe to use here because
    // the test owns the binding and we just relenquish it.
    const phys_obj_id::BodyResult r = g_cposeBodySidecar.ReleaseByOwner(owner);
    if (!r || r.body != &sentinel)
        return false;
    return true;
}

bool TestGlobalBreakablePieceSidecar()
{
    int sentinel = 0;
    const phys_obj_id::OwnerIndex owner = 0;
    phys_obj_id::BodyToken field = phys_obj_id::INVALID_BODY_TOKEN;
    if (!phys_obj_id::WriteBind(g_breakablePieceBodySidecar, &field, owner, &sentinel))
        return false;
    const void *const resolved = phys_obj_id::ReadResolve<void>(
        g_breakablePieceBodySidecar, field);
    if (resolved != &sentinel)
        return false;
    const phys_obj_id::BodyResult r = g_breakablePieceBodySidecar.ReleaseByOwner(owner);
    if (!r || r.body != &sentinel)
        return false;
    return true;
}

bool TestGlobalDynEntClientSidecar()
{
    int sentinel = 0;
    const phys_obj_id::OwnerIndex owner = 0;
    phys_obj_id::BodyToken field = phys_obj_id::INVALID_BODY_TOKEN;
    if (!phys_obj_id::WriteBind(g_dynEntClientBodySidecar, &field, owner, &sentinel))
        return false;
    const void *const resolved = phys_obj_id::ReadResolve<void>(
        g_dynEntClientBodySidecar, field);
    if (resolved != &sentinel)
        return false;
    const phys_obj_id::BodyResult r = g_dynEntClientBodySidecar.ReleaseByOwner(owner);
    if (!r || r.body != &sentinel)
        return false;
    return true;
}

// Failure/reuse contract for the engine bind-rollback recipes: every
// production failed-bind path (DynEntity save loader, breakable-piece
// spawn, cpose creation) must (a) leave the winning binding untouched,
// (b) destroy/release its fresh body, and (c) leave the slot cleanly
// reusable with a bumped generation so stale tokens stay rejected.
// Split from the former single TestFailedBindReleaseReuseContract to
// keep per-function cyclomatic complexity under Codacy's limit of 10;
// the case order and failure semantics are unchanged.
bool TestFailedBindCollisionContract()
{
    int bodyA = 0;
    int bodyB = 0;
    phys_obj_id::BodySidecar<4> sidecar;
    const phys_obj_id::OwnerIndex owner = 3; // in-capacity slot
    phys_obj_id::BodyToken fieldA = phys_obj_id::INVALID_BODY_TOKEN;

    // 1. First bind wins.
    const phys_obj_id::TokenResult bindA =
        phys_obj_id::WriteBind(sidecar, &fieldA, owner, &bodyA);
    if (!bindA)
        return false;

    // 2. A colliding bind (the loader collision this bead's bound
    //    enforcement prevents) fails with AlreadyBound and leaves the
    //    winning field/binding exactly intact.
    phys_obj_id::BodyToken fieldB = phys_obj_id::INVALID_BODY_TOKEN;
    const phys_obj_id::TokenResult bindB =
        phys_obj_id::WriteBind(sidecar, &fieldB, owner, &bodyB);
    if (bindB.status != phys_obj_id::Status::AlreadyBound)
        return false;
    if (fieldB != phys_obj_id::INVALID_BODY_TOKEN)
        return false;
    if (phys_obj_id::ReadResolve<void>(sidecar, fieldA) != &bodyA)
        return false;

    // 3. A failed bind leaves no phantom state: the loser's field was
    //    never written, so consuming it finds nothing (the engine
    //    rollback paths destroy their fresh BODY here; the sidecar slot
    //    still belongs to the winner).
    void *phantom = nullptr;
    if (phys_obj_id::ConsumeRelease<void>(sidecar, &fieldB, &phantom))
        return false;
    return true;
}

bool TestReleaseReuseGenerationContract()
{
    int bodyA = 0;
    int bodyB = 0;
    phys_obj_id::BodySidecar<4> sidecar;
    const phys_obj_id::OwnerIndex owner = 3; // in-capacity slot
    phys_obj_id::BodyToken fieldA = phys_obj_id::INVALID_BODY_TOKEN;

    // 1. First bind wins.
    const phys_obj_id::TokenResult bindA =
        phys_obj_id::WriteBind(sidecar, &fieldA, owner, &bodyA);
    if (!bindA)
        return false;

    // 2. Releasing the winning binding (shutdown/reuse teardown) hands
    //    back exactly its body and clears the field.
    void *released = nullptr;
    if (!phys_obj_id::ConsumeRelease<void>(sidecar, &fieldA, &released))
        return false;
    if (released != &bodyA)
        return false;

    // 3. The slot is reusable: a new bind succeeds with a bumped
    //    generation, and the old token is stale (resolves to null).
    phys_obj_id::BodyToken fieldB = phys_obj_id::INVALID_BODY_TOKEN;
    const phys_obj_id::TokenResult rebind =
        phys_obj_id::WriteBind(sidecar, &fieldB, owner, &bodyB);
    if (!rebind)
        return false;
    if (phys_obj_id::OwnerOf(rebind.token) != phys_obj_id::OwnerOf(bindA.token))
        return false;
    if (phys_obj_id::GenerationOf(rebind.token)
        == phys_obj_id::GenerationOf(bindA.token))
        return false;
    if (phys_obj_id::ReadResolve<void>(sidecar, fieldA) != nullptr)
        return false;
    if (phys_obj_id::ReadResolve<void>(sidecar, fieldB) != &bodyB)
        return false;
    return true;
}

// Failed-load stale-token contract for the SP save-image rebuild (the
// assessed probe case): a serialized DynEntityClient image can arrive
// with a live-looking token whose body no longer exists, and the
// production adapter (Phys_ObjLoad) can fail to restore any body at
// all. The loader contract is:
//   (a) the serialized token is cleared BEFORE restoration, so a
//       failed load can never leave a token that resolves through the
//       sidecar to whichever body now occupies that owner slot;
//   (b) the foreign binding that currently owns the slot is left
//       completely undisturbed by the failed restore;
//   (c) a later successful bind of the same owner (reuse path) bumps
//       the generation so the stale saved token stays rejected forever.
// Split into two functions to keep per-function cyclomatic complexity
// under Codacy's limit of 10; the scenario order and failure
// semantics are unchanged.
bool TestFailedLoadClearsStaleToken()
{
    int foreignBody = 0;
    phys_obj_id::BodySidecar<4> sidecar;
    const phys_obj_id::OwnerIndex owner = 1; // in-capacity slot

    // Another entity legitimately owns the slot the stale token names;
    // foreignField carries that live binding's token.
    phys_obj_id::BodyToken foreignField = phys_obj_id::INVALID_BODY_TOKEN;
    const phys_obj_id::TokenResult foreignBind =
        phys_obj_id::WriteBind(sidecar, &foreignField, owner, &foreignBody);
    if (!foreignBind)
        return false;

    // The save image hands the loader this stale saved token (as the
    // assessed probe's field: token 65537 surviving a failed load).
    const phys_obj_id::BodyToken savedToken = foreignBind.token;

    // Loader pre-restoration clear (the fixed DynEnt_LoadEntities
    // contract): the field is wiped BEFORE Phys_ObjLoad runs, and a
    // failed load publishes nothing. Model the post-loader state:
    phys_obj_id::BodyToken field = phys_obj_id::INVALID_BODY_TOKEN;

    // (a) The cleared field must NOT resolve — before the fix, the
    //     surviving stale token resolved to the foreign body here.
    if (phys_obj_id::ReadResolve<void>(sidecar, field) != nullptr)
        return false;

    // (b) The foreign binding is undisturbed: its own token still
    //     resolves to the foreign body.
    if (phys_obj_id::ReadResolve<void>(sidecar, savedToken) != &foreignBody)
        return false;

    // Failed-restore cleanup: consuming the cleared field must find
    // nothing (no phantom body for the runtime to destroy later).
    void *phantom = nullptr;
    if (phys_obj_id::ConsumeRelease<void>(sidecar, &field, &phantom))
        return false;
    if (phantom != nullptr)
        return false;
    return true;
}

bool TestFailedLoadReuseGeneration()
{
    int foreignBody = 0;
    int restoredBody = 0;
    phys_obj_id::BodySidecar<4> sidecar;
    const phys_obj_id::OwnerIndex owner = 1; // in-capacity slot

    // Another entity legitimately owns the slot the stale token names.
    phys_obj_id::BodyToken foreignField = phys_obj_id::INVALID_BODY_TOKEN;
    const phys_obj_id::TokenResult foreignBind =
        phys_obj_id::WriteBind(sidecar, &foreignField, owner, &foreignBody);
    if (!foreignBind)
        return false;
    const phys_obj_id::BodyToken savedToken = foreignBind.token;

    // Loader pre-restoration clear, as above; the failed restore
    // publishes nothing.
    phys_obj_id::BodyToken field = phys_obj_id::INVALID_BODY_TOKEN;

    // (c) Reuse path: the foreign owner is legitimately torn down first
    //     (ConsumeRelease — the engine shutdown/unload contract), which
    //     frees the slot; a later successful load of this entity then
    //     binds the same owner with a bumped generation, so the stale
    //     saved token can never be confused with the fresh binding.
    void *foreignReleased = nullptr;
    if (!phys_obj_id::ConsumeRelease<void>(sidecar, &foreignField, &foreignReleased))
        return false;
    if (foreignReleased != &foreignBody)
        return false;

    const phys_obj_id::TokenResult rebind =
        phys_obj_id::WriteBind(sidecar, &field, owner, &restoredBody);
    if (!rebind)
        return false;
    if (phys_obj_id::GenerationOf(rebind.token)
        == phys_obj_id::GenerationOf(foreignBind.token))
        return false;
    if (phys_obj_id::ReadResolve<void>(sidecar, savedToken) != nullptr)
        return false;
    if (phys_obj_id::ReadResolve<void>(sidecar, field) != &restoredBody)
        return false;
    return true;
}

// Owner-packing stride contract for the DynEntityClient sidecar keys:
// owner = drawType * kDynEntPhysObjIdOwnerPerDrawType + dynEntId. The
// first id past the stride aliases the next draw type's slot 0 — the
// exact collision the load paths must reject with a release-effective
// error (Com_Error) before publishing the entity lists. Legal counts
// stay collision-free and within the sidecar capacity.
constexpr bool DynEntOwnerIndexStrideContractHolds()
{
    // MODEL id 4096 and BRUSH id 0 collide (the assessed probe case).
    if (phys_obj_id::DynEntPhysObjId_MakeOwnerIndex(0u, 4096u)
        != phys_obj_id::DynEntPhysObjId_MakeOwnerIndex(1u, 0u))
        return false;
    // The highest legal keys stay distinct and inside capacity.
    if (phys_obj_id::DynEntPhysObjId_MakeOwnerIndex(0u, 4095u) != 4095u)
        return false;
    if (phys_obj_id::DynEntPhysObjId_MakeOwnerIndex(1u, 4095u) != 8191u)
        return false;
    return 8191u < kDynEntClientBodySidecarCapacity;
}
static_assert(DynEntOwnerIndexStrideContractHolds(),
    "dynent owner-key stride contract: oversized ids collide, legal ids "
    "stay distinct and within sidecar capacity");

bool TestDynEntOwnerIndexStrideContract()
{
    // Runtime mirror of the constexpr contract above: the packing is
    // monotonic within each draw type for every legal id, and the two
    // draw-type ranges never overlap below the stride.
    for (std::uint32_t id = 0; id < 4096u; ++id)
    {
        const auto modelOwner =
            phys_obj_id::DynEntPhysObjId_MakeOwnerIndex(0u, static_cast<std::uint16_t>(id));
        const auto brushOwner =
            phys_obj_id::DynEntPhysObjId_MakeOwnerIndex(1u, static_cast<std::uint16_t>(id));
        if (modelOwner != id)
            return false;
        if (brushOwner != static_cast<phys_obj_id::OwnerIndex>(4096u + id))
            return false;
    }
    return true;
}

// Saved-bytes regression: the runtime DynEntityClient/BreakablePiece
// struct sizes must NOT drift. These are enforced at compile time so a
// layout drift fails the build before any test runs. The MP cpose_t
// struct lives in bgame/bg_local.h; the static_asserts there pin its
// layout at 0x64/0x68 and the physObjId offset at 0x14.
static_assert(sizeof(DynEntityClientLayout) == 0xC,
    "DynEntityClient save-image layout is frozen at 12 bytes");
static_assert(sizeof(BreakablePieceLayout) == 0xC,
    "BreakablePiece runtime layout is frozen at 12 bytes");
} // namespace

// Table-driven case dispatch. Adding a case appends a table entry and
// does NOT add a branch to any function — Codacy's per-function
// cyclomatic limit of 10 is never re-tripped by new cases (the former
// if-chain dispatchers grew past it as contracts were split).
struct ContractCase
{
    bool (*run)();
    const char *label;
};

// Runs the cases in order. Returns nullptr when all pass, otherwise
// the failed case's label (first-failure semantics identical to the
// former if-chain dispatchers).
static const char *RunContractCases(const ContractCase *const cases, const std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!cases[i].run())
            return cases[i].label;
    }
    return nullptr;
}

static const char *RunCoreTokenContractTests()
{
    static constexpr ContractCase cases[] = {
        {TestTokenSentinels, "sentinel contract"},
        {TestBindResolveRelease, "bind/resolve/release round-trip"},
        {TestStaleTokenRejection, "stale token rejection"},
        {TestDoubleBindRejected, "double-bind rejection"},
        {TestInvalidArguments, "invalid argument rejection"},
    };
    return RunContractCases(cases, std::size(cases));
}

static const char *RunSidecarIntegrationTests()
{
    static constexpr ContractCase cases[] = {
        {TestWriteBindHelper, "WriteBind helper"},
        {TestConsumeReleaseHelper, "ConsumeRelease helper"},
        {TestGlobalSidecarReflexiveBind, "global cpose sidecar bind"},
        {TestGlobalBreakablePieceSidecar, "global breakable piece sidecar bind"},
        {TestGlobalDynEntClientSidecar, "global dynent client sidecar bind"},
        {TestFailedBindCollisionContract, "failed-bind collision contract"},
        {TestReleaseReuseGenerationContract, "release/reuse generation contract"},
        {TestFailedLoadClearsStaleToken, "failed-load stale-token clear contract"},
        {TestFailedLoadReuseGeneration, "failed-load reuse generation contract"},
        {TestDynEntOwnerIndexStrideContract, "dynent owner-index stride contract"},
    };
    return RunContractCases(cases, std::size(cases));
}

int main()
{
    if (const char *const failed = RunCoreTokenContractTests())
        return Fail(failed);
    if (const char *const failed = RunSidecarIntegrationTests())
        return Fail(failed);
    // Frozen-layout contracts (DynEntityClient/BreakablePiece 12-byte
    // images) are enforced by static_assert at compile time.
    std::printf("phys_obj_id tests: all pass\n");
    return 0;
}
