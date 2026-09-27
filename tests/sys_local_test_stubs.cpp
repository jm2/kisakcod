// Link stubs for the sys_local portable-surface contract test target.
//
// The contract tests link src/universal/win_common.cpp directly, the same way
// the engine links it: it is the production implementation of the portable
// Sys_* path entry points (Sys_Mkdir, Sys_Cwd, Sys_CountFileList, ...) that
// qcommon/sys_local.h declares, so the link itself proves the split kept the
// declarations and definitions in agreement, and the tests execute that engine
// code for real. That translation unit references the engine's zone allocator
// (Hunk_User*) for its Sys_ListFiles listing path and, on the Windows branch,
// Com_sprintf. None of that environment is under test, so it is provided here
// as stand-ins: the production code under test is NOT stubbed, only its unused
// surroundings.
//
// This file includes the engine headers directly so any signature drift
// between the stubs and the real declarations fails the build instead of
// linking wrong.

#include <universal/com_memory.h>
#include <universal/platform_compat.h>
#include <universal/q_shared.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{
// A malloc-backed arena standing in for the engine's HunkUser. The engine
// type is only ever used as an identity handle through the Hunk_User* API,
// so the real bookkeeping lives here and the handle is a reinterpreted
// pointer to it.
struct StubHunkArena
{
    std::vector<void *> blocks;
};

void *AlignedBlock(const std::size_t size, const int alignment)
{
    std::size_t align = static_cast<std::size_t>(alignment);
    if (align < alignof(void *))
        align = alignof(void *);
    if ((align & (align - 1)) != 0)
        return nullptr;
    const std::size_t rounded = (size + align - 1) & ~(align - 1);
#if defined(_WIN32)
    return _aligned_malloc(rounded, align);
#else
    void *block = nullptr;
    if (posix_memalign(&block, align, rounded != 0 ? rounded : align) != 0)
        return nullptr;
    return block;
#endif
}

void FreeBlock(void *block)
{
#if defined(_WIN32)
    _aligned_free(block);
#else
    free(block);
#endif
}
} // namespace

HunkUser *__cdecl Hunk_UserCreate(int maxSize, const char *name, bool fixed, bool tempMem, int type)
{
    (void)maxSize;
    (void)name;
    (void)fixed;
    (void)tempMem;
    (void)type;
    return reinterpret_cast<HunkUser *>(new StubHunkArena());
}

void *Hunk_UserAlloc(HunkUser *user, uint32_t size, int alignment)
{
    StubHunkArena *arena = reinterpret_cast<StubHunkArena *>(user);
    if (!arena)
        return nullptr;
    void *block = AlignedBlock(size, alignment);
    if (block)
        arena->blocks.push_back(block);
    return block;
}

void __cdecl Hunk_UserDestroy(HunkUser *user)
{
    StubHunkArena *arena = reinterpret_cast<StubHunkArena *>(user);
    if (!arena)
        return;
    for (void *block : arena->blocks)
        FreeBlock(block);
    delete arena;
}

int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int written = std::vsnprintf(dest, size, fmt, ap);
    va_end(ap);
    // Keep the engine's MSVC truncation contract: -1 when the output does not
    // fit (see msvc_printf_shim.h).
    if (size != 0 && written >= 0 && static_cast<uint32_t>(written) >= size)
        return -1;
    return written;
}
