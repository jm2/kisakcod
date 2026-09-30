#pragma once

// Shader cleanup seam for shared database code (KPI K5,
// docs/design/NATIVE64.md). The material shader load failure paths release
// and null one opaque COM shader pointer held by a shared asset record
// (MaterialVertexShaderProgram.vs / MaterialPixelShaderProgram.ps).
//
// The COM interfaces are forward-declared so this header never reaches the
// D3D9 SDK: <d3d9.h> is a Win32 renderer header (see gfx_d3d/r_init.h), and
// the headless build must keep compiling without it.

struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;

// Release and null the shader pointer in *shader. The Release() call is
// compiled out of the KISAK_DEDI_HEADLESS build, which never creates COM
// objects (the headless branches of Load_Material*ShaderProgram leave the
// pointer null), so the opaque forward declaration above is complete enough
// there. The slot is always nulled, and calling this on an already-null slot
// is a no-op.
void DB_ReleaseVertexShader(IDirect3DVertexShader9 **shader);
void DB_ReleasePixelShader(IDirect3DPixelShader9 **shader);
