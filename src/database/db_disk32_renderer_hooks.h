#pragma once

// db_disk32_renderer_hooks.h: the renderer calls db_load.cpp's x86 steps make
// under !KISAK_DEDI_HEADLESS, for the 64-bit disk32 converters. A client
// creates the runtime object; a headless server keeps today's null handles.
// The one client/headless switch the converters share lives here.

#include <gfx_d3d/r_material.h>

namespace db::disk32_load
{
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
} // namespace db::disk32_load
