#pragma once

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
