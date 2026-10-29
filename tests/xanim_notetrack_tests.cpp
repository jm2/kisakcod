// xanim_notetrack_tests.cpp: ReadNoteTracks (production xanim/xanim_load_obj.cpp)
// on a raw xanim with the most notetracks its u8 count can hold. notifyCount
// is a u8 that also holds the end marker, so 255 tracks wrapped it to 0: a
// zero-byte allocation, then 256 notify records written into it.
//
// It also drives the production XModelPiecesLoadFile over an in-memory
// xmodelpieces file whose piece model does not register. The error message
// must name both the missing piece and the pieces file it came from.

#include <xanim/xanim.h>
#include <xanim/buf_cursor.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

void __cdecl ReadNoteTracks(const char *name, unsigned char **pos, XAnimParts *parts, void *(__cdecl *Alloc)(int));
XModelPieces *__cdecl XModelPiecesLoadFile(const char *name, void *(__cdecl *Alloc)(int));

namespace
{
size_t g_allocBytes = 0;
std::vector<void *> g_blocks;
std::vector<unsigned char> g_piecesFile;
std::string g_lastError;

void *__cdecl TestAlloc(int size)
{
    g_allocBytes = static_cast<size_t>(size);
    void *const block = std::calloc(1, g_allocBytes ? g_allocBytes : 1);
    g_blocks.push_back(block);
    return block;
}
}

uint32_t SL_GetString_(const char *, uint32_t, int)
{
    return 1;
}

int __cdecl FS_ReadFile(const char *qpath, void **buffer)
{
    if (std::strcmp(qpath, "xmodelpieces/broken") != 0)
    {
        *buffer = nullptr;
        return -1;
    }
    unsigned char *dest = static_cast<unsigned char *>(std::malloc(g_piecesFile.size()));
    if (!dest)
    {
        *buffer = nullptr;
        return -1;
    }
    for (size_t i = 0; i < g_piecesFile.size(); ++i)
        dest[i] = g_piecesFile[i];
    *buffer = dest;
    return static_cast<int>(g_piecesFile.size());
}

void __cdecl FS_FreeFile(char *buffer)
{
    std::free(buffer);
}

int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(dest, size, fmt, args);
    va_end(args);
    return written;
}

void Com_PrintError(int, const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    g_lastError = buffer;
}

XModel *__cdecl R_RegisterModel(const char *)
{
    return nullptr;
}

int main()
{
    // 255 notetracks, each "n\0" and a u16 frame.
    std::vector<unsigned char> file;
    file.push_back(255);
    for (int i = 0; i < 255; ++i)
    {
        file.push_back('n');
        file.push_back(0);
        file.push_back(1);
        file.push_back(0);
    }
    XAnimParts parts{};
    parts.numframes = 2;
    unsigned char *pos = file.data();
    buf_cursor::Activate(file.data(), file.size());
    buf_cursor::AnchorPos(&pos);
    ReadNoteTracks("test", &pos, &parts, TestAlloc);
    buf_cursor::Deactivate();

    const bool notetracksOk = parts.notifyCount == 255 && g_allocBytes == 255 * sizeof(XAnimNotifyInfo);
    // Each track sits on frame 1 of 2: time 0.5, as retail's float divide gives
    // (1.8 0x5850be-0x5850ca); the end marker is 1.
    const bool timesOk = parts.notify[0].time == 0.5f && parts.notify[253].time == 0.5f
        && parts.notify[254].time == 1.0f;

    // version 1, one piece, "missing_piece", then three offsets.
    g_piecesFile = {1, 0, 1, 0};
    const char piecename[] = "missing_piece";
    g_piecesFile.insert(g_piecesFile.end(), piecename, piecename + sizeof(piecename));
    for (int i = 0; i < 12; ++i)
        g_piecesFile.push_back(0);
    g_lastError.clear();
    const XModelPieces *missing = XModelPiecesLoadFile("broken", TestAlloc);
    const bool piecesOk = missing == nullptr
        && g_lastError == "ERROR: xmodel piece 'missing_piece' missing from pieces model 'xmodelpieces/broken'\n";

    for (void *block : g_blocks)
        std::free(block);
    if (!notetracksOk)
    {
        std::fprintf(stderr, "FAIL 255 notetracks: notifyCount %u, %zu bytes\n", parts.notifyCount, g_allocBytes);
        return 1;
    }
    if (!timesOk)
    {
        std::fputs("FAIL notetrack times are not frame / numframes in float\n", stderr);
        return 1;
    }
    if (!piecesOk)
    {
        std::fprintf(stderr, "FAIL pieces message: '%s'\n", g_lastError.c_str());
        return 1;
    }
    std::puts("xanim notetracks: ok");
    return 0;
}
