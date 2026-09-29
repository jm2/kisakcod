// SPDX-License-Identifier: GPL-3.0
//
// MSVC-decompiled-dialect shim for the native64_game_mp_hazards subjects;
// force-included first and never changes a layout. Declares the template and
// host-Win32 seams the subjects need on POSIX hosts (see PR body).
#pragma once

#if !defined(_MSC_VER)
#  if !defined(__clang__)
     // gcc needs the MSVC alignment spellings rewritten. clang builds pass
     // -fdeclspec and read __declspec(align(N)) natively, and an `align(x)`
     // function-like macro would collide with libc++'s std::align declaration
     // (__memory/align.h) the moment a standard header is included.
#    undef __declspec
#    define __declspec(x) __attribute__((x))
#    undef align
#    define align(x) aligned(x)
#  endif
#  if defined(__APPLE__) && !defined(IS_LE) && !defined(IS_BE)
     // mss.h's legacy platform detection knows 68k/PPC/i386 Macs only, so
     // arm64/x86_64 Apple hosts fall through with neither endianness set and
     // every DXDEC declaration fails. Apple targets here are little-endian.
#    define IS_LE 1
#  endif
inline bool IsValidSeed(int, int) { return true; }
typedef void *HWND;
HWND GetActiveWindow();
int MessageBoxA(HWND, const char *, const char *, unsigned int);
#endif
