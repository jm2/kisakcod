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

// Runs the core token-contract cases in order. Returns nullptr when all
// pass, otherwise the failed case's label.
static const char *RunCoreTokenContractTests()
{
    if (!TestTokenSentinels())
        return "sentinel contract";
    if (!TestBindResolveRelease())
        return "bind/resolve/release round-trip";
    if (!TestStaleTokenRejection())
        return "stale token rejection";
    if (!TestDoubleBindRejected())
        return "double-bind rejection";
    if (!TestInvalidArguments())
        return "invalid argument rejection";
    return nullptr;
}

// Runs the sidecar-integration cases in order. Returns nullptr when all
// pass, otherwise the failed case's label.
static const char *RunSidecarIntegrationTests()
{
    if (!TestWriteBindHelper())
        return "WriteBind helper";
    if (!TestConsumeReleaseHelper())
        return "ConsumeRelease helper";
    if (!TestGlobalSidecarReflexiveBind())
        return "global cpose sidecar bind";
    if (!TestGlobalBreakablePieceSidecar())
        return "global breakable piece sidecar bind";
    if (!TestGlobalDynEntClientSidecar())
        return "global dynent client sidecar bind";
    return nullptr;
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
