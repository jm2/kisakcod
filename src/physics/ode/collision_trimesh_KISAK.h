#pragma once

// LWSS ADD

// KisakCOD port: calling-convention spellings from the dependency-free
// compatibility leaf; this header is reachable before q_shared.h.
#include <universal/platform_compat.h>
#include <cstdint>

int __cdecl dCollideBoxTriangleList(
    const uint16_t *indices,
    const float (*verts)[3],
    int triCount,
    const float *boxR,
    const float *boxPos,
    const float *boxHalfExtents,
    const float *bodyCenter,
    int Flags,
    struct dContactGeom *Contacts,
    int Stride);