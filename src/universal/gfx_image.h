#pragma once

// The image asset record (ASSET_TYPE_IMAGE) and the small types it holds. It
// lives outside gfx_d3d/ so headless code can name its fields without a
// client include; gfx_d3d/r_gfx.h includes it. The texture union holds the
// renderer's objects and the load definition by pointer only.

#include <universal/kisak_abi.h>

#include <cstdint>

struct GfxImageLoadDef;
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DVolumeTexture9;
struct IDirect3DCubeTexture9;

enum MapType : std::int32_t
{                                       // ...
    MAPTYPE_NONE = 0x0,
    MAPTYPE_INVALID1 = 0x1,
    MAPTYPE_INVALID2 = 0x2,
    MAPTYPE_2D = 0x3,
    MAPTYPE_3D = 0x4,
    MAPTYPE_CUBE = 0x5,
    MAPTYPE_COUNT = 0x6,
};

struct Picmip // sizeof=0x2
{                                       // ...
    Picmip()
    {
        platform[0] = 0;
        platform[1] = 0;
    }
    Picmip(int i)
    {
        platform[0] = i;
        platform[1] = i;
    }
    uint8_t platform[2];        // ...
};

struct CardMemory // sizeof=0x8
{                                       // ...
    int platform[2];                    // ...
};

union GfxTexture // sizeof=0x4
{                                       // ...
    IDirect3DBaseTexture9* basemap;
    IDirect3DTexture9* map;
    IDirect3DVolumeTexture9* volmap;
    IDirect3DCubeTexture9* cubemap;
    GfxImageLoadDef* loadDef;
};

struct GfxImage // sizeof=0x24
{                                       // ...
    MapType mapType;                    // ...
    GfxTexture texture;
    Picmip picmip;
    bool noPicmip;
    uint8_t semantic;
    uint8_t track;
    // padding byte
    // padding byte
    // padding byte
    CardMemory cardMemory;
    uint16_t width;
    uint16_t height;
    uint16_t depth;
    uint8_t category;
    bool delayLoadPixels;
    const char* name;
};
RUNTIME_SIZE(GfxImage, 0x24, 0x30);
