#pragma once

// db_disk32_renderer_hooks.h: the renderer calls db_load.cpp's x86 steps make
// under !KISAK_DEDI_HEADLESS, for the 64-bit disk32 converters. A client
// creates the runtime object; a headless server keeps today's null handles.
// The one client/headless switch the converters share lives here.

#include <cstdint>

#ifndef KISAK_DEDI_HEADLESS
#include <gfx_d3d/r_image.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_water.h>
#else
// A headless server only names these; it keeps the gfx headers out
// (tests/headless_include_debt.allow).
struct GfxImage;
struct GfxImageLoadDef;
struct MaterialVertexDeclaration;
struct MaterialVertexShader;
struct MaterialPixelShader;
struct water_t;
#endif

namespace db::disk32_load
{
#ifndef KISAK_DEDI_HEADLESS
inline constexpr bool kCreatesRendererObjects = true;
#else
inline constexpr bool kCreatesRendererObjects = false;
#endif

// Load_BuildVertexDecl: the declaration's runtime handles per vertex type.
// It raises ERR_DROP on a bad routing.
inline void BuildVertexDecl(MaterialVertexDeclaration *decl)
{
#ifndef KISAK_DEDI_HEADLESS
    Load_BuildVertexDecl(&decl);
#else
    (void)decl;
#endif
}

// Load_CreateMaterial{Vertex,Pixel}Shader: the shader's D3D9 object from its
// program, for the renderer in use.
inline bool CreateShader(MaterialVertexShader *shader)
{
#ifndef KISAK_DEDI_HEADLESS
    return Load_CreateMaterialVertexShader(&shader->prog.loadDef, shader);
#else
    (void)shader;
    return true;
#endif
}

inline bool CreateShader(MaterialPixelShader *shader)
{
#ifndef KISAK_DEDI_HEADLESS
    return Load_CreateMaterialPixelShader(&shader->prog.loadDef, shader);
#else
    (void)shader;
    return true;
#endif
}
// Load_Texture: the image's D3D9 texture from its load definition (embedded
// pixels, a water image, or an external file). A headless server finalizes
// the load definition itself and keeps the texture null.
inline void CreateTexture(GfxImage *image, GfxImageLoadDef *loadDef)
{
#ifndef KISAK_DEDI_HEADLESS
    image->texture.loadDef = loadDef;
    Load_Texture(&image->texture, image);
#else
    (void)image;
    (void)loadDef;
#endif
}

// A texture offset: the image shares an earlier image's texture, and takes a
// COM reference, since Image_Release drops one for every GfxImage. Out of
// line (db_disk32_renderer_hooks.cpp), where the D3D9 interfaces are
// complete.
void ShareTexture(GfxImage *image, std::uintptr_t texture);

// Load_PicmipWater: a client compacts a water's grids to r_picmip_water and
// checks its image at that size. Call it once per completed water, never on
// an alias.
inline bool PicmipWater(water_t **water)
{
#ifndef KISAK_DEDI_HEADLESS
    return Load_PicmipWater(water);
#else
    (void)water;
    return true;
#endif
}
} // namespace db::disk32_load
