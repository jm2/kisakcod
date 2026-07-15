#include <EffectsCore/fx_archive_body_state_disk32.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
namespace archive = fx::archive;

constexpr std::size_t BODY_STATE_BYTES = 0x70;
constexpr std::size_t POSITION_OFFSET = 0x00;
constexpr std::size_t ROTATION_OFFSET = 0x0C;
constexpr std::size_t VELOCITY_OFFSET = 0x30;
constexpr std::size_t ANGULAR_VELOCITY_OFFSET = 0x3C;
constexpr std::size_t CENTER_OF_MASS_OFFSET = 0x48;
constexpr std::size_t MASS_OFFSET = 0x54;
constexpr std::size_t FRICTION_OFFSET = 0x58;
constexpr std::size_t BOUNCE_OFFSET = 0x5C;
constexpr std::size_t STATE_OFFSET = 0x60;
constexpr std::size_t TIME_LAST_ASLEEP_OFFSET = 0x64;
constexpr std::size_t TYPE_OFFSET = 0x68;
constexpr std::size_t UNDERWATER_OFFSET = 0x6C;

constexpr float SPATIAL_MAX = 1048576.0f;
constexpr float ANGULAR_MAX = 65536.0f;
constexpr float MASS_MIN = 0.0001f;
constexpr float MASS_MAX = 1000000.0f;
constexpr float FRICTION_MAX = 10000.0f;

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
    std::array<std::uint8_t, sizeof(TYPE)> bytes{};
    std::memcpy(bytes.data(), &object, bytes.size());
    return bytes;
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

archive::BodyStateDisk32 MakeValidState()
{
    archive::BodyStateDisk32 source{};
    source.position[0] = 1.25f;
    source.position[1] = -2.5f;
    source.position[2] = 3.75f;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
            source.rotation[row][column] = row == column ? 1.0f : 0.0f;
    }
    source.velocity[0] = -4.0f;
    source.velocity[1] = 5.0f;
    source.velocity[2] = -6.0f;
    source.angVelocity[0] = 7.0f;
    source.angVelocity[1] = -8.0f;
    source.angVelocity[2] = 9.0f;
    source.centerOfMassOffset[0] = -0.25f;
    source.centerOfMassOffset[1] = 0.5f;
    source.centerOfMassOffset[2] = -0.75f;
    source.mass = 12.5f;
    source.friction = 0.625f;
    source.bounce = 0.375f;
    source.state = 2;
    source.timeLastAsleep = INT32_C(-123456789);
    source.type = 49;
    source.underwater = UINT32_C(0xABCDEF01);
    return source;
}

BodyState MakeSentinelState()
{
    BodyState sentinel{};
    std::memset(&sentinel, 0xA5, sizeof(sentinel));
    return sentinel;
}

template <typename MUTATOR>
void ExpectRejected(MUTATOR mutate)
{
    archive::BodyStateDisk32 source = MakeValidState();
    mutate(source);
    BodyState output = MakeSentinelState();
    const auto before = ObjectBytes(output);
    CHECK(!archive::TryUnpackBodyStateDisk32(source, 2000, &output));
    CHECK(ObjectBytes(output) == before);
}

template <typename MUTATOR>
void ExpectAccepted(MUTATOR mutate)
{
    archive::BodyStateDisk32 source = MakeValidState();
    mutate(source);
    BodyState output = MakeSentinelState();
    CHECK(archive::TryUnpackBodyStateDisk32(source, 2000, &output));
}

template <typename SETTER>
void CheckSymmetricVectorPolicy(
    SETTER setValue,
    const float magnitudeLimit)
{
    const float above = std::nextafter(
        magnitudeLimit, (std::numeric_limits<float>::infinity)());
    const float below = std::nextafter(
        -magnitudeLimit, -(std::numeric_limits<float>::infinity)());
    for (std::size_t component = 0; component < 3; ++component)
    {
        ExpectAccepted([&](auto &source) {
            setValue(source, component, magnitudeLimit);
        });
        ExpectAccepted([&](auto &source) {
            setValue(source, component, -magnitudeLimit);
        });
        ExpectRejected([&](auto &source) {
            setValue(source, component, above);
        });
        ExpectRejected([&](auto &source) {
            setValue(source, component, below);
        });
        ExpectRejected([&](auto &source) {
            setValue(
                source,
                component,
                (std::numeric_limits<float>::quiet_NaN)());
        });
        ExpectRejected([&](auto &source) {
            setValue(
                source,
                component,
                (std::numeric_limits<float>::infinity)());
        });
        ExpectRejected([&](auto &source) {
            setValue(
                source,
                component,
                -(std::numeric_limits<float>::infinity)());
        });
    }
}

