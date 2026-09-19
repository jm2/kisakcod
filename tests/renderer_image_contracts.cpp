// Current production records and bodies; D3D/engine services below are doubles.
// This protects the naming migration, not full GPU or retail-image parity.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#ifndef _MSC_VER
#define __cdecl
#define __int32 std::int32_t
#define __int16 std::int16_t
#endif
// Legacy production names and byte conversions are intentionally unchanged.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
#endif
#ifdef _MSC_VER
#pragma warning(disable: 4244 4245)
#endif

namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "renderer image contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#define iassert(value) CHECK(value)
#include "image_format_enum.inc"
#include "image_flags_enum.inc"
#include "image_category_enum.inc"
#include "image_semantic_enum.inc"
#include "image_blend_enum.inc"
#include "image_file_header.inc"
#include "image_raw_pixel.inc"
#include "image_raw_record.inc"
#include "image_map_enum.inc"
#include "image_picmip_type.inc"
#include "image_memory_type.inc"
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DVolumeTexture9;
struct IDirect3DCubeTexture9;
struct GfxImageLoadDef;
#include "image_texture_type.inc"
#include "image_record.inc"
static_assert(std::is_same_v<std::underlying_type_t<GfxImageFileFormat>, std::int32_t>);
static_assert(sizeof(GfxImageFileHeader) == 28 && alignof(GfxImageFileHeader) == 4);
static_assert(offsetof(GfxImageFileHeader, format) == 4);
static_assert(offsetof(GfxImageFileHeader, flags) == 5);
static_assert(offsetof(GfxImageFileHeader, dimensions) == 6);
static_assert(offsetof(GfxImageFileHeader, fileSizeForPicmip) == 12);
static_assert(std::is_same_v<decltype(GfxImageFileHeader::format), std::uint8_t>);
static_assert(std::is_same_v<decltype(GfxImageFileHeader::flags), std::uint8_t>);
static_assert(std::is_same_v<decltype(GfxImage::semantic), std::uint8_t>);
static_assert(std::is_same_v<decltype(GfxImage::category), std::uint8_t>);
static_assert(IMG_FORMAT_INVALID == 0 && IMG_FORMAT_BITMAP_RGBA == 1
    && IMG_FORMAT_BITMAP_RGB == 2 && IMG_FORMAT_BITMAP_LUMINANCE_ALPHA == 3
    && IMG_FORMAT_BITMAP_LUMINANCE == 4 && IMG_FORMAT_BITMAP_ALPHA == 5
    && IMG_FORMAT_WAVELET_RGBA == 6 && IMG_FORMAT_WAVELET_RGB == 7
    && IMG_FORMAT_WAVELET_LUMINANCE_ALPHA == 8 && IMG_FORMAT_WAVELET_LUMINANCE == 9
    && IMG_FORMAT_WAVELET_ALPHA == 10 && IMG_FORMAT_DXT1 == 11
    && IMG_FORMAT_DXT3 == 12 && IMG_FORMAT_DXT5 == 13 && IMG_FORMAT_DXN == 14
    && IMG_FORMAT_COUNT == 15);
static_assert(IMG_FLAG_NOPICMIP == 1 && IMG_FLAG_NOMIPMAPS == 2
    && IMG_FLAG_CUBEMAP == 4 && IMG_FLAG_VOLMAP == 8 && IMG_FLAG_STREAMING == 16
    && IMG_FLAG_LEGACY_NORMALS == 32 && IMG_FLAG_CLAMP_U == 64 && IMG_FLAG_CLAMP_V == 128
    && IMG_FLAG_DYNAMIC == 0x10000 && IMG_FLAG_RENDER_TARGET == 0x20000
    && IMG_FLAG_SYSTEMMEM == 0x40000);
static_assert(IMG_CATEGORY_UNKNOWN == 0 && IMG_CATEGORY_AUTO_GENERATED == 1
    && IMG_CATEGORY_LIGHTMAP == 2 && IMG_CATEGORY_LOAD_FROM_FILE == 3
    && IMG_CATEGORY_RAW == 4 && IMG_CATEGORY_FIRST_UNMANAGED == 5
    && IMG_CATEGORY_WATER == 5 && IMG_CATEGORY_RENDERTARGET == 6 && IMG_CATEGORY_TEMP == 7);
static_assert(TS_2D == 0 && TS_FUNCTION == 1 && TS_COLOR_MAP == 2
    && TS_UNUSED_1 == 3 && TS_UNUSED_2 == 4 && TS_NORMAL_MAP == 5
    && TS_UNUSED_3 == 6 && TS_UNUSED_4 == 7 && TS_SPECULAR_MAP == 8
    && TS_UNUSED_5 == 9 && TS_UNUSED_6 == 10 && TS_WATER_MAP == 11);

