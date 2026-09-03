#define KISAK_FX_FASTFILE_ZONE_ADAPTER_TESTING 1
#define KISAK_DB_FX_ZONE_ADAPTER_WIRING_TESTING 1

#include <database/db_fx_zone_adapter_wiring.h>

#include <EffectsCore/fx_fastfile_disk32.h>
#include <EffectsCore/fx_fastfile_native_arena.h>
#include <EffectsCore/fx_fastfile_zone_adapter_disk32.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
namespace fastfile = fx::fastfile;

int failures = 0;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

constexpr std::size_t kArenaCapacity = 64u * 1024u;

struct ArenaBinding final
{
    alignas(fastfile::kFxFastFileNativeArenaStorageAlignment)
        std::uint8_t storage[kArenaCapacity]{};
    fastfile::FxFastFileNativeArena arena{};

    void Bind()
    {
        const auto status = arena.TryBind(storage, sizeof(storage), 0xAC1D);
        CHECK(status == fastfile::FxFastFileNativeArenaStatus::Success);
    }

    void Unbind()
    {
        const auto status = arena.TryUnbind();
        CHECK(status == fastfile::FxFastFileNativeArenaStatus::Success);
    }
};

struct WorkspaceBinding final
{
    std::unique_ptr<fastfile::FxFastFileZoneAdapterDisk32Workspace> workspace =
        std::make_unique<fastfile::FxFastFileZoneAdapterDisk32Workspace>();

    bool CompositionReady() const
    {
        return workspace->readyForCompositionAuthentication();
    }
};

void TestNoBindingReturnsNull()
{
    using namespace db::fx_zone_adapter_wiring;

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    CHECK(!IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace() == nullptr);
    CHECK(TryGetActiveFxZoneAdapterArena() == nullptr);
    CHECK(TryAbortActiveFxZoneAdapterTransaction() == false);

    const std::uint8_t scratch[16]{};
    CHECK(TryWireImpactTableThroughActiveFxZoneAdapter(
              true, scratch, sizeof(scratch))
          == nullptr);
    CHECK(TryWireEffectDefThroughActiveFxZoneAdapter(
              true, scratch, sizeof(scratch))
          == nullptr);

    ResetActiveFxZoneAdapterBindingProbe();
    CHECK(!IsFxZoneAdapterBindingActive());
}

void TestNullBytesReturnsNull()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    arenaBinding.Bind();
    CHECK(workspaceBinding.CompositionReady());
    FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
        workspaceBinding.workspace.get(), &arenaBinding.arena);
    CHECK(IsFxZoneAdapterBindingActive());

    CHECK(TryWireImpactTableThroughActiveFxZoneAdapter(
              true, nullptr, 0)
          == nullptr);
    CHECK(TryWireImpactTableThroughActiveFxZoneAdapter(
              true, nullptr, sizeof(fastfile::FxImpactTableDisk32))
          == nullptr);
    CHECK(TryWireEffectDefThroughActiveFxZoneAdapter(
              true, nullptr, 0)
          == nullptr);
    CHECK(TryWireEffectDefThroughActiveFxZoneAdapter(
              true, nullptr, sizeof(fastfile::FxEffectDefDisk32))
          == nullptr);

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    arenaBinding.Unbind();
}

void TestShortBytesReturnsNull()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    arenaBinding.Bind();
    CHECK(workspaceBinding.CompositionReady());
    FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
        workspaceBinding.workspace.get(), &arenaBinding.arena);
    CHECK(IsFxZoneAdapterBindingActive());

    const std::uint8_t scratch[4]{};
    CHECK(TryWireImpactTableThroughActiveFxZoneAdapter(
              true, scratch, sizeof(scratch))
          == nullptr);
    CHECK(TryWireEffectDefThroughActiveFxZoneAdapter(
              true, scratch, sizeof(scratch))
          == nullptr);

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    arenaBinding.Unbind();
}

void TestInvalidHeaderTokensFailClosed()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    arenaBinding.Bind();
    CHECK(workspaceBinding.CompositionReady());
    FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
        workspaceBinding.workspace.get(), &arenaBinding.arena);
    CHECK(IsFxZoneAdapterBindingActive());

    alignas(4) std::uint8_t impactScratch[16]{};
    std::memset(impactScratch, 0, sizeof(impactScratch));
    CHECK(TryWireImpactTableThroughActiveFxZoneAdapter(
              true,
              impactScratch,
              sizeof(fastfile::FxImpactTableDisk32))
          == nullptr);

    alignas(4) std::uint8_t effectScratch[32]{};
    std::memset(effectScratch, 0, sizeof(effectScratch));
    CHECK(TryWireEffectDefThroughActiveFxZoneAdapter(
              true,
              effectScratch,
              sizeof(fastfile::FxEffectDefDisk32))
          == nullptr);

    CHECK(TryAbortActiveFxZoneAdapterTransaction() == false);

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    arenaBinding.Unbind();
}

