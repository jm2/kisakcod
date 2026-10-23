// fs_pure_iwds_tests.cpp: the production FS_PureServerSetLoadedIwds
// (qcommon/files.cpp) at native width. A client on a pure server stores the
// server's iwd checksums and names from each gamestate, and compares the next
// gamestate's lists against them. The names are a pointer array that was
// copied with 4 bytes per entry, so at 64-bit only half of it arrived; the
// next gamestate (a reconnect, a map change) compared or freed stale pointers
// and crashed.
//
// The engine boundary is weak; --gc-sections drops the rest of files.cpp.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <qcommon/cmd.h>
#include <qcommon/files.h>
#include <qcommon/qcommon.h>
#include <sound/snd_public.h>
#include <universal/com_files.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

std::vector<std::string> g_args; // the tokenized string, split on spaces
} // namespace

// The globals the check reads, owned here (com_files.cpp isn't linked).
int fs_numServerIwds;
int fs_serverIwds[1024];
const char *fs_serverIwdNames[1024];
int fs_fakeChkSum;

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
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_Memcpy(void *dest, const void *src, const size_t count) { std::memcpy(dest, src, count); }
WEAK const char *CopyString(const char *in) { return strdup(in); }
WEAK void __cdecl FreeString(const char *str) { std::free(const_cast<char *>(str)); }
WEAK void Cmd_TokenizeString(char *text)
{
    g_args.clear();
    std::string word;
    for (const char *c = text;; ++c)
    {
        if (*c == ' ' || *c == '\0')
        {
            if (!word.empty())
                g_args.push_back(word);
            word.clear();
            if (*c == '\0')
                break;
        }
        else
            word += *c;
    }
}
WEAK void Cmd_EndTokenizedString() {}
WEAK void SND_StopSounds(snd_stopsounds_arg_t) {}
WEAK int Cmd_Argc() { return static_cast<int>(g_args.size()); }
WEAK const char *Cmd_Argv(int arg) { return arg < Cmd_Argc() ? g_args[arg].c_str() : ""; }

int main()
{
    char sums[] = "101 202 303";
    char names[] = "main/iw_00 main/iw_01 main/iw_02";

    // The first gamestate stores the server's lists, every name whole.
    FS_PureServerSetLoadedIwds(sums, names);
    CHECK(fs_numServerIwds == 3);
    CHECK(fs_serverIwds[2] == 303);
    for (int i = 0; i < 3; ++i)
    {
        const std::string expected = "main/iw_0" + std::to_string(i);
        CHECK(fs_serverIwdNames[i] && expected == fs_serverIwdNames[i]);
    }

    // The next one (a reconnect) compares against them and keeps them.
    const char *const kept = fs_serverIwdNames[2];
    FS_PureServerSetLoadedIwds(sums, names);
    CHECK(fs_numServerIwds == 3);
    CHECK(fs_serverIwdNames[2] == kept);

    // A different server replaces them.
    char otherSums[] = "404";
    char otherNames[] = "main/iw_03";
    FS_PureServerSetLoadedIwds(otherSums, otherNames);
    CHECK(fs_numServerIwds == 1);
    CHECK(fs_serverIwdNames[0] && std::strcmp(fs_serverIwdNames[0], "main/iw_03") == 0);

    if (g_failures == 0)
        std::printf("fs pure iwds: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
