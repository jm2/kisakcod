#pragma once

#include <bgame/bg_target_protocol.h>
#include <universal/kisak_abi.h>

struct gentity_s;

struct target_t
{
    gentity_s *ent;
    float offset[3];
    int materialIndex;
    int offscreenMaterialIndex;
    int flags;
};

// Storage-only initialization deliberately does not inspect or dereference
// the previous entity pointer. Startup and save publication may encounter a
// pointer from an older entity generation, so detaching a known-live target is
// a separate game-layer operation.
inline void ClearTargetEntry(target_t *const target) noexcept
{
    target->ent = nullptr;
    target->offset[0] = 0.0f;
    target->offset[1] = 0.0f;
    target->offset[2] = 0.0f;
    target->materialIndex = bg::target_protocol::kNoMaterial;
    target->offscreenMaterialIndex = bg::target_protocol::kNoMaterial;
    target->flags = 0;
}

RUNTIME_SIZE(target_t, 0x1C, 0x20);
RUNTIME_OFFSET(target_t, ent, 0x0, 0x0);
RUNTIME_OFFSET(target_t, offset, 0x4, 0x8);
RUNTIME_OFFSET(target_t, materialIndex, 0x10, 0x14);
RUNTIME_OFFSET(target_t, offscreenMaterialIndex, 0x14, 0x18);
RUNTIME_OFFSET(target_t, flags, 0x18, 0x1C);

struct TargetGlob
{
    target_t targets[bg::target_protocol::kMaxTargets];
    unsigned int targetCount;
};

RUNTIME_SIZE(TargetGlob, 0x384, 0x408);
RUNTIME_OFFSET(TargetGlob, targets, 0x0, 0x0);
RUNTIME_OFFSET(TargetGlob, targetCount, 0x380, 0x400);