enum _D3DFORMAT { D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_A8L8, D3DFMT_L8,
    D3DFMT_A8, D3DFMT_DXT1, D3DFMT_DXT3, D3DFMT_DXT5, D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D16 };
constexpr bool alwaysfails = false;
int assertions;
void MyAssertHandler(const char *, int, int, const char *, ...) { ++assertions; }
const char *va(const char *, ...) { return "fixture diagnostic"; }
#include "image_usage_body.inc"
#include "image_mip_count_body.inc"
#include "image_bitmap_body.inc"
struct {
    int picmip;
    int picmipBump;
    int picmipSpec;
} imageGlobals{};
#include "image_picmip_body.inc"

int dispatchKind;
int dispatchBytes;
int dispatchCalls;
_D3DFORMAT dispatchFormat;
GfxImage *expectedImage;
const GfxImageFileHeader *expectedHeader;
uint8_t *expectedData;
void Capture(int kind, GfxImage *image, const GfxImageFileHeader *header, uint8_t *data, _D3DFORMAT format, int bytes)
{
    CHECK(image == expectedImage && header == expectedHeader && data == expectedData);
    CHECK(image->texture.basemap == nullptr);
    dispatchKind = kind; dispatchFormat = format; dispatchBytes = bytes; ++dispatchCalls;
}
void Image_LoadBitmap(GfxImage *image, const GfxImageFileHeader *header, uint8_t *data, _D3DFORMAT format, int bytes)
{ Capture(1, image, header, data, format, bytes); }
void Image_LoadWavelet(GfxImage *image, const GfxImageFileHeader *header, uint8_t *data, _D3DFORMAT format, int bytes)
{ Capture(2, image, header, data, format, bytes); }
void Image_LoadDxtc(GfxImage *image, const GfxImageFileHeader *header, uint8_t *data, _D3DFORMAT format, int bytes)
{ Capture(3, image, header, data, format, bytes); }
#include "image_dispatch_body.inc"

void TestFormatDispatch()
{
    constexpr _D3DFORMAT formats[] = {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_A8L8, D3DFMT_L8, D3DFMT_A8,
        D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_A8L8, D3DFMT_L8, D3DFMT_A8, D3DFMT_DXT1, D3DFMT_DXT3, D3DFMT_DXT5};
    constexpr int bytes[] = {4,3,2,1,1,4,3,2,1,1,8,16,16};
    GfxImage image{}; GfxImageFileHeader header{}; uint8_t data{};
    expectedImage=&image; expectedHeader=&header; expectedData=&data;
    for (int format=0; format<256; ++format) {
        header.format=static_cast<uint8_t>(format);
        dispatchCalls=assertions=0;
        Image_LoadFromData(&image,&header,&data);
        if (format>=1 && format<=13) {
            CHECK(dispatchCalls==1 && assertions==0);
            CHECK(dispatchFormat==formats[format-1] && dispatchBytes==bytes[format-1]);
            CHECK(dispatchKind==(format<=5 ? 1 : format<=10 ? 2 : 3));
        } else {
            CHECK(dispatchCalls==0 && assertions==1); // Includes unsupported DXN (14).
        }
    }
    expectedImage=nullptr; expectedHeader=nullptr; expectedData=nullptr;
}
void TestBitmapPixels()
{
    GfxImageFileHeader header{};
    constexpr uint8_t input[][8]={{1,2,3,4,5,6,7,8},{1,2,3,4,5,6,0,0},{11,22,33,44,0,0,0,0},{11,22,0,0,0,0,0,0},{11,22,0,0,0,0,0,0}};
    constexpr GfxRawPixel output[][2]={{{3,2,1,4},{7,6,5,8}},{{3,2,1,255},{6,5,4,255}},{{11,11,11,22},{33,33,33,44}},{{11,11,11,255},{22,22,22,255}},{{0,0,0,11},{0,0,0,22}}};
    header.dimensions[0]=2; header.dimensions[1]=1;
    for (int format=1; format<=10; ++format) {
        header.format=static_cast<uint8_t>(format);
        GfxRawPixel pixels[3]={{},{},{0xA5,0xA5,0xA5,0xA5}};
        GfxRawImage raw{}; raw.pixels=pixels;
        uint8_t source[8]; const int kind=(format-1)%5;
        std::memcpy(source,input[kind],sizeof(source));
        Image_CopyBitmapData(&raw,&header,source);
        CHECK(std::memcmp(pixels,output[kind],sizeof(output[kind]))==0);
        CHECK(pixels[2].r==0xA5 && pixels[2].g==0xA5 && pixels[2].b==0xA5 && pixels[2].a==0xA5);
    }
}
void TestFlags()
{
    for (int low=0; low<256; ++low) {
        CHECK(Image_CountMipmaps(static_cast<char>(low),16,4,1)==((low&2) ? 1u : 5u));
        CHECK(Image_GetUsage(low,D3DFMT_A8R8G8B8)==0);
        CHECK(Image_GetUsage(low|0x10000,D3DFMT_A8R8G8B8)==512);
        CHECK(Image_GetUsage(low|0x20000,D3DFMT_A8R8G8B8)==1);
        for (auto depth : {D3DFMT_D24S8,D3DFMT_D24X8,D3DFMT_D16})
            CHECK(Image_GetUsage(low|0x30000,depth)==2);
        CHECK(Image_GetUsage(low|0x40000,D3DFMT_A8R8G8B8)==0);
    }
}
void TestSemantics()
{
    // Frozen baseline outcomes, independent of the production enum labels.
    struct Expected {
        bool usesLevel;
        int first;
        int second;
        int failures;
    };
    constexpr Expected expected[] = {
        {false,0,0,0}, {false,0,0,0}, {true,0,2,0}, {false,0,0,1},
        {false,0,0,1}, {false,1,2,0}, {false,0,0,1}, {false,0,0,1},
        {false,2,2,0}, {false,0,0,1}, {false,0,0,1}, {true,0,2,0}
    };
    for (int semantic=0; semantic<12; ++semantic) {
        for (int level : {-1,0,1,3,4}) {
            imageGlobals.picmip=level;
            imageGlobals.picmipBump=1;
            imageGlobals.picmipSpec=2;
            Picmip mip{};
            assertions=0;
            Image_PicmipForSemantic(static_cast<uint8_t>(semantic),&mip);
            const auto &outcome=expected[semantic];
            CHECK(mip.platform[0]==(outcome.usesLevel ? std::clamp(level,0,3) : outcome.first));
            CHECK(mip.platform[1]==outcome.second);
            CHECK(assertions==outcome.failures);
        }
    }
}

