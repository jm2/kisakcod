#pragma once

#include <cstddef>
#include <cstdint>

// r_screenshot_rect.h: where a screenshot's pixels sit in the surface that
// GetFrontBufferData fills, when that surface is the swapchain image rather
// than the desktop. Windows D3D9 copies the whole screen, so r_screenshot.cpp
// finds the window on its monitor. dxvk-native copies the swapchain's image
// (the backbuffer, client-sized) to the destination's top-left (dxvk 3.1.1,
// D3D9SwapChainEx::GetFrontBufferData), so the window's screen position plays
// no part.

struct ScreenshotSourceRect
{
    int surfaceWidth;
    int surfaceHeight;
    int left;
    int top;
    int right;
    int bottom;
};

// The scratch surface is the backbuffer's size, and the requested rect must
// lie inside it. Fails on an empty, negative or out-of-bounds request.
constexpr bool R_SwapchainScreenshotRect(
    int x,
    int y,
    int width,
    int height,
    int backBufferWidth,
    int backBufferHeight,
    ScreenshotSourceRect *out)
{
    if (width <= 0 || height <= 0 || x < 0 || y < 0 || backBufferWidth <= 0 || backBufferHeight <= 0
        || x > backBufferWidth - width || y > backBufferHeight - height)
    {
        return false;
    }
    *out = { backBufferWidth, backBufferHeight, x, y, x + width, y + height };
    return true;
}

// The TGA a screenshot writes: an 18-byte header, then width * height BGR
// pixels, top row first (descriptor 0x20), as R_TakeScreenshotTga lays it out.
constexpr std::uint64_t R_ScreenshotTgaBytes(int width, int height)
{
    return width <= 0 || height <= 0 ? 0 : 18u + 3u * static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
}

// Packs a locked 32-bit BGRA/BGRX surface (rows `pitch` bytes apart) into that
// TGA. Fails on a null buffer, a size TGA can't hold, a pitch shorter than a
// row, or an output smaller than R_ScreenshotTgaBytes.
inline bool R_PackScreenshotTga(
    const std::uint8_t *bgra,
    int pitch,
    int width,
    int height,
    std::uint8_t *out,
    std::size_t outBytes)
{
    const std::uint64_t needed = R_ScreenshotTgaBytes(width, height);
    if (!bgra || !out || !needed || width > 0xFFFF || height > 0xFFFF || pitch < 4 * width || outBytes < needed)
        return false;
    for (int i = 0; i < 18; ++i)
        out[i] = 0;
    out[2] = 2; // uncompressed true colour
    out[12] = static_cast<std::uint8_t>(width & 0xFF);
    out[13] = static_cast<std::uint8_t>(width >> 8);
    out[14] = static_cast<std::uint8_t>(height & 0xFF);
    out[15] = static_cast<std::uint8_t>(height >> 8);
    out[16] = 24;
    out[17] = 32; // top row first
    std::uint8_t *dst = out + 18;
    for (int row = 0; row < height; ++row)
    {
        const std::uint8_t *src = bgra + static_cast<std::size_t>(row) * static_cast<std::size_t>(pitch);
        for (int col = 0; col < width; ++col)
        {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            src += 4;
            dst += 3;
        }
    }
    return true;
}
