#pragma once

// The K4 digest pipeline (docs/design/FASTFILE_LOADER.md). A walker per asset
// family hashes each loaded asset's logical values through db::graph_hash:
// names, counts, scalars and the bytes its pointers lead to, never a pointer
// or padding. The Windows x86 load and the 64-bit load of one zone therefore
// give the same digest per family, which is the parity K4 counts.

#include "database/db_graph_hash.h"

#include <cstdint>

union XAssetHeader;

// One dynamic entity definition's fields, for code that must not include the
// client DynEntity header (the K4 walker in the headless server); db_load.cpp defines it.
struct DynEntityDefView
{
    std::int32_t type;
    const float *quat;   // 4 floats
    const float *origin; // 3 floats
    const struct XModel *xModel;
    std::uint16_t brushModel;
    std::uint16_t physicsBrushModel;
    const struct FxEffectDef *destroyFx;
    const struct XModelPieces *destroyPieces;
    const struct PhysPreset *physPreset;
    std::int32_t health;
    const float *mass; // centerOfMass, momentsOfInertia, productsOfInertia: 9 floats
    std::int32_t contents;
};
DynEntityDefView DB_DynEntityDefView(const struct DynEntityDef *defs, std::int32_t index);

// One asset's digest; a family with no walker hashes only its type.
db::graph_hash::Digest DB_GraphHashAsset(std::int32_t assetType, const XAssetHeader &header);

// The digest of every pooled asset of assetType in name order: the type and
// count, then each asset's digest. perAsset, when given, sees each asset as it
// is hashed. Returns the asset count, or -1 when the family has no walker.
int DB_GraphHashFamily(
    std::int32_t assetType,
    db::graph_hash::Digest &digest,
    void (*perAsset)(const char *name, const db::graph_hash::Digest &digest) = nullptr);

// `db_graphhash <family> [list]`, once the map has loaded: prints the family's
// asset count and digest, and with `list` each asset's digest first.
void __cdecl DB_GraphHash_f();
