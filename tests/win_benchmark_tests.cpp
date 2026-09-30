// Runs the real autoconfigure CPU benchmark (win32/win_benchmark.cpp) after the
// real timer calibration (universal/timing.cpp), in WinMain's order. When MSVC
// deleted the timed loop, x86 and x64 measured two back-to-back counter reads
// (20206 or 666814 "GHz", jumping between the two from one start to the next)
// and ARM64 measured nothing (inf). With the loop kept, every run gives a few
// retail-normalized GHz, and repeated runs agree within a factor of two.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <qcommon/threads.h>
#include <universal/q_shared.h>
#include <universal/timing.h>
#include <win32/win_benchmark.h>

// InitTiming takes the thread lock through threads.cpp, whose other externals
// live in engine translation units outside this target, as in
// worker_thread_lifecycle_tests.cpp. The dvar stub keeps the lock switch off.
void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

void KISAK_CDECL Com_InitThreadData(int threadContext)
{
    (void)threadContext;
}

void KISAK_CDECL Profile_InitContext(int profileContext)
{
    (void)profileContext;
}

static dvar_s s_lockThreadsDvar{};
const dvar_t *sys_lockThreads = &s_lockThreadsDvar;

namespace
{
// A current desktop core measures about 14; a deleted loop measures about
// 10000 or more, a dead counter inf. The bounds leave an order of magnitude
// either side for slow CI VMs and faster hosts.
constexpr double kMinGHz = 0.2;
constexpr double kMaxGHz = 200.0;
constexpr double kMaxSpread = 2.0;
constexpr int kRuns = 5;
} // namespace

int main()
{
    Sys_InitMainThread();
    InitTiming();
    if (!(msecPerRawTimerTick > 0.0L))
    {
        std::fprintf(stderr, "InitTiming calibrated %Lg ms per tick\n", msecPerRawTimerTick);
        return 1;
    }

    double lowest = 0.0;
    double highest = 0.0;
    for (int run = 0; run < kRuns; ++run)
    {
        const double ghz = static_cast<double>(Sys_BenchmarkGHz());
        std::printf("Sys_BenchmarkGHz run %d: %.2f GHz\n", run + 1, ghz);
        if (!std::isfinite(ghz) || ghz < kMinGHz || ghz > kMaxGHz)
        {
            std::fprintf(stderr, "run %d: %g GHz is outside [%g, %g]\n", run + 1, ghz, kMinGHz, kMaxGHz);
            return 1;
        }
        lowest = run == 0 || ghz < lowest ? ghz : lowest;
        highest = run == 0 || ghz > highest ? ghz : highest;
    }
    if (highest > lowest * kMaxSpread)
    {
        std::fprintf(stderr, "runs spread from %g to %g GHz, more than %gx\n", lowest, highest, kMaxSpread);
        return 1;
    }
    return 0;
}
