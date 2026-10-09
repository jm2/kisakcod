// live_storage_tests.cpp: the persistent-stats file protection of
// win32/win_storage.cpp, which a POSIX client builds too. LiveStorage_Encrypt
// stamps the "iwm0" magic and a nonce (Sys_Milliseconds off Windows), hashes
// the stats block and encrypts it with a key derived from the CD key and the
// nonce; LiveStorage_DecryptAndCheck must accept exactly that file, for its
// own player directory, and reject any change.
//
// The engine boundary is weak: --gc-sections drops the engine code the
// encrypt/check path never reaches.

#include <cstdio>
#include <cstring>
#include <memory>

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

// Engine boundary.
WEAK char cl_cdkey[34] = "ABCDEFGHIJKLMNOPQRST0123456789ab";
WEAK uint32_t Sys_Milliseconds() { return 0x12345678u; }
WEAK int I_stricmp(const char *a, const char *b) { return strcasecmp(a, b); }

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

    if (g_failures == 0)
        std::printf("live storage: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
