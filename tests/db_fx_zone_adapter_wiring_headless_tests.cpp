#include <database/db_fx_zone_adapter_wiring.h>

#include <cstdint>
#include <cstdio>

namespace
{
int g_checks = 0;

#define CHECK(expr) do {                                                  \
    ++g_checks;                                                           \
    if (!(expr)) {                                                        \
        std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);   \
        return 1;                                                         \
    }                                                                     \
} while (0)
}

int main()
{
    using namespace db::fx_zone_adapter_wiring;

    ResetActiveFxZoneAdapterBindingProbe();
    CHECK(!IsFxZoneAdapterBindingActive());
    CHECK(TryGetActiveFxZoneAdapterWorkspace() == nullptr);
    CHECK(TryGetActiveFxZoneAdapterArena() == nullptr);
    CHECK(!TryEnrollActiveFxZoneAdapterBinding(nullptr, nullptr));
    CHECK(!TryClearActiveFxZoneAdapterBinding(nullptr, nullptr));

    const std::uint8_t wireBytes[32]{};
    CHECK(TryWireImpactTableThroughActiveFxZoneAdapter(
              true, wireBytes, sizeof(wireBytes))
          == nullptr);
    CHECK(TryWireEffectDefThroughActiveFxZoneAdapter(
              true, wireBytes, sizeof(wireBytes))
          == nullptr);
    CHECK(!TryAbortActiveFxZoneAdapterTransaction());

    std::printf(
        "headless FX zone-adapter wiring contract passed (%d checks)\n",
        g_checks);
    return 0;
}
