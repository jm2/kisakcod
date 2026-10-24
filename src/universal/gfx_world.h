#pragma once

// The world asset record (ASSET_TYPE_GFXWORLD) and the records it holds. They
// live outside gfx_d3d/ so headless code can name their fields without a
// client include; gfx_d3d/r_gfx.h includes this. Renderer objects (vertex and
// index buffers, textures) are held by pointer only.

#include <qcommon/com_pack.h>
#include <universal/com_math.h>
#include <universal/gfx_image.h>
#include <universal/gfx_lightdef.h>
#include <universal/gfx_material.h>
#include <universal/kisak_abi.h>

#include <cstdint>

struct GfxCell;
struct GfxPortal;
struct GfxSceneDynBrush;
struct GfxSceneDynModel;
struct IDirect3DIndexBuffer9;
struct IDirect3DQuery9;
struct IDirect3DVertexBuffer9;
struct XModel;

struct srfTriangles_t // sizeof=0x10
{                                       // ...
    int vertexLayerData;
    int firstVertex;
    uint16_t vertexCount;
    uint16_t triCount;
    int baseIndex;
};

union GfxColor // sizeof=0x4
{                                       // ...
    operator uint32_t()
    {
        return packed;
    }
    GfxColor()
    {
        packed = 0;
    }
    GfxColor(int i)
    {
        packed = i;
    }
    GfxColor(uint32_t i)
    {
        packed = i;
    }
    uint32_t packed;
    uint8_t array[4];
};

struct GfxPlacement // sizeof=0x1C
{                                       // ...
    float quat[4];                      // ...
    float origin[3];                    // ...
};

struct GfxStaticModelInst // sizeof=0x1C
{                                       // ...
    float mins[3];
    float maxs[3];
    GfxColor groundLighting;
};

struct GfxSurface // sizeof=0x30
{                                       // ...
    srfTriangles_t tris;
    Material* material;
    uint8_t lightmapIndex;
    uint8_t reflectionProbeIndex;
    uint8_t primaryLightIndex;
    uint8_t flags;
    float bounds[2][3];
};

struct GfxCullGroup // sizeof=0x20
{
    float mins[3];
    float maxs[3];
    int surfaceCount;
    int startSurfIndex;
};

struct GfxPackedPlacement // sizeof=0x34
{                                       // ...
    float origin[3];
    float axis[3][3];
    float scale;
};

struct GfxStaticModelDrawInst // sizeof=0x4C
{                                       // ...
    float cullDist;
    GfxPackedPlacement placement;
    XModel* model;
    uint16_t smodelCacheIndex[4];
    uint8_t reflectionProbeIndex;
    uint8_t primaryLightIndex;
    uint16_t lightingHandle;
    uint8_t flags;
    // padding byte
    // padding byte
    // padding byte
};

struct GfxWorldDpvsStatic // sizeof=0x68
{                                       // ...
    uint32_t smodelCount;           // ...
    uint32_t staticSurfaceCount;    // ...
    uint32_t staticSurfaceCountNoDecal; // ...
    uint32_t litSurfsBegin;         // ...
    uint32_t litSurfsEnd;           // ...
    uint32_t decalSurfsBegin;       // ...
    uint32_t decalSurfsEnd;         // ...
    uint32_t emissiveSurfsBegin;    // ...
    uint32_t emissiveSurfsEnd;      // ...
    uint32_t smodelVisDataCount;    // ...
    uint32_t surfaceVisDataCount;   // ...
    uint8_t* smodelVisData[3];  // ...
    uint8_t* surfaceVisData[3]; // ... [1] = CSM Parition Near, [2] = CSM Parition Far
    uint32_t* lodData;              // ...
    uint16_t* sortedSurfIndex;  // ...
    GfxStaticModelInst* smodelInsts;    // ...
    GfxSurface* surfaces;               // ...
    GfxCullGroup* cullGroups;           // ...
    GfxStaticModelDrawInst* smodelDrawInsts; // ...
    GfxDrawSurf* surfaceMaterials;      // ...
    uint32_t* surfaceCastsSunShadow; // ...
    volatile int usageCount;
};
RUNTIME_SIZE(GfxWorldDpvsStatic, 0x68, 0xA8);

// Each local client's visibility bytes per dynamic entity; unsigned char is the
// type byte names.
using EntVisData = unsigned char *[3];

struct GfxWorldDpvsDynamic // sizeof=0x30
{                                       // ...
    uint32_t dynEntClientWordCount[2]; // ...
    uint32_t dynEntClientCount[2];  // ...
    uint32_t* dynEntCellBits[2];    // ...
    EntVisData dynEntVisData[2]; // ...
};

struct GfxWorldStreamInfo // sizeof=0x0
{                                       // ...
};