void TestUnboundArenaLeavesProbeInactive()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    CHECK(workspaceBinding.CompositionReady());
    FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
        workspaceBinding.workspace.get(), &arenaBinding.arena);
    CHECK(!IsFxZoneAdapterBindingActive());

    const std::uint8_t scratch[16]{};
    CHECK(TryWireImpactTableThroughActiveFxZoneAdapter(
              true, scratch, sizeof(scratch))
          == nullptr);
    CHECK(TryWireEffectDefThroughActiveFxZoneAdapter(
              true, scratch, sizeof(scratch))
          == nullptr);

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
}

void TestProductionEnrollmentAndExactClear()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding firstArenaBinding;
    ArenaBinding secondArenaBinding;
    WorkspaceBinding firstWorkspaceBinding;
    WorkspaceBinding secondWorkspaceBinding;
    firstArenaBinding.Bind();
    secondArenaBinding.Bind();

    CHECK(TryEnrollActiveFxZoneAdapterBinding(
        firstWorkspaceBinding.workspace.get(), &firstArenaBinding.arena));
    CHECK(IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace()
          == firstWorkspaceBinding.workspace.get());
    CHECK(TryGetActiveFxZoneAdapterArena() == &firstArenaBinding.arena);
    CHECK(TryEnrollActiveFxZoneAdapterBinding(
        firstWorkspaceBinding.workspace.get(), &firstArenaBinding.arena));
    CHECK(!TryEnrollActiveFxZoneAdapterBinding(
        secondWorkspaceBinding.workspace.get(), &secondArenaBinding.arena));
    CHECK(!TryClearActiveFxZoneAdapterBinding(
        secondWorkspaceBinding.workspace.get(), &firstArenaBinding.arena));
    CHECK(IsFxZoneAdapterBindingActive());
    CHECK(TryClearActiveFxZoneAdapterBinding(
        firstWorkspaceBinding.workspace.get(), &firstArenaBinding.arena));
    CHECK(!IsFxZoneAdapterBindingActive());
    CHECK(!TryClearActiveFxZoneAdapterBinding(
        firstWorkspaceBinding.workspace.get(), &firstArenaBinding.arena));

    firstArenaBinding.Unbind();
    secondArenaBinding.Unbind();
}

void TestResetClearsProbe()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    arenaBinding.Bind();
    CHECK(workspaceBinding.CompositionReady());
    FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
        workspaceBinding.workspace.get(), &arenaBinding.arena);
    CHECK(IsFxZoneAdapterBindingActive());

    ResetActiveFxZoneAdapterBindingProbe();
    CHECK(!IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace() == nullptr);
    CHECK(TryGetActiveFxZoneAdapterArena() == nullptr);

    arenaBinding.Unbind();
}

// Unload-order contract at the wiring layer.  The zone-runtime table tears
// its native workspace down by first making external consumers unreachable,
// then clearing the wiring binding, then destroying the bound storage; a
// Busy/InvalidPhase destruction result re-enrolls the exact pair to restore
// authority.  This pins both halves: once the arena's storage region is
// released (unbound) the binding loses every piece of wiring authority even
// though the record still names the pair, exact-pair cleanup still succeeds
// so the table can always retire the record, and re-binding plus
// re-enrollment restores full authority for the same pair.
void TestUnloadOrderAuthorityDiesWithUnboundStorage()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    arenaBinding.Bind();
    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    CHECK(TryEnrollActiveFxZoneAdapterBinding(
        workspaceBinding.workspace.get(), &arenaBinding.arena));
    CHECK(IsFxZoneAdapterBindingActive());

    // Teardown begins: the zone releases the storage region (the arena
    // unbinds) while the binding record still names this exact pair.  No
    // wiring authority may survive: the loader cannot materialize anything
    // new into storage that is being torn down.
    arenaBinding.Unbind();
    CHECK(!IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace() == nullptr);
    CHECK(TryGetActiveFxZoneAdapterArena() == nullptr);
    const std::uint8_t scratch[16]{};
    CHECK(TryWireImpactTableThroughActiveFxZoneAdapter(
              true, scratch, sizeof(scratch))
          == nullptr);
    CHECK(TryWireEffectDefThroughActiveFxZoneAdapter(
              true, scratch, sizeof(scratch))
          == nullptr);
    CHECK(TryAbortActiveFxZoneAdapterTransaction() == false);

    // Exact-pair cleanup still succeeds while storage is gone, so the table
    // can retire the record; a second clear cannot.
    CHECK(TryClearActiveFxZoneAdapterBinding(
        workspaceBinding.workspace.get(), &arenaBinding.arena));
    CHECK(!IsFxZoneAdapterBindingActive());
    CHECK(!TryClearActiveFxZoneAdapterBinding(
        workspaceBinding.workspace.get(), &arenaBinding.arena));

    // Recovery path: storage destruction reported a recoverable result, the
    // table re-binds the arena and re-enrolls the exact pair.  Authority
    // must return intact for the same generation.
    arenaBinding.Bind();
    CHECK(TryEnrollActiveFxZoneAdapterBinding(
        workspaceBinding.workspace.get(), &arenaBinding.arena));
    CHECK(IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace()
          == workspaceBinding.workspace.get());
    CHECK(TryGetActiveFxZoneAdapterArena() == &arenaBinding.arena);

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    arenaBinding.Unbind();
}

