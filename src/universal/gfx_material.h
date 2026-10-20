#pragma once

// The material and technique-set asset records (ASSET_TYPE_MATERIAL,
// ASSET_TYPE_TECHNIQUE_SET) and the records they hold. They live outside
// gfx_d3d/ so headless code can name their fields without a client include;
// gfx_d3d/r_gfx.h and r_material.h include this. Renderer objects (vertex
// declarations, shaders) are held by pointer only. unsigned long long and
// unsigned int are the types unsigned __int64 and unsigned __int32 name.

#include <universal/gfx_image.h>
#include <universal/kisak_abi.h>

#include <cstdint>

struct complex_s;
struct IDirect3DPixelShader9;
struct IDirect3DVertexDeclaration9;
struct IDirect3DVertexShader9;

struct GfxDrawSurfFields // sizeof=0x8
{                                       // ...
    /* 0*/ unsigned long long objectId : 16;
    /*16*/ unsigned long long reflectionProbeIndex : 8;
    /*24*/ unsigned long long customIndex : 5;         
    /*29*/ unsigned long long materialSortedIndex : 11;
    /*40*/ unsigned long long prepass : 2;             
    /*42*/ unsigned long long primaryLightIndex : 8;   
    /*50*/ unsigned long long surfType : 4;            
    /*54*/ unsigned long long primarySortKey : 6;      
    /*60*/ unsigned long long unused : 4;              
};

constexpr unsigned long long DRAWSURF_KEY_MASK = 0xFFFFFFFFE0000000;

union GfxDrawSurf // sizeof=0x8
{                                       // ...
    GfxDrawSurfFields fields;
    unsigned long long packed;
    unsigned int packed_low;// KISAKTODO dumb hack
};

struct GfxStateBits // sizeof=0x8
{
    uint32_t loadBits[2];
};

struct GfxVertexShaderLoadDef // sizeof=0x8
{                                       // ...
    void* program;
    uint16_t programSize;
    uint16_t loadForRenderer;
};

struct GfxPixelShaderLoadDef // sizeof=0x8
{                                       // ...
    void* program;
    uint16_t programSize;
    uint16_t loadForRenderer;
};
RUNTIME_SIZE(GfxPixelShaderLoadDef, 0x8, 0x10);

enum MaterialTextureSource : uint32_t
{                                       // ...
    TEXTURE_SRC_CODE_BLACK = 0x0, // ...
    TEXTURE_SRC_CODE_WHITE = 0x1, // ...
    TEXTURE_SRC_CODE_IDENTITY_NORMAL_MAP = 0x2, // ...
    TEXTURE_SRC_CODE_MODEL_LIGHTING = 0x3, // ...
    TEXTURE_SRC_CODE_LIGHTMAP_PRIMARY = 0x4, // ...
    TEXTURE_SRC_CODE_LIGHTMAP_SECONDARY = 0x5, // ...
    TEXTURE_SRC_CODE_SHADOWCOOKIE = 0x6, // ...
    TEXTURE_SRC_CODE_SHADOWMAP_SUN = 0x7, // ...
    TEXTURE_SRC_CODE_SHADOWMAP_SPOT = 0x8, // ...
    TEXTURE_SRC_CODE_FEEDBACK = 0x9, // ...
    TEXTURE_SRC_CODE_RESOLVED_POST_SUN = 0xA, // ...
    TEXTURE_SRC_CODE_RESOLVED_SCENE = 0xB, // ...
    TEXTURE_SRC_CODE_POST_EFFECT_0 = 0xC, // ...
    TEXTURE_SRC_CODE_POST_EFFECT_1 = 0xD, // ...
    TEXTURE_SRC_CODE_SKY = 0xE, // ...
    TEXTURE_SRC_CODE_LIGHT_ATTENUATION = 0xF, // ...
    TEXTURE_SRC_CODE_DYNAMIC_SHADOWS = 0x10, // ...
    TEXTURE_SRC_CODE_OUTDOOR = 0x11, // ...
    TEXTURE_SRC_CODE_FLOATZ = 0x12, // ...
    TEXTURE_SRC_CODE_PROCESSED_FLOATZ = 0x13, // ...
    TEXTURE_SRC_CODE_RAW_FLOATZ = 0x14, // ...
    TEXTURE_SRC_CODE_CASE_TEXTURE = 0x15,
    TEXTURE_SRC_CODE_CINEMATIC_Y = 0x16, // ...
    TEXTURE_SRC_CODE_CINEMATIC_CR = 0x17, // ...
    TEXTURE_SRC_CODE_CINEMATIC_CB = 0x18, // ...
    TEXTURE_SRC_CODE_CINEMATIC_A = 0x19, // ...
    TEXTURE_SRC_CODE_REFLECTION_PROBE = 0x1A, // ...
    TEXTURE_SRC_CODE_COUNT = 0x1B,
};

