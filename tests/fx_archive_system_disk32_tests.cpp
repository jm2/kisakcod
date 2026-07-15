#include <EffectsCore/fx_archive_system_disk32.h>

#include <EffectsCore/fx_pool.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
namespace archive = fx::archive;

constexpr std::size_t DISK_SYSTEM_BYTES = 0xA60;
constexpr std::size_t CAMERA_BYTES = 0xB0;
constexpr std::size_t EFFECT_HANDLE_DISK_STRIDE = 0x80 / FxEffect::HANDLE_SCALE;
constexpr std::size_t EFFECT_HANDLE_NATIVE_STRIDE =
    sizeof(FxEffect) / FxEffect::HANDLE_SCALE;
constexpr std::size_t ELEM_HANDLE_DISK_STRIDE = 0x28 / FxElem::HANDLE_SCALE;
constexpr std::size_t ELEM_HANDLE_DISK_LIMIT =
    MAX_ELEMS * ELEM_HANDLE_DISK_STRIDE;
constexpr std::uint16_t INVALID_HANDLE = UINT16_C(0xFFFF);

constexpr std::size_t CAMERA_OFFSET = 0x000;
constexpr std::size_t CAMERA_PREV_OFFSET = 0x0B0;
constexpr std::size_t SPRITE_OFFSET = 0x160;
constexpr std::size_t SPRITE_INDICES_OFFSET = 0x160;
constexpr std::size_t SPRITE_INDEX_COUNT_OFFSET = 0x164;
constexpr std::size_t SPRITE_MATERIAL_OFFSET = 0x168;
constexpr std::size_t SPRITE_NAME_OFFSET = 0x16C;
constexpr std::size_t EFFECTS_ADDRESS_OFFSET = 0x170;
constexpr std::size_t ELEMS_ADDRESS_OFFSET = 0x174;
constexpr std::size_t TRAILS_ADDRESS_OFFSET = 0x178;
constexpr std::size_t TRAIL_ELEMS_ADDRESS_OFFSET = 0x17C;
constexpr std::size_t DEFERRED_ELEMS_ADDRESS_OFFSET = 0x180;
constexpr std::size_t FIRST_FREE_ELEM_OFFSET = 0x184;
constexpr std::size_t FIRST_FREE_TRAIL_ELEM_OFFSET = 0x188;
constexpr std::size_t FIRST_FREE_TRAIL_OFFSET = 0x18C;
constexpr std::size_t DEFERRED_ELEM_COUNT_OFFSET = 0x190;
constexpr std::size_t ACTIVE_ELEM_COUNT_OFFSET = 0x194;
constexpr std::size_t ACTIVE_TRAIL_ELEM_COUNT_OFFSET = 0x198;
constexpr std::size_t ACTIVE_TRAIL_COUNT_OFFSET = 0x19C;
constexpr std::size_t GFX_CLOUD_COUNT_OFFSET = 0x1A0;
constexpr std::size_t VIS_STATE_ADDRESS_OFFSET = 0x1A4;
constexpr std::size_t VIS_STATE_READ_ADDRESS_OFFSET = 0x1A8;
constexpr std::size_t VIS_STATE_WRITE_ADDRESS_OFFSET = 0x1AC;
constexpr std::size_t FIRST_ACTIVE_EFFECT_OFFSET = 0x1B0;
constexpr std::size_t FIRST_NEW_EFFECT_OFFSET = 0x1B4;
constexpr std::size_t FIRST_FREE_EFFECT_OFFSET = 0x1B8;
constexpr std::size_t ALL_EFFECT_HANDLES_OFFSET = 0x1BC;
constexpr std::size_t ACTIVE_SPOT_EFFECT_COUNT_OFFSET = 0x9BC;
constexpr std::size_t ACTIVE_SPOT_ELEM_COUNT_OFFSET = 0x9C0;
constexpr std::size_t ACTIVE_SPOT_EFFECT_HANDLE_OFFSET = 0x9C4;
constexpr std::size_t ACTIVE_SPOT_ELEM_HANDLE_OFFSET = 0x9C6;
constexpr std::size_t ACTIVE_SPOT_BOLT_DOBJ_OFFSET = 0x9C8;
constexpr std::size_t ITERATOR_COUNT_OFFSET = 0x9CC;
constexpr std::size_t MSEC_NOW_OFFSET = 0x9D0;
constexpr std::size_t MSEC_DRAW_OFFSET = 0x9D4;
constexpr std::size_t FRAME_COUNT_OFFSET = 0x9D8;
constexpr std::size_t IS_INITIALIZED_OFFSET = 0x9DC;
constexpr std::size_t NEEDS_GARBAGE_COLLECTION_OFFSET = 0x9DD;
constexpr std::size_t IS_ARCHIVING_OFFSET = 0x9DE;
constexpr std::size_t LOCAL_CLIENT_NUM_OFFSET = 0x9DF;
constexpr std::size_t RESTART_LIST_OFFSET = 0x9E0;

constexpr std::uint32_t BUFFER_BASE = UINT32_C(0x24000000);
constexpr std::uint32_t ELEMS_DELTA = UINT32_C(0x00020000);
constexpr std::uint32_t TRAILS_DELTA = UINT32_C(0x00034000);
constexpr std::uint32_t TRAIL_ELEMS_DELTA = UINT32_C(0x00034400);
constexpr std::uint32_t VIS_STATE_DELTA = UINT32_C(0x00044400);
constexpr std::uint32_t DEFERRED_ELEMS_DELTA = UINT32_C(0x00046420);
constexpr std::uint32_t VIS_STATE_STRIDE = UINT32_C(0x1010);
constexpr std::uint32_t BUFFER_DISK32_SIZE = UINT32_C(0x00047480);

constexpr std::int32_t SOURCE_FIRST_ACTIVE = 1027;
constexpr std::int32_t SOURCE_FIRST_FREE = 1032;
constexpr std::int32_t NORMALIZED_FIRST_ACTIVE = 3;
constexpr std::int32_t NORMALIZED_FIRST_FREE = 8;

int failures = 0;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

template <std::size_t SIZE>
void StoreU8(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const std::uint8_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset < SIZE);
    if (bytes && offset < SIZE)
        (*bytes)[offset] = value;
}

