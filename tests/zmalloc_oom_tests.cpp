// zmalloc_oom_tests.cpp: Z_Malloc and Z_MallocGarbage (universal/com_memory.cpp)
// on allocation failure. Retail 1.8 never returns NULL from them: every
// inlined allocation site tail-jumps to Z_MallocFailed (0x0055DDD0), which
// prints the size and calls Sys_OutOfMemErrorInternal. The port had the
// guard commented out, so callers dereferenced NULL instead (#208 item 1).
//
// A negative size becomes a huge size_t that malloc refuses. The engine
// boundary is weak: --gc-sections drops what the checks never reach.

#include <cstdarg>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <qcommon/qcommon.h>
#include <qcommon/sys_error.h>
#include <universal/com_memory.h>

#define WEAK __attribute__((weak))

// Let malloc fail with NULL under ASan instead of reporting the size.
extern "C" const char *__asan_default_options() { return "allocator_may_return_null=1"; }

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

std::jmp_buf g_outOfMem;
int g_outOfMemCalls = 0;
char g_printed[256];
} // namespace

// Engine boundary.
WEAK void KISAK_CDECL Sys_OutOfMemErrorInternal(const char *, int)
{
    ++g_outOfMemCalls;
    std::longjmp(g_outOfMem, 1);
}
WEAK void Com_PrintError(int, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(g_printed, sizeof(g_printed), fmt, args);
    va_end(args);
}
WEAK void Com_Memset(void *dest, int value, size_t count) { std::memset(dest, value, count); }
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    ++g_failures;
}

namespace
{
// Returns true when the allocation reached the out-of-memory fatal.
template <typename Alloc> bool ReachesOutOfMem(Alloc alloc)
{
    g_outOfMemCalls = 0;
    g_printed[0] = 0;
    if (setjmp(g_outOfMem) == 0)
    {
        void *volatile buf = alloc();
        std::free(buf);
        return false;
    }
    return g_outOfMemCalls == 1;
}
} // namespace

int main()
{
    CHECK(ReachesOutOfMem([] { return Z_Malloc(-64, "test", 0); }));
    CHECK(std::strcmp(g_printed, "Failed to Z_Malloc -64 bytes\n") == 0);
    CHECK(ReachesOutOfMem([] { return static_cast<void *>(Z_MallocGarbage(-64, "test", 0)); }));

    // A normal allocation still succeeds, zeroed for Z_Malloc.
    unsigned char *buf = static_cast<unsigned char *>(Z_Malloc(32, "test", 0));
    CHECK(buf != nullptr);
    if (buf)
    {
        bool zero = true;
        for (int i = 0; i < 32; ++i)
            zero = zero && buf[i] == 0;
        CHECK(zero);
        Z_Free(buf, 0);
    }

    if (g_failures)
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
    else
        std::puts("zmalloc-oom: all checks passed");
    return g_failures ? 1 : 0;
}