struct MaterialStreamRouting // sizeof=0x2
{                                       // ...
    uint8_t source;             // ...
    uint8_t dest;               // ...
};

struct MaterialVertexStreamRouting // sizeof=0x60
{                                       // ...
    MaterialStreamRouting data[16];
    IDirect3DVertexDeclaration9 *decl[16];
};

struct MaterialVertexDeclaration // sizeof=0x64
{                                       // ...
    uint8_t streamCount;
    bool hasOptionalSource;
    bool isLoaded;
    // padding byte
    MaterialVertexStreamRouting routing;
};

struct MaterialVertexShaderProgram // sizeof=0xC
{                                       // ...
    IDirect3DVertexShader9 *vs;
    GfxVertexShaderLoadDef loadDef;
};

struct MaterialVertexShader // sizeof=0x10
{                                       // ...
    const char *name;
    MaterialVertexShaderProgram prog;
};

struct MaterialPixelShaderProgram // sizeof=0xC
{                                       // ...
    IDirect3DPixelShader9 *ps;
    GfxPixelShaderLoadDef loadDef;
};
RUNTIME_SIZE(MaterialPixelShaderProgram, 0xC, 0x18);

struct MaterialPixelShader // sizeof=0x10
{                                       // ...
    const char *name;
    MaterialPixelShaderProgram prog;
};

struct MaterialArgumentCodeConst // sizeof=0x4
{                                       // ...
    uint16_t index;
    uint8_t firstRow;
    uint8_t rowCount;
};

union MaterialArgumentDef // sizeof=0x4
{                                       // ...
    const float *literalConst;
    MaterialArgumentCodeConst codeConst;
    MaterialTextureSource codeSampler;
    uint32_t nameHash;
};

struct MaterialShaderArgument // sizeof=0x8
{                                       // ...
    uint16_t type; // $74254F2FBB58A9D84A85080F50CF363D
    uint16_t dest;
    MaterialArgumentDef u;
};

struct MaterialPass // sizeof=0x14
{                                       // ...
    MaterialVertexDeclaration *vertexDecl; // ... // 0
    MaterialVertexShader *vertexShader; // 4
    MaterialPixelShader *pixelShader; // 8
    uint8_t perPrimArgCount;
    uint8_t perObjArgCount;
    uint8_t stableArgCount;
    uint8_t customSamplerFlags;
    MaterialShaderArgument *args;
};

struct MaterialTechnique // sizeof=0x1C
{
    const char *name;
    uint16_t flags;
    uint16_t passCount;
    MaterialPass passArray[1];
};

struct WaterWritable // sizeof=0x4
{                                       // ...
    float floatTime;
};

struct water_t // sizeof=0x44
{                                       // ...
    WaterWritable writable;
    complex_s *H0;
    float *wTerm;
    int M;                              // ...
    int N;                              // ...
    float Lx;                           // ...
    float Lz;                           // ...
    float gravity;                      // ...
    float windvel;                      // ...
    float winddir[2];                   // ...
    float amplitude;                    // ...
    float codeConstant[4];
    GfxImage *image;                    // ...
};

union MaterialTextureDefInfo // sizeof=0x4
{                                       // ...
    GfxImage *image;
    water_t *water;
};

struct MaterialTextureDef // sizeof=0xC
{
    uint32_t nameHash;
    char nameStart;
    char nameEnd;
    uint8_t samplerState;
    uint8_t semantic;
    MaterialTextureDefInfo u;
};

struct MaterialConstantDef // sizeof=0x20
{
    uint32_t nameHash;
    char name[12];
    float literal[4];
};

struct MaterialInfo // sizeof=0x18
{                                       // ...
    const char *name;                   // ...
    uint8_t gameFlags;
    uint8_t sortKey;
    uint8_t textureAtlasRowCount; // ...
    uint8_t textureAtlasColumnCount; // ...
    GfxDrawSurf drawSurf;
    uint32_t surfaceTypeBits;
    uint16_t hashIndex;
    // padding byte
    // padding byte
};

struct MaterialTechniqueSet // sizeof=0x94
{                                       // ...
    const char *name;
    uint8_t worldVertFormat;
    bool hasBeenUploaded;
    uint8_t unused[1];
    // padding byte
    MaterialTechniqueSet *remappedTechniqueSet;
    MaterialTechnique *techniques[34];
};
RUNTIME_SIZE(MaterialTechniqueSet, 0x94, 0x128);

struct Material // sizeof=0x50
{                                       // ...
    MaterialInfo info;
    uint8_t stateBitsEntry[34];
    uint8_t textureCount;
    uint8_t constantCount;
    uint8_t stateBitsCount;
    uint8_t stateFlags;
    uint8_t cameraRegion;
    // padding byte
    MaterialTechniqueSet *techniqueSet;
    MaterialTextureDef *textureTable;
    MaterialConstantDef *constantTable;
    GfxStateBits *stateBitsTable;
};
RUNTIME_SIZE(Material, 0x50, 0x68);
