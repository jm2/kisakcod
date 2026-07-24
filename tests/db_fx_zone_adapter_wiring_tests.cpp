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
    fastfile::FxFastFileZoneAdapterDisk32Workspace workspace{};

    bool CompositionReady() const
    {
        return workspace.readyForCompositionAuthentication();
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
        &workspaceBinding.workspace, &arenaBinding.arena);
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
        &workspaceBinding.workspace, &arenaBinding.arena);
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
        &workspaceBinding.workspace, &arenaBinding.arena);
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
        &workspaceBinding.workspace, &arenaBinding.arena);
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

void TestResetClearsProbe()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    arenaBinding.Bind();
    CHECK(workspaceBinding.CompositionReady());
    FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
        &workspaceBinding.workspace, &arenaBinding.arena);
    CHECK(IsFxZoneAdapterBindingActive());

    ResetActiveFxZoneAdapterBindingProbe();
    CHECK(!IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace() == nullptr);
    CHECK(TryGetActiveFxZoneAdapterArena() == nullptr);

    arenaBinding.Unbind();
}

} // namespace

int main()
{
    TestNoBindingReturnsNull();
    TestNullBytesReturnsNull();
    TestShortBytesReturnsNull();
    TestInvalidHeaderTokensFailClosed();
    TestUnboundArenaLeavesProbeInactive();
    TestResetClearsProbe();

    if (failures != 0)
    {
        std::fprintf(stderr, "fx zone adapter wiring tests failed: %d\n", failures);
        return 1;
    }
    std::printf("fx zone adapter wiring tests passed\n");
    return 0;
}
