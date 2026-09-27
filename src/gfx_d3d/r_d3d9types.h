#pragma once

// D3D9 types the shared asset headers hold: COM interfaces by pointer and the
// format enums that appear in asset records (GfxImageLoadDef.format) and in
// r_image.h signatures. <d3d9.h> is a Win32 renderer header (see r_init.h);
// on every other platform these declarations keep the shared headers free of
// the D3D9 SDK, which is what KPI K5 counts
// (docs/design/PLATFORM_POSIX.md, "K5: D3D reach").
//
// The _D3DFORMAT / _D3DCUBEMAP_FACES enumerators are the fixed D3D9 values
// (d3d9types.h) because GfxImageLoadDef stores them on disk and r_image.h
// compares them; changing a number here breaks image loads.

#if defined(_WIN32)

#include <d3d9.h>

#else

// Opaque COM interfaces: shared headers only ever hold these by pointer.
struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DSwapChain9;
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DVolumeTexture9;
struct IDirect3DCubeTexture9;
struct IDirect3DSurface9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DVertexDeclaration9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;
struct IDirect3DQuery9;

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
    ((static_cast<unsigned long>(static_cast<unsigned char>(ch0)))  \
    | (static_cast<unsigned long>(static_cast<unsigned char>(ch1)) << 8) \
    | (static_cast<unsigned long>(static_cast<unsigned char>(ch2)) << 16) \
    | (static_cast<unsigned long>(static_cast<unsigned char>(ch3)) << 24))
#endif

// Fixed D3D9 values (d3d9types.h).
enum _D3DFORMAT
{
    D3DFMT_UNKNOWN              =   0,

    D3DFMT_R8G8B8               =  20,
    D3DFMT_A8R8G8B8             =  21,
    D3DFMT_X8R8G8B8             =  22,
    D3DFMT_R5G6B5               =  23,
    D3DFMT_X1R5G5B5             =  24,
    D3DFMT_A1R5G5B5             =  25,
    D3DFMT_A4R4G4B4             =  26,
    D3DFMT_R3G3B2               =  27,
    D3DFMT_A8                   =  28,
    D3DFMT_A8R3G3B2             =  29,
    D3DFMT_X4R4G4B4             =  30,
    D3DFMT_A2B10G10R10          =  31,
    D3DFMT_A8B8G8R8             =  32,
    D3DFMT_X8B8G8R8             =  33,
    D3DFMT_G16R16               =  34,
    D3DFMT_A2R10G10B10          =  35,
    D3DFMT_A16B16G16R16         =  36,

    D3DFMT_A8P8                 =  40,
    D3DFMT_P8                   =  41,

    D3DFMT_L8                   =  50,
    D3DFMT_A8L8                 =  51,
    D3DFMT_A4L4                 =  52,

    D3DFMT_V8U8                 =  60,
    D3DFMT_L6V5U5               =  61,
    D3DFMT_X8L8V8U8             =  62,
    D3DFMT_Q8W8V8U8             =  63,
    D3DFMT_V16U16               =  64,
    D3DFMT_A2W10V10U10          =  67,

    D3DFMT_UYVY                 =  MAKEFOURCC('U', 'Y', 'V', 'Y'),
    D3DFMT_YUY2                 =  MAKEFOURCC('Y', 'U', 'Y', '2'),
    D3DFMT_DXT1                 =  MAKEFOURCC('D', 'X', 'T', '1'),
    D3DFMT_DXT2                 =  MAKEFOURCC('D', 'X', 'T', '2'),
    D3DFMT_DXT3                 =  MAKEFOURCC('D', 'X', 'T', '3'),
    D3DFMT_DXT4                 =  MAKEFOURCC('D', 'X', 'T', '4'),
    D3DFMT_DXT5                 =  MAKEFOURCC('D', 'X', 'T', '5'),
    D3DFMT_MULTI2_ARGB8         =  MAKEFOURCC('M', 'E', 'T', '1'),
    D3DFMT_G8R8_G8B8            =  MAKEFOURCC('G', 'R', 'G', 'B'),
    D3DFMT_R8G8_B8G8            =  MAKEFOURCC('R', 'G', 'B', 'G'),

    D3DFMT_D16_LOCKABLE         =  70,
    D3DFMT_D32                  =  71,
    D3DFMT_D15S1                =  73,
    D3DFMT_D24S8                =  75,
    D3DFMT_D24X8                =  77,
    D3DFMT_D24X4S4              =  79,
    D3DFMT_D16                  =  80,
    D3DFMT_L16                  =  81,
    D3DFMT_D32F_LOCKABLE        =  82,
    D3DFMT_D24FS8               =  83,

    D3DFMT_VERTEXDATA           = 100,
    D3DFMT_INDEX16              = 101,
    D3DFMT_INDEX32              = 102,
    D3DFMT_Q16W16V16U16         = 110,

    // Floating point formats
    D3DFMT_R16F                 = 111,
    D3DFMT_G16R16F              = 112,
    D3DFMT_A16B16G16R16F        = 113,

    // IEEE formats
    D3DFMT_R32F                 = 114,
    D3DFMT_G32R32F              = 115,
    D3DFMT_A32B32G32R32F        = 116,

    D3DFMT_CxV8U8               = 117,

    D3DFMT_FORCE_DWORD          = 0xFFFFFFFF
};
typedef enum _D3DFORMAT D3DFORMAT;

enum _D3DCUBEMAP_FACES
{
    D3DCUBEMAP_FACE_POSITIVE_X  = 0,
    D3DCUBEMAP_FACE_NEGATIVE_X  = 1,
    D3DCUBEMAP_FACE_POSITIVE_Y  = 2,
    D3DCUBEMAP_FACE_NEGATIVE_Y  = 3,
    D3DCUBEMAP_FACE_POSITIVE_Z  = 4,
    D3DCUBEMAP_FACE_NEGATIVE_Z  = 5,

    D3DCUBEMAP_FACE_FORCE_DWORD = 0xFFFFFFFF
};
typedef enum _D3DCUBEMAP_FACES D3DCUBEMAP_FACES;

#endif