template <std::size_t SIZE>
void StoreU16(
    std::array<std::uint8_t, SIZE> *const bytes,
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
void StoreU32(
    std::array<std::uint8_t, SIZE> *const bytes,
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
void StoreI32(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const std::int32_t value)
{
    StoreU32(bytes, offset, static_cast<std::uint32_t>(value));
}

template <std::size_t SIZE>
void StoreFloat(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const float value)
{
    StoreU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

constexpr std::size_t PermutedEffectIndex(const std::size_t handleIndex)
{
    return (handleIndex * 37u + 11u) & (MAX_EFFECTS - 1u);
}

constexpr std::uint16_t DiskEffectHandle(const std::size_t effectIndex)
{
    return static_cast<std::uint16_t>(
        effectIndex * EFFECT_HANDLE_DISK_STRIDE);
}

constexpr std::uint16_t NativeEffectHandle(const std::size_t effectIndex)
{
    return static_cast<std::uint16_t>(
        effectIndex * EFFECT_HANDLE_NATIVE_STRIDE);
}

template <std::size_t SIZE>
void StoreCamera(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t base,
    const float originBias)
{
    StoreFloat(bytes, base + 0x00, originBias + 1.0f);
    StoreFloat(bytes, base + 0x04, originBias + 2.0f);
    StoreFloat(bytes, base + 0x08, originBias + 3.0f);
    StoreI32(bytes, base + 0x0C, 1);
    for (std::size_t component = 0; component < 24; ++component)
        StoreFloat(bytes, base + 0x10 + component * 4u, 0.0f);
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            StoreFloat(
                bytes,
                base + 0x70 + (row * 3u + column) * 4u,
                row == column ? 1.0f : 0.0f);
        }
    }
    StoreU32(bytes, base + 0x94, 0);
    StoreFloat(bytes, base + 0x98, 0.25f);
    StoreFloat(bytes, base + 0x9C, -0.5f);
    StoreFloat(bytes, base + 0xA0, 0.75f);
    StoreU32(bytes, base + 0xA4, 0);
    StoreU32(bytes, base + 0xA8, 0);
    StoreU32(bytes, base + 0xAC, 0);
}

template <std::size_t SIZE>
void StoreCanonicalInvalidCamera(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t base)
{
    for (std::size_t offset = 0; offset < CAMERA_BYTES; offset += 4u)
        StoreU32(bytes, base + offset, 0);
}

template <std::size_t SIZE>
void StoreBufferTopology(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::uint32_t base,
    const std::uint8_t readSelector,
    const std::uint8_t writeSelector)
{
    StoreU32(bytes, EFFECTS_ADDRESS_OFFSET, base);
    StoreU32(bytes, ELEMS_ADDRESS_OFFSET, base + ELEMS_DELTA);
    StoreU32(bytes, TRAILS_ADDRESS_OFFSET, base + TRAILS_DELTA);
    StoreU32(
        bytes,
        TRAIL_ELEMS_ADDRESS_OFFSET,
        base + TRAIL_ELEMS_DELTA);
    StoreU32(
        bytes,
        DEFERRED_ELEMS_ADDRESS_OFFSET,
        base + DEFERRED_ELEMS_DELTA);

    const std::uint32_t visBase = base + VIS_STATE_DELTA;
    StoreU32(bytes, VIS_STATE_ADDRESS_OFFSET, visBase);
    StoreU32(
        bytes,
        VIS_STATE_READ_ADDRESS_OFFSET,
        visBase + static_cast<std::uint32_t>(readSelector) * VIS_STATE_STRIDE);
    StoreU32(
        bytes,
        VIS_STATE_WRITE_ADDRESS_OFFSET,
        visBase + static_cast<std::uint32_t>(writeSelector) * VIS_STATE_STRIDE);
}

std::array<std::uint8_t, DISK_SYSTEM_BYTES> MakeGoldenBytes(
    const std::uint8_t readSelector = 0,
    const std::uint8_t writeSelector = 1)
{
    std::array<std::uint8_t, DISK_SYSTEM_BYTES> bytes{};
    StoreCamera(&bytes, CAMERA_OFFSET, 0.0f);
    StoreCamera(&bytes, CAMERA_PREV_OFFSET, 10.0f);

    StoreU32(&bytes, SPRITE_INDICES_OFFSET, UINT32_C(0x11112222));
    StoreU32(&bytes, SPRITE_INDEX_COUNT_OFFSET, 0);
    StoreU32(&bytes, SPRITE_MATERIAL_OFFSET, UINT32_C(0x33334444));
    StoreU32(&bytes, SPRITE_NAME_OFFSET, UINT32_C(0x55556666));

    StoreBufferTopology(
        &bytes, BUFFER_BASE, readSelector, writeSelector);

    StoreI32(&bytes, FIRST_FREE_ELEM_OFFSET, 17);
    StoreI32(&bytes, FIRST_FREE_TRAIL_ELEM_OFFSET, 19);
    StoreI32(&bytes, FIRST_FREE_TRAIL_OFFSET, 7);
    StoreI32(&bytes, DEFERRED_ELEM_COUNT_OFFSET, 0);
    StoreI32(&bytes, ACTIVE_ELEM_COUNT_OFFSET, 12);
    StoreI32(&bytes, ACTIVE_TRAIL_ELEM_COUNT_OFFSET, 13);
    StoreI32(&bytes, ACTIVE_TRAIL_COUNT_OFFSET, 4);
    StoreI32(&bytes, GFX_CLOUD_COUNT_OFFSET, 5);

    StoreI32(&bytes, FIRST_ACTIVE_EFFECT_OFFSET, SOURCE_FIRST_ACTIVE);
    StoreI32(&bytes, FIRST_NEW_EFFECT_OFFSET, SOURCE_FIRST_FREE);
    StoreI32(&bytes, FIRST_FREE_EFFECT_OFFSET, SOURCE_FIRST_FREE);
    for (std::size_t handleIndex = 0; handleIndex < MAX_EFFECTS; ++handleIndex)
    {
        StoreU16(
            &bytes,
            ALL_EFFECT_HANDLES_OFFSET + handleIndex * 2u,
            DiskEffectHandle(PermutedEffectIndex(handleIndex)));
    }

    StoreI32(&bytes, ACTIVE_SPOT_EFFECT_COUNT_OFFSET, 1);
    StoreI32(&bytes, ACTIVE_SPOT_ELEM_COUNT_OFFSET, 1);
    StoreU16(
        &bytes,
        ACTIVE_SPOT_EFFECT_HANDLE_OFFSET,
        DiskEffectHandle(
            PermutedEffectIndex(
                static_cast<std::size_t>(NORMALIZED_FIRST_ACTIVE))));
    StoreU16(&bytes, ACTIVE_SPOT_ELEM_HANDLE_OFFSET, UINT16_C(0x0046));
    StoreU16(
        &bytes,
        ACTIVE_SPOT_BOLT_DOBJ_OFFSET,
        static_cast<std::uint16_t>(INT16_C(-7)));

    StoreI32(&bytes, ITERATOR_COUNT_OFFSET, 0);
    StoreI32(&bytes, MSEC_NOW_OFFSET, 1000);
    StoreI32(&bytes, MSEC_DRAW_OFFSET, 900);
    StoreI32(&bytes, FRAME_COUNT_OFFSET, 7);
    StoreU8(&bytes, IS_INITIALIZED_OFFSET, 1);
    StoreU8(&bytes, NEEDS_GARBAGE_COLLECTION_OFFSET, 1);
    StoreU8(&bytes, IS_ARCHIVING_OFFSET, 1);
    StoreU8(&bytes, LOCAL_CLIENT_NUM_OFFSET, 0);
    for (std::size_t index = 0; index < 32; ++index)
    {
        StoreU32(
            &bytes,
            RESTART_LIST_OFFSET + index * 4u,
            UINT32_C(0xA5000000) + static_cast<std::uint32_t>(index));
    }
    return bytes;
}

archive::FxSystemDisk32 SystemFromBytes(
    const std::array<std::uint8_t, DISK_SYSTEM_BYTES> &bytes)
{
    archive::FxSystemDisk32 system{};
    static_assert(sizeof(system) == DISK_SYSTEM_BYTES);
    std::memcpy(&system, bytes.data(), bytes.size());
    return system;
}

template <typename TYPE>
std::array<std::uint8_t, sizeof(TYPE)> ObjectBytes(const TYPE &object)
{
    std::array<std::uint8_t, sizeof(TYPE)> bytes{};
    std::memcpy(bytes.data(), &object, bytes.size());
    return bytes;
}

template <typename METADATA>
auto &ActiveEffectSlots(METADATA &metadata)
{
    if constexpr (requires { metadata.activeEffectSlots; })
        return metadata.activeEffectSlots;
    else if constexpr (requires { metadata.activeEffects; })
        return metadata.activeEffects;
    else
        return metadata.activeSlots;
}

template <typename METADATA>
const auto &ActiveEffectSlots(const METADATA &metadata)
{
    if constexpr (requires { metadata.activeEffectSlots; })
        return metadata.activeEffectSlots;
    else if constexpr (requires { metadata.activeEffects; })
        return metadata.activeEffects;
    else
        return metadata.activeSlots;
}

template <typename METADATA>
std::uint8_t &ReadSelector(METADATA &metadata)
{
    if constexpr (requires { metadata.visibilityReadSelector; })
        return metadata.visibilityReadSelector;
    else if constexpr (requires { metadata.readVisibilitySelector; })
        return metadata.readVisibilitySelector;
    else
        return metadata.readSelector;
}

template <typename METADATA>
std::uint8_t ReadSelector(const METADATA &metadata)
{
    if constexpr (requires { metadata.visibilityReadSelector; })
        return metadata.visibilityReadSelector;
    else if constexpr (requires { metadata.readVisibilitySelector; })
        return metadata.readVisibilitySelector;
    else
        return metadata.readSelector;
}

template <typename METADATA>
std::uint8_t &WriteSelector(METADATA &metadata)
{
    if constexpr (requires { metadata.visibilityWriteSelector; })
        return metadata.visibilityWriteSelector;
    else if constexpr (requires { metadata.writeVisibilitySelector; })
        return metadata.writeVisibilitySelector;
    else
        return metadata.writeSelector;
}

template <typename METADATA>
std::uint8_t WriteSelector(const METADATA &metadata)
{
    if constexpr (requires { metadata.visibilityWriteSelector; })
        return metadata.visibilityWriteSelector;
    else if constexpr (requires { metadata.writeVisibilitySelector; })
        return metadata.writeVisibilitySelector;
    else
        return metadata.writeSelector;
}

void InitializeSentinels(
    FxSystem *const system,
    archive::FxSystemDisk32Metadata *const metadata)
{
    CHECK(system != nullptr);
    CHECK(metadata != nullptr);
    if (!system || !metadata)
        return;
    std::memset(system, 0, sizeof(*system));
    system->msecNow = 0x12345678;
    system->msecDraw = 0x23456789;
    system->frameCount = 0x3456789A;
    system->restartList[17] = UINT32_C(0xDEADBEEF);

    *metadata = archive::FxSystemDisk32Metadata{};
    ReadSelector(*metadata) = UINT8_C(0xA5);
    WriteSelector(*metadata) = UINT8_C(0x5A);
    ActiveEffectSlots(*metadata)[17] = true;
}

void CheckFailurePreservesOutputs(const archive::FxSystemDisk32 &source)
{
    FxSystem output{};
    archive::FxSystemDisk32Metadata metadata{};
    InitializeSentinels(&output, &metadata);
    const auto outputBefore = ObjectBytes(output);
    const auto metadataBefore = ObjectBytes(metadata);
    CHECK(!archive::TryUnpackFxSystemDisk32(source, &output, &metadata));
    CHECK(ObjectBytes(output) == outputBefore);
    CHECK(ObjectBytes(metadata) == metadataBefore);
}

void CheckCameraBytes(
    const FxCamera &camera,
    const std::array<std::uint8_t, DISK_SYSTEM_BYTES> &golden,
    const std::size_t diskOffset)
{
    static_assert(sizeof(FxCamera) == CAMERA_BYTES);
    const auto observed = ObjectBytes(camera);
    CHECK(std::memcmp(
        observed.data(), golden.data() + diskOffset, CAMERA_BYTES) == 0);
}

void TestGoldenRecordAndConversion()
{
    const auto golden = MakeGoldenBytes();
    const archive::FxSystemDisk32 source = SystemFromBytes(golden);
    CHECK(ObjectBytes(source) == golden);

    FxSystem output{};
    archive::FxSystemDisk32Metadata metadata{};
    InitializeSentinels(&output, &metadata);
    CHECK(archive::TryUnpackFxSystemDisk32(source, &output, &metadata));

    CheckCameraBytes(output.camera, golden, CAMERA_OFFSET);
    CheckCameraBytes(output.cameraPrev, golden, CAMERA_PREV_OFFSET);
    CHECK(output.sprite.indices == nullptr);
    CHECK(output.sprite.indexCount == 0);
    CHECK(output.sprite.material == nullptr);
    CHECK(output.sprite.name == nullptr);
    CHECK(output.effects == nullptr);
    CHECK(output.elems == nullptr);
    CHECK(output.trails == nullptr);
    CHECK(output.trailElems == nullptr);
    CHECK(output.deferredElems == nullptr);
    CHECK(output.visState == nullptr);
    CHECK(output.visStateBufferRead == nullptr);
    CHECK(output.visStateBufferWrite == nullptr);

    CHECK(output.firstFreeElem == 17);
    CHECK(output.firstFreeTrailElem == 19);
    CHECK(output.firstFreeTrail == 7);
    CHECK(output.deferredElemCount == 0);
    CHECK(output.activeElemCount == 12);
    CHECK(output.activeTrailElemCount == 13);
    CHECK(output.activeTrailCount == 4);
    CHECK(output.gfxCloudCount == 5);
    CHECK(output.firstActiveEffect == NORMALIZED_FIRST_ACTIVE);
    CHECK(output.firstNewEffect == NORMALIZED_FIRST_FREE);
    CHECK(output.firstFreeEffect == NORMALIZED_FIRST_FREE);

    std::array<bool, MAX_EFFECTS> expectedActive{};
    for (std::size_t handleIndex = 0; handleIndex < MAX_EFFECTS; ++handleIndex)
    {
        const std::size_t effectIndex = PermutedEffectIndex(handleIndex);
        CHECK(output.allEffectHandles[handleIndex]
            == NativeEffectHandle(effectIndex));
        if (handleIndex >= static_cast<std::size_t>(NORMALIZED_FIRST_ACTIVE)
            && handleIndex < static_cast<std::size_t>(NORMALIZED_FIRST_FREE))
        {
            expectedActive[effectIndex] = true;
        }
    }
    for (std::size_t effectIndex = 0; effectIndex < MAX_EFFECTS; ++effectIndex)
    {
        CHECK(static_cast<bool>(ActiveEffectSlots(metadata)[effectIndex])
            == expectedActive[effectIndex]);
    }

    CHECK(ReadSelector(metadata) == 0);
    CHECK(WriteSelector(metadata) == 1);
    CHECK(output.activeSpotLightEffectCount == 1);
    CHECK(output.activeSpotLightElemCount == 1);
    CHECK(output.activeSpotLightEffectHandle
        == NativeEffectHandle(
            PermutedEffectIndex(
                static_cast<std::size_t>(NORMALIZED_FIRST_ACTIVE))));
    CHECK(output.activeSpotLightElemHandle == UINT16_C(0x0046));
    CHECK(output.activeSpotLightBoltDobj == -7);
    CHECK(output.iteratorCount == 0);
    CHECK(output.msecNow == 1000);
    CHECK(output.msecDraw == 900);
    CHECK(output.frameCount == 7);
    CHECK(output.isInitialized);
    CHECK(output.needsGarbageCollection);
    CHECK(output.isArchiving);
    CHECK(output.localClientNum == 0);
    for (std::size_t index = 0; index < 32; ++index)
    {
        CHECK(output.restartList[index]
            == UINT32_C(0xA5000000) + static_cast<std::uint32_t>(index));
    }
}

void TestVisibilitySelectorOrientations()
{
    for (const std::array<std::uint8_t, 2> selectors : {
             std::array<std::uint8_t, 2>{0, 1},
             std::array<std::uint8_t, 2>{1, 0}})
    {
        const auto bytes = MakeGoldenBytes(selectors[0], selectors[1]);
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CHECK(ReadSelector(metadata) == selectors[0]);
        CHECK(WriteSelector(metadata) == selectors[1]);
    }
}

struct CameraFloatMutation
{
    std::size_t offset;
    std::uint32_t bits;
};

void TestCameraValidationAndReadiness()
{
    constexpr std::uint32_t quietNaN = UINT32_C(0x7FC00000);
    constexpr std::uint32_t positiveInfinity = UINT32_C(0x7F800000);
    constexpr std::array<CameraFloatMutation, 11> invalidFloats{{
        {CAMERA_OFFSET + 0x00, quietNaN},
        {CAMERA_PREV_OFFSET + 0x04, positiveInfinity},
        {CAMERA_OFFSET + 0x08,
         std::bit_cast<std::uint32_t>(1048577.0f)},
        {CAMERA_OFFSET + 0x10, quietNaN},
        {CAMERA_PREV_OFFSET + 0x1C, positiveInfinity},
        {CAMERA_OFFSET + 0x10, std::bit_cast<std::uint32_t>(2.01f)},
        {CAMERA_OFFSET + 0x1C,
         std::bit_cast<std::uint32_t>(16777218.0f)},
        {CAMERA_OFFSET + 0x70, positiveInfinity},
        {CAMERA_PREV_OFFSET + 0x8C, quietNaN},
        {CAMERA_OFFSET + 0x98, quietNaN},
        {CAMERA_PREV_OFFSET + 0xA0,
         std::bit_cast<std::uint32_t>(1048577.0f)},
    }};
    for (const CameraFloatMutation &mutation : invalidFloats)
    {
        auto bytes = MakeGoldenBytes();
        StoreU32(&bytes, mutation.offset, mutation.bits);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    // Once a frustum plane is active its normal must be unit length.
    {
        auto bytes = MakeGoldenBytes();
        StoreU32(&bytes, CAMERA_OFFSET + 0x94, 1);
        StoreFloat(&bytes, CAMERA_OFFSET + 0x10, 0.5f);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    // An active frustum also requires a right-handed orthonormal camera basis.
    for (const std::array<float, 3> replacementRow : {
             std::array<float, 3>{1.0f, 0.0f, 0.0f},
             std::array<float, 3>{0.0f, 0.0f, -1.0f}})
    {
        auto bytes = MakeGoldenBytes();
        StoreU32(&bytes, CAMERA_OFFSET + 0x94, 1);
        StoreFloat(&bytes, CAMERA_OFFSET + 0x10, 1.0f);
        StoreFloat(&bytes, CAMERA_OFFSET + 0x14, 0.0f);
        StoreFloat(&bytes, CAMERA_OFFSET + 0x18, 0.0f);
        const std::size_t rowOffset = replacementRow[0] == 1.0f
            ? CAMERA_OFFSET + 0x7C
            : CAMERA_OFFSET + 0x88;
        for (std::size_t component = 0; component < 3; ++component)
        {
            StoreFloat(
                &bytes,
                rowOffset + component * 4u,
                replacementRow[component]);
        }
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    // A well-formed active frustum is accepted and copied byte-for-byte.
    {
        auto bytes = MakeGoldenBytes();
        StoreU32(&bytes, CAMERA_OFFSET + 0x94, 2);
        StoreFloat(&bytes, CAMERA_OFFSET + 0x10, 1.0f);
        StoreFloat(&bytes, CAMERA_OFFSET + 0x1C, 4.0f);
        StoreFloat(&bytes, CAMERA_OFFSET + 0x24, 1.0f);
        StoreFloat(&bytes, CAMERA_OFFSET + 0x2C, 5.0f);
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CheckCameraBytes(output.camera, bytes, CAMERA_OFFSET);
    }

    // At the pre-draw sentinel, absent cameras are accepted only in their
    // canonical all-zero representation.
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, MSEC_DRAW_OFFSET, -1);
        StoreCanonicalInvalidCamera(&bytes, CAMERA_OFFSET);
        StoreCanonicalInvalidCamera(&bytes, CAMERA_PREV_OFFSET);
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CheckCameraBytes(output.camera, bytes, CAMERA_OFFSET);
        CheckCameraBytes(output.cameraPrev, bytes, CAMERA_PREV_OFFSET);
        CHECK(output.camera.isValid == 0);
        CHECK(output.cameraPrev.isValid == 0);
    }
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, MSEC_DRAW_OFFSET, -1);
        StoreCanonicalInvalidCamera(&bytes, CAMERA_OFFSET);
        StoreU32(&bytes, CAMERA_OFFSET + 0xAC, 1);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }
    {
        auto bytes = MakeGoldenBytes();
        StoreCanonicalInvalidCamera(&bytes, CAMERA_OFFSET);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

}

struct I32Mutation
{
    std::size_t offset;
    std::int32_t value;
};

struct U8Mutation
{
    std::size_t offset;
    std::uint8_t value;
};

void TestInvalidScalarAndBooleanFields()
{
    constexpr std::array<I32Mutation, 24> invalidI32{{
        {CAMERA_OFFSET + 0x0C, 2},
        {CAMERA_PREV_OFFSET + 0x0C, -1},
        {CAMERA_OFFSET + 0x94, 7},
        {CAMERA_PREV_OFFSET + 0x94, 7},
        {FIRST_FREE_ELEM_OFFSET, -2},
        {FIRST_FREE_ELEM_OFFSET, static_cast<std::int32_t>(MAX_ELEMS)},
        {FIRST_FREE_TRAIL_ELEM_OFFSET, -2},
        {FIRST_FREE_TRAIL_ELEM_OFFSET, static_cast<std::int32_t>(MAX_TRAIL_ELEMS)},
        {FIRST_FREE_TRAIL_OFFSET, -2},
        {FIRST_FREE_TRAIL_OFFSET, static_cast<std::int32_t>(MAX_TRAILS)},
        {DEFERRED_ELEM_COUNT_OFFSET, 1},
        {ACTIVE_ELEM_COUNT_OFFSET, -1},
        {ACTIVE_ELEM_COUNT_OFFSET, static_cast<std::int32_t>(MAX_ELEMS + 1)},
        {ACTIVE_TRAIL_ELEM_COUNT_OFFSET, -1},
        {ACTIVE_TRAIL_ELEM_COUNT_OFFSET, static_cast<std::int32_t>(MAX_TRAIL_ELEMS + 1)},
        {ACTIVE_TRAIL_COUNT_OFFSET, -1},
        {ACTIVE_TRAIL_COUNT_OFFSET, static_cast<std::int32_t>(MAX_TRAILS + 1)},
        {GFX_CLOUD_COUNT_OFFSET, -1},
        {GFX_CLOUD_COUNT_OFFSET, 257},
        {ACTIVE_SPOT_EFFECT_COUNT_OFFSET, -1},
        {ACTIVE_SPOT_EFFECT_COUNT_OFFSET, 2},
        {ACTIVE_SPOT_ELEM_COUNT_OFFSET, -1},
        {ACTIVE_SPOT_ELEM_COUNT_OFFSET, 2},
        {ITERATOR_COUNT_OFFSET, 1},
    }};
    for (const I32Mutation &mutation : invalidI32)
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, mutation.offset, mutation.value);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    for (const std::size_t offset : {
             IS_INITIALIZED_OFFSET,
             NEEDS_GARBAGE_COLLECTION_OFFSET,
             IS_ARCHIVING_OFFSET})
    {
        auto bytes = MakeGoldenBytes();
        StoreU8(&bytes, offset, 2);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    constexpr std::array<U8Mutation, 3> invalidU8{{
        {IS_INITIALIZED_OFFSET, 0},
        {IS_ARCHIVING_OFFSET, 0},
        {LOCAL_CLIENT_NUM_OFFSET, 1},
    }};
    for (const U8Mutation &mutation : invalidU8)
    {
        auto bytes = MakeGoldenBytes();
        StoreU8(&bytes, mutation.offset, mutation.value);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    for (const I32Mutation mutation : {
             I32Mutation{MSEC_NOW_OFFSET, -1},
             I32Mutation{MSEC_DRAW_OFFSET, -2},
             I32Mutation{
                 MSEC_NOW_OFFSET,
                 (std::numeric_limits<std::int32_t>::max)()},
             I32Mutation{
                 MSEC_DRAW_OFFSET,
                 (std::numeric_limits<std::int32_t>::max)()}})
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, mutation.offset, mutation.value);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    {
        auto bytes = MakeGoldenBytes();
        StoreU32(&bytes, SPRITE_INDEX_COUNT_OFFSET, 1);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, ACTIVE_SPOT_EFFECT_COUNT_OFFSET, 0);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    // Archive timestamps need arithmetic headroom, but draw may legitimately
    // be one tick ahead of now; no temporal ordering is implied here.
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, MSEC_DRAW_OFFSET, 1001);
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CHECK(output.msecNow == 1000);
        CHECK(output.msecDraw == 1001);
    }
}

