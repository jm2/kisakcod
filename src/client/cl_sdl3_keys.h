#pragma once

#include <cstdint>

// SDL3 keyboard -> engine keyNum_t mapping for the SDL3 client backend
// (KISAK_CLIENT_SDL3, docs/design/CLIENT.md). It reproduces what
// win32/win_wndproc.cpp's MapKey hands Sys_QueEvent for the same physical key,
// so key bindings and the console behave the same on every platform.
//
// The scancode is an SDL_Scancode, whose values are USB HID keyboard usage IDs
// (a stable part of the SDL3 ABI). The keycode is the SDL_Keycode for the
// current layout: for a printable key it is the unshifted character, which is
// what MapVirtualKeyA(vk, MAPVK_VK_TO_CHAR) gives the Win32 path. Neither needs
// SDL headers, so the mapping builds and is tested on every target.

// keypadAsText is true while the console or a UI text field has focus
// (keyCatchers & 0x11). The Win32 path then drops keypad digit keys so they
// arrive only as characters; numLock says whether they are digits at all.
int32_t CL_SdlMapKey(uint32_t scancode, uint32_t keycode, bool keypadAsText, bool numLock);

// SDL_MouseButtonFlags (SDL_BUTTON_LMASK etc.) -> the bit layout
// IN_MouseEvent takes from WM_*BUTTON* wParam: 1 left, 2 right, 4 middle,
// 8 X1, 16 X2.
int32_t CL_SdlMapMouseButtons(uint32_t sdlButtons);
