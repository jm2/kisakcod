#pragma once

#include <physics/phys_body_state.h>

#include <universal/kisak_abi.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace fx::archive
{
// Exact pointer-free image written for each physics body by the legacy x86
// FX archive.  Keep this record distinct from BodyState even while their
// layouts coincide: BodyState is a native runtime type, while every field in
// this type is a width-frozen little-endian disk value.
struct BodyStateDisk32 final
{
    float position[3];
    float rotation[3][3];
    float velocity[3];
    float angVelocity[3];
    float centerOfMassOffset[3];
    float mass;
    float friction;
    float bounce;
    std::int32_t state;
    std::int32_t timeLastAsleep;
    std::int32_t type;
    std::uint32_t underwater;
};

static_assert(
    std::endian::native == std::endian::little,
    "FX BodyState Disk32 records require a little-endian target");
ONDISK_SIZE(float, 0x04);
static_assert(
    std::numeric_limits<float>::is_iec559,
    "FX BodyState Disk32 records require IEC 60559 binary32 float");
ONDISK_SIZE(BodyStateDisk32, 0x70);
ONDISK_OFFSET(BodyStateDisk32, position, 0x00);
ONDISK_OFFSET(BodyStateDisk32, rotation, 0x0C);
ONDISK_OFFSET(BodyStateDisk32, velocity, 0x30);
ONDISK_OFFSET(BodyStateDisk32, angVelocity, 0x3C);
ONDISK_OFFSET(BodyStateDisk32, centerOfMassOffset, 0x48);
ONDISK_OFFSET(BodyStateDisk32, mass, 0x54);
ONDISK_OFFSET(BodyStateDisk32, friction, 0x58);
ONDISK_OFFSET(BodyStateDisk32, bounce, 0x5C);
ONDISK_OFFSET(BodyStateDisk32, state, 0x60);
ONDISK_OFFSET(BodyStateDisk32, timeLastAsleep, 0x64);
ONDISK_OFFSET(BodyStateDisk32, type, 0x68);
ONDISK_OFFSET(BodyStateDisk32, underwater, 0x6C);
static_assert(alignof(BodyStateDisk32) == 4);
static_assert(std::is_standard_layout_v<BodyStateDisk32>);
static_assert(std::is_trivially_copyable_v<BodyStateDisk32>);

// Validates and converts one complete legacy body record without consulting
// live physics. The serialized sleep timestamp is deliberately not copied:
// legacy FX save canonicalizes it to the owning system's clock, and restore
// rebases it once more to the live physics clock immediately before body
// creation. The caller must therefore supply the already validated,
// nonnegative staged FxSystem::msecNow as archiveTime. Legacy writers assigned
// only the low byte of underwater, so dirty upper bytes remain compatible but
// the native output is always the complete integer 0 or 1.
//
// The output is committed only after every check succeeds. A null output,
// negative archive time, or malformed record returns false and leaves every
// caller-owned output byte unchanged.
[[nodiscard]] bool TryUnpackBodyStateDisk32(
    const BodyStateDisk32 &source,
    std::int32_t archiveTime,
    BodyState *outState) noexcept;
} // namespace fx::archive
