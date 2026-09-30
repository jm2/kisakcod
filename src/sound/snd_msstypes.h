#pragma once

// Miles Sound System types the shared sound headers hold. Miles ships here
// only as a 32-bit Windows DLL, and msslib/mss.h detects no 64-bit Mac
// (it #errors on macOS arm64). Nothing off Windows calls Miles: the sound
// driver and cinematics TUs are Windows client code. So on every other
// platform these declarations keep snd_public.h and snd_local.h free of the
// Miles SDK, the way gfx_d3d/r_d3d9types.h stands in for <d3d9.h>.

#if defined(_WIN32)

#include <msslib/mss.h>

#else

#include <cstdint>

// Opaque driver handles: the shared headers only hold these by pointer.
struct _SAMPLE;
struct _DIG_DRIVER;
struct _STREAM;

// The file callback types in the MSS_File*Callback declarations.
typedef char MSS_FILE;
typedef std::uintptr_t UINTa;

#endif
