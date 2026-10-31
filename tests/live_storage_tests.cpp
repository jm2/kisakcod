// live_storage_tests.cpp: the persistent-stats file protection of
// win32/win_storage.cpp, which a POSIX client builds too. LiveStorage_Encrypt
// stamps the "iwm0" magic and a nonce (Sys_Milliseconds off Windows), hashes
// the stats block and encrypts it with a key derived from the CD key and the
// nonce; LiveStorage_DecryptAndCheck must accept exactly that file, for its
// own player directory, and reject any change. LiveStorage_ReadStatsFile reads
// the file from the writable players dir only, never a retail install's own
// (#940).
//
// The engine boundary is weak: --gc-sections drops the engine code the
// encrypt/check path never reaches.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <win32/win_storage.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

std::unique_ptr<StatsFile> Encrypted(const char *dir)
{
    auto file = std::make_unique<StatsFile>();
    std::memset(file.get(), 0, sizeof(StatsFile));
    std::snprintf(file->body.statsData.path, sizeof(file->body.statsData.path), "%s", dir);
    for (std::size_t i = 0; i < sizeof(file->body.statsData.stats); ++i)
        file->body.statsData.stats[i] = static_cast<unsigned char>(i * 7 + 3);
    LiveStorage_Encrypt(file.get());
    return file;
}
} // namespace

// Engine boundary. An engine assert (live in debug builds) fails the test.
WEAK void MyAssertHandler(const char *file, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", file ? file : "?", line);
    std::exit(3);
}
WEAK char cl_cdkey[34] = "ABCDEFGHIJKLMNOPQRST0123456789ab";
WEAK uint32_t Sys_Milliseconds() { return 0x12345678u; }
WEAK int I_stricmp(const char *a, const char *b) { return strcasecmp(a, b); }

// The file system: a stats file in fs_homepath's players dir, read by
// FS_FOpenFileReadFromDir, and one in a retail install, which only the
// search-path FS_FOpenFileRead would find.
namespace
{
std::string g_homeStats;
std::string g_installStats;
std::string g_readDir;
const std::string *g_open = nullptr;
} // namespace
WEAK void FS_CheckFileSystemStarted() {}
WEAK uint32_t FS_FOpenFileRead(const char *, int *h)
{
    g_open = &g_installStats;
    *h = g_installStats.empty() ? 0 : 1;
    return *h ? static_cast<uint32_t>(g_installStats.size()) : static_cast<uint32_t>(-1);
}
WEAK int FS_FOpenFileReadFromDir(const char *, const char *dir, int *h)
{
    g_readDir = dir;
    g_open = &g_homeStats;
    *h = g_homeStats.empty() ? 0 : 1;
    return *h ? static_cast<int>(g_homeStats.size()) : -1;
}
WEAK uint32_t FS_Read(uint8_t *buffer, uint32_t len, int)
{
    const uint32_t n = std::min<uint32_t>(len, static_cast<uint32_t>(g_open->size()));
    std::copy_n(g_open->data(), n, buffer);
    return n;
}
WEAK void FS_FCloseFile(int) {}

int main()
{
    // The round trip: the file decrypts and verifies for its own directory.
    auto file = Encrypted("players/profiles/kisak");
    CHECK(std::memcmp(file->magic, "iwm0", 4) == 0);
    CHECK(file->nonce == 0x12345678u);
    {
        StatsFile copy = *file;
        CHECK(LiveStorage_DecryptAndCheck(&copy, "players/profiles/kisak"));
        CHECK(copy.body.statsData.stats[10] == static_cast<unsigned char>(10 * 7 + 3));
    }
    // Another player's directory is refused.
    {
        StatsFile copy = *file;
        CHECK(!LiveStorage_DecryptAndCheck(&copy, "players/profiles/other"));
    }
    // A flipped byte anywhere in the encrypted body is refused.
    {
        StatsFile copy = *file;
        reinterpret_cast<unsigned char *>(&copy.body)[sizeof(copy.body) / 2] ^= 0x01;
        CHECK(!LiveStorage_DecryptAndCheck(&copy, "players/profiles/kisak"));
    }
    // A file without the magic is refused.
    {
        StatsFile copy = *file;
        copy.magic[0] = 'x';
        CHECK(!LiveStorage_DecryptAndCheck(&copy, "players/profiles/kisak"));
    }

    // The stats are read from the writable players dir. A retail install's own
    // file, encrypted with its CD key, isn't read, so a first run starts with
    // fresh stats instead of dropping the launch +connect as corrupt (#940).
    {
        const std::string bytes(reinterpret_cast<const char *>(file.get()), sizeof(StatsFile));
        auto buffer = std::make_unique<StatsFile>();
        g_installStats = bytes;
        g_homeStats.clear();
        CHECK(!LiveStorage_ReadStatsFile("profiles/kisak/mpdata", buffer->magic, sizeof(StatsFile)));
        g_homeStats = bytes;
        CHECK(LiveStorage_ReadStatsFile("profiles/kisak/mpdata", buffer->magic, sizeof(StatsFile)));
        CHECK(g_readDir == "players");
        CHECK(std::memcmp(buffer.get(), file.get(), sizeof(StatsFile)) == 0);
    }

    if (g_failures == 0)
        std::printf("live storage: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