union XAssetHeader { GfxImage *image; };
int releases;
int reloads;
int defaults;
int rebuilds;
int errors;
bool reloadOK;
bool defaultOK;
bool prog;
void Image_Release(GfxImage *image) { CHECK(image==expectedImage); ++releases; }
char Image_ReloadFromFile(GfxImage *) { ++reloads; return reloadOK; }
char Image_AssignDefaultTexture(GfxImage *) { ++defaults; return defaultOK; }
bool Image_IsProg(GfxImage *) { return prog; }
void Image_Rebuild(GfxImage *) { ++rebuilds; }
void Com_PrintError(int, const char *, ...) { ++errors; }
#include "image_release_body.inc"
#include "image_recovery_body.inc"
void CheckFailedRecovery(const GfxImage &image, bool failed)
{
    CHECK(reloads==int(image.category==3 && !image.delayLoadPixels));
    CHECK(defaults==reloads);
    CHECK(rebuilds==int(image.category>=5 && !prog));
    CHECK(failed==(image.category<5 && !(image.category==3 && image.delayLoadPixels)));
    CHECK(errors==int(failed));
}
void TestCategoryFailures()
{
    GfxImage image{}; image.name="test"; expectedImage=&image;
    XAssetHeader asset{};
    asset.image=&image;
    for (int category=1; category<=7; ++category) {
        image.category=static_cast<uint8_t>(category);
        releases=0; R_FreeLostImage(asset,nullptr);
        CHECK(releases==int(category>=5));
        for (bool delayed : {false,true}) {
            for (bool isProg : {false,true}) {
                image.delayLoadPixels=delayed; prog=isProg;
                reloadOK=defaultOK=false; reloads=defaults=rebuilds=errors=0;
                bool failed=false;
                R_RebuildLostImage(asset,&failed);
                CheckFailedRecovery(image,failed);
            }
        }
    }
    expectedImage=nullptr;
}
void TestCategoryRecovery()
{
    GfxImage image{}; image.name="test"; expectedImage=&image;
    XAssetHeader asset{};
    asset.image=&image;
    image.category=3; image.delayLoadPixels=false;
    for (bool successViaReload : {false,true}) {
        reloadOK=successViaReload; defaultOK=!successViaReload;
        reloads=defaults=errors=0; bool failed=false;
        R_RebuildLostImage(asset,&failed);
        CHECK(!failed && errors==0 && reloads==1 && defaults==int(!successViaReload));
    }
    expectedImage=nullptr;
}
} // namespace
void RunRendererImageContracts()
{
    TestFormatDispatch();
    TestBitmapPixels();
    TestFlags();
    TestSemantics();
    TestCategoryFailures();
    TestCategoryRecovery();
}
