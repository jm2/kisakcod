#pragma once

#include <cstddef>

struct IDirect3D9;

// Direct3DCreate9 through dxvk-native, which throws instead of returning null
// when it can't start Vulkan or the display (for one, no usable Vulkan
// driver). Returns null on failure and writes dxvk's reason to
// error (posix_direct3d.cpp).
IDirect3D9 *Posix_Direct3DCreate9(char *error, std::size_t errorSize);
