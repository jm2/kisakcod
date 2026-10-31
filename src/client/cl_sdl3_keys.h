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

// A Unicode code point from SDL text input -> the CP1252 byte an ANSI window's
// WM_CHAR would carry, or 0 when CP1252 has no printable character for it.
uint32_t CL_SdlCodepointToCp1252(uint32_t codepoint);

// The reverse, for text the engine hands back (the clipboard): a CP1252 byte
// -> its Unicode code point, or 0 for a control or unassigned byte.
uint32_t CL_SdlCp1252ToCodepoint(uint8_t byte);

// The SDL_Keymod bits the fullscreen toggle reads (SDL_keycode.h).
constexpr uint32_t kSdlModAlt = 0x0100u | 0x0200u; // SDL_KMOD_LALT | SDL_KMOD_RALT
constexpr uint32_t kSdlModGui = 0x0400u | 0x0800u; // SDL_KMOD_LGUI | SDL_KMOD_RGUI

// Whether a key press is the fullscreen toggle: Return (scancode 40) with
// Command on macOS (apple), the Mac convention, and with Alt elsewhere, as
// Win32's WM_SYSKEYDOWN. Such a press is the toggle only, never a game key.
bool CL_SdlIsFullscreenToggle(uint32_t scancode, uint32_t mods, bool apple);