struct GfxWorldVertex // sizeof=0x2C
{                                       // ...
    float xyz[3];
    float binormalSign;
    GfxColor color;
    float texCoord[2];
    float lmapCoord[2];
    PackedUnitVec normal;
    PackedUnitVec tangent;
};

struct GfxWorldVertexData // sizeof=0x8
{                                       // ...
    GfxWorldVertex* vertices;           // ...
    IDirect3DVertexBuffer9* worldVb;    // ...
};

struct GfxWorldVertexLayerData // sizeof=0x8
{                                       // ...
    uint8_t* data;              // ...
    IDirect3DVertexBuffer9* layerVb;    // ...
};

struct SunLightParseParams // sizeof=0x80
{                                       // ...
    char name[64];
    float ambientScale;
    float ambientColor[3];
    float diffuseFraction;
    float sunLight;
    float sunColor[3];
    float diffuseColor[3];
    bool diffuseColorHasBeenSet;
    // padding byte
    // padding byte
    // padding byte
    float angles[3];
};

struct GfxLight // sizeof=0x40
{                                       // ...
    uint8_t type;
    uint8_t canUseShadowMap;
    uint8_t unused[2];
    float color[3];
    float dir[3];
    float origin[3];
    float radius;
    float cosHalfFovOuter;
    float cosHalfFovInner;
    int exponent;
    uint32_t spotShadowIndex;
    GfxLightDef* def;
};

struct GfxReflectionProbe // sizeof=0x10
{
    float origin[3];
    GfxImage* reflectionImage;
};

struct GfxWorldDpvsPlanes // sizeof=0x10
{                                       // ...
    int cellCount;                      // ...
    cplane_s* planes;                   // ...
    uint16_t* nodes;            // ...
    uint32_t* sceneEntCellBits;     // ...
};

struct GfxAabbTree // sizeof=0x2C
{
    float mins[3];
    float maxs[3];
    uint16_t childCount;
    uint16_t surfaceCount;
    uint16_t startSurfIndex;
    uint16_t surfaceCountNoDecal;
    uint16_t startSurfIndexNoDecal;
    uint16_t smodelIndexCount;
    uint16_t* smodelIndexes;
    int childrenOffset;
};

struct GfxPortalWritable // sizeof=0xC
{
    bool isQueued;
    bool isAncestor;
    uint8_t recursionDepth;
    uint8_t hullPointCount;
    float (*hullPoints)[2];
    GfxPortal* queuedParent;
};

struct DpvsPlane // sizeof=0x14
{                                       // ...
    float coeffs[4];                    // ...
    uint8_t side[3];            // ...
    uint8_t pad;
};

struct GfxPortal // sizeof=0x44
{
    GfxPortalWritable writable;
    DpvsPlane plane;
    GfxCell* cell;
    float (*vertices)[3];
    uint8_t vertexCount;
    // padding byte
    // padding byte
    // padding byte
    float hullAxis[2][3];
};

struct GfxCell // sizeof=0x38
{
    float mins[3];
    float maxs[3];
    int aabbTreeCount;
    GfxAabbTree* aabbTree;
    int portalCount;
    GfxPortal* portals;
    int cullGroupCount;
    int* cullGroups;
    uint8_t reflectionProbeCount;
    // padding byte
    // padding byte
    // padding byte
    uint8_t* reflectionProbes;
};

struct GfxLightmapArray // sizeof=0x8
{
    GfxImage* primary;
    GfxImage* secondary;
};

struct GfxLightGridEntry // sizeof=0x4
{                                       // ...
    uint16_t colorsIndex;
    uint8_t primaryLightIndex;  // ...
    uint8_t needsTrace;
};

struct GfxLightGridColors // sizeof=0xA8
{                                       // ...
    uint8_t rgb[56][3];
};

struct GfxLightGrid // sizeof=0x38
{                                       // ...
    bool hasLightRegions;               // ...
    // padding byte
    // padding byte
    // padding byte
    uint32_t sunPrimaryLightIndex;  // ...
    uint16_t mins[3];           // ...
    uint16_t maxs[3];           // ...
    uint32_t rowAxis;               // ...
    uint32_t colAxis;               // ...
    uint16_t* rowDataStart;     // ...
    uint32_t rawRowDataSize;        // ...
    uint8_t* rawRowData;        // ...
    uint32_t entryCount;            // ...
    GfxLightGridEntry* entries;         // ...
    uint32_t colorCount;            // ...
    GfxLightGridColors* colors;         // ...
};

struct GfxBrushModelWritable // sizeof=0x18
{                                       // ...
    float mins[3];
    float maxs[3];
};

struct GfxBrushModel // sizeof=0x38
{
    GfxBrushModelWritable writable;
    float bounds[2][3];
    uint16_t surfaceCount;
    uint16_t startSurfIndex;
    uint16_t surfaceCountNoDecal;
    // padding byte
    // padding byte
};