void TestFrameCountNormalization()
{
    for (const std::int32_t value : {
             0,
             -1,
             (std::numeric_limits<std::int32_t>::max)() - 1,
             (std::numeric_limits<std::int32_t>::max)()})
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, FRAME_COUNT_OFFSET, value);
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CHECK(output.frameCount == 1);
    }
}

struct PoolMutation
{
    std::size_t freeHeadOffset;
    std::size_t activeCountOffset;
    std::int32_t capacity;
};

void TestPoolCountAndFreeHeadConsistency()
{
    constexpr std::array<PoolMutation, 3> pools{{
        {FIRST_FREE_ELEM_OFFSET,
         ACTIVE_ELEM_COUNT_OFFSET,
         static_cast<std::int32_t>(MAX_ELEMS)},
        {FIRST_FREE_TRAIL_ELEM_OFFSET,
         ACTIVE_TRAIL_ELEM_COUNT_OFFSET,
         static_cast<std::int32_t>(MAX_TRAIL_ELEMS)},
        {FIRST_FREE_TRAIL_OFFSET,
         ACTIVE_TRAIL_COUNT_OFFSET,
         static_cast<std::int32_t>(MAX_TRAILS)},
    }};
    for (const PoolMutation &pool : pools)
    {
        // A full pool cannot retain a free-list head.
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, pool.activeCountOffset, pool.capacity);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));

        // A non-full pool must have a free-list head.
        bytes = MakeGoldenBytes();
        StoreI32(&bytes, pool.freeHeadOffset, -1);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    // All three exact-full boundary encodings are valid and copied.
    auto bytes = MakeGoldenBytes();
    for (const PoolMutation &pool : pools)
    {
        StoreI32(&bytes, pool.freeHeadOffset, -1);
        StoreI32(&bytes, pool.activeCountOffset, pool.capacity);
    }
    const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
    FxSystem output{};
    archive::FxSystemDisk32Metadata metadata{};
    CHECK(archive::TryUnpackFxSystemDisk32(
        source, &output, &metadata));
    CHECK(output.firstFreeElem == -1);
    CHECK(output.firstFreeTrailElem == -1);
    CHECK(output.firstFreeTrail == -1);
    CHECK(output.activeElemCount == static_cast<std::int32_t>(MAX_ELEMS));
    CHECK(output.activeTrailElemCount
        == static_cast<std::int32_t>(MAX_TRAIL_ELEMS));
    CHECK(output.activeTrailCount
        == static_cast<std::int32_t>(MAX_TRAILS));
}

