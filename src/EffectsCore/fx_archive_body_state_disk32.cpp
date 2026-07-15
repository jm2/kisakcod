#include <EffectsCore/fx_archive_body_state_disk32.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fx::archive
{
namespace
{
constexpr float SPATIAL_COMPONENT_MAX = 1048576.0f;
constexpr float LINEAR_VELOCITY_MAX = 1048576.0f;
constexpr float ANGULAR_VELOCITY_MAX = 65536.0f;
constexpr float PHYSICS_MASS_MIN = 0.0001f;
constexpr float PHYSICS_MASS_MAX = 1000000.0f;
constexpr float PHYSICS_FRICTION_MAX = 10000.0f;
constexpr float PHYSICS_BOUNCE_MAX = 1.0f;
constexpr float ROTATION_COMPONENT_MAX = 1.001f;
constexpr double UNIT_LENGTH_TOLERANCE = 0.025;
constexpr double ORTHOGONAL_TOLERANCE = 0.025;
constexpr std::int32_t PHYSICS_STATE_MIN = 0;
constexpr std::int32_t PHYSICS_STATE_MAX = 2;
constexpr std::int32_t SOUND_CLASS_MIN = 0;
constexpr std::int32_t SOUND_CLASS_MAX = 49;
constexpr std::uint32_t UNDERWATER_VALUE_MASK = UINT32_C(0xFF);

bool FloatIsBounded(
    const float value,
    const float magnitudeLimit) noexcept
{
    return std::isfinite(value)
        && value >= -magnitudeLimit
        && value <= magnitudeLimit;
}

template <std::size_t COUNT>
bool VectorIsBounded(
    const float (&values)[COUNT],
    const float magnitudeLimit) noexcept
{
    for (const float value : values)
    {
        if (!FloatIsBounded(value, magnitudeLimit))
            return false;
    }
    return true;
}

bool OrthonormalBasisIsValid(const float (&basis)[3][3]) noexcept
{
    for (const auto &row : basis)
    {
        if (!VectorIsBounded(row, ROTATION_COMPONENT_MAX))
            return false;

        double lengthSquared = 0.0;
        for (const float value : row)
            lengthSquared += static_cast<double>(value) * value;
        if (lengthSquared < 1.0 - UNIT_LENGTH_TOLERANCE
            || lengthSquared > 1.0 + UNIT_LENGTH_TOLERANCE)
        {
            return false;
        }
    }

    for (std::size_t first = 0; first < 3; ++first)
    {
        for (std::size_t second = first + 1; second < 3; ++second)
        {
            double dot = 0.0;
            for (std::size_t component = 0; component < 3; ++component)
            {
                dot += static_cast<double>(basis[first][component])
                    * basis[second][component];
            }
            if (dot < -ORTHOGONAL_TOLERANCE
                || dot > ORTHOGONAL_TOLERANCE)
            {
                return false;
            }
        }
    }

    const double determinant =
        static_cast<double>(basis[0][0])
            * (static_cast<double>(basis[1][1]) * basis[2][2]
                - static_cast<double>(basis[1][2]) * basis[2][1])
        - static_cast<double>(basis[0][1])
            * (static_cast<double>(basis[1][0]) * basis[2][2]
                - static_cast<double>(basis[1][2]) * basis[2][0])
        + static_cast<double>(basis[0][2])
            * (static_cast<double>(basis[1][0]) * basis[2][1]
                - static_cast<double>(basis[1][1]) * basis[2][0]);
    return determinant >= 1.0 - 3.0 * UNIT_LENGTH_TOLERANCE
        && determinant <= 1.0 + 3.0 * UNIT_LENGTH_TOLERANCE;
}

bool BodyStateIsValid(const BodyStateDisk32 &source) noexcept
{
    const bool frictionValid =
        (FloatIsBounded(source.friction, PHYSICS_FRICTION_MAX)
            && source.friction >= 0.0f)
        // The legacy asset format intentionally uses FLT_MAX for infinite
        // friction.
        || source.friction == (std::numeric_limits<float>::max)();
    return VectorIsBounded(source.position, SPATIAL_COMPONENT_MAX)
        && OrthonormalBasisIsValid(source.rotation)
        && VectorIsBounded(source.velocity, LINEAR_VELOCITY_MAX)
        && VectorIsBounded(
            source.angVelocity, ANGULAR_VELOCITY_MAX)
        && VectorIsBounded(
            source.centerOfMassOffset, SPATIAL_COMPONENT_MAX)
        && FloatIsBounded(source.mass, PHYSICS_MASS_MAX)
        && source.mass >= PHYSICS_MASS_MIN
        && frictionValid
        && FloatIsBounded(source.bounce, PHYSICS_BOUNCE_MAX)
        && source.bounce >= 0.0f
        && source.state >= PHYSICS_STATE_MIN
        && source.state <= PHYSICS_STATE_MAX
        && source.type >= SOUND_CLASS_MIN
        && source.type <= SOUND_CLASS_MAX
        && (source.underwater & UNDERWATER_VALUE_MASK) <= 1u;
}
} // namespace

bool TryUnpackBodyStateDisk32(
    const BodyStateDisk32 &source,
    const std::int32_t archiveTime,
    BodyState *const outState) noexcept
{
    if (!outState || archiveTime < 0 || !BodyStateIsValid(source))
        return false;

    BodyState decoded{};
    for (std::size_t component = 0; component < 3; ++component)
    {
        decoded.position[component] = source.position[component];
        decoded.velocity[component] = source.velocity[component];
        decoded.angVelocity[component] = source.angVelocity[component];
        decoded.centerOfMassOffset[component] =
            source.centerOfMassOffset[component];
        for (std::size_t column = 0; column < 3; ++column)
        {
            decoded.rotation[component][column] =
                source.rotation[component][column];
        }
    }
    decoded.mass = source.mass;
    decoded.friction = source.friction;
    decoded.bounce = source.bounce;
    decoded.state = static_cast<int>(source.state);
    decoded.timeLastAsleep = static_cast<int>(archiveTime);
    decoded.type = static_cast<int>(source.type);
    decoded.underwater = static_cast<int>(
        source.underwater & UNDERWATER_VALUE_MASK);

    *outState = decoded;
    return true;
}
} // namespace fx::archive