// Slot-generation-reuse contract at the wiring layer.  When a zone slot is
// reused after unload, the fresh generation's pair must own the binding
// exclusively while every piece of stale generation-one authority stays
// dead, and destroying the stale generation's objects must not disturb the
// live binding (no cross-generation aliasing through recycled identities).
void TestSlotGenerationReuseRejectsStaleAuthority()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding generationOneArena;
    WorkspaceBinding generationOneWorkspace;
    generationOneArena.Bind();
    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    CHECK(TryEnrollActiveFxZoneAdapterBinding(
        generationOneWorkspace.workspace.get(), &generationOneArena.arena));
    CHECK(IsFxZoneAdapterBindingActive());

    // Generation-one unload: exact-pair clear retires the binding.
    CHECK(TryClearActiveFxZoneAdapterBinding(
        generationOneWorkspace.workspace.get(), &generationOneArena.arena));
    CHECK(!IsFxZoneAdapterBindingActive());

    // The slot is reused by a fresh generation with distinct workspace and
    // storage identities.
    ArenaBinding generationTwoArena;
    WorkspaceBinding generationTwoWorkspace;
    generationTwoArena.Bind();
    CHECK(TryEnrollActiveFxZoneAdapterBinding(
        generationTwoWorkspace.workspace.get(), &generationTwoArena.arena));
    CHECK(IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace()
          == generationTwoWorkspace.workspace.get());
    CHECK(TryGetActiveFxZoneAdapterArena() == &generationTwoArena.arena);

    // Stale generation-one authority is dead: it can neither reclaim the
    // slot nor clear the fresh generation's record, in any pairing.
    CHECK(!TryEnrollActiveFxZoneAdapterBinding(
        generationOneWorkspace.workspace.get(), &generationOneArena.arena));
    CHECK(!TryClearActiveFxZoneAdapterBinding(
        generationOneWorkspace.workspace.get(), &generationOneArena.arena));
    CHECK(!TryClearActiveFxZoneAdapterBinding(
        generationOneWorkspace.workspace.get(), &generationTwoArena.arena));
    CHECK(!TryClearActiveFxZoneAdapterBinding(
        generationTwoWorkspace.workspace.get(), &generationOneArena.arena));
    CHECK(IsFxZoneAdapterBindingActive());

    // Destroying the stale generation's objects cannot disturb the live
    // binding: the record resolves only through the registered pair.
    generationOneWorkspace.workspace.reset();
    generationOneArena.Unbind();
    CHECK(IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace()
          == generationTwoWorkspace.workspace.get());
    CHECK(TryGetActiveFxZoneAdapterArena() == &generationTwoArena.arena);

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    generationTwoArena.Unbind();
}

} // namespace

int main()
{
    TestNoBindingReturnsNull();
    TestNullBytesReturnsNull();
    TestShortBytesReturnsNull();
    TestInvalidHeaderTokensFailClosed();
    TestUnboundArenaLeavesProbeInactive();
    TestProductionEnrollmentAndExactClear();
    TestResetClearsProbe();
    TestUnloadOrderAuthorityDiesWithUnboundStorage();
    TestSlotGenerationReuseRejectsStaleAuthority();

    if (failures != 0)
    {
        std::fprintf(stderr, "fx zone adapter wiring tests failed: %d\n", failures);
        return 1;
    }
    std::printf("fx zone adapter wiring tests passed\n");
    return 0;
}
