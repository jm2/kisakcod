// xanim_notetrack_tests.cpp: ReadNoteTracks (production xanim/xanim_load_obj.cpp)
// on a raw xanim with the most notetracks its u8 count can hold. notifyCount
// is a u8 that also holds the end marker, so 255 tracks wrapped it to 0: a
// zero-byte allocation, then 256 notify records written into it.

#include <xanim/xanim.h>
#include <xanim/buf_cursor.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

void __cdecl ReadNoteTracks(const char *name, unsigned char **pos, XAnimParts *parts, void *(__cdecl *Alloc)(int));

namespace
{
size_t g_allocBytes = 0;
std::vector<void *> g_blocks;

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

    const bool ok = parts.notifyCount == 255 && g_allocBytes == 255 * sizeof(XAnimNotifyInfo);
    for (void *block : g_blocks)
        std::free(block);
    if (!ok)
    {
        std::fprintf(stderr, "FAIL 255 notetracks: notifyCount %u, %zu bytes\n", parts.notifyCount, g_allocBytes);
        return 1;
    }
    std::puts("xanim notetracks: ok");
    return 0;
}
