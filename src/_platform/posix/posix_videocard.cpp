// posix_videocard.cpp: the video card description a dxvk-native client
// reports, the way win32/win_configure.cpp's Sys_DetectVideoCard does on
// Windows: adapter 0's D3D9 Description, or "Unknown video card". The
// headless server keeps posix_sys.cpp's "headless". Under dxvk-native the
// Description is dxvk's, which names a GPU from a vendor D3D9 never knew
// (Apple, for one) after a card games expect.

#include "posix_videocard.h"

#include <qcommon/sys_local.h>

#include <cstdio>

#include <d3d9.h>

void Posix_DetectVideoCard()
{
    static_assert(sizeof(sys_info.gpuDescription) == sizeof(D3DADAPTER_IDENTIFIER9::Description));
    std::snprintf(sys_info.gpuDescription, sizeof(sys_info.gpuDescription), "Unknown video card");
    IDirect3D9 *d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9)
        return;
    D3DADAPTER_IDENTIFIER9 id;
    if (d3d9->GetAdapterIdentifier(0, 0, &id) >= 0)
        std::snprintf(sys_info.gpuDescription, sizeof(sys_info.gpuDescription), "%s", id.Description);
    d3d9->Release();
}