void TestLayoutAndGoldenBytes()
{
    static_assert(sizeof(archive::BodyStateDisk32) == BODY_STATE_BYTES);
    static_assert(sizeof(BodyState) == BODY_STATE_BYTES);
    static_assert(alignof(archive::BodyStateDisk32) == 4);
    static_assert(alignof(BodyState) == 4);
    static_assert(std::is_standard_layout_v<archive::BodyStateDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::BodyStateDisk32>);
    static_assert(std::is_standard_layout_v<BodyState>);
    static_assert(std::is_trivially_copyable_v<BodyState>);

    std::array<std::uint8_t, BODY_STATE_BYTES> bytes{};
    StoreFloat(&bytes, POSITION_OFFSET + 0u, 11.0f);
    StoreFloat(&bytes, POSITION_OFFSET + 4u, -12.0f);
    StoreFloat(&bytes, POSITION_OFFSET + 8u, 13.0f);
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            StoreFloat(
                &bytes,
                ROTATION_OFFSET + (row * 3u + column) * 4u,
                row == column ? 1.0f : 0.0f);
        }
    }
    StoreFloat(&bytes, VELOCITY_OFFSET + 0u, -21.0f);
    StoreFloat(&bytes, VELOCITY_OFFSET + 4u, 22.0f);
    StoreFloat(&bytes, VELOCITY_OFFSET + 8u, -23.0f);
    StoreFloat(&bytes, ANGULAR_VELOCITY_OFFSET + 0u, 31.0f);
    StoreFloat(&bytes, ANGULAR_VELOCITY_OFFSET + 4u, -32.0f);
    StoreFloat(&bytes, ANGULAR_VELOCITY_OFFSET + 8u, 33.0f);
    StoreFloat(&bytes, CENTER_OF_MASS_OFFSET + 0u, -41.0f);
    StoreFloat(&bytes, CENTER_OF_MASS_OFFSET + 4u, 42.0f);
    StoreFloat(&bytes, CENTER_OF_MASS_OFFSET + 8u, -43.0f);
    StoreFloat(&bytes, MASS_OFFSET, 50.0f);
    StoreFloat(&bytes, FRICTION_OFFSET, 0.75f);
    StoreFloat(&bytes, BOUNCE_OFFSET, 0.25f);
    StoreI32(&bytes, STATE_OFFSET, 1);
    StoreI32(&bytes, TIME_LAST_ASLEEP_OFFSET, INT32_MIN);
    StoreI32(&bytes, TYPE_OFFSET, 48);
    StoreU32(&bytes, UNDERWATER_OFFSET, UINT32_C(0xFFFFFF01));

    archive::BodyStateDisk32 source{};
    std::memcpy(&source, bytes.data(), bytes.size());
    BodyState output = MakeSentinelState();
    CHECK(archive::TryUnpackBodyStateDisk32(source, 7654321, &output));
    CHECK(output.position[0] == 11.0f);
    CHECK(output.position[1] == -12.0f);
    CHECK(output.position[2] == 13.0f);
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            CHECK(output.rotation[row][column]
                == (row == column ? 1.0f : 0.0f));
        }
    }
    CHECK(output.velocity[0] == -21.0f);
    CHECK(output.velocity[1] == 22.0f);
    CHECK(output.velocity[2] == -23.0f);
    CHECK(output.angVelocity[0] == 31.0f);
    CHECK(output.angVelocity[1] == -32.0f);
    CHECK(output.angVelocity[2] == 33.0f);
    CHECK(output.centerOfMassOffset[0] == -41.0f);
    CHECK(output.centerOfMassOffset[1] == 42.0f);
    CHECK(output.centerOfMassOffset[2] == -43.0f);
    CHECK(output.mass == 50.0f);
    CHECK(output.friction == 0.75f);
    CHECK(output.bounce == 0.25f);
    CHECK(output.state == 1);
    CHECK(output.timeLastAsleep == 7654321);
    CHECK(output.type == 48);
    CHECK(output.underwater == 1);
}

