// renderer_screenshot_rect_tests.cpp: R_SwapchainScreenshotRect
// (gfx_d3d/r_screenshot_rect.h), which places a screenshot in the surface
// dxvk-native's GetFrontBufferData fills: the swapchain image at the top-left,
// so only the backbuffer's size bounds the request.

#include <gfx_d3d/r_screenshot_rect.h>

#include <climits>
#include <cstdint>
#include <cstdio>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char *what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool Rejects(int x, int y, int width, int height, int backWidth, int backHeight)
{
    ScreenshotSourceRect rect{ -1, -1, -1, -1, -1, -1 };
    const bool placed = R_SwapchainScreenshotRect(x, y, width, height, backWidth, backHeight, &rect);
    return !placed && rect.surfaceWidth == -1 && rect.left == -1;
}
} // namespace

// The whole frame, which is what the screenshot command asks for.
static_assert([] {
    ScreenshotSourceRect rect{};
    return R_SwapchainScreenshotRect(0, 0, 640, 480, 640, 480, &rect) && rect.surfaceWidth == 640
        && rect.surfaceHeight == 480 && rect.left == 0 && rect.top == 0 && rect.right == 640 && rect.bottom == 480;
}());

int main()
{
    ScreenshotSourceRect rect{};
    Expect(R_SwapchainScreenshotRect(0, 0, 1920, 1080, 1920, 1080, &rect)
               && rect.surfaceWidth == 1920 && rect.surfaceHeight == 1080 && rect.left == 0 && rect.top == 0
               && rect.right == 1920 && rect.bottom == 1080,
           "the whole frame maps to the whole backbuffer, at its origin");

    // A sub-rect keeps its own origin: the window's screen position never
    // enters, unlike the Win32 desktop path.
    Expect(R_SwapchainScreenshotRect(10, 20, 100, 50, 640, 480, &rect) && rect.surfaceWidth == 640
               && rect.surfaceHeight == 480 && rect.left == 10 && rect.top == 20 && rect.right == 110
               && rect.bottom == 70,
           "a sub-rect sits at its requested offset in a backbuffer-sized surface");
    Expect(R_SwapchainScreenshotRect(540, 380, 100, 100, 640, 480, &rect) && rect.right == 640 && rect.bottom == 480,
           "a sub-rect touching the far edges fits");

    Expect(Rejects(541, 0, 100, 10, 640, 480), "a rect past the right edge fails and leaves the output alone");
    Expect(Rejects(0, 381, 10, 100, 640, 480), "a rect past the bottom edge fails");
    Expect(Rejects(0, 0, 641, 480, 640, 480), "a rect wider than the backbuffer fails");
    Expect(Rejects(-1, 0, 10, 10, 640, 480), "a negative x fails");
    Expect(Rejects(0, -1, 10, 10, 640, 480), "a negative y fails");
    Expect(Rejects(0, 0, 0, 10, 640, 480), "an empty width fails");
    Expect(Rejects(0, 0, 10, 0, 640, 480), "an empty height fails");
    Expect(Rejects(0, 0, 10, 10, 0, 480), "a backbuffer without width fails");
    Expect(Rejects(INT_MAX, 0, 10, 10, 640, 480), "a huge x fails without overflowing");
    Expect(Rejects(0, 0, INT_MAX, 10, 640, 480), "a huge width fails without overflowing");

    // R_PackScreenshotTga: a 2x2 BGRX surface whose rows are 12 bytes apart
    // (4 bytes of padding) packs into an 18-byte header and 12 BGR bytes.
    const std::uint8_t surface[24] = {
        1, 2, 3, 0xFF, 4, 5, 6, 0xFF, 0xEE, 0xEE, 0xEE, 0xEE,     // row 0, then padding
        7, 8, 9, 0xFF, 10, 11, 12, 0xFF, 0xEE, 0xEE, 0xEE, 0xEE, // row 1, then padding
    };
    std::uint8_t tga[30] = {};
    Expect(R_ScreenshotTgaBytes(2, 2) == 30 && R_ScreenshotTgaBytes(0, 2) == 0, "a 2x2 TGA is 18 + 12 bytes");
    Expect(R_PackScreenshotTga(surface, 12, 2, 2, tga, sizeof(tga)), "a padded 2x2 surface packs");
    Expect(tga[2] == 2 && tga[12] == 2 && tga[13] == 0 && tga[14] == 2 && tga[15] == 0 && tga[16] == 24 && tga[17] == 32,
           "the header says uncompressed 24-bit 2x2, top row first");
    const std::uint8_t pixels[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    bool same = true;
    for (int i = 0; i < 12; ++i)
        same = same && tga[18 + i] == pixels[i];
    Expect(same, "each pixel keeps its B, G, R bytes in row order; alpha and row padding are dropped");

    std::uint8_t one[21] = {};
    const std::uint8_t magenta[4] = { 0xFF, 0x00, 0xFF, 0xFF };
    Expect(R_PackScreenshotTga(magenta, 4, 1, 1, one, sizeof(one)) && one[18] == 0xFF && one[19] == 0 && one[20] == 0xFF,
           "a 1x1 surface fills exactly 21 bytes");
    Expect(!R_PackScreenshotTga(magenta, 4, 1, 1, one, sizeof(one) - 1), "an output one byte short fails");
    Expect(!R_PackScreenshotTga(surface, 7, 2, 2, tga, sizeof(tga)), "a pitch shorter than a row fails");
    Expect(!R_PackScreenshotTga(nullptr, 12, 2, 2, tga, sizeof(tga)), "a null surface fails");
    Expect(!R_PackScreenshotTga(surface, 12, 0, 2, tga, sizeof(tga)), "an empty image fails");
    Expect(!R_PackScreenshotTga(surface, 4 * 0x10000, 0x10000, 1, tga, sizeof(tga)), "a width TGA can't hold fails");

    if (g_failures)
        return 1;
    std::puts("renderer screenshot rect: ok");
    return 0;
}
