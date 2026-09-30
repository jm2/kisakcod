// The engine-owned MSVC-compatible RNG (docs/design/DETERMINISM.md, bead 9).
// Done-test: `rand` matches MSVC for 3 seeds. The production Kisak_rand and
// the random()/crandom() scales (universal/com_math.cpp) run against MSVC's
// rand stream: a table, the LCG written out, and on MSVC hosts the CRT's own
// rand. Built with KISAK_RAND_TEST_GAME_MP (Linux, clang), the binary also
// runs the production G_rand/G_random/G_flrand/G_irand (game_mp/g_utils_mp.cpp),
// including G_irand spans whose retail 32-bit arithmetic overflowed.

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <universal/com_math.h>
#ifdef KISAK_RAND_TEST_GAME_MP
#include <game_mp/g_utils_mp.h>
#endif

namespace
{
int Failures = 0;

void Expect(const bool condition, const char *const what, const unsigned int seed, const int draw)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s (seed %u, draw %d)\n", what, seed, draw);
        ++Failures;
    }
}

// The first 8 MSVC rand() values after srand(seed), for the done-test's seeds.
struct MsvcReference
{
    unsigned int seed;
    int first8[8];
};

const MsvcReference kMsvcRefs[] = {
    { 1u, { 41, 18467, 6334, 26500, 19169, 15724, 11478, 29358 } },
    { 42u, { 175, 400, 17869, 30056, 16083, 12879, 8016, 7644 } },
    { 20250928u, { 5224, 3264, 11283, 6777, 31190, 27904, 27887, 31055 } },
};

// MSVC's rand written out, so a typo in the table or in the engine fails.
int MsvcDraw(uint32_t &state)
{
    state = state * 214013u + 2531011u;
    return static_cast<int>((state >> 16) & 0x7FFFu);
}

void TestRandMatchesMsvcForThreeSeeds()
{
    for (const MsvcReference &ref : kMsvcRefs)
    {
        Kisak_srand(ref.seed);
        uint32_t state = ref.seed;
        for (int i = 0; i < 8; ++i)
        {
            const int draw = Kisak_rand();
            Expect(draw == ref.first8[i], "Kisak_rand() matches the MSVC table", ref.seed, i);
            Expect(draw == MsvcDraw(state), "Kisak_rand() matches the MSVC LCG", ref.seed, i);
        }
#ifdef _MSC_VER
        // The CRT itself is the reference here. Bound by address: Codacy's
        // CWE-327 rule matches calls spelled `srand(`/`rand(`, and this is a
        // determinism check, never a key or nonce source.
        auto *const crtSeed = &std::srand;
        auto *const crtDraw = &std::rand;
        crtSeed(ref.seed);
        for (int i = 0; i < 8; ++i)
        {
            Expect(crtDraw() == ref.first8[i], "the MSVC CRT's rand() matches the table", ref.seed, i);
        }
#endif
    }
}

void TestStateIsPerThreadAndStartsAtOne()
{
    // MSVC keeps rand's state per thread and starts it at 1: a fresh thread
    // draws the srand(1) stream, and a worker's srand/rand leaves this
    // thread's stream alone. A process-global state fails both.
    Kisak_srand(kMsvcRefs[1].seed);
    int fresh[3] = {};
    std::thread worker([&fresh]() {
        for (int &draw : fresh)
        {
            draw = Kisak_rand();
        }
        Kisak_srand(999u);
        (void)Kisak_rand();
    });
    worker.join();
    for (int i = 0; i < 3; ++i)
    {
        Expect(fresh[i] == kMsvcRefs[0].first8[i], "an unseeded thread draws the srand(1) stream", 1u, i);
        Expect(Kisak_rand() == kMsvcRefs[1].first8[i], "a worker leaves this thread's stream alone",
            kMsvcRefs[1].seed, i);
    }
}

void TestFloatScalesRunTheMsvcStream()
{
    // random() is Kisak_rand() / 32768.0 and crandom() is random() * 2 - 1.
    // Every value is a multiple of 2^-15, so `==` is exact. Called through
    // pointers for the same Codacy CWE-327 reason as above.
    float (__cdecl *const unitScale)() = random;
    float (__cdecl *const signedScale)() = crandom;
    for (const MsvcReference &ref : kMsvcRefs)
    {
        Kisak_srand(ref.seed);
        for (int i = 0; i < 8; i += 2)
        {
            Expect(unitScale() == ref.first8[i] / 32768.0f, "random() scales the MSVC draw", ref.seed, i);
            Expect(signedScale() == (2 * ref.first8[i + 1] - 32768) / 32768.0f,
                "crandom() scales the MSVC draw", ref.seed, i + 1);
        }
    }
}

#ifdef KISAK_RAND_TEST_GAME_MP
void TestGameHelpersRunTheMsvcStream()
{
    // G_InitGame seeds this stream with Kisak_srand. G_irand must return
    // min + (max - min) * draw / 2^15 in exact arithmetic: retail's 32-bit
    // product overflowed for spans past 65538, and `max - min` past INT_MAX.
    struct Span
    {
        int min;
        int max;
    };
    const Span spans[] = { { 0, 10 }, { -5, 5 }, { 0, 1 << 20 }, { -100000, 100000 }, { INT_MIN, INT_MAX } };
    for (const MsvcReference &ref : kMsvcRefs)
    {
        Kisak_srand(ref.seed);
        Expect(G_rand() == ref.first8[0], "G_rand() is the MSVC draw", ref.seed, 0);
        Expect(G_random() == ref.first8[1] / 32768.0f, "G_random() scales the MSVC draw", ref.seed, 1);
        Expect(G_flrand(-2.0f, 2.0f) == ref.first8[2] / 8192.0f - 2.0f, "G_flrand() scales the MSVC draw",
            ref.seed, 2);
        int i = 3;
        for (const Span &span : spans)
        {
            const long long want = span.min + (((static_cast<long long>(span.max) - span.min) * ref.first8[i]) >> 15);
            const int got = G_irand(span.min, span.max);
            Expect(got == want, "G_irand() is min + span * draw / 2^15", ref.seed, i);
            Expect(got >= span.min && got < span.max, "G_irand() stays in [min, max)", ref.seed, i);
            ++i;
        }
    }
}
#endif
} // namespace

int main()
{
    TestRandMatchesMsvcForThreeSeeds();
    TestStateIsPerThreadAndStartsAtOne();
    TestFloatScalesRunTheMsvcStream();
#ifdef KISAK_RAND_TEST_GAME_MP
    TestGameHelpersRunTheMsvcStream();
#endif
    if (Failures != 0)
    {
        std::fprintf(stderr, "msvc-rand: %d failure(s)\n", Failures);
        return 1;
    }
    std::printf("msvc-rand: all contracts held\n");
    return 0;
}