void TestCompleteMappingAndCanonicalization()
{
    const archive::BodyStateDisk32 source = MakeValidState();
    BodyState output = MakeSentinelState();
    CHECK(archive::TryUnpackBodyStateDisk32(source, 2468, &output));
    for (std::size_t component = 0; component < 3; ++component)
    {
        CHECK(output.position[component] == source.position[component]);
        CHECK(output.velocity[component] == source.velocity[component]);
        CHECK(output.angVelocity[component]
            == source.angVelocity[component]);
        CHECK(output.centerOfMassOffset[component]
            == source.centerOfMassOffset[component]);
        for (std::size_t column = 0; column < 3; ++column)
        {
            CHECK(output.rotation[component][column]
                == source.rotation[component][column]);
        }
    }
    CHECK(output.mass == source.mass);
    CHECK(output.friction == source.friction);
    CHECK(output.bounce == source.bounce);
    CHECK(output.state == source.state);
    CHECK(output.timeLastAsleep == 2468);
    CHECK(output.timeLastAsleep != source.timeLastAsleep);
    CHECK(output.type == source.type);
    CHECK(output.underwater == 1);

    archive::BodyStateDisk32 dirtyZero = source;
    dirtyZero.underwater = UINT32_C(0xFFFFFF00);
    dirtyZero.timeLastAsleep = INT32_MAX;
    CHECK(archive::TryUnpackBodyStateDisk32(dirtyZero, 0, &output));
    CHECK(output.underwater == 0);
    CHECK(output.timeLastAsleep == 0);

    archive::BodyStateDisk32 dirtyOne = source;
    dirtyOne.underwater = UINT32_C(0x80000001);
    dirtyOne.timeLastAsleep = INT32_MIN;
    CHECK(archive::TryUnpackBodyStateDisk32(
        dirtyOne, INT32_MAX, &output));
    CHECK(output.underwater == 1);
    CHECK(output.timeLastAsleep == INT32_MAX);

    ExpectRejected([](auto &state) {
        state.underwater = UINT32_C(0xFFFFFF02);
    });
    ExpectRejected([](auto &state) {
        state.underwater = UINT32_C(0x123456FF);
    });
}

void TestArgumentsAndFailureAtomicity()
{
    const archive::BodyStateDisk32 source = MakeValidState();
    CHECK(!archive::TryUnpackBodyStateDisk32(source, 100, nullptr));

    BodyState output = MakeSentinelState();
    const auto before = ObjectBytes(output);
    CHECK(!archive::TryUnpackBodyStateDisk32(source, -1, &output));
    CHECK(ObjectBytes(output) == before);
    CHECK(!archive::TryUnpackBodyStateDisk32(
        source, INT32_MIN, &output));
    CHECK(ObjectBytes(output) == before);

    archive::BodyStateDisk32 malformed = source;
    malformed.mass = (std::numeric_limits<float>::quiet_NaN)();
    CHECK(!archive::TryUnpackBodyStateDisk32(malformed, 100, &output));
    CHECK(ObjectBytes(output) == before);
}

void TestVectorBounds()
{
    CheckSymmetricVectorPolicy(
        [](auto &state, const std::size_t component, const float value) {
            state.position[component] = value;
        },
        SPATIAL_MAX);
    CheckSymmetricVectorPolicy(
        [](auto &state, const std::size_t component, const float value) {
            state.velocity[component] = value;
        },
        SPATIAL_MAX);
    CheckSymmetricVectorPolicy(
        [](auto &state, const std::size_t component, const float value) {
            state.angVelocity[component] = value;
        },
        ANGULAR_MAX);
    CheckSymmetricVectorPolicy(
        [](auto &state, const std::size_t component, const float value) {
            state.centerOfMassOffset[component] = value;
        },
        SPATIAL_MAX);
}

void TestRotationPolicy()
{
    ExpectAccepted([](auto &) {});
    ExpectAccepted([](auto &state) {
        state.rotation[0][0] = 0.99f;
    });
    ExpectAccepted([](auto &state) {
        state.rotation[0][0] = 1.001f;
    });
    ExpectRejected([](auto &state) {
        state.rotation[0][0] = 0.98f;
    });
    ExpectRejected([](auto &state) {
        state.rotation[0][0] = 1.02f;
    });
    ExpectRejected([](auto &state) {
        state.rotation[0][0] = 1.002f;
    });
    ExpectRejected([](auto &state) {
        state.rotation[0][0] =
            (std::numeric_limits<float>::quiet_NaN)();
    });
    ExpectRejected([](auto &state) {
        state.rotation[0][0] =
            (std::numeric_limits<float>::infinity)();
    });
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            ExpectRejected([&](auto &state) {
                state.rotation[row][column] =
                    (std::numeric_limits<float>::quiet_NaN)();
            });
            ExpectRejected([&](auto &state) {
                state.rotation[row][column] =
                    (std::numeric_limits<float>::infinity)();
            });
            ExpectRejected([&](auto &state) {
                state.rotation[row][column] =
                    -(std::numeric_limits<float>::infinity)();
            });
        }
    }
    ExpectAccepted([](auto &state) {
        constexpr float component = 0.024f;
        state.rotation[1][0] = component;
        state.rotation[1][1] = std::sqrt(1.0f - component * component);
    });
    ExpectAccepted([](auto &state) {
        constexpr float component = -0.024f;
        state.rotation[1][0] = component;
        state.rotation[1][1] = std::sqrt(1.0f - component * component);
    });
    ExpectRejected([](auto &state) {
        constexpr float component = 0.026f;
        state.rotation[1][0] = component;
        state.rotation[1][1] = std::sqrt(1.0f - component * component);
    });
    ExpectRejected([](auto &state) {
        constexpr float component = -0.026f;
        state.rotation[1][0] = component;
        state.rotation[1][1] = std::sqrt(1.0f - component * component);
    });
    ExpectRejected([](auto &state) {
        state.rotation[0][0] = -1.0f;
    });
}

