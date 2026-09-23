// Census-only stub (scripts/ci/native64_census.py); never used by a real build.
// Opaque <d3d9.h> stand-in for the Linux targets. Every TU that includes it
// counts toward KPI K5 (docs/design/PLATFORM_POSIX.md); the pragma lets the
// census detect that.
#pragma once
#pragma message("kisak-census-d3d9-stub")
struct IDirect3DDevice9;
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
struct IDirect3D9;
struct IDirect3DStateBlock9;
struct IDirect3DSwapChain9;
typedef enum _D3DFORMAT { D3DFMT_UNKNOWN = 0, D3DFMT_FORCE_DWORD = 0x7fffffff } D3DFORMAT;
typedef int BOOL;
typedef unsigned char byte;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef int HRESULT;
