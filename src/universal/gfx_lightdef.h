#pragma once

// The light-def asset record (ASSET_TYPE_LIGHT_DEF). It lives outside
// gfx_d3d/ so headless code can name its fields without a client include;
// gfx_d3d/r_gfx.h includes it.

#include <cstdint>

struct GfxImage;

struct GfxLightImage // sizeof=0x8
{                                       // ...
    GfxImage* image;
    uint8_t samplerState;
    // padding byte
    // padding byte
    // padding byte
};

struct GfxLightDef // sizeof=0x10
{                                       // ...
    const char* name;
    GfxLightImage attenuation;
    int lmapLookupStart;
};