void TestAddressTopologyFailures()
{
    constexpr std::uint32_t lastFittingBase =
        (std::numeric_limits<std::uint32_t>::max)()
        - (BUFFER_DISK32_SIZE - 1u);
    static_assert((lastFittingBase & 0x3u) == 0);
    {
        auto bytes = MakeGoldenBytes();
        StoreBufferTopology(&bytes, lastFittingBase, 0, 1);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            SystemFromBytes(bytes), &output, &metadata));
        CHECK(ReadSelector(metadata) == 0);
        CHECK(WriteSelector(metadata) == 1);
    }
    {
        constexpr std::uint32_t firstAlignedOverflowingBase =
            lastFittingBase + 4u;
        auto bytes = MakeGoldenBytes();
        StoreBufferTopology(&bytes, firstAlignedOverflowingBase, 0, 1);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    {
        auto bytes = MakeGoldenBytes();
        StoreU32(&bytes, EFFECTS_ADDRESS_OFFSET, BUFFER_BASE + 1u);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    for (const std::size_t offset : {
             EFFECTS_ADDRESS_OFFSET,
             ELEMS_ADDRESS_OFFSET,
             TRAILS_ADDRESS_OFFSET,
             TRAIL_ELEMS_ADDRESS_OFFSET,
             DEFERRED_ELEMS_ADDRESS_OFFSET,
             VIS_STATE_ADDRESS_OFFSET,
             VIS_STATE_READ_ADDRESS_OFFSET,
             VIS_STATE_WRITE_ADDRESS_OFFSET})
    {
        auto bytes = MakeGoldenBytes();
        StoreU32(&bytes, offset, 0);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    for (const std::size_t offset : {
             ELEMS_ADDRESS_OFFSET,
             TRAILS_ADDRESS_OFFSET,
             TRAIL_ELEMS_ADDRESS_OFFSET,
             DEFERRED_ELEMS_ADDRESS_OFFSET,
             VIS_STATE_ADDRESS_OFFSET})
    {
        auto bytes = MakeGoldenBytes();
        StoreU32(&bytes, offset, BUFFER_BASE + 1u);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    {
        auto bytes = MakeGoldenBytes();
        const std::uint32_t visBase = BUFFER_BASE + VIS_STATE_DELTA;
        StoreU32(&bytes, VIS_STATE_READ_ADDRESS_OFFSET, visBase + 1u);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }
    {
        auto bytes = MakeGoldenBytes();
        const std::uint32_t visBase = BUFFER_BASE + VIS_STATE_DELTA;
        StoreU32(
            &bytes,
            VIS_STATE_WRITE_ADDRESS_OFFSET,
            visBase + 2u * VIS_STATE_STRIDE);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }
    {
        auto bytes = MakeGoldenBytes();
        const std::uint32_t readAddress = BUFFER_BASE + VIS_STATE_DELTA;
        StoreU32(&bytes, VIS_STATE_WRITE_ADDRESS_OFFSET, readAddress);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }
    {
        auto bytes = MakeGoldenBytes();
        constexpr std::uint32_t overflowingBase = UINT32_C(0xFFFF0000);
        StoreU32(&bytes, EFFECTS_ADDRESS_OFFSET, overflowingBase);
        StoreU32(&bytes, ELEMS_ADDRESS_OFFSET, overflowingBase + ELEMS_DELTA);
        StoreU32(&bytes, TRAILS_ADDRESS_OFFSET, overflowingBase + TRAILS_DELTA);
        StoreU32(
            &bytes,
            TRAIL_ELEMS_ADDRESS_OFFSET,
            overflowingBase + TRAIL_ELEMS_DELTA);
        StoreU32(
            &bytes,
            DEFERRED_ELEMS_ADDRESS_OFFSET,
            overflowingBase + DEFERRED_ELEMS_DELTA);
        const std::uint32_t wrappedVis = overflowingBase + VIS_STATE_DELTA;
        StoreU32(&bytes, VIS_STATE_ADDRESS_OFFSET, wrappedVis);
        StoreU32(&bytes, VIS_STATE_READ_ADDRESS_OFFSET, wrappedVis);
        StoreU32(
            &bytes,
            VIS_STATE_WRITE_ADDRESS_OFFSET,
            wrappedVis + VIS_STATE_STRIDE);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }
}

void TestEffectRingFailures()
{
    constexpr std::array<std::array<std::int32_t, 3>, 7> invalidRings{{
        {{-1, 0, 0}},
        {{5, 4, 4}},
        {{5, 6, 7}},
        {{0, 1025, 1025}},
        {{0, 0, -1}},
        {{(std::numeric_limits<std::int32_t>::max)() - 2,
          (std::numeric_limits<std::int32_t>::max)(),
          (std::numeric_limits<std::int32_t>::max)() - 1}},
        {{0, (std::numeric_limits<std::int32_t>::max)(),
          (std::numeric_limits<std::int32_t>::max)()}},
    }};
    for (const auto &ring : invalidRings)
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, FIRST_ACTIVE_EFFECT_OFFSET, ring[0]);
        StoreI32(&bytes, FIRST_NEW_EFFECT_OFFSET, ring[1]);
        StoreI32(&bytes, FIRST_FREE_EFFECT_OFFSET, ring[2]);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    // The maximum legal ring covers every physical effect exactly once.
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(
            &bytes,
            FIRST_NEW_EFFECT_OFFSET,
            SOURCE_FIRST_ACTIVE + static_cast<std::int32_t>(MAX_EFFECTS));
        StoreI32(
            &bytes,
            FIRST_FREE_EFFECT_OFFSET,
            SOURCE_FIRST_ACTIVE + static_cast<std::int32_t>(MAX_EFFECTS));
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CHECK(output.firstActiveEffect == NORMALIZED_FIRST_ACTIVE);
        CHECK(output.firstNewEffect
            == NORMALIZED_FIRST_ACTIVE
                + static_cast<std::int32_t>(MAX_EFFECTS));
        CHECK(output.firstFreeEffect == output.firstNewEffect);
        for (const std::uint8_t active : ActiveEffectSlots(metadata))
            CHECK(active == 1);
    }

    // Ring arithmetic remains defined and normalized at the int32 upper edge.
    {
        constexpr std::int32_t first =
            (std::numeric_limits<std::int32_t>::max)() - 3;
        constexpr std::int32_t end =
            (std::numeric_limits<std::int32_t>::max)();
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, FIRST_ACTIVE_EFFECT_OFFSET, first);
        StoreI32(&bytes, FIRST_NEW_EFFECT_OFFSET, end);
        StoreI32(&bytes, FIRST_FREE_EFFECT_OFFSET, end);
        StoreI32(&bytes, ACTIVE_SPOT_EFFECT_COUNT_OFFSET, 0);
        StoreI32(&bytes, ACTIVE_SPOT_ELEM_COUNT_OFFSET, 0);
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        const std::int32_t normalized =
            first & static_cast<std::int32_t>(MAX_EFFECTS - 1u);
        CHECK(output.firstActiveEffect == normalized);
        CHECK(output.firstNewEffect == normalized + 3);
        CHECK(output.firstFreeEffect == normalized + 3);
    }
}

