#pragma once
#include <cstdint>
#include <cstring>

// Layout of pointers and static-model groups in the prim draw-surf stream
// (GfxBackEndData::primDrawSurfsBuf). The stream is a dword array that lives
// in one frame's delayed command buffers; it is never on disk or on the wire.
//
// A pointer takes kPrimDrawSurfPtrDwords dwords: one on x86, two on 64-bit.
// The stream is only 4-byte aligned, so pointers go in and out via memcpy.

struct XSurface;
struct GfxDelayedCmdBuf;

constexpr uint32_t kPrimDrawSurfPtrDwords = sizeof(void *) / sizeof(uint32_t);

inline void R_StorePrimDrawSurfPtr(uint32_t *dst, const void *ptr)
{
    std::memcpy(dst, &ptr, sizeof(ptr));
}

// Writes ptr at the delayed command buffer's stream position (r_add_cmdbuf.cpp).
void __cdecl R_WritePrimDrawSurfPtr(GfxDelayedCmdBuf *delayedCmdBuf, const void *ptr);

inline const void *R_ReadPrimDrawSurfPtr(const uint32_t *&pos)
{
    const void *ptr;
    std::memcpy(&ptr, pos, sizeof(ptr));
    pos += kPrimDrawSurfPtrDwords;
    return ptr;
}

// A static-model group is its smodel count, its XSurface pointer and the
// count uint16_t smodel indices packed two per dword. A zero count ends the
// stream.
constexpr uint32_t R_StaticModelGroupDwords(uint32_t count)
{
    return 1 + kPrimDrawSurfPtrDwords + ((count + 1) >> 1);
}

// Reads the group at pos and advances past it. Returns the group's surface,
// or null at the terminating zero count (pos then points past it).
inline XSurface *R_ReadStaticModelGroup(const uint32_t *&pos, uint32_t *count, const uint16_t **list)
{
    *count = *pos++;
    if (!*count)
        return nullptr;
    XSurface *xsurf = static_cast<XSurface *>(const_cast<void *>(R_ReadPrimDrawSurfPtr(pos)));
    *list = reinterpret_cast<const uint16_t *>(pos);
    pos += (*count + 1) >> 1;
    return xsurf;
}