struct GfxShadowGeometry // sizeof=0xC
{
    uint16_t surfaceCount;
    uint16_t smodelCount;
    uint16_t *sortedSurfIndex;
    uint16_t *smodelIndex;
};

struct GfxLightRegionAxis // sizeof=0x14
{
    float dir[3];
    float midPoint;
    float halfSize;
};

struct GfxLightRegionHull // sizeof=0x50
{
    float kdopMidPoint[9];
    float kdopHalfSize[9];
    uint32_t axisCount;
    GfxLightRegionAxis *axis;
};

struct GfxLightRegion // sizeof=0x8
{
    uint32_t hullCount;
    GfxLightRegionHull *hulls;
};

struct sunflare_t // sizeof=0x60
{                                       // ...
    bool hasValidData;
    // padding byte
    // padding byte
    // padding byte
    Material *spriteMaterial;
    Material *flareMaterial;
    float spriteSize;
    float flareMinSize;
    float flareMinDot;
    float flareMaxSize;
    float flareMaxDot;
    float flareMaxAlpha;
    int flareFadeInTime;
    int flareFadeOutTime;
    float blindMinDot;
    float blindMaxDot;
    float blindMaxDarken;
    int blindFadeInTime;
    int blindFadeOutTime;
    float glareMinDot;
    float glareMaxDot;
    float glareMaxLighten;
    int glareFadeInTime;
    int glareFadeOutTime;
    float sunFxPosition[3];
};

struct SunFlareDynamic // sizeof=0x28
{                                       // ...
    float flareIntensity;
    float currentBlind;
    float currentGlare;
    int lastTime;
    float lastVisibility;
    float lastDot;
    bool error;
    bool sunQueryIssued[2];             // ...
    // padding byte
    IDirect3DQuery9 *sunQuery[2];       // ...
    int hitNum;
};

struct MaterialMemory // sizeof=0x8
{                                       // ...
    Material* material;
    int memory;
};

struct GfxWorld // sizeof=0x2DC
{                                       // ...
    const char *name;                   // ...
    const char *baseName;               // ...
    int planeCount;                     // ...
    int nodeCount;                      // ...
    int indexCount;                     // ...
    uint16_t *indices;          // ...
    int surfaceCount;                   // ...
    GfxWorldStreamInfo streamInfo;
    // padding byte
    // padding byte
    // padding byte
    int skySurfCount;                   // ...
    int *skyStartSurfs;                 // ...
    GfxImage *skyImage;                 // ...
    uint8_t skySamplerState;    // ...
    // padding byte
    // padding byte
    // padding byte
    uint32_t vertexCount;           // ...
    GfxWorldVertexData vd;              // ...
    uint32_t vertexLayerDataSize;   // ...
    GfxWorldVertexLayerData vld;        // ...
    SunLightParseParams sunParse;       // ...
    GfxLight *sunLight;                 // ...
    float sunColorFromBsp[3];
    uint32_t sunPrimaryLightIndex;  // ...
    uint32_t primaryLightCount;     // ...
    int cullGroupCount;                 // ...
    uint32_t reflectionProbeCount;  // ...
    GfxReflectionProbe *reflectionProbes; // ...
    GfxTexture *reflectionProbeTextures; // ...
    GfxWorldDpvsPlanes dpvsPlanes;      // ...
    int cellBitsCount;                  // ...
    GfxCell *cells;                     // ...
    int lightmapCount;                  // ...
    GfxLightmapArray *lightmaps;        // ...
    GfxLightGrid lightGrid;             // ...
    GfxTexture *lightmapPrimaryTextures; // ...
    GfxTexture *lightmapSecondaryTextures; // ...
    int modelCount;                     // ...
    GfxBrushModel *models;              // ...
    float mins[3];                      // ...
    float maxs[3];                      // ...
    uint32_t checksum;
    int materialMemoryCount;            // ...
    struct MaterialMemory *materialMemory;     // ...
    sunflare_t sun;                     // ...
    float outdoorLookupMatrix[4][4];
    GfxImage *outdoorImage;
    uint32_t *cellCasterBits;       // ...
    struct GfxSceneDynModel *sceneDynModel;    // ...
    struct GfxSceneDynBrush *sceneDynBrush;    // ...
    uint32_t *primaryLightEntityShadowVis; // ...
    uint32_t *primaryLightDynEntShadowVis[2]; // ...
    uint8_t *nonSunPrimaryLightForModelDynEnt; // ...
    struct GfxShadowGeometry *shadowGeom;      // ...
    struct GfxLightRegion *lightRegion;        // ...
    GfxWorldDpvsStatic dpvs;            // ...
    GfxWorldDpvsDynamic dpvsDyn;        // ...
};
RUNTIME_SIZE(GfxWorld, 0x2DC, 0x408);
