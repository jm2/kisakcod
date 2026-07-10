#include "r_water.h"
#include <database/db_validation.h>
#include <qcommon/mem_track.h>
#include "r_image.h"
#include <universal/fft.h>
#include "r_dvars.h"
#include <qcommon/qcommon.h>
#include <universal/profile.h>

#include <algorithm>
#include <cmath>

WaterGlob waterGlob;
WaterGlobStatic waterGlobStatic;

//uint32_t const *const g_selectByteFromInt__uint4 820e3810     gfx_d3d : r_water.obj
//struct __vector4 const g_selectByteFromInt 85b45900     gfx_d3d : r_water.obj


void __cdecl TRACK_r_water()
{
    track_static_alloc_internal(&waterGlob, 36864, "waterGlob", 18);
}

void __cdecl R_UploadWaterTextureInternal(water_t **data)
{
    water_t *water; // [esp+68h] [ebp-4h]

    PROF_SCOPED("R_UploadWaterTextureInternal");

    water = *data;

    {
        PROF_SCOPED("FrequenciesAtTime");
        WaterFrequenciesAtTime(waterGlob.H, water, water->writable.floatTime);
    }
    {
        PROF_SCOPED("AmplitudesFromFrequencies");
        WaterAmplitudesFromFrequencies(waterGlob.H, water);
    }
    {
        PROF_SCOPED("PixelsFromAmplitudes");
        WaterPixelsFromAmplitudes((GfxColor *)waterGlob.pixels, waterGlob.H, water);
    }
    {
        PROF_SCOPED("GenerateMipMaps");
        GenerateMipMaps(D3DFMT_L8, waterGlob.pixels, water);
    }
}

void __cdecl WaterFrequenciesAtTime(complex_s *H, const water_t *water, float t)
{
    float sinReal; // [esp+28h] [ebp-1Ch]
    int vecKCount; // [esp+2Ch] [ebp-18h]
    int vecKIndex; // [esp+30h] [ebp-14h]
    float *wTerm; // [esp+34h] [ebp-10h]
    complex_s *H0; // [esp+38h] [ebp-Ch]
    float sinImag; // [esp+3Ch] [ebp-8h]

    iassert( H );
    iassert( water );
    vecKCount = water->N * water->M;
    iassert( (vecKCount <= (64 * 64)) );
    vecKIndex = 0;
    wTerm = water->wTerm;
    H0 = water->H0;
    while (vecKIndex < vecKCount)
    {
        if (*wTerm != 0.0f)
        {
            const double scaledTimeDouble =
                static_cast<double>(t) * 162.9746551513672;
            const float scaledTime = static_cast<float>(scaledTimeDouble);
            const float legacyPhase = *wTerm * scaledTime;
            const double phaseSource = std::isfinite(legacyPhase)
                ? static_cast<double>(legacyPhase)
                : static_cast<double>(*wTerm) * scaledTimeDouble;
            const double phase = std::fmod(phaseSource, 1024.0);
            if (!std::isfinite(phase))
            {
                H[vecKIndex].real = 0.0f;
                H[vecKIndex].imag = 0.0f;
                ++vecKIndex;
                ++wTerm;
                ++H0;
                continue;
            }
            const int64_t phaseIndex = static_cast<int64_t>(phase);
            const uint32_t tableIndex =
                static_cast<uint32_t>(phaseIndex) & UINT32_C(0x3FF);
            sinReal = waterGlobStatic.sinTable[(tableIndex + 255) & 0x3FF];
            sinImag = waterGlobStatic.sinTable[tableIndex];
            iassert(!IS_NAN(H0->real));
            iassert(!IS_NAN(H0->imag));
            iassert(!IS_NAN(sinReal));
            iassert(!IS_NAN(sinImag));

            H[vecKIndex].real = H0->real * sinReal;
            H[vecKIndex].imag = H0->imag * sinImag;

            iassert(!IS_NAN(H[vecKIndex].real));
            iassert(!IS_NAN(H[vecKIndex].imag));
        }
        else
        {
            H[vecKIndex].real = 0.0;
            H[vecKIndex].imag = 0.0;
        }
        ++vecKIndex;
        ++wTerm;
        ++H0;
    }
}

