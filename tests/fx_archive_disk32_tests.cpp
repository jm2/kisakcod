#include <EffectsCore/fx_archive_disk32.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
namespace archive = fx::archive;

constexpr std::size_t DISK_EFFECT_BYTES = 0x80;
constexpr std::size_t DISK_HANDLE_STRIDE =
    DISK_EFFECT_BYTES / FxEffect::HANDLE_SCALE;
constexpr std::size_t NATIVE_HANDLE_STRIDE =
    sizeof(FxEffect) / FxEffect::HANDLE_SCALE;

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
void StoreLittleEndianU16(std::array<std::uint8_t, SIZE> *const bytes,
                          const std::size_t offset, const std::uint16_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset <= SIZE && SIZE - offset >= sizeof(value));
    if (!bytes || offset > SIZE || SIZE - offset < sizeof(value))
        return;
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

template <std::size_t SIZE>
void StoreLittleEndianU32(std::array<std::uint8_t, SIZE> *const bytes,
                          const std::size_t offset, const std::uint32_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset <= SIZE && SIZE - offset >= sizeof(value));
    if (!bytes || offset > SIZE || SIZE - offset < sizeof(value))
        return;
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    (*bytes)[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    (*bytes)[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

constexpr std::array<std::uint32_t, 21> FRAME_WORDS{{
    UINT32_C(0x3F800000), UINT32_C(0xBF000000), UINT32_C(0x40000000),
    UINT32_C(0xC0400000), UINT32_C(0x3E800000), UINT32_C(0x41200000),
    UINT32_C(0xC1200000), UINT32_C(0x00000000), UINT32_C(0x80000000),
    UINT32_C(0x3F000000), UINT32_C(0x3FC00000), UINT32_C(0x42C80000),
    UINT32_C(0xC2C80000), UINT32_C(0x40490FDB), UINT32_C(0x3F3504F3),
    UINT32_C(0xBF3504F3), UINT32_C(0x3F7FFFFF), UINT32_C(0x00800000),
    UINT32_C(0x7F7FFFFF), UINT32_C(0xFF7FFFFF), UINT32_C(0x00000001),
}};

constexpr std::uint32_t DEFINITION_KEY = UINT32_C(0x78563412);
constexpr std::uint32_t BOLT_AND_SORT_WORD = UINT32_C(0xD3B55ABC);
constexpr std::uint32_t DISTANCE_WORD = UINT32_C(0x42280000);
constexpr std::size_t OWNER_INDEX = 7;

std::array<std::uint8_t, DISK_EFFECT_BYTES> MakeGoldenBytes()
{
    std::array<std::uint8_t, DISK_EFFECT_BYTES> bytes{};
    StoreLittleEndianU32(&bytes, 0x00, DEFINITION_KEY);
    StoreLittleEndianU32(&bytes, 0x04, UINT32_C(0xFFFFFFFE));
    StoreLittleEndianU16(&bytes, 0x08, UINT16_C(0x0102));
    StoreLittleEndianU16(&bytes, 0x0A, UINT16_C(0xA0B0));
    StoreLittleEndianU16(&bytes, 0x0C, UINT16_C(0xFFFE));
    StoreLittleEndianU16(&bytes, 0x0E, UINT16_C(0x1122));
    StoreLittleEndianU16(&bytes, 0x10, UINT16_C(0x3344));
    StoreLittleEndianU16(&bytes, 0x12, UINT16_C(0x5566));
    StoreLittleEndianU16(
        &bytes, 0x14,
        static_cast<std::uint16_t>(OWNER_INDEX * DISK_HANDLE_STRIDE));
    StoreLittleEndianU16(&bytes, 0x16, UINT16_C(0x7788));
    StoreLittleEndianU32(&bytes, 0x18, BOLT_AND_SORT_WORD);
    StoreLittleEndianU32(&bytes, 0x1C, UINT32_C(0x10203040));
    StoreLittleEndianU32(&bytes, 0x20, UINT32_C(0xFFFFFC18));
    StoreLittleEndianU32(&bytes, 0x24, UINT32_C(0x55667788));
    for (std::size_t index = 0; index < FRAME_WORDS.size(); ++index)
        StoreLittleEndianU32(&bytes, 0x28 + index * 4u, FRAME_WORDS[index]);
    StoreLittleEndianU32(&bytes, 0x7C, DISTANCE_WORD);
    return bytes;
}

void PopulateFrame(FxSpatialFrame *const frame, const std::size_t firstWord)
{
    CHECK(frame != nullptr);
    CHECK(firstWord <= FRAME_WORDS.size());
    CHECK(FRAME_WORDS.size() - firstWord >= 7u);
    if (!frame || firstWord > FRAME_WORDS.size() ||
        FRAME_WORDS.size() - firstWord < 7u)
    {
        return;
    }
    for (std::size_t index = 0; index < 4; ++index)
        frame->quat[index] =
            std::bit_cast<float>(FRAME_WORDS[firstWord + index]);
    for (std::size_t index = 0; index < 3; ++index)
    {
        frame->origin[index] =
            std::bit_cast<float>(FRAME_WORDS[firstWord + 4u + index]);
    }
}

FxEffect MakeGoldenNativeEffect(const FxEffectDef *const definition)
{
    FxEffect effect{};
    effect.def = definition;
    effect.status = -2;
    effect.firstElemHandle[0] = UINT16_C(0x0102);
    effect.firstElemHandle[1] = UINT16_C(0xA0B0);
    effect.firstElemHandle[2] = UINT16_C(0xFFFE);
    effect.firstSortedElemHandle = UINT16_C(0x1122);
    effect.firstTrailHandle = UINT16_C(0x3344);
    effect.randomSeed = UINT16_C(0x5566);
    effect.owner =
        static_cast<std::uint16_t>(OWNER_INDEX * NATIVE_HANDLE_STRIDE);
    effect.packedLighting = UINT16_C(0x7788);
    effect.boltAndSortOrder.dobjHandle = UINT32_C(0xABC);
    effect.boltAndSortOrder.temporalBits = 1;
    effect.boltAndSortOrder.boneIndex = UINT32_C(0x5AA);
    effect.boltAndSortOrder.sortOrder = UINT32_C(0xD3);
    effect.frameCount = static_cast<std::int32_t>(UINT32_C(0x10203040));
    effect.msecBegin = -1000;
    effect.msecLastUpdate = static_cast<std::int32_t>(UINT32_C(0x55667788));
    PopulateFrame(&effect.frameAtSpawn, 0);
    PopulateFrame(&effect.frameNow, 7);
    PopulateFrame(&effect.framePrev, 14);
    effect.distanceTraveled = std::bit_cast<float>(DISTANCE_WORD);
    return effect;
}

template <typename TYPE>
std::array<std::uint8_t, sizeof(TYPE)> ObjectBytes(const TYPE &object)
{
    std::array<std::uint8_t, sizeof(TYPE)> bytes{};
    std::memcpy(bytes.data(), &object, bytes.size());
    return bytes;
}

template <typename TYPE>
void CopyObjectBytes(const TYPE &source, TYPE *const destination)
{
    std::memcpy(destination, &source, sizeof(source));
}

bool FramesEqual(const FxSpatialFrame &left, const FxSpatialFrame &right)
{
    for (std::size_t index = 0; index < 4; ++index)
    {
        if (std::bit_cast<std::uint32_t>(left.quat[index]) !=
            std::bit_cast<std::uint32_t>(right.quat[index]))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < 3; ++index)
    {
        if (std::bit_cast<std::uint32_t>(left.origin[index]) !=
            std::bit_cast<std::uint32_t>(right.origin[index]))
        {
            return false;
        }
    }
    return true;
}

bool EffectsEqualSemantically(const FxEffect &left, const FxEffect &right)
{
    if (left.def != right.def || left.status != right.status ||
        left.firstSortedElemHandle != right.firstSortedElemHandle ||
        left.firstTrailHandle != right.firstTrailHandle ||
        left.randomSeed != right.randomSeed || left.owner != right.owner ||
        left.packedLighting != right.packedLighting ||
        left.boltAndSortOrder.dobjHandle != right.boltAndSortOrder.dobjHandle ||
        left.boltAndSortOrder.temporalBits !=
            right.boltAndSortOrder.temporalBits ||
        left.boltAndSortOrder.boneIndex != right.boltAndSortOrder.boneIndex ||
        left.boltAndSortOrder.sortOrder != right.boltAndSortOrder.sortOrder ||
        left.frameCount != right.frameCount ||
        left.msecBegin != right.msecBegin ||
        left.msecLastUpdate != right.msecLastUpdate ||
        !FramesEqual(left.frameAtSpawn, right.frameAtSpawn) ||
        !FramesEqual(left.frameNow, right.frameNow) ||
        !FramesEqual(left.framePrev, right.framePrev) ||
        std::bit_cast<std::uint32_t>(left.distanceTraveled) !=
            std::bit_cast<std::uint32_t>(right.distanceTraveled))
    {
        return false;
    }
    for (std::size_t index = 0; index < 3; ++index)
    {
        if (left.firstElemHandle[index] != right.firstElemHandle[index])
            return false;
    }
    return true;
}

const FxEffectDef *ResolvedDefinition()
{
#if UINTPTR_MAX > UINT32_MAX
    constexpr std::uintptr_t address = UINT64_C(0x00000123456789A0);
    static_assert(address > UINT32_MAX);
    return reinterpret_cast<const FxEffectDef *>(address);
#else
    static FxEffectDef definition{};
    return &definition;
#endif
}

void TestEveryEffectHandleRoundTrip()
{
    static_assert(DISK_HANDLE_STRIDE == 32);
    static_assert(NATIVE_HANDLE_STRIDE == (KISAK_PTR_BITS == 32 ? 32 : 34));

    for (std::size_t index = 0; index < MAX_EFFECTS; ++index)
    {
        const auto diskHandle =
            static_cast<std::uint16_t>(index * DISK_HANDLE_STRIDE);
        const auto nativeHandle =
            static_cast<std::uint16_t>(index * NATIVE_HANDLE_STRIDE);
        std::uint16_t observedNative = UINT16_C(0xA55A);
        std::uint16_t observedDisk = UINT16_C(0x5AA5);
        CHECK(archive::TryDecodeFxEffectHandleDisk32(diskHandle,
                                                     &observedNative));
        CHECK(observedNative == nativeHandle);
        CHECK(archive::TryEncodeFxEffectHandleDisk32(nativeHandle,
                                                     &observedDisk));
        CHECK(observedDisk == diskHandle);
    }
}

void TestMalformedEffectHandlesAreTransactional()
{
    constexpr std::array<std::uint16_t, 7> invalidDisk{{
        1,
        static_cast<std::uint16_t>(DISK_HANDLE_STRIDE - 1u),
        static_cast<std::uint16_t>(DISK_HANDLE_STRIDE + 1u),
        static_cast<std::uint16_t>(MAX_EFFECTS * DISK_HANDLE_STRIDE - 1u),
        static_cast<std::uint16_t>(MAX_EFFECTS * DISK_HANDLE_STRIDE),
        UINT16_C(0xFFFE),
        (std::numeric_limits<std::uint16_t>::max)(),
    }};
    for (const std::uint16_t handle : invalidDisk)
    {
        std::uint16_t output = UINT16_C(0xA55A);
        CHECK(!archive::TryDecodeFxEffectHandleDisk32(handle, &output));
        CHECK(output == UINT16_C(0xA55A));
    }
    CHECK(!archive::TryDecodeFxEffectHandleDisk32(0, nullptr));

    const auto nativeLimit =
        static_cast<std::uint16_t>(MAX_EFFECTS * NATIVE_HANDLE_STRIDE);
    const std::array<std::uint16_t, 7> invalidNative{{
        1,
        static_cast<std::uint16_t>(NATIVE_HANDLE_STRIDE - 1u),
        static_cast<std::uint16_t>(NATIVE_HANDLE_STRIDE + 1u),
        static_cast<std::uint16_t>(nativeLimit - 1u),
        nativeLimit,
        UINT16_C(0xFFFE),
        (std::numeric_limits<std::uint16_t>::max)(),
    }};
    for (const std::uint16_t handle : invalidNative)
    {
        std::uint16_t output = UINT16_C(0x5AA5);
        CHECK(!archive::TryEncodeFxEffectHandleDisk32(handle, &output));
        CHECK(output == UINT16_C(0x5AA5));
    }
    CHECK(!archive::TryEncodeFxEffectHandleDisk32(0, nullptr));
}

void TestEveryEffectHandleInput()
{
    constexpr std::uint32_t valueCount =
        static_cast<std::uint32_t>(
            (std::numeric_limits<std::uint16_t>::max)()) +
        1u;
    for (std::uint32_t value = 0; value < valueCount; ++value)
    {
        const auto handle = static_cast<std::uint16_t>(value);

        const bool diskShouldDecode =
            value != (std::numeric_limits<std::uint16_t>::max)() &&
            value % DISK_HANDLE_STRIDE == 0 &&
            value / DISK_HANDLE_STRIDE < MAX_EFFECTS;
        std::uint16_t nativeOutput = UINT16_C(0xA55A);
        const bool diskDecoded =
            archive::TryDecodeFxEffectHandleDisk32(handle, &nativeOutput);
        CHECK(diskDecoded == diskShouldDecode);
        if (diskShouldDecode)
        {
            CHECK(nativeOutput ==
                  static_cast<std::uint16_t>(value / DISK_HANDLE_STRIDE *
                                             NATIVE_HANDLE_STRIDE));
        }
        else
        {
            CHECK(nativeOutput == UINT16_C(0xA55A));
        }

        const bool nativeShouldEncode =
            value != (std::numeric_limits<std::uint16_t>::max)() &&
            value % NATIVE_HANDLE_STRIDE == 0 &&
            value / NATIVE_HANDLE_STRIDE < MAX_EFFECTS;
        std::uint16_t diskOutput = UINT16_C(0x5AA5);
        const bool nativeEncoded =
            archive::TryEncodeFxEffectHandleDisk32(handle, &diskOutput);
        CHECK(nativeEncoded == nativeShouldEncode);
        if (nativeShouldEncode)
        {
            CHECK(diskOutput ==
                  static_cast<std::uint16_t>(value / NATIVE_HANDLE_STRIDE *
                                             DISK_HANDLE_STRIDE));
        }
        else
        {
            CHECK(diskOutput == UINT16_C(0x5AA5));
        }
    }
}

void TestGoldenLittleEndianRecordAndHighDefinitionPointer()
{
    const std::array<std::uint8_t, DISK_EFFECT_BYTES> goldenBytes =
        MakeGoldenBytes();
    archive::FxEffectDisk32 diskEffect{};
    std::memcpy(&diskEffect, goldenBytes.data(), goldenBytes.size());

    const FxEffectDef *const definition = ResolvedDefinition();
#if UINTPTR_MAX > UINT32_MAX
    CHECK(reinterpret_cast<std::uintptr_t>(definition) > UINT32_MAX);
#endif
    FxEffect unpacked{};
    CHECK(archive::TryUnpackFxEffectDisk32(diskEffect, true, definition,
                                           &unpacked));
    const FxEffect expected = MakeGoldenNativeEffect(definition);
    CHECK(EffectsEqualSemantically(unpacked, expected));

    archive::FxEffectDisk32 repacked{};
    CHECK(archive::TryPackFxEffectDisk32(
        unpacked, true, archive::EffectDefinitionKey32{DEFINITION_KEY},
        &repacked));
    CHECK(ObjectBytes(repacked) == goldenBytes);
}

void TestRecordFailuresPreserveOutputs()
{
    const auto goldenBytes = MakeGoldenBytes();
    archive::FxEffectDisk32 source{};
    std::memcpy(&source, goldenBytes.data(), goldenBytes.size());
    const FxEffectDef *const definition = ResolvedDefinition();
    const FxEffect sentinel = MakeGoldenNativeEffect(definition);
    const auto sentinelBytes = ObjectBytes(sentinel);

    FxEffect unpacked{};
    CopyObjectBytes(sentinel, &unpacked);
    archive::FxEffectDisk32 invalidKey = source;
    invalidKey.definitionKey = archive::EffectDefinitionKey32{};
    CHECK(!archive::TryUnpackFxEffectDisk32(invalidKey, true, definition,
                                            &unpacked));
    CHECK(ObjectBytes(unpacked) == sentinelBytes);

    CopyObjectBytes(sentinel, &unpacked);
    CHECK(!archive::TryUnpackFxEffectDisk32(source, true, nullptr, &unpacked));
    CHECK(ObjectBytes(unpacked) == sentinelBytes);

    archive::FxEffectDisk32 invalidOwner = source;
    invalidOwner.owner = 1;
    CopyObjectBytes(sentinel, &unpacked);
    CHECK(!archive::TryUnpackFxEffectDisk32(invalidOwner, true, definition,
                                            &unpacked));
    CHECK(ObjectBytes(unpacked) == sentinelBytes);

    CopyObjectBytes(sentinel, &unpacked);
    CHECK(!archive::TryUnpackFxEffectDisk32(source, false, definition,
                                            &unpacked));
    CHECK(ObjectBytes(unpacked) == sentinelBytes);
    CHECK(!archive::TryUnpackFxEffectDisk32(source, true, definition, nullptr));

    archive::FxEffectDisk32 packed = source;
    const auto packedSentinelBytes = ObjectBytes(packed);
    FxEffect invalidSource = sentinel;
    invalidSource.def = nullptr;
    CHECK(!archive::TryPackFxEffectDisk32(
        invalidSource, true, archive::EffectDefinitionKey32{DEFINITION_KEY},
        &packed));
    CHECK(ObjectBytes(packed) == packedSentinelBytes);

    packed = source;
    CHECK(!archive::TryPackFxEffectDisk32(
        sentinel, true, archive::EffectDefinitionKey32{}, &packed));
    CHECK(ObjectBytes(packed) == packedSentinelBytes);

    invalidSource = sentinel;
    invalidSource.owner = 1;
    packed = source;
    CHECK(!archive::TryPackFxEffectDisk32(
        invalidSource, true, archive::EffectDefinitionKey32{DEFINITION_KEY},
        &packed));
    CHECK(ObjectBytes(packed) == packedSentinelBytes);
    CHECK(!archive::TryPackFxEffectDisk32(
        sentinel, true, archive::EffectDefinitionKey32{DEFINITION_KEY},
        nullptr));
}

void TestInactiveRecordsRemainInert()
{
    archive::FxEffectDisk32 source{};
    source.definitionKey = archive::EffectDefinitionKey32{UINT32_C(0xFFFFFFFF)};
    source.status = 17;
    source.owner = UINT16_C(0x1235);
    source.boltAndSortOrder = UINT32_C(0xA5A55A5A);
    source.distanceTraveled = std::bit_cast<float>(UINT32_C(0xC1200000));

    FxEffect unpacked{};
    CHECK(archive::TryUnpackFxEffectDisk32(source, false, nullptr, &unpacked));
    CHECK(unpacked.def == nullptr);
    CHECK(unpacked.owner == source.owner);
    CHECK(unpacked.status == source.status);

    archive::FxEffectDisk32 repacked{};
    CHECK(archive::TryPackFxEffectDisk32(unpacked, false, source.definitionKey,
                                         &repacked));
    CHECK(ObjectBytes(repacked) == ObjectBytes(source));

    source.definitionKey = archive::EffectDefinitionKey32{};
    CHECK(archive::TryUnpackFxEffectDisk32(source, false, nullptr, &unpacked));
    CHECK(archive::TryPackFxEffectDisk32(
        unpacked, false, archive::EffectDefinitionKey32{}, &repacked));
    CHECK(ObjectBytes(repacked) == ObjectBytes(source));
}

void TestCanonicalX86RawByteEquivalence()
{
    static_assert(KISAK_PTR_BITS != 32 || sizeof(FxEffect) == 0x80);
    if constexpr (KISAK_PTR_BITS == 32)
    {
        FxEffectDef definition{};
        const std::uintptr_t identity =
            reinterpret_cast<std::uintptr_t>(&definition);
        CHECK(identity != 0 && identity <= UINT32_MAX);
        const FxEffect source = MakeGoldenNativeEffect(&definition);
        archive::FxEffectDisk32 packed{};
        CHECK(archive::TryPackFxEffectDisk32(
            source, true,
            archive::EffectDefinitionKey32{
                static_cast<std::uint32_t>(identity)},
            &packed));
        CHECK(std::memcmp(&packed, &source, sizeof(packed)) == 0);
    }
    else
    {
        CHECK(sizeof(FxEffect) == 0x88);
        CHECK(sizeof(archive::FxEffectDisk32) == 0x80);
    }
}
} // namespace

int main()
{
    static_assert(sizeof(archive::FxSpatialFrameDisk32) == 0x1C);
    static_assert(sizeof(archive::FxEffectDisk32) == DISK_EFFECT_BYTES);
    static_assert(offsetof(archive::FxEffectDisk32, definitionKey) == 0x00);
    static_assert(offsetof(archive::FxEffectDisk32, boltAndSortOrder) == 0x18);
    static_assert(offsetof(archive::FxEffectDisk32, frameAtSpawn) == 0x28);
    static_assert(offsetof(archive::FxEffectDisk32, frameNow) == 0x44);
    static_assert(offsetof(archive::FxEffectDisk32, framePrev) == 0x60);
    static_assert(offsetof(archive::FxEffectDisk32, distanceTraveled) == 0x7C);
    static_assert(std::endian::native == std::endian::little);

    TestEveryEffectHandleRoundTrip();
    TestMalformedEffectHandlesAreTransactional();
    TestEveryEffectHandleInput();
    TestGoldenLittleEndianRecordAndHighDefinitionPointer();
    TestRecordFailuresPreserveOutputs();
    TestInactiveRecordsRemainInert();
    TestCanonicalX86RawByteEquivalence();
    return failures == 0 ? 0 : 1;
}
