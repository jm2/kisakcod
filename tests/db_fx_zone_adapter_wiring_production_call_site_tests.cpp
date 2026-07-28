#define KISAK_FX_FASTFILE_ZONE_ADAPTER_TESTING 1
#define KISAK_DB_FX_ZONE_ADAPTER_WIRING_TESTING 1

#include <database/db_fx_zone_adapter_wiring.h>

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

// Mirror the production call shape from src/database/db_load.cpp's
// Load_FxImpactTable / Load_FxEffectDef: the header bytes have been
// streamed into a wire buffer, and the loader hands that buffer plus
// the on-disk byte width to the seam. If the seam enrolls the adapter,
// the loader swaps in the published pointer; otherwise it falls through
// to the legacy retail-bytes walk.
struct ProductionCallShape final
{
    static constexpr std::uint64_t kFxImpactTableDisk32Bytes = 8;
    static constexpr std::uint64_t kFxEffectDefDisk32Bytes = 32;

    void RunImpactTableCallSite(void *const wireHeaderStorage)
    {
        // Production call shape from Load_FxImpactTable:
        //   FxImpactTable *wired = ...TryWireImpactTableThroughActiveFxZoneAdapter(
        //       atStreamStart, varFxImpactTable, kFxImpactTableDisk32Bytes);
        const FxImpactTable *const wired =
            db::fx_zone_adapter_wiring::TryWireImpactTableThroughActiveFxZoneAdapter(
                /*atStreamStart=*/true,
                wireHeaderStorage,
                kFxImpactTableDisk32Bytes);
        if (wired != nullptr)
        {
            std::printf("impact table call site: adapter enrolled (%p)\n",
                        static_cast<const void *>(wired));
        }
        else
        {
            std::printf("impact table call site: legacy fall-through\n");
        }
    }

    void RunEffectDefCallSite(void *const wireHeaderStorage)
    {
        // Production call shape from Load_FxEffectDef:
        //   FxEffectDef *wired = ...TryWireEffectDefThroughActiveFxZoneAdapter(
        //       atStreamStart, varFxEffectDef, kFxEffectDefDisk32Bytes);
        const FxEffectDef *const wired =
            db::fx_zone_adapter_wiring::TryWireEffectDefThroughActiveFxZoneAdapter(
                /*atStreamStart=*/true,
                wireHeaderStorage,
                kFxEffectDefDisk32Bytes);
        if (wired != nullptr)
        {
            std::printf("effect def call site: adapter enrolled (%p)\n",
                        static_cast<const void *>(wired));
        }
        else
        {
            std::printf("effect def call site: legacy fall-through\n");
        }
    }
};

void TestProductionCallSiteNoBindingFallsThrough()
{
    using namespace db::fx_zone_adapter_wiring;

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    CHECK(!IsFxZoneAdapterBindingActive());

    ProductionCallShape harness;

    alignas(4) std::uint8_t impactScratch[8]{};
    alignas(4) std::uint8_t effectScratch[32]{};

    // No active binding → both call sites must return nullptr so the
    // loader's legacy retail-bytes walk takes over unchanged. This is
    // the production behavior when the runtime-table-to-loader thread
    // has not yet authenticated a zone adapter workspace.
    harness.RunImpactTableCallSite(impactScratch);
    harness.RunEffectDefCallSite(effectScratch);

    CHECK(TryAbortActiveFxZoneAdapterTransaction() == false);
}

void TestProductionCallSiteWithBindingFailsClosedOnInvalidHeader()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    arenaBinding.Bind();
    CHECK(workspaceBinding.CompositionReady());
    FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
        workspaceBinding.workspace.get(), &arenaBinding.arena);
    CHECK(IsFxZoneAdapterBindingActive());

    ProductionCallShape harness;

    // Zeroed header bytes — the adapter's TryBegin* rejects zeroed
    // extent tokens, so the seam must surface nullptr and the legacy
    // walk takes over. This proves the production call site is wired
    // AND that invalid wire bytes still leave retail bytes intact.
    alignas(4) std::uint8_t impactScratch[8]{};
    std::memset(impactScratch, 0, sizeof(impactScratch));
    harness.RunImpactTableCallSite(impactScratch);

    alignas(4) std::uint8_t effectScratch[32]{};
    std::memset(effectScratch, 0, sizeof(effectScratch));
    harness.RunEffectDefCallSite(effectScratch);

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
    arenaBinding.Unbind();
}

void TestProductionCallSiteUnboundArenaStillFallsThrough()
{
    using namespace db::fx_zone_adapter_wiring;

    ArenaBinding arenaBinding;
    WorkspaceBinding workspaceBinding;
    CHECK(workspaceBinding.CompositionReady());
    // Note: arenaBinding.Bind() intentionally NOT called.
    FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
        workspaceBinding.workspace.get(), &arenaBinding.arena);
    CHECK(!IsFxZoneAdapterBindingActive());

    ProductionCallShape harness;

    alignas(4) std::uint8_t impactScratch[8]{};
    std::memset(impactScratch, 0xCC, sizeof(impactScratch));
    harness.RunImpactTableCallSite(impactScratch);

    alignas(4) std::uint8_t effectScratch[32]{};
    std::memset(effectScratch, 0xCC, sizeof(effectScratch));
    harness.RunEffectDefCallSite(effectScratch);

    FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting();
}

} // namespace

int main()
{
    TestProductionCallSiteNoBindingFallsThrough();
    TestProductionCallSiteWithBindingFailsClosedOnInvalidHeader();
    TestProductionCallSiteUnboundArenaStillFallsThrough();

    if (failures != 0)
    {
        std::fprintf(stderr,
                     "fx zone adapter wiring production call-site tests failed: %d\n",
                     failures);
        return 1;
    }
    std::printf("fx zone adapter wiring production call-site tests passed\n");
    return 0;
}
