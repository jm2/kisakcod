#include "db_shader_release.h"

// The complete COM interfaces (for the Release() call) come from the Win32
// renderer SDK and only exist outside the headless build, so the include is
// guarded by the same seam as the call itself. See gfx_d3d/r_d3d9types.h for
// the opaque declarations the headless side compiles against.
#ifndef KISAK_DEDI_HEADLESS
#if defined(_WIN32) || defined(KISAK_DXVK_NATIVE)
#include <d3d9.h>
#endif
#endif

void DB_ReleaseVertexShader(IDirect3DVertexShader9 **shader)
{
    if (*shader)
    {
#ifndef KISAK_DEDI_HEADLESS
        (*shader)->Release();
#endif
        *shader = nullptr;
    }
}

void DB_ReleasePixelShader(IDirect3DPixelShader9 **shader)
{
    if (*shader)
    {
#ifndef KISAK_DEDI_HEADLESS
        (*shader)->Release();
#endif
        *shader = nullptr;
    }
}
