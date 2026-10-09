#include "cl_sdl3_keys.h"

#include <ui/keycodes.h>

namespace
{
// SDL_Scancode values (USB HID usage IDs). cl_sdl3.cpp static_asserts these
// against SDL's own enum, so a drift fails the SDL3 build.
enum : uint32_t
{
    SC_RETURN = 40, SC_ESCAPE = 41, SC_BACKSPACE = 42, SC_TAB = 43, SC_SPACE = 44,
    SC_GRAVE = 53, SC_CAPSLOCK = 57, SC_F1 = 58, SC_F12 = 69, SC_PAUSE = 72,
    SC_INSERT = 73, SC_HOME = 74, SC_PAGEUP = 75, SC_DELETE = 76, SC_END = 77,
    SC_PAGEDOWN = 78, SC_RIGHT = 79, SC_LEFT = 80, SC_DOWN = 81, SC_UP = 82,
    SC_NUMLOCKCLEAR = 83, SC_KP_DIVIDE = 84, SC_KP_MULTIPLY = 85, SC_KP_MINUS = 86,
    SC_KP_PLUS = 87, SC_KP_ENTER = 88, SC_KP_1 = 89, SC_KP_0 = 98, SC_KP_PERIOD = 99,
    SC_KP_EQUALS = 103, SC_LCTRL = 224, SC_LSHIFT = 225, SC_LALT = 226,
    SC_RCTRL = 228, SC_RSHIFT = 229, SC_RALT = 230,
};

// Keypad 1..9 then 0. With Num Lock on Windows sends VK_NUMPADn; with it off,
// the non-extended navigation VKs. The off column keeps retail's 9 -> RIGHTARROW
// (virtualKeyConvert[VK_PRIOR][0]) so bindings match the Win32 build.
constexpr int32_t kKeypadNumLock[10] = {
    K_KP_END, K_KP_DOWNARROW, K_KP_PGDN, K_KP_LEFTARROW, K_KP_5,
    K_KP_RIGHTARROW, K_KP_HOME, K_KP_UPARROW, K_KP_PGUP, K_KP_INS,
};
constexpr int32_t kKeypadNoNumLock[10] = {
    K_KP_END, K_KP_DOWNARROW, K_KP_PGDN, K_KP_LEFTARROW, K_KP_5,
    K_KP_RIGHTARROW, K_KP_HOME, K_KP_UPARROW, K_KP_RIGHTARROW, K_KP_INS,
};

// win_wndproc.cpp's extendedVirtualKeyConvert: Latin-1 characters from
// European layouts that have a K_ASCII_* key. SDL keycodes are Unicode, which
// agrees with the CP1252 byte MapVirtualKeyA returns in this range.
constexpr uint8_t kLatin1Keys[][2] = {
    { 0xB5, K_ASCII_181 }, { 0xBF, K_ASCII_191 }, { 0xDF, K_ASCII_223 },
    { 0xE0, K_ASCII_224 }, { 0xE1, K_ASCII_225 }, { 0xE4, K_ASCII_228 },
    { 0xE5, K_ASCII_229 }, { 0xE6, K_ASCII_230 }, { 0xE7, K_ASCII_231 },
    { 0xE8, K_ASCII_232 }, { 0xE9, K_ASCII_233 }, { 0xEC, K_ASCII_236 },
    { 0xF1, K_ASCII_241 }, { 0xF2, K_ASCII_242 }, { 0xF3, K_ASCII_243 },
    { 0xF6, K_ASCII_246 }, { 0xF8, K_ASCII_248 }, { 0xF9, K_ASCII_249 },
    { 0xFA, K_ASCII_250 }, { 0xFC, K_ASCII_252 },
};

int32_t MapScancode(uint32_t scancode, bool keypadAsText, bool numLock)
{
    if (scancode >= SC_F1 && scancode <= SC_F12)
        return K_F1 + static_cast<int32_t>(scancode - SC_F1);
    if (scancode >= SC_KP_1 && scancode <= SC_KP_0)
    {
        if (keypadAsText && numLock)
            return 0;
        return (numLock ? kKeypadNumLock : kKeypadNoNumLock)[scancode - SC_KP_1];
    }

    switch (scancode)
    {
    case SC_RETURN: return K_ENTER;
    case SC_ESCAPE: return K_ESCAPE;
    case SC_BACKSPACE: return K_BACKSPACE;
    case SC_TAB: return K_TAB;
    case SC_SPACE: return K_SPACE;
    // MapKey turns hardware scan code 0x29 into the console key on any layout.
    case SC_GRAVE: return '~';
    case SC_CAPSLOCK: return K_CAPSLOCK;
    case SC_PAUSE: return K_PAUSE;
    case SC_INSERT: return K_INS;
    case SC_HOME: return K_HOME;
    case SC_PAGEUP: return K_PGUP;
    case SC_DELETE: return K_DEL;
    case SC_END: return K_END;
    case SC_PAGEDOWN: return K_PGDN;
    case SC_RIGHT: return K_RIGHTARROW;
    case SC_LEFT: return K_LEFTARROW;
    case SC_DOWN: return K_DOWNARROW;
    case SC_UP: return K_UPARROW;
    case SC_NUMLOCKCLEAR: return K_KP_NUMLOCK;
    case SC_KP_DIVIDE: return K_KP_SLASH;
    case SC_KP_MULTIPLY: return K_KP_STAR;
    case SC_KP_MINUS: return K_KP_MINUS;
    case SC_KP_PLUS: return K_KP_PLUS;
    case SC_KP_ENTER: return K_KP_ENTER;
    case SC_KP_PERIOD: return keypadAsText && numLock ? 0 : K_KP_DEL;
    case SC_KP_EQUALS: return K_KP_EQUALS;
    case SC_LCTRL: case SC_RCTRL: return K_CTRL;
    case SC_LSHIFT: case SC_RSHIFT: return K_SHIFT;
    case SC_LALT: case SC_RALT: return K_ALT;
    default: return 0;
    }
}
} // namespace

int32_t CL_SdlMapKey(uint32_t scancode, uint32_t keycode, bool keypadAsText, bool numLock)
{
    const int32_t key = MapScancode(scancode, keypadAsText, numLock);
    if (key || (scancode >= SC_KP_1 && scancode <= SC_KP_PERIOD))
        return key;

    // Printable keys follow the layout, as MapVirtualKeyA does. SDL letter
    // keycodes are already lower case, which is what the Win32 table yields.
    if (keycode > 0x20 && keycode < 0x7F)
        return static_cast<int32_t>(keycode);
    if (keycode >= 0x80 && keycode <= 0xFF)
    {
        for (const auto &entry : kLatin1Keys)
        {
            if (entry[0] == keycode)
                return entry[1];
        }
        // Like MapKey, an unlisted Latin-1 character passes through unchanged.
        return static_cast<int32_t>(keycode);
    }
    return 0;
}

int32_t CL_SdlMapMouseButtons(uint32_t sdlButtons)
{
    // SDL_BUTTON_MASK(n) is 1 << (n - 1) with left = 1, middle = 2, right = 3,
    // X1 = 4, X2 = 5; the engine swaps middle and right.
    int32_t buttons = 0;
    if (sdlButtons & 0x01)
        buttons |= 0x01;
    if (sdlButtons & 0x04)
        buttons |= 0x02;
    if (sdlButtons & 0x02)
        buttons |= 0x04;
    if (sdlButtons & 0x08)
        buttons |= 0x08;
    if (sdlButtons & 0x10)
        buttons |= 0x10;
    return buttons;
}
