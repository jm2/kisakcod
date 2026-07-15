#pragma once

#include <universal/kisak_abi.h>

#include <type_traits>

// Pointer-free native representation of one reconstructible physics body.
// The legacy archive record has the same field widths and offsets, but uses a
// separate Disk32 type so persisted bytes never inherit a future runtime ABI
// change implicitly.
struct BodyState // sizeof=0x70
{
    float position[3];
    float rotation[3][3];
    float velocity[3];
    float angVelocity[3];
    float centerOfMassOffset[3];
    float mass;
    float friction;
    float bounce;
    int state;
    int timeLastAsleep;
    int type;
    int underwater;
};

static_assert(sizeof(int) == 4, "BodyState requires a 32-bit native int");
RUNTIME_SIZE(BodyState, 0x70, 0x70);
RUNTIME_OFFSET(BodyState, position, 0x00, 0x00);
RUNTIME_OFFSET(BodyState, rotation, 0x0C, 0x0C);
RUNTIME_OFFSET(BodyState, velocity, 0x30, 0x30);
RUNTIME_OFFSET(BodyState, angVelocity, 0x3C, 0x3C);
RUNTIME_OFFSET(BodyState, centerOfMassOffset, 0x48, 0x48);
RUNTIME_OFFSET(BodyState, mass, 0x54, 0x54);
RUNTIME_OFFSET(BodyState, friction, 0x58, 0x58);
RUNTIME_OFFSET(BodyState, bounce, 0x5C, 0x5C);
RUNTIME_OFFSET(BodyState, state, 0x60, 0x60);
RUNTIME_OFFSET(BodyState, timeLastAsleep, 0x64, 0x64);
RUNTIME_OFFSET(BodyState, type, 0x68, 0x68);
RUNTIME_OFFSET(BodyState, underwater, 0x6C, 0x6C);
static_assert(alignof(BodyState) == 4);
static_assert(std::is_standard_layout_v<BodyState>);
static_assert(std::is_trivially_copyable_v<BodyState>);
