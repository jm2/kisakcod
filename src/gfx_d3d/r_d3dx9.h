#pragma once

// The D3DX9 shader API the renderer and the Bink texture helper use: the
// D3DX9 SDK on Windows; on a dxvk-native client (KISAK_DXVK_NATIVE) the
// declared subset of _platform/posix/d3dx9_native.h, plus inert forms of the
// Win32 directory calls the loose-shader cache keeps next to its D3DX
// compiles. One include line per site, so Windows TUs keep their line numbers.
#if defined(KISAK_DXVK_NATIVE)
#include <_platform/posix/d3dx9_native.h>
#include <_platform/posix/win32_file_shim.h>
#else
#include <d3dx9shader.h>
#endif