void TestEffectHandleFailures()
{
    constexpr std::array<std::uint16_t, 6> invalidInventoryHandles{{
        UINT16_C(1),
        static_cast<std::uint16_t>(EFFECT_HANDLE_DISK_STRIDE - 1u),
        static_cast<std::uint16_t>(EFFECT_HANDLE_DISK_STRIDE + 1u),
        static_cast<std::uint16_t>(
            MAX_EFFECTS * EFFECT_HANDLE_DISK_STRIDE),
        UINT16_C(0xFFFE),
        INVALID_HANDLE,
    }};
    for (const std::uint16_t invalid : invalidInventoryHandles)
    {
        auto bytes = MakeGoldenBytes();
        StoreU16(&bytes, ALL_EFFECT_HANDLES_OFFSET + 18u * 2u, invalid);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    {
        auto bytes = MakeGoldenBytes();
        const std::uint16_t duplicate = DiskEffectHandle(PermutedEffectIndex(0));
        StoreU16(&bytes, ALL_EFFECT_HANDLES_OFFSET + 1u * 2u, duplicate);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    constexpr std::array<std::uint16_t, 3> invalidSpotEffectHandles{{
        UINT16_C(1),
        static_cast<std::uint16_t>(
            MAX_EFFECTS * EFFECT_HANDLE_DISK_STRIDE),
        INVALID_HANDLE,
    }};
    for (const std::uint16_t invalid : invalidSpotEffectHandles)
    {
        auto bytes = MakeGoldenBytes();
        StoreU16(&bytes, ACTIVE_SPOT_EFFECT_HANDLE_OFFSET, invalid);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    {
        auto bytes = MakeGoldenBytes();
        StoreU16(
            &bytes,
            ACTIVE_SPOT_EFFECT_HANDLE_OFFSET,
            DiskEffectHandle(PermutedEffectIndex(100)));
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    constexpr std::array<std::uint16_t, 6> invalidSpotElemHandles{{
        UINT16_C(1),
        static_cast<std::uint16_t>(ELEM_HANDLE_DISK_STRIDE - 1u),
        static_cast<std::uint16_t>(ELEM_HANDLE_DISK_STRIDE + 1u),
        static_cast<std::uint16_t>(ELEM_HANDLE_DISK_LIMIT),
        UINT16_C(0xFFFE),
        INVALID_HANDLE,
    }};
    for (const std::uint16_t invalid : invalidSpotElemHandles)
    {
        auto bytes = MakeGoldenBytes();
        StoreU16(&bytes, ACTIVE_SPOT_ELEM_HANDLE_OFFSET, invalid);
        CheckFailurePreservesOutputs(SystemFromBytes(bytes));
    }

    {
        auto bytes = MakeGoldenBytes();
        const std::uint16_t lastValidElemHandle =
            static_cast<std::uint16_t>(
                ELEM_HANDLE_DISK_LIMIT - ELEM_HANDLE_DISK_STRIDE);
        StoreU16(
            &bytes,
            ACTIVE_SPOT_ELEM_HANDLE_OFFSET,
            lastValidElemHandle);
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CHECK(output.activeSpotLightElemHandle == lastValidElemHandle);
    }
}

void TestSpotlightCanonicalization()
{
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, ACTIVE_SPOT_EFFECT_COUNT_OFFSET, 0);
        StoreI32(&bytes, ACTIVE_SPOT_ELEM_COUNT_OFFSET, 0);
        StoreU16(&bytes, ACTIVE_SPOT_EFFECT_HANDLE_OFFSET, UINT16_C(0x1235));
        StoreU16(&bytes, ACTIVE_SPOT_ELEM_HANDLE_OFFSET, UINT16_C(0x5679));
        StoreU16(&bytes, ACTIVE_SPOT_BOLT_DOBJ_OFFSET, UINT16_C(0x1357));
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CHECK(output.activeSpotLightEffectCount == 0);
        CHECK(output.activeSpotLightElemCount == 0);
        CHECK(output.activeSpotLightEffectHandle == INVALID_HANDLE);
        CHECK(output.activeSpotLightElemHandle == INVALID_HANDLE);
        CHECK(output.activeSpotLightBoltDobj == -1);
    }

    // An inactive elem field is inert even if its serialized handle is
    // malformed.  The active effect and its bolt remain published.
    {
        auto bytes = MakeGoldenBytes();
        StoreI32(&bytes, ACTIVE_SPOT_ELEM_COUNT_OFFSET, 0);
        StoreU16(&bytes, ACTIVE_SPOT_ELEM_HANDLE_OFFSET, UINT16_C(0x5679));
        const archive::FxSystemDisk32 source = SystemFromBytes(bytes);
        FxSystem output{};
        archive::FxSystemDisk32Metadata metadata{};
        CHECK(archive::TryUnpackFxSystemDisk32(
            source, &output, &metadata));
        CHECK(output.activeSpotLightEffectCount == 1);
        CHECK(output.activeSpotLightElemCount == 0);
        CHECK(output.activeSpotLightEffectHandle
            == NativeEffectHandle(
                PermutedEffectIndex(
                    static_cast<std::size_t>(NORMALIZED_FIRST_ACTIVE))));
        CHECK(output.activeSpotLightElemHandle == INVALID_HANDLE);
        CHECK(output.activeSpotLightBoltDobj == -7);
    }
}

void TestNullOutputs()
{
    const archive::FxSystemDisk32 source = SystemFromBytes(MakeGoldenBytes());
    FxSystem output{};
    archive::FxSystemDisk32Metadata metadata{};
    InitializeSentinels(&output, &metadata);
    const auto outputBefore = ObjectBytes(output);
    const auto metadataBefore = ObjectBytes(metadata);
    CHECK(!archive::TryUnpackFxSystemDisk32(source, nullptr, &metadata));
    CHECK(ObjectBytes(metadata) == metadataBefore);
    CHECK(!archive::TryUnpackFxSystemDisk32(source, &output, nullptr));
    CHECK(ObjectBytes(output) == outputBefore);
}

#if KISAK_PTR_BITS == 32
void PopulateExpectedNativeSystem(FxSystem *const expected)
{
    CHECK(expected != nullptr);
    if (!expected)
        return;

    const auto golden = MakeGoldenBytes();
    std::memcpy(&expected->camera, golden.data() + CAMERA_OFFSET, CAMERA_BYTES);
    std::memcpy(
        &expected->cameraPrev,
        golden.data() + CAMERA_PREV_OFFSET,
        CAMERA_BYTES);
    expected->sprite.indices = nullptr;
    expected->sprite.indexCount = 0;
    expected->sprite.material = nullptr;
    expected->sprite.name = nullptr;
    expected->effects = nullptr;
    expected->elems = nullptr;
    expected->trails = nullptr;
    expected->trailElems = nullptr;
    expected->deferredElems = nullptr;
    expected->firstFreeElem = 17;
    expected->firstFreeTrailElem = 19;
    expected->firstFreeTrail = 7;
    expected->deferredElemCount = 0;
    expected->activeElemCount = 12;
    expected->activeTrailElemCount = 13;
    expected->activeTrailCount = 4;
    expected->gfxCloudCount = 5;
    expected->visState = nullptr;
    expected->visStateBufferRead = nullptr;
    expected->visStateBufferWrite = nullptr;
    expected->firstActiveEffect = NORMALIZED_FIRST_ACTIVE;
    expected->firstNewEffect = NORMALIZED_FIRST_FREE;
    expected->firstFreeEffect = NORMALIZED_FIRST_FREE;
    for (std::size_t index = 0; index < MAX_EFFECTS; ++index)
    {
        expected->allEffectHandles[index] =
            NativeEffectHandle(PermutedEffectIndex(index));
    }
    expected->activeSpotLightEffectCount = 1;
    expected->activeSpotLightElemCount = 1;
    expected->activeSpotLightEffectHandle =
        NativeEffectHandle(
            PermutedEffectIndex(
                static_cast<std::size_t>(NORMALIZED_FIRST_ACTIVE)));
    expected->activeSpotLightElemHandle = UINT16_C(0x0046);
    expected->activeSpotLightBoltDobj = -7;
    expected->iteratorCount = 0;
    expected->msecNow = 1000;
    expected->msecDraw = 900;
    expected->frameCount = 7;
    expected->isInitialized = true;
    expected->needsGarbageCollection = true;
    expected->isArchiving = true;
    expected->localClientNum = 0;
    for (std::size_t index = 0; index < 32; ++index)
    {
        expected->restartList[index] =
            UINT32_C(0xA5000000) + static_cast<std::uint32_t>(index);
    }
}
#endif

void TestConditionalX86RawLayoutOracle()
{
#if KISAK_PTR_BITS == 32
    static_assert(sizeof(FxSystem) == DISK_SYSTEM_BYTES);
    static_assert(offsetof(FxSystem, camera) == CAMERA_OFFSET);
    static_assert(offsetof(FxSystem, cameraPrev) == CAMERA_PREV_OFFSET);
    static_assert(offsetof(FxSystem, sprite) == SPRITE_OFFSET);
    static_assert(
        offsetof(FxSpriteInfo, indices) == SPRITE_INDICES_OFFSET - SPRITE_OFFSET);
    static_assert(
        offsetof(FxSpriteInfo, indexCount)
        == SPRITE_INDEX_COUNT_OFFSET - SPRITE_OFFSET);
    static_assert(
        offsetof(FxSpriteInfo, material)
        == SPRITE_MATERIAL_OFFSET - SPRITE_OFFSET);
    static_assert(
        offsetof(FxSpriteInfo, name) == SPRITE_NAME_OFFSET - SPRITE_OFFSET);
    static_assert(offsetof(FxSystem, effects) == EFFECTS_ADDRESS_OFFSET);
    static_assert(offsetof(FxSystem, elems) == ELEMS_ADDRESS_OFFSET);
    static_assert(offsetof(FxSystem, trails) == TRAILS_ADDRESS_OFFSET);
    static_assert(
        offsetof(FxSystem, trailElems) == TRAIL_ELEMS_ADDRESS_OFFSET);
    static_assert(
        offsetof(FxSystem, deferredElems) == DEFERRED_ELEMS_ADDRESS_OFFSET);
    static_assert(
        offsetof(FxSystem, firstFreeElem) == FIRST_FREE_ELEM_OFFSET);
    static_assert(
        offsetof(FxSystem, firstFreeTrailElem)
        == FIRST_FREE_TRAIL_ELEM_OFFSET);
    static_assert(
        offsetof(FxSystem, firstFreeTrail) == FIRST_FREE_TRAIL_OFFSET);
    static_assert(
        offsetof(FxSystem, deferredElemCount)
        == DEFERRED_ELEM_COUNT_OFFSET);
    static_assert(
        offsetof(FxSystem, activeElemCount) == ACTIVE_ELEM_COUNT_OFFSET);
    static_assert(
        offsetof(FxSystem, activeTrailElemCount)
        == ACTIVE_TRAIL_ELEM_COUNT_OFFSET);
    static_assert(
        offsetof(FxSystem, activeTrailCount)
        == ACTIVE_TRAIL_COUNT_OFFSET);
    static_assert(
        offsetof(FxSystem, gfxCloudCount) == GFX_CLOUD_COUNT_OFFSET);
    static_assert(offsetof(FxSystem, visState) == VIS_STATE_ADDRESS_OFFSET);
    static_assert(
        offsetof(FxSystem, visStateBufferRead)
        == VIS_STATE_READ_ADDRESS_OFFSET);
    static_assert(
        offsetof(FxSystem, visStateBufferWrite)
        == VIS_STATE_WRITE_ADDRESS_OFFSET);
    static_assert(
        offsetof(FxSystem, firstActiveEffect)
        == FIRST_ACTIVE_EFFECT_OFFSET);
    static_assert(
        offsetof(FxSystem, firstNewEffect) == FIRST_NEW_EFFECT_OFFSET);
    static_assert(
        offsetof(FxSystem, firstFreeEffect) == FIRST_FREE_EFFECT_OFFSET);
    static_assert(
        offsetof(FxSystem, allEffectHandles)
        == ALL_EFFECT_HANDLES_OFFSET);
    static_assert(
        offsetof(FxSystem, activeSpotLightEffectCount)
        == ACTIVE_SPOT_EFFECT_COUNT_OFFSET);
    static_assert(
        offsetof(FxSystem, activeSpotLightElemCount)
        == ACTIVE_SPOT_ELEM_COUNT_OFFSET);
    static_assert(
        offsetof(FxSystem, activeSpotLightEffectHandle)
        == ACTIVE_SPOT_EFFECT_HANDLE_OFFSET);
    static_assert(
        offsetof(FxSystem, activeSpotLightElemHandle)
        == ACTIVE_SPOT_ELEM_HANDLE_OFFSET);
    static_assert(
        offsetof(FxSystem, activeSpotLightBoltDobj)
        == ACTIVE_SPOT_BOLT_DOBJ_OFFSET);
    static_assert(
        offsetof(FxSystem, iteratorCount) == ITERATOR_COUNT_OFFSET);
    static_assert(offsetof(FxSystem, msecNow) == MSEC_NOW_OFFSET);
    static_assert(offsetof(FxSystem, msecDraw) == MSEC_DRAW_OFFSET);
    static_assert(offsetof(FxSystem, frameCount) == FRAME_COUNT_OFFSET);
    static_assert(
        offsetof(FxSystem, isInitialized) == IS_INITIALIZED_OFFSET);
    static_assert(
        offsetof(FxSystem, needsGarbageCollection)
        == NEEDS_GARBAGE_COLLECTION_OFFSET);
    static_assert(offsetof(FxSystem, isArchiving) == IS_ARCHIVING_OFFSET);
    static_assert(
        offsetof(FxSystem, localClientNum) == LOCAL_CLIENT_NUM_OFFSET);
    static_assert(offsetof(FxSystem, restartList) == RESTART_LIST_OFFSET);

    const archive::FxSystemDisk32 source = SystemFromBytes(MakeGoldenBytes());
    FxSystem output{};
    archive::FxSystemDisk32Metadata metadata{};
    CHECK(archive::TryUnpackFxSystemDisk32(source, &output, &metadata));
    FxSystem expected{};
    PopulateExpectedNativeSystem(&expected);
    CHECK(ObjectBytes(output) == ObjectBytes(expected));
#else
    CHECK(sizeof(FxSystem) == 0xA90);
    CHECK(sizeof(archive::FxSystemDisk32) == DISK_SYSTEM_BYTES);
#endif
}
} // namespace

int main()
{
    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof(float) == 4);
    static_assert(std::numeric_limits<float>::is_iec559);
    static_assert(EFFECT_HANDLE_DISK_STRIDE == 32);
    static_assert(EFFECT_HANDLE_NATIVE_STRIDE
        == (KISAK_PTR_BITS == 32 ? 32u : 34u));
    static_assert(std::is_standard_layout_v<archive::ArchiveAddress32>);
    static_assert(std::is_trivially_copyable_v<archive::ArchiveAddress32>);
    static_assert(sizeof(archive::ArchiveAddress32) == 4);
    static_assert(alignof(archive::ArchiveAddress32) == 4);
    static_assert(offsetof(archive::ArchiveAddress32, value) == 0);
    static_assert(sizeof(archive::FxCameraDisk32) == CAMERA_BYTES);
    static_assert(alignof(archive::FxCameraDisk32) == 4);
    static_assert(std::is_standard_layout_v<archive::FxCameraDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::FxCameraDisk32>);
    static_assert(offsetof(archive::FxCameraDisk32, origin) == 0x00);
    static_assert(offsetof(archive::FxCameraDisk32, isValid) == 0x0C);
    static_assert(offsetof(archive::FxCameraDisk32, frustum) == 0x10);
    static_assert(offsetof(archive::FxCameraDisk32, axis) == 0x70);
    static_assert(
        offsetof(archive::FxCameraDisk32, frustumPlaneCount) == 0x94);
    static_assert(offsetof(archive::FxCameraDisk32, viewOffset) == 0x98);
    static_assert(offsetof(archive::FxCameraDisk32, pad) == 0xA4);
    static_assert(sizeof(archive::FxSpriteInfoDisk32) == 0x10);
    static_assert(alignof(archive::FxSpriteInfoDisk32) == 4);
    static_assert(std::is_standard_layout_v<archive::FxSpriteInfoDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::FxSpriteInfoDisk32>);
    static_assert(offsetof(archive::FxSpriteInfoDisk32, indices) == 0x00);
    static_assert(offsetof(archive::FxSpriteInfoDisk32, indexCount) == 0x04);
    static_assert(offsetof(archive::FxSpriteInfoDisk32, material) == 0x08);
    static_assert(offsetof(archive::FxSpriteInfoDisk32, name) == 0x0C);
    static_assert(sizeof(archive::FxSystemDisk32) == DISK_SYSTEM_BYTES);
    static_assert(alignof(archive::FxSystemDisk32) == 4);
    static_assert(std::is_standard_layout_v<archive::FxSystemDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::FxSystemDisk32>);
    static_assert(offsetof(archive::FxSystemDisk32, camera) == CAMERA_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, cameraPrev) == CAMERA_PREV_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, sprite) == SPRITE_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, effects) == EFFECTS_ADDRESS_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, elems) == ELEMS_ADDRESS_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, trails) == TRAILS_ADDRESS_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, trailElems) == TRAIL_ELEMS_ADDRESS_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, deferredElems) == DEFERRED_ELEMS_ADDRESS_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, firstFreeElem) == FIRST_FREE_ELEM_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, firstFreeTrailElem) == FIRST_FREE_TRAIL_ELEM_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, firstFreeTrail) == FIRST_FREE_TRAIL_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, deferredElemCount) == DEFERRED_ELEM_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, activeElemCount) == ACTIVE_ELEM_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, activeTrailElemCount) == ACTIVE_TRAIL_ELEM_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, activeTrailCount) == ACTIVE_TRAIL_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, gfxCloudCount) == GFX_CLOUD_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, visState) == VIS_STATE_ADDRESS_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, visStateBufferRead) == VIS_STATE_READ_ADDRESS_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, visStateBufferWrite) == VIS_STATE_WRITE_ADDRESS_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, firstActiveEffect) == FIRST_ACTIVE_EFFECT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, firstNewEffect) == FIRST_NEW_EFFECT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, firstFreeEffect) == FIRST_FREE_EFFECT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, allEffectHandles) == ALL_EFFECT_HANDLES_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, activeSpotLightEffectCount) == ACTIVE_SPOT_EFFECT_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, activeSpotLightElemCount) == ACTIVE_SPOT_ELEM_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, activeSpotLightEffectHandle) == ACTIVE_SPOT_EFFECT_HANDLE_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, activeSpotLightElemHandle) == ACTIVE_SPOT_ELEM_HANDLE_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, activeSpotLightBoltDobj) == ACTIVE_SPOT_BOLT_DOBJ_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, spotLightPadding) == 0x9CA);
    static_assert(offsetof(archive::FxSystemDisk32, iteratorCount) == ITERATOR_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, msecNow) == MSEC_NOW_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, msecDraw) == MSEC_DRAW_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, frameCount) == FRAME_COUNT_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, isInitialized) == IS_INITIALIZED_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, needsGarbageCollection) == NEEDS_GARBAGE_COLLECTION_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, isArchiving) == IS_ARCHIVING_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, localClientNum) == LOCAL_CLIENT_NUM_OFFSET);
    static_assert(offsetof(archive::FxSystemDisk32, restartList) == RESTART_LIST_OFFSET);
    static_assert(sizeof(archive::FxSystemDisk32Metadata) == MAX_EFFECTS + 2u);
    static_assert(alignof(archive::FxSystemDisk32Metadata) == 1);
    static_assert(std::is_standard_layout_v<archive::FxSystemDisk32Metadata>);
    static_assert(std::is_trivially_copyable_v<archive::FxSystemDisk32Metadata>);
    static_assert(offsetof(archive::FxSystemDisk32Metadata, readVisibilitySelector) == 0);
    static_assert(offsetof(archive::FxSystemDisk32Metadata, writeVisibilitySelector) == 1);
    static_assert(offsetof(archive::FxSystemDisk32Metadata, activeEffectSlots) == 2);

    const archive::ArchiveAddress32 zeroAddress{};
    const archive::ArchiveAddress32 explicitAddress{UINT32_C(0xFEDCBA98)};
    CHECK(zeroAddress.value == 0);
    CHECK(explicitAddress.value == UINT32_C(0xFEDCBA98));

    TestGoldenRecordAndConversion();
    TestVisibilitySelectorOrientations();
    TestCameraValidationAndReadiness();
    TestInvalidScalarAndBooleanFields();
    TestFrameCountNormalization();
    TestPoolCountAndFreeHeadConsistency();
    TestAddressTopologyFailures();
    TestEffectRingFailures();
    TestEffectHandleFailures();
    TestSpotlightCanonicalization();
    TestNullOutputs();
    TestConditionalX86RawLayoutOracle();
    return failures == 0 ? 0 : 1;
}
