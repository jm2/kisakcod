// SPDX-License-Identifier: GPL-3.0
//
// MSVC-decompiled-dialect shim for the native64_game_mp_hazards production
// subjects (same constraint tests/cmake/xmodel_xanim.cmake documents for the
// loader TUs). Force-included ahead of each subject; it never changes a
// layout: __declspec(align(N)) maps to the equivalent GNU attribute and the
// trailing `align` macro only re-spells that one token. ui_shared.h also has
// an uninstantiated template calling a never-declared free function, and
// com_playerprofile.cpp has the only two host Win32 calls in that TU -- both
// seams are declared here so the subjects build on POSIX hosts, with
// definitions in native64_game_mp_hazards_test.cpp. MSVC needs none of this.
#pragma once

#if !defined(_MSC_VER)
#  undef __declspec
#  define __declspec(x) __attribute__((x))
#  undef align
#  define align(x) aligned(x)
inline bool IsValidSeed(int, int)
{
    return true;
}
typedef void *HWND;
HWND GetActiveWindow();
int MessageBoxA(HWND, const char *, const char *, unsigned int);
#endif
