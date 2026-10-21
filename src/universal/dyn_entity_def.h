#pragma once

#include <cstdint>

#include <universal/gfx_placement.h>
#include <universal/kisak_abi.h>
#include <universal/phys_mass.h>

struct FxEffectDef;
struct PhysPreset;
struct XModel;
struct XModelPieces;

enum DynEntityType : std::int32_t
{                                       // ...
    DYNENT_TYPE_INVALID = 0x0,
    DYNENT_TYPE_CLUTTER = 0x1,
    DYNENT_TYPE_DESTRUCT = 0x2,
    DYNENT_TYPE_COUNT = 0x3,
};

// A dynamic entity's definition, loaded from a clip map's dynEntDefList and
// hashed by the K4 digests. The pointers are forward declarations here; the
// families that own them resolve the names.
struct DynEntityDef // sizeof=0x60
{
    DynEntityType type;
    GfxPlacement pose;
    XModel *xModel;
    uint16_t brushModel;
    uint16_t physicsBrushModel;
    const FxEffectDef *destroyFx;
    XModelPieces *destroyPieces;
    PhysPreset *physPreset;
    std::int32_t health;
    PhysMass mass;
    std::int32_t contents;
};
RUNTIME_SIZE(DynEntityDef, 0x60, 0x78);