void TestScalarPolicies()
{
    ExpectAccepted([](auto &state) { state.mass = MASS_MIN; });
    ExpectAccepted([](auto &state) { state.mass = MASS_MAX; });
    ExpectRejected([](auto &state) {
        state.mass = std::nextafter(MASS_MIN, 0.0f);
    });
    ExpectRejected([](auto &state) {
        state.mass = std::nextafter(
            MASS_MAX, (std::numeric_limits<float>::infinity)());
    });
    ExpectRejected([](auto &state) { state.mass = -MASS_MIN; });
    ExpectRejected([](auto &state) {
        state.mass = (std::numeric_limits<float>::quiet_NaN)();
    });
    ExpectRejected([](auto &state) {
        state.mass = (std::numeric_limits<float>::infinity)();
    });
    ExpectRejected([](auto &state) {
        state.mass = -(std::numeric_limits<float>::infinity)();
    });

    ExpectAccepted([](auto &state) { state.friction = 0.0f; });
    ExpectAccepted([](auto &state) { state.friction = -0.0f; });
    ExpectAccepted([](auto &state) { state.friction = FRICTION_MAX; });
    ExpectAccepted([](auto &state) {
        state.friction = (std::numeric_limits<float>::max)();
    });
    ExpectRejected([](auto &state) {
        state.friction = std::nextafter(
            (std::numeric_limits<float>::max)(), 0.0f);
    });
    ExpectRejected([](auto &state) {
        state.friction = std::nextafter(0.0f, -1.0f);
    });
    ExpectRejected([](auto &state) {
        state.friction = std::nextafter(
            FRICTION_MAX, (std::numeric_limits<float>::infinity)());
    });
    ExpectRejected([](auto &state) {
        state.friction = (std::numeric_limits<float>::quiet_NaN)();
    });
    ExpectRejected([](auto &state) {
        state.friction = (std::numeric_limits<float>::infinity)();
    });
    ExpectRejected([](auto &state) {
        state.friction = -(std::numeric_limits<float>::infinity)();
    });

    ExpectAccepted([](auto &state) { state.bounce = 0.0f; });
    ExpectAccepted([](auto &state) { state.bounce = -0.0f; });
    ExpectAccepted([](auto &state) { state.bounce = 1.0f; });
    ExpectRejected([](auto &state) {
        state.bounce = std::nextafter(0.0f, -1.0f);
    });
    ExpectRejected([](auto &state) {
        state.bounce = std::nextafter(
            1.0f, (std::numeric_limits<float>::infinity)());
    });
    ExpectRejected([](auto &state) {
        state.bounce = (std::numeric_limits<float>::quiet_NaN)();
    });
    ExpectRejected([](auto &state) {
        state.bounce = (std::numeric_limits<float>::infinity)();
    });
    ExpectRejected([](auto &state) {
        state.bounce = -(std::numeric_limits<float>::infinity)();
    });
}

void TestIntegerPolicies()
{
    ExpectAccepted([](auto &state) { state.state = 0; });
    ExpectAccepted([](auto &state) { state.state = 1; });
    ExpectAccepted([](auto &state) { state.state = 2; });
    ExpectRejected([](auto &state) { state.state = -1; });
    ExpectRejected([](auto &state) { state.state = 3; });
    ExpectRejected([](auto &state) { state.state = INT32_MIN; });
    ExpectRejected([](auto &state) { state.state = INT32_MAX; });

    ExpectAccepted([](auto &state) { state.type = 0; });
    ExpectAccepted([](auto &state) { state.type = 49; });
    ExpectRejected([](auto &state) { state.type = -1; });
    ExpectRejected([](auto &state) { state.type = 50; });
    ExpectRejected([](auto &state) { state.type = INT32_MIN; });
    ExpectRejected([](auto &state) { state.type = INT32_MAX; });
}
} // namespace

int main()
{
    TestLayoutAndGoldenBytes();
    TestCompleteMappingAndCanonicalization();
    TestArgumentsAndFailureAtomicity();
    TestVectorBounds();
    TestRotationPolicy();
    TestScalarPolicies();
    TestIntegerPolicies();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d BodyState Disk32 check(s) failed\n", failures);
        return 1;
    }
    std::puts("BodyState Disk32 checks passed");
    return 0;
}
