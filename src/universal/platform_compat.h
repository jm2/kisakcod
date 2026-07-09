#pragma once

// Compiler spelling compatibility. This does not make Win32 APIs portable; it
// only lets shared engine code express ABI and alignment requirements without
// depending directly on MSVC extensions.
#if defined(_MSC_VER)
#define KISAK_CDECL __cdecl
#define KISAK_STDCALL __stdcall
#define KISAK_FASTCALL __fastcall
#define KISAK_ALIGNAS(bytes) __declspec(align(bytes))
#else
#define KISAK_CDECL
#define KISAK_STDCALL
#define KISAK_FASTCALL
#define KISAK_ALIGNAS(bytes) alignas(bytes)

#ifndef __cdecl
#define __cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __thiscall
#define __thiscall
#endif
#endif
