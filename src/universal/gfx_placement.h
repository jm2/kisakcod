#pragma once

// GfxPlacement: the quaternion-and-origin pose the clip-map dynamic entities
// and the renderer's scene entities both hold by value. It lives here, not in
// gfx_d3d/r_gfx.h, because a headless TU hashes and loads it (NATIVE64.md:
// move a record definition-only into universal/ before a headless TU uses it).
struct GfxPlacement // sizeof=0x1C
{                                       // ...
    float quat[4];                      // ...
    float origin[3];                    // ...
};
