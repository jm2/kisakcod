// cg_scoreboard_tests.cpp: UI_GetScoreAtRank (cgame_mp/cg_scoreboard_mp.cpp)
// at 64-bit returns the rank'th entry of cg_s::scores. The decompile wrote it
// as teamScores[10 * rank + 2], the same address on x86 (score_t is 40 bytes
// and scores sits 48 bytes past teamScores), but at 64-bit score_t carries two
// pointers, so the index lands mid-entry and the client index it reads is a
// pointer half. A HUD menu expression asking for a score by rank crashed the
// client right after spawning.
//
// The engine boundary is weak: --gc-sections drops what the check never
// reaches.

#include <cstdio>
#include <cstdlib>

#include <cgame_mp/cg_local_mp.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
} // namespace

// The global the check reads, owned here.
cg_s cgArray[1];

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}

int main()
{
    cg_s *cgameGlob = &cgArray[0];
    const int clients[] = {5, 2, 9};
    cgameGlob->numScores = 3;
    for (int i = 0; i < 3; ++i)
    {
        cgameGlob->scores[i].client = clients[i];
        cgameGlob->scores[i].score = 100 - i;
        cgameGlob->scores[i].hStatusIcon = reinterpret_cast<Material *>(std::uintptr_t{0x7FFF00001000} + i);
        cgameGlob->bgs.clientinfo[clients[i]].infoValid = 1;
    }

    for (int rank = 1; rank <= 3; ++rank)
    {
        const score_t *score = UI_GetScoreAtRank(0, rank);
        CHECK(score == &cgameGlob->scores[rank - 1]);
        CHECK(score && score->client == clients[rank - 1]);
        CHECK(score && score->score == 101 - rank);
    }

    // Out of range, and a rank whose client has no valid info.
    CHECK(UI_GetScoreAtRank(0, 0) == nullptr);
    CHECK(UI_GetScoreAtRank(0, 4) == nullptr);
    cgameGlob->bgs.clientinfo[clients[1]].infoValid = 0;
    CHECK(UI_GetScoreAtRank(0, 2) == nullptr);

    if (g_failures == 0)
        std::printf("cg scoreboard: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