void __cdecl WaterAmplitudesFromFrequencies(complex_s *H, const water_t *water)
{
    int fftIndex; // [esp+0h] [ebp-Ch]
    int fftIndexa; // [esp+0h] [ebp-Ch]
    uint32_t log2_m; // [esp+4h] [ebp-8h]
    int waterIndex; // [esp+8h] [ebp-4h]
    int waterIndexa; // [esp+8h] [ebp-4h]

    iassert( H );
    iassert( water );
    iassert( water->M == water->N );
    for (log2_m = 0; water->M != 1 << log2_m; ++log2_m)
        ;
    for (waterIndex = 0; waterIndex < water->N; ++waterIndex)
    {
        fftIndex = water->M * waterIndex;
        iassert( fftIndex >= 0 );
        iassert( fftIndex < HCOUNT );
        FFT(&H[fftIndex], log2_m, waterGlobStatic.fftBitswap, waterGlobStatic.fftTrigTable);
    }
    TransposeArray(H, water->M);
    for (waterIndexa = 0; waterIndexa < water->M; ++waterIndexa)
    {
        fftIndexa = water->N * waterIndexa;
        iassert( fftIndexa >= 0 );
        iassert( fftIndexa < HCOUNT );
        FFT(&H[fftIndexa], log2_m, waterGlobStatic.fftBitswap, waterGlobStatic.fftTrigTable);
    }
    TransposeArray(H, water->M);
}

void __cdecl TransposeArray(complex_s *H, uint32_t M)
{
    uint32_t v2; // edx
    float real; // ecx
    float imag; // edx
    uint32_t v5; // eax
    uint32_t j; // [esp+4h] [ebp-10h]
    complex_s temp; // [esp+8h] [ebp-Ch]
    uint32_t i; // [esp+10h] [ebp-4h]

    for (i = 0; i < M; ++i)
    {
        for (j = 0; j < i; ++j)
        {
            temp = H[j + M * i];
            v2 = i + M * j;
            real = H[v2].real;
            imag = H[v2].imag;
            v5 = j + M * i;
            H[v5].real = real;
            H[v5].imag = imag;
            H[i + M * j] = temp;
        }
    }
}

void __cdecl WaterPixelsFromAmplitudes(GfxColor *pixels, complex_s *H, const water_t *water)
{
    iassert( pixels );
    iassert( H );
    iassert( water->M == water->N );
    const uint32_t count = water->N * water->M;
    iassert( (!(count & 3)) );
    const float normalization = static_cast<float>(
        1.0 / static_cast<double>(count));
    for (uint32_t sampleIndex = 0; sampleIndex < count; sampleIndex += 4)
    {
        GfxColor color = {};
        for (uint32_t component = 0; component < 4; ++component)
        {
            const complex_s &sample = H[sampleIndex + component];
            const float magnitudeSquared =
                sample.imag * sample.imag + sample.real * sample.real;
            const float magnitude = std::isfinite(magnitudeSquared)
                ? std::sqrt(magnitudeSquared)
                : std::hypot(sample.real, sample.imag);
            float intensity = magnitude * normalization;
            if (!std::isfinite(intensity))
                intensity = 1.0f;
            intensity = std::clamp(intensity, 0.0f, 1.0f);
            color.array[component] = static_cast<uint8_t>(
                intensity * 255.9989929199219);
        }
        pixels->packed = color.packed;
        ++pixels;
    }
}

