#include <client/cl_sdl3_keys.h>
#include <ui/keycodes.h>

#include <cstdio>

// The values here are what win32/win_wndproc.cpp's MapKey queues for the same
// physical key, so a binding made on Windows means the same key under SDL3.
// Scancodes are SDL_Scancode (USB HID usage) numbers; keycodes are the
// SDL_Keycode a US layout reports unless the case names another layout.
namespace
{
int failures = 0;

void Expect(int32_t actual, int32_t expected, const char *what)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "%s: got %d, expected %d\n", what, actual, expected);
        ++failures;
    }
}

int32_t Key(uint32_t scancode, uint32_t keycode, bool keypadAsText = false, bool numLock = true)
{
    return CL_SdlMapKey(scancode, keycode, keypadAsText, numLock);
}
} // namespace

int main()
{
    Expect(Key(4, 'a'), 'a', "A key gives lower-case a");
    Expect(Key(20, 'a'), 'a', "AZERTY: the Q position follows the layout");
    Expect(Key(30, '1'), '1', "digit row");
    Expect(Key(51, ';'), ';', "punctuation follows the layout");
    Expect(Key(53, '`'), '~', "the scan-code-0x29 key is the console key");
    Expect(Key(53, 0xB2), '~', "console key on a French layout");
    Expect(Key(40, '\r'), K_ENTER, "Enter");
    Expect(Key(88, 0x40000058), K_KP_ENTER, "keypad Enter");
    Expect(Key(42, '\b'), K_BACKSPACE, "Backspace is 127, not 8");
    Expect(Key(41, 0x1B), K_ESCAPE, "Escape");
    Expect(Key(43, '\t'), K_TAB, "Tab");
    Expect(Key(44, ' '), K_SPACE, "Space");
    Expect(Key(58, 0x4000003A), K_F1, "F1");
    Expect(Key(69, 0x40000045), K_F12, "F12");
    Expect(Key(104, 0x40000068), 0, "F13 has no Win32 mapping");
    Expect(Key(82, 0x40000052), K_UPARROW, "Up arrow");
    Expect(Key(75, 0x4000004B), K_PGUP, "Page Up");
    Expect(Key(76, 0x7F), K_DEL, "Delete is a scancode key, not char 127");
    Expect(Key(225, 0x400000E1), K_SHIFT, "left Shift");
    Expect(Key(229, 0x400000E5), K_SHIFT, "right Shift");
    Expect(Key(228, 0x400000E4), K_CTRL, "right Ctrl");
    Expect(Key(226, 0x400000E2), K_ALT, "left Alt");
    Expect(Key(227, 0x400000E3), 0, "Windows key has no binding");
    Expect(Key(83, 0x40000053), K_KP_NUMLOCK, "Num Lock");

    Expect(Key(97, 0x40000061), K_KP_PGUP, "keypad 9, Num Lock on");
    Expect(Key(97, 0x40000061, false, false), K_KP_RIGHTARROW, "keypad 9, Num Lock off keeps retail's VK_PRIOR entry");
    Expect(Key(98, 0x40000062), K_KP_INS, "keypad 0");
    Expect(Key(93, 0x4000005D), K_KP_5, "keypad 5");
    Expect(Key(99, 0x40000063), K_KP_DEL, "keypad period");
    Expect(Key(93, 0x4000005D, true, true), 0, "keypad digit is text while typing");
    Expect(Key(99, 0x40000063, true, true), 0, "keypad period is text while typing");
    Expect(Key(93, 0x4000005D, true, false), K_KP_5, "keypad 5 with Num Lock off is a key while typing");
    Expect(Key(84, 0x40000054, true, true), K_KP_SLASH, "keypad slash is not Num Lock dependent");

    Expect(Key(51, 0xF6), K_ASCII_246, "German o-umlaut");
    Expect(Key(45, 0xDF), K_ASCII_223, "German sharp s");
    Expect(Key(52, 0xA7), 0xA7, "unlisted Latin-1 passes through");
    Expect(Key(0, 0x20AC), 0, "a character above Latin-1 has no key");

    Expect(CL_SdlMapMouseButtons(0x01), 0x01, "left");
    Expect(CL_SdlMapMouseButtons(0x02), 0x04, "middle");
    Expect(CL_SdlMapMouseButtons(0x04), 0x02, "right");
    Expect(CL_SdlMapMouseButtons(0x08 | 0x10), 0x18, "X1 and X2");
    Expect(CL_SdlMapMouseButtons(0x1F), 0x1F, "all five");

    Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252('a')), 'a', "ASCII is unchanged");
    Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252(0xE9)), 0xE9, "Latin-1 e-acute is unchanged");
    Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252(0x20AC)), 0x80, "euro sign is CP1252 0x80");
    Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252(0x2019)), 0x92, "right single quote is CP1252 0x92");
    Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252(0x0178)), 0x9F, "Y-diaeresis is CP1252 0x9F");
    Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252(0x81)), 0, "C1 control has no character");
    Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252(0x7F)), 0, "DEL has no character");
    Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252(0x1F600)), 0, "an emoji has no CP1252 byte");

    Expect(static_cast<int32_t>(CL_SdlCp1252ToCodepoint('a')), 'a', "ASCII back is unchanged");
    Expect(static_cast<int32_t>(CL_SdlCp1252ToCodepoint(0xE9)), 0xE9, "Latin-1 back is unchanged");
    Expect(static_cast<int32_t>(CL_SdlCp1252ToCodepoint(0x80)), 0x20AC, "CP1252 0x80 is the euro sign");
    Expect(static_cast<int32_t>(CL_SdlCp1252ToCodepoint(0x92)), 0x2019, "CP1252 0x92 is the right single quote");
    Expect(static_cast<int32_t>(CL_SdlCp1252ToCodepoint(0x81)), 0, "an unassigned byte has no code point");
    Expect(static_cast<int32_t>(CL_SdlCp1252ToCodepoint(0x09)), 0, "a control byte has no code point");
    for (uint32_t byte = 0x20; byte < 0x100; ++byte)
    {
        const uint32_t cp = CL_SdlCp1252ToCodepoint(static_cast<uint8_t>(byte));
        if (cp && CL_SdlCodepointToCp1252(cp) != byte)
            Expect(static_cast<int32_t>(CL_SdlCodepointToCp1252(cp)), static_cast<int32_t>(byte), "CP1252 round trip");
    }

    if (failures)
        return 1;
    std::puts("client-sdl3-keys: all checks passed");
    return 0;
}
