#include <EffectsCore/fx_fastfile_disk32.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

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

template <typename TYPE>
std::array<std::uint8_t, sizeof(TYPE)> ObjectBytes(const TYPE &object)
{
    static_assert(std::is_trivially_copyable_v<TYPE>);
    std::array<std::uint8_t, sizeof(TYPE)> bytes{};
    std::memcpy(bytes.data(), &object, bytes.size());
    return bytes;
}

template <std::size_t SIZE>
void StoreU8(std::array<std::uint8_t, SIZE> *const bytes,
             const std::size_t offset,
             const std::uint8_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset < SIZE);
    if (bytes && offset < SIZE)
        (*bytes)[offset] = value;
}

template <std::size_t SIZE>
void StoreU16(std::array<std::uint8_t, SIZE> *const bytes,
              const std::size_t offset,
              const std::uint16_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset <= SIZE && SIZE - offset >= 2u);
    if (!bytes || offset > SIZE || SIZE - offset < 2u)
        return;
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

template <std::size_t SIZE>
void StoreI16(std::array<std::uint8_t, SIZE> *const bytes,
              const std::size_t offset,
              const std::int16_t value)
{
    StoreU16(bytes, offset, static_cast<std::uint16_t>(value));
}

template <std::size_t SIZE>
void StoreU32(std::array<std::uint8_t, SIZE> *const bytes,
              const std::size_t offset,
              const std::uint32_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset <= SIZE && SIZE - offset >= 4u);
    if (!bytes || offset > SIZE || SIZE - offset < 4u)
        return;
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    (*bytes)[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    (*bytes)[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

template <std::size_t SIZE>
void StoreI32(std::array<std::uint8_t, SIZE> *const bytes,
              const std::size_t offset,
              const std::int32_t value)
{
    StoreU32(bytes, offset, static_cast<std::uint32_t>(value));
}

template <std::size_t SIZE>
void StoreFloat(std::array<std::uint8_t, SIZE> *const bytes,
                const std::size_t offset,
                const float value)
{
    StoreU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void SetRange(fastfile::FxFloatRangeDisk32 *const range, const float base)
{
    CHECK(range != nullptr);
    if (!range)
        return;
    range->base = base;
    range->amplitude = base + 0.5f;
}

template <std::size_t SIZE>
void StoreRange(std::array<std::uint8_t, SIZE> *const bytes,
                const std::size_t offset,
                const float base)
{
    StoreFloat(bytes, offset, base);
    StoreFloat(bytes, offset + 4u, base + 0.5f);
}

void SetIntRange(fastfile::FxIntRangeDisk32 *const range,
                 const std::int32_t base)
{
    CHECK(range != nullptr);
    if (!range)
        return;
    range->base = base;
    range->amplitude = base + 1;
}

template <std::size_t SIZE>
void StoreIntRange(std::array<std::uint8_t, SIZE> *const bytes,
                   const std::size_t offset,
                   const std::int32_t base)
{
    StoreI32(bytes, offset, base);
    StoreI32(bytes, offset + 4u, base + 1);
}

void SetVecRange(fastfile::FxElemVec3RangeDisk32 *const range, const float base)
{
    CHECK(range != nullptr);
    if (!range)
        return;
    for (std::size_t component = 0; component < 3u; ++component)
    {
        range->base[component] = base + static_cast<float>(component);
        range->amplitude[component] =
            base + 3.0f + static_cast<float>(component);
    }
}

template <std::size_t SIZE>
void StoreVecRange(std::array<std::uint8_t, SIZE> *const bytes,
                   const std::size_t offset,
                   const float base)
{
    for (std::size_t component = 0; component < 6u; ++component)
    {
        StoreFloat(bytes,
                   offset + component * sizeof(float),
                   base + static_cast<float>(component));
    }
}

void SetVisualState(fastfile::FxElemVisualStateDisk32 *const state,
                    const std::uint8_t colorBase,
                    const float scalarBase)
{
    CHECK(state != nullptr);
    if (!state)
        return;
    for (std::size_t component = 0; component < 4u; ++component)
        state->color[component] = colorBase + component;
    state->rotationDelta = scalarBase;
    state->rotationTotal = scalarBase + 1.0f;
    state->size[0] = scalarBase + 2.0f;
    state->size[1] = scalarBase + 3.0f;
    state->scale = scalarBase + 4.0f;
}

template <std::size_t SIZE>
void StoreVisualState(std::array<std::uint8_t, SIZE> *const bytes,
                      const std::size_t offset,
                      const std::uint8_t colorBase,
                      const float scalarBase)
{
    for (std::size_t component = 0; component < 4u; ++component)
        StoreU8(bytes, offset + component, colorBase + component);
    for (std::size_t scalar = 0; scalar < 5u; ++scalar)
    {
        StoreFloat(bytes,
                   offset + 4u + scalar * sizeof(float),
                   scalarBase + static_cast<float>(scalar));
    }
}

template <typename TYPE, std::size_t EXPECTED_SIZE, std::size_t EXPECTED_ALIGN>
constexpr void CheckRecordContract()
{
    static_assert(sizeof(TYPE) == EXPECTED_SIZE);
    static_assert(alignof(TYPE) == EXPECTED_ALIGN);
    static_assert(std::is_trivial_v<TYPE>);
    static_assert(std::is_standard_layout_v<TYPE>);
    static_assert(std::is_trivially_copyable_v<TYPE>);
}

void TestEnumAndOpaqueTokens()
{
    using Type = fastfile::FxElemTypeDisk32;
    CHECK(static_cast<std::uint8_t>(Type::SpriteBillboard) == 0u);
    CHECK(static_cast<std::uint8_t>(Type::SpriteOriented) == 1u);
    CHECK(static_cast<std::uint8_t>(Type::Tail) == 2u);
    CHECK(static_cast<std::uint8_t>(Type::Trail) == 3u);
    CHECK(static_cast<std::uint8_t>(Type::Cloud) == 4u);
    CHECK(static_cast<std::uint8_t>(Type::Model) == 5u);
    CHECK(static_cast<std::uint8_t>(Type::OmniLight) == 6u);
    CHECK(static_cast<std::uint8_t>(Type::SpotLight) == 7u);
    CHECK(static_cast<std::uint8_t>(Type::Sound) == 8u);
    CHECK(static_cast<std::uint8_t>(Type::Decal) == 9u);
    CHECK(static_cast<std::uint8_t>(Type::Runner) == 10u);
    CHECK(static_cast<std::uint8_t>(Type::Count) == 11u);

    fastfile::FxEffectDefRefDisk32 reference{};
    fastfile::FxEffectDefHandleDisk32 handle{};
    fastfile::FxElemVisualsDisk32 visual{};
    fastfile::FxElemDefVisualsDisk32 visuals{};
    reference.token.value = disk32::kInline;
    handle.token.value = disk32::kSharedInline;
    visual.token.value = UINT32_C(0x12345678);
    visuals.token.value = UINT32_C(0x87654321);
    constexpr std::array<std::uint8_t, 4> inlineBytes{0xFF, 0xFF, 0xFF, 0xFF};
    constexpr std::array<std::uint8_t, 4> sharedInlineBytes{
        0xFE, 0xFF, 0xFF, 0xFF};
    constexpr std::array<std::uint8_t, 4> visualBytes{0x78, 0x56, 0x34, 0x12};
    constexpr std::array<std::uint8_t, 4> visualsBytes{0x21, 0x43, 0x65, 0x87};
    CHECK(ObjectBytes(reference) == inlineBytes);
    CHECK(ObjectBytes(handle) == sharedInlineBytes);
    CHECK(ObjectBytes(visual) == visualBytes);
    CHECK(ObjectBytes(visuals) == visualsBytes);
}

void TestNestedGoldenBytes()
{
    fastfile::FxElemAtlasDisk32 atlas{};
    atlas.behavior = 0x10;
    atlas.index = 0x21;
    atlas.fps = 0x32;
    atlas.loopCount = 0x43;
    atlas.colIndexBits = 0x54;
    atlas.rowIndexBits = 0x65;
    atlas.entryCount = INT16_C(-1234);
    std::array<std::uint8_t, sizeof(atlas)> atlasExpected{
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x2E, 0xFB};
    CHECK(ObjectBytes(atlas) == atlasExpected);

    fastfile::FxElemVisStateSampleDisk32 visSample{};
    SetVisualState(&visSample.base, 0x10, 1.0f);
    SetVisualState(&visSample.amplitude, 0x80, 11.0f);
    std::array<std::uint8_t, sizeof(visSample)> visExpected{};
    StoreVisualState(&visExpected, 0x00, 0x10, 1.0f);
    StoreVisualState(&visExpected, 0x18, 0x80, 11.0f);
    CHECK(ObjectBytes(visSample) == visExpected);

    fastfile::FxElemVelStateSampleDisk32 velSample{};
    SetVecRange(&velSample.local.velocity, 1.0f);
    SetVecRange(&velSample.local.totalDelta, 11.0f);
    SetVecRange(&velSample.world.velocity, 21.0f);
    SetVecRange(&velSample.world.totalDelta, 31.0f);
    std::array<std::uint8_t, sizeof(velSample)> velExpected{};
    StoreVecRange(&velExpected, 0x00, 1.0f);
    StoreVecRange(&velExpected, 0x18, 11.0f);
    StoreVecRange(&velExpected, 0x30, 21.0f);
    StoreVecRange(&velExpected, 0x48, 31.0f);
    CHECK(ObjectBytes(velSample) == velExpected);

    fastfile::FxElemMarkVisualsDisk32 marks{};
    marks.materials[0].token.value = UINT32_C(0x01020304);
    marks.materials[1].token.value = UINT32_C(0xA0B0C0D0);
    std::array<std::uint8_t, sizeof(marks)> marksExpected{};
    StoreU32(&marksExpected, 0x00, UINT32_C(0x01020304));
    StoreU32(&marksExpected, 0x04, UINT32_C(0xA0B0C0D0));
    CHECK(ObjectBytes(marks) == marksExpected);

    fastfile::FxTrailVertexDisk32 vertex{};
    vertex.pos[0] = 1.0f;
    vertex.pos[1] = 2.0f;
    vertex.normal[0] = 3.0f;
    vertex.normal[1] = 4.0f;
    vertex.texCoord = 5.0f;
    std::array<std::uint8_t, sizeof(vertex)> vertexExpected{};
    for (std::size_t value = 0; value < 5u; ++value)
        StoreFloat(&vertexExpected, value * 4u, 1.0f + value);
    CHECK(ObjectBytes(vertex) == vertexExpected);
}

void TestEffectDefinitionGoldenBytes()
{
    fastfile::FxEffectDefDisk32 effect{};
    effect.name.token.value = UINT32_C(0x11223344);
    effect.flags = INT32_C(0x01020304);
    effect.totalSize = INT32_C(0x10203040);
    effect.msecLoopingLife = -2000;
    effect.elemDefCountLooping = 3;
    effect.elemDefCountOneShot = 5;
    effect.elemDefCountEmission = 7;
    effect.elemDefs.token.value = disk32::kInline;

    std::array<std::uint8_t, sizeof(effect)> expected{};
    StoreU32(&expected, 0x00, UINT32_C(0x11223344));
    StoreI32(&expected, 0x04, INT32_C(0x01020304));
    StoreI32(&expected, 0x08, INT32_C(0x10203040));
    StoreI32(&expected, 0x0C, -2000);
    StoreI32(&expected, 0x10, 3);
    StoreI32(&expected, 0x14, 5);
    StoreI32(&expected, 0x18, 7);
    StoreU32(&expected, 0x1C, disk32::kInline);
    CHECK(ObjectBytes(effect) == expected);
}

void TestElementDefinitionGoldenBytes()
{
    fastfile::FxElemDefDisk32 elem{};
    elem.flags = INT32_C(0x01020304);
    elem.spawn.intervalMsecOrCountBase = -101;
    elem.spawn.loopCountOrCountAmplitude = 202;
    SetRange(&elem.spawnRange, 1.0f);
    SetRange(&elem.fadeInRange, 2.0f);
    SetRange(&elem.fadeOutRange, 3.0f);
    elem.spawnFrustumCullRadius = 4.0f;
    SetIntRange(&elem.spawnDelayMsec, -50);
    SetIntRange(&elem.lifeSpanMsec, 60);
    for (std::size_t index = 0; index < 3u; ++index)
    {
        SetRange(&elem.spawnOrigin[index], 10.0f + index);
        SetRange(&elem.spawnAngles[index], 20.0f + index);
        SetRange(&elem.angularVelocity[index], 30.0f + index);
    }
    SetRange(&elem.spawnOffsetRadius, 13.0f);
    SetRange(&elem.spawnOffsetHeight, 14.0f);
    SetRange(&elem.initialRotation, 33.0f);
    SetRange(&elem.gravity, 34.0f);
    SetRange(&elem.reflectionFactor, 35.0f);
    elem.atlas = {1, 2, 3, 4, 5, 6, INT16_C(0x0708)};
    elem.elemType = fastfile::FxElemTypeDisk32::Trail;
    elem.visualCount = 9;
    elem.velIntervalCount = 10;
    elem.visStateIntervalCount = 11;
    elem.velSamples.token.value = UINT32_C(0x11111111);
    elem.visSamples.token.value = UINT32_C(0x22222222);
    elem.visuals.token.value = UINT32_C(0x33333333);
    for (std::size_t component = 0; component < 3u; ++component)
    {
        elem.collMins[component] = 40.0f + component;
        elem.collMaxs[component] = 50.0f + component;
    }
    elem.effectOnImpact.token.value = UINT32_C(0x44444444);
    elem.effectOnDeath.token.value = UINT32_C(0x55555555);
    elem.effectEmitted.token.value = UINT32_C(0x66666666);
    SetRange(&elem.emitDist, 60.0f);
    SetRange(&elem.emitDistVariance, 61.0f);
    elem.trailDef.token.value = UINT32_C(0x77777777);
    elem.sortOrder = 0x81;
    elem.lightingFrac = 0x82;
    elem.useItemClip = 0x83;
    elem.unused[0] = 0x84;

    std::array<std::uint8_t, sizeof(elem)> expected{};
    StoreI32(&expected, 0x00, INT32_C(0x01020304));
    StoreI32(&expected, 0x04, -101);
    StoreI32(&expected, 0x08, 202);
    StoreRange(&expected, 0x0C, 1.0f);
    StoreRange(&expected, 0x14, 2.0f);
    StoreRange(&expected, 0x1C, 3.0f);
    StoreFloat(&expected, 0x24, 4.0f);
    StoreIntRange(&expected, 0x28, -50);
    StoreIntRange(&expected, 0x30, 60);
    for (std::size_t index = 0; index < 3u; ++index)
    {
        StoreRange(&expected, 0x38 + index * 8u, 10.0f + index);
        StoreRange(&expected, 0x60 + index * 8u, 20.0f + index);
        StoreRange(&expected, 0x78 + index * 8u, 30.0f + index);
    }
    StoreRange(&expected, 0x50, 13.0f);
    StoreRange(&expected, 0x58, 14.0f);
    StoreRange(&expected, 0x90, 33.0f);
    StoreRange(&expected, 0x98, 34.0f);
    StoreRange(&expected, 0xA0, 35.0f);
    StoreU8(&expected, 0xA8, 1);
    StoreU8(&expected, 0xA9, 2);
    StoreU8(&expected, 0xAA, 3);
    StoreU8(&expected, 0xAB, 4);
    StoreU8(&expected, 0xAC, 5);
    StoreU8(&expected, 0xAD, 6);
    StoreI16(&expected, 0xAE, INT16_C(0x0708));
    StoreU8(&expected, 0xB0, 3);
    StoreU8(&expected, 0xB1, 9);
    StoreU8(&expected, 0xB2, 10);
    StoreU8(&expected, 0xB3, 11);
    StoreU32(&expected, 0xB4, UINT32_C(0x11111111));
    StoreU32(&expected, 0xB8, UINT32_C(0x22222222));
    StoreU32(&expected, 0xBC, UINT32_C(0x33333333));
    for (std::size_t component = 0; component < 3u; ++component)
    {
        StoreFloat(&expected, 0xC0 + component * 4u, 40.0f + component);
        StoreFloat(&expected, 0xCC + component * 4u, 50.0f + component);
    }
    StoreU32(&expected, 0xD8, UINT32_C(0x44444444));
    StoreU32(&expected, 0xDC, UINT32_C(0x55555555));
    StoreU32(&expected, 0xE0, UINT32_C(0x66666666));
    StoreRange(&expected, 0xE4, 60.0f);
    StoreRange(&expected, 0xEC, 61.0f);
    StoreU32(&expected, 0xF4, UINT32_C(0x77777777));
    StoreU8(&expected, 0xF8, 0x81);
    StoreU8(&expected, 0xF9, 0x82);
    StoreU8(&expected, 0xFA, 0x83);
    StoreU8(&expected, 0xFB, 0x84);
    CHECK(ObjectBytes(elem) == expected);
}

void TestTrailGoldenBytes()
{
    fastfile::FxTrailDefDisk32 trail{};
    trail.scrollTimeMsec = -1;
    trail.repeatDist = 2;
    trail.splitDist = 3;
    trail.vertCount = 4;
    trail.verts.token.value = UINT32_C(0x12345678);
    trail.indCount = 6;
    trail.inds.token.value = disk32::kInline;
    std::array<std::uint8_t, sizeof(trail)> expected{};
    StoreI32(&expected, 0x00, -1);
    StoreI32(&expected, 0x04, 2);
    StoreI32(&expected, 0x08, 3);
    StoreI32(&expected, 0x0C, 4);
    StoreU32(&expected, 0x10, UINT32_C(0x12345678));
    StoreI32(&expected, 0x14, 6);
    StoreU32(&expected, 0x18, disk32::kInline);
    CHECK(ObjectBytes(trail) == expected);
}

void TestImpactGoldenBytes()
{
    fastfile::FxImpactEntryDisk32 entry{};
    std::array<std::uint8_t, sizeof(entry)> entryExpected{};
    for (std::size_t index = 0; index < fastfile::kImpactNonFleshEffectCount;
         ++index)
    {
        const std::uint32_t token = UINT32_C(0x10000000) + index;
        entry.nonflesh[index].token.value = token;
        StoreU32(&entryExpected, index * 4u, token);
    }
    for (std::size_t index = 0; index < fastfile::kImpactFleshEffectCount;
         ++index)
    {
        const std::uint32_t token = UINT32_C(0x20000000) + index;
        entry.flesh[index].token.value = token;
        StoreU32(&entryExpected, 0x74 + index * 4u, token);
    }
    CHECK(ObjectBytes(entry) == entryExpected);

    fastfile::FxImpactTableDisk32 table{};
    table.name.token.value = UINT32_C(0x01020304);
    table.table.token.value = disk32::kInline;
    std::array<std::uint8_t, sizeof(table)> tableExpected{};
    StoreU32(&tableExpected, 0x00, UINT32_C(0x01020304));
    StoreU32(&tableExpected, 0x04, disk32::kInline);
    CHECK(ObjectBytes(table) == tableExpected);
}
} // namespace

int main()
{
    static_assert(std::endian::native == std::endian::little);
    CheckRecordContract<fastfile::FxFloatRangeDisk32, 0x08, 4>();
    CheckRecordContract<fastfile::FxIntRangeDisk32, 0x08, 4>();
    CheckRecordContract<fastfile::FxSpawnDefDisk32, 0x08, 4>();
    CheckRecordContract<fastfile::FxElemAtlasDisk32, 0x08, 2>();
    CheckRecordContract<fastfile::FxElemVec3RangeDisk32, 0x18, 4>();
    CheckRecordContract<fastfile::FxElemVisualStateDisk32, 0x18, 4>();
    CheckRecordContract<fastfile::FxElemVisStateSampleDisk32, 0x30, 4>();
    CheckRecordContract<fastfile::FxElemVelStateInFrameDisk32, 0x30, 4>();
    CheckRecordContract<fastfile::FxElemVelStateSampleDisk32, 0x60, 4>();
    CheckRecordContract<fastfile::FxEffectDefRefDisk32, 0x04, 4>();
    CheckRecordContract<fastfile::FxEffectDefHandleDisk32, 0x04, 4>();
    CheckRecordContract<fastfile::FxElemVisualsDisk32, 0x04, 4>();
    CheckRecordContract<fastfile::FxElemDefVisualsDisk32, 0x04, 4>();
    CheckRecordContract<fastfile::FxElemMarkVisualsDisk32, 0x08, 4>();
    CheckRecordContract<fastfile::FxTrailVertexDisk32, 0x14, 4>();
    CheckRecordContract<fastfile::FxTrailDefDisk32, 0x1C, 4>();
    CheckRecordContract<fastfile::FxElemDefDisk32, 0xFC, 4>();
    CheckRecordContract<fastfile::FxEffectDefDisk32, 0x20, 4>();
    CheckRecordContract<fastfile::FxImpactEntryDisk32, 0x84, 4>();
    CheckRecordContract<fastfile::FxImpactTableDisk32, 0x08, 4>();

    TestEnumAndOpaqueTokens();
    TestNestedGoldenBytes();
    TestEffectDefinitionGoldenBytes();
    TestElementDefinitionGoldenBytes();
    TestTrailGoldenBytes();
    TestImpactGoldenBytes();
    return failures == 0 ? 0 : 1;
}
