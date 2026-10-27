#pragma once
#include <cstring>

// Encoded HUD-icon command shared by the console/death-message writer
// (src/client/cl_console.cpp) and the render-thread reader
// (src/gfx_d3d/rb_backend.cpp).
//
// After the '^' introducer the record is:
//   [0] command type (CONTXTCMD_TYPE_HUDICON / _FLIP)
//   [1] encoded width
//   [2] encoded height
//   [3] material handle, sizeof(void *) bytes
//
// The handle is copied with memcpy because the record is unaligned inside the
// console text buffer, and its width follows the pointer width of the build:
// a 7-byte record on 32-bit (the original layout, byte-identical) and an
// 11-byte record on 64-bit.  Reading the handle as a Material* at a fixed
// offset 3 on a 64-bit build loaded 8 bytes out of a 7-byte record and walked
// off the end of the command; that is issue #847.
namespace hudicon
{
constexpr int kTypeOffset = 0;
constexpr int kWidthOffset = 1;
constexpr int kHeightOffset = 2;
constexpr int kHandleOffset = 3;
constexpr int kHandleBytes = static_cast<int>(sizeof(void *));
constexpr int kRecordBytes = kHandleOffset + kHandleBytes;
constexpr int kIntroducerBytes = 1; // '^'
constexpr int kEncodedBytes = kIntroducerBytes + kRecordBytes;

// Material handles are passed around as `Material *` in this port, so the
// record carries a pointer-width value on every platform.
// Both helpers take the address of the handle field itself (record +
// kHandleOffset), so a caller cannot double-apply the offset.
inline void WriteHandle(char *handleField, const void *handle)
{
    std::memcpy(handleField, &handle, sizeof(handle));
}

inline void *ReadHandle(const char *handleField)
{
    void *handle = nullptr;
    std::memcpy(&handle, handleField, sizeof(handle));
    return handle;
}
}
