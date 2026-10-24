// fs_pure_checksums_tests.cpp: the production FS_ReferencedIwdPureChecksums
// (universal/com_files.cpp). A client on a pure server sends the checksums
// of its referenced iwds after each gamestate: "@ <sums...> <hash>". The
// decompiled build cleared only the first byte of its static buffer and then
// placed "@ " with strlen, so every later call (a reconnect, a map change)
// appended the new list to the previous one. The server's duplicate check
// failed and dropped the client as unpure.
//
// The engine boundary is weak; --gc-sections drops the rest of com_files.cpp.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <qcommon/qcommon.h>
#include <universal/com_files.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
} // namespace

extern int fs_checksumFeed;
extern searchpath_s *fs_searchpaths;

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error: %s\n", fmt);
    std::abort();
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_DPrintf(int, const char *, ...) {}
va_info_t g_vaInfo; // value 1: the one thread's va() buffers
WEAK void *__cdecl Sys_GetValue(int valueIndex)
{
    return valueIndex == 1 ? &g_vaInfo : nullptr;
}

int main()
{
    iwd_t iwds[3]{};
    searchpath_s paths[3]{};
    const int sums[3] = {101, 202, 303};
    for (int i = 0; i < 3; ++i)
    {
        iwds[i].pure_checksum = sums[i];
        iwds[i].referenced = 1;
        paths[i].iwd = &iwds[i];
        paths[i].next = i < 2 ? &paths[i + 1] : nullptr;
    }
    iwds[1].referenced = 0; // not referenced: left out
    paths[2].bLocalized = 1; // localized: left out
    fs_searchpaths = &paths[0];
    fs_checksumFeed = 7;

    // One referenced iwd: its sum, then numIwds ^ feed ^ sum.
    const std::string expected = "@ 101 " + std::to_string(1 ^ 7 ^ 101) + " ";
    const std::string first = FS_ReferencedIwdPureChecksums();
    CHECK(first == expected);

    // The next gamestate sends the same list, not the old one plus the new.
    const std::string second = FS_ReferencedIwdPureChecksums();
    CHECK(second == expected);

    // A longer list, then a shorter one: nothing of the longer one is left.
    iwds[1].referenced = 1;
    const std::string both = FS_ReferencedIwdPureChecksums();
    CHECK(both == "@ 101 202 " + std::to_string(2 ^ 7 ^ 101 ^ 202) + " ");
    iwds[1].referenced = 0;
    CHECK(std::string(FS_ReferencedIwdPureChecksums()) == expected);

    if (g_failures == 0)
        std::printf("fs pure checksums: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