void __cdecl GenerateMipMaps(_D3DFORMAT format, uint8_t *pixels, water_t *water)
{
    int srcWidth;
    uint32_t mipIndex;

    iassert(pixels);
    iassert(water);
    iassert(water->M == water->N);

    {
        PROF_SCOPED("UploadImage");
        Image_UploadData(water->image, D3DFMT_L8, D3DCUBEMAP_FACE_POSITIVE_X, 0, waterGlob.pixels);
    }

    srcWidth = water->M;
    mipIndex = 1;

    while (srcWidth > 1)
    {
        R_DownsampleMipMapBilinear(pixels, srcWidth, srcWidth, 1, pixels);
        {
            PROF_SCOPED("UploadImage");
            Image_UploadData(water->image, format, D3DCUBEMAP_FACE_POSITIVE_X, mipIndex, pixels);
        }
        srcWidth >>= 1;
        ++mipIndex;
    }
}

void __cdecl R_UploadWaterTexture(water_t *water, float floatTime)
{
    iassert( water );
    if (!water
        || !std::isfinite(floatTime)
        || !db::validation::WaterGridValid(water->M, water->N)
        || !water->H0
        || !water->wTerm
        || !water->image
        || water->image->mapType != MAPTYPE_2D
        || water->image->semantic != TS_WATER_MAP
        || water->image->category != IMG_CATEGORY_WATER
        || water->image->depth != 1
        || water->image->width != water->M
        || water->image->height != water->N)
    {
        Com_Error(ERR_DROP, "Invalid runtime material water upload");
        return;
    }
    if (floatTime != water->writable.floatTime)
    {
        water->writable.floatTime = floatTime;
        R_UploadWaterTextureInternal(&water);
    }
}

void __cdecl R_InitWater()
{
    float v0; // [esp+0h] [ebp-Ch]
    float v1; // [esp+4h] [ebp-8h]
    int tableIndex; // [esp+8h] [ebp-4h]

    for (tableIndex = 0; tableIndex < 1024; ++tableIndex)
    {
        v1 = (double)tableIndex * 0.3515625 * 0.01745329238474369;
        v0 = sin(v1);
        waterGlobStatic.sinTable[tableIndex] = v0;
    }
    FFT_Init(waterGlobStatic.fftBitswap, waterGlobStatic.fftTrigTable);
}

bool __cdecl Load_PicmipWater(water_t **waterRef)
{
    if (!waterRef || !*waterRef || !r_picmip_water || !r_loadForRenderer)
    {
        Com_Error(ERR_DROP, "Invalid material water picmip state");
        return false;
    }

    water_t *water = *waterRef;
    const int32_t sourceWidth = water->M;
    const int32_t sourceHeight = water->N;
    const int32_t picmipLevel = r_picmip_water->current.integer;
    const int32_t targetWidth =
        db::validation::WaterPicmipDimension(sourceWidth, picmipLevel);
    const int32_t targetHeight =
        db::validation::WaterPicmipDimension(sourceHeight, picmipLevel);
    if (!db::validation::WaterGridValid(sourceWidth, sourceHeight)
        || !targetWidth
        || targetWidth != targetHeight
        || !water->H0
        || !water->wTerm
        || !water->image)
    {
        Com_Error(ERR_DROP, "Invalid material water picmip input");
        return false;
    }

    const int32_t expectedImageWidth =
        r_loadForRenderer->current.enabled ? targetWidth : sourceWidth;
    const int32_t expectedImageHeight =
        r_loadForRenderer->current.enabled ? targetHeight : sourceHeight;
    if (water->image->mapType != MAPTYPE_2D
        || water->image->semantic != TS_WATER_MAP
        || water->image->category != IMG_CATEGORY_WATER
        || water->image->depth != 1
        || water->image->width != expectedImageWidth
        || water->image->height != expectedImageHeight)
    {
        Com_Error(ERR_DROP, "Invalid material water image contract");
        return false;
    }

    if (!db::validation::DownsampleWaterGridInPlace(
            water->H0,
            sourceWidth,
            targetWidth)
        || !db::validation::DownsampleWaterGridInPlace(
            water->wTerm,
            sourceWidth,
            targetWidth))
    {
        Com_Error(ERR_DROP, "Invalid material water picmip dimensions");
        return false;
    }

    water->M = targetWidth;
    water->N = targetHeight;
    return true;
}
