// ui_string_alloc_tests.cpp: String_Alloc (ui/ui_utils.cpp), the menu
// system's interned strings. Each new string takes its bytes, then a
// stringDef_s list node, from the UI hunk. The node was allocated as 8 bytes,
// its x86 size; at 64-bit it is 16, so the node overran into the next
// string's bytes and the client registered materials under garbled names.
//
// UI_Alloc's hunk is a contiguous bump arena here, as Hunk_AllocAlign is, so
// an overrun lands in the next allocation. The engine boundary is weak:
// --gc-sections drops what the checks never reach.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <qcommon/threads.h>
#include <ui/ui_shared.h>
#include <universal/com_memory.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

alignas(16) std::uint8_t g_hunk[1 << 16];
std::size_t g_used = 0;
} // namespace

// Engine boundary.
WEAK std::uint8_t *KISAK_CDECL Hunk_AllocAlign(std::uint32_t size, int alignment, const char *, int)
{
    const std::size_t start = (g_used + alignment - 1) & ~static_cast<std::size_t>(alignment - 1);
    g_used = start + size;
    if (g_used > sizeof(g_hunk))
    {
        std::fprintf(stderr, "test hunk exhausted\n");
        std::abort();
    }
    std::memset(g_hunk + start, 0xCD, size);
    return g_hunk + start;
}
WEAK bool KISAK_CDECL Sys_IsMainThread() { return true; }
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    ++g_failures;
}

int main()
{
    String_Init();

    // Strings that share hash buckets and follow one another in the hunk:
    // every one reads back intact after the later allocations.
    std::vector<std::string> texts;
    std::vector<const char *> interned;
    for (int i = 0; i < 64; ++i)
    {
        texts.push_back("loadscreen_mp_map" + std::to_string(i));
        interned.push_back(String_Alloc(texts.back().c_str()));
    }
    for (std::size_t i = 0; i < texts.size(); ++i)
        CHECK(interned[i] && texts[i] == interned[i]);

    // Interning returns the stored copy; the empty string is the shared one.
    CHECK(String_Alloc(texts[5].c_str()) == interned[5]);
    CHECK(String_Alloc(texts[63].c_str()) == interned[63]);
    CHECK(String_Alloc("") == String_Alloc(""));
    CHECK(String_Alloc(nullptr) == nullptr);

    if (g_failures == 0)
        std::printf("ui string alloc: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
