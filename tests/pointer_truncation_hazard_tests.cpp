// pointer_truncation_hazard_tests.cpp: 64-bit pointer truncations on headless
// server paths (WS-3 silent hazards, docs/design/NATIVE64.md). Each check runs
// the production function with its data placed so that a pointer cut to 32
// bits is a wrong value, and fails on the old code.
//
// One source, one executable per subject (POINTER_TRUNCATION_SUBJECT), so no
// subject's engine TU pulls another's into the link:
//   1: universal/q_shared.cpp  Info_Validate
// The engine boundary is weak: the engine TUs replace the stubs they define,
// and --gc-sections drops the engine code no check reaches.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include <universal/q_shared.h>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// Maps the page on each side of a 4 GiB-aligned address and returns that
// address: its low 32 bits are zero, so an (int) cast of a pointer to it is 0,
// the "not found" value. The first candidates lie above ASan's x86-64 shadow;
// the later ones fit a 39-bit AArch64 address space.
char *MapAt4GiBBoundary()
{
    static constexpr uintptr_t kBoundaries[] = {0x7E0000000000, 0x3F0000000000, 0x7E00000000, 0x200000000};
    const uintptr_t page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    for (const uintptr_t boundary : kBoundaries)
    {
        void *const want = reinterpret_cast<void *>(boundary - page);
        void *const got = mmap(want, 2 * page, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (got == want)
            return reinterpret_cast<char *>(boundary);
        if (got != MAP_FAILED)
            munmap(got, 2 * page); // an old kernel took the address as a hint only
    }
    return nullptr;
}

// Copies a literal, terminator included, so that text[at] lands on the
// boundary; returns the copy. It stays inside the two mapped pages.
template <size_t N>
const char *PlaceAcross(char *boundary, const char (&text)[N], size_t at)
{
    static_assert(N <= 256, "keep test strings well inside one page");
    char *const start = boundary - (at < N ? at : 0);
    std::copy_n(text, N, start);
    return start;
}
} // namespace

#define WEAK __attribute__((weak))

WEAK void MyAssertHandler(const char *filename, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "engine assert %s:%d: %s\n", filename ? filename : "?", line, fmt ? fmt : "");
    std::exit(3);
}

#if POINTER_TRUNCATION_SUBJECT == 1
// Info_Validate rejects userinfo and info strings that hold '"' or ';'. It
// kept strchr's result in an int, so a match at an address whose low 32 bits
// are zero read as "no match" and the string passed.
static void CheckInfoValidate(char *boundary)
{
    CHECK(Info_Validate("\\name\\player\\rate\\25000"));
    CHECK(!Info_Validate("\\name\\pla\"yer"));
    CHECK(!Info_Validate("\\name\\pla;yer"));

    const char *const quote = PlaceAcross(boundary, "\\name\\pla\"yer", 9);
    CHECK(reinterpret_cast<uintptr_t>(std::strchr(quote, '"')) == reinterpret_cast<uintptr_t>(boundary));
    CHECK(!Info_Validate(quote));

    const char *const semicolon = PlaceAcross(boundary, "\\name\\pla;yer", 9);
    CHECK(reinterpret_cast<uintptr_t>(std::strchr(semicolon, ';')) == reinterpret_cast<uintptr_t>(boundary));
    CHECK(!Info_Validate(semicolon));
}
#endif

int main()
{
    char *const boundary = MapAt4GiBBoundary();
    if (!boundary)
    {
        std::fprintf(stderr, "FAIL no mapping at a 4 GiB boundary; the checks would prove nothing\n");
        return 1;
    }
#if POINTER_TRUNCATION_SUBJECT == 1
    CheckInfoValidate(boundary);
#endif
    if (g_failures)
        return 1;
    std::printf("pointer truncation hazards (subject %d): all checks passed\n", POINTER_TRUNCATION_SUBJECT);
    return 0;
}
