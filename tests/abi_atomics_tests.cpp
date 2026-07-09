// M1 portable compile-check + contract test for the new ABI headers.
//
// This is the ONLY compile of the non-MSVC atomics path anywhere: every one of the
// 197 Interlocked call sites lives in an engine TU that reaches <Windows.h>, and the
// portable-tests CI legs build with KISAK_BUILD_MP/DEDICATED/SP=OFF, so none of those
// TUs compile on Linux/macOS. This standalone target rides the portable-tests leg on
// all five runners under kisakcod_test_warnings() (-Wall -Wextra -Wpedantic -Werror /
// MSVC /W4 /WX), and forces the __atomic_* branch of sys_atomic.h to be parsed and run.
//
// What it proves: (1) kisak_abi.h / sys_atomic.h / platform_compat.h / db_disk32.h all
// parse clean under GCC, Clang and MSVC; (2) the atomics wrappers honor the Win32
// return-value / arg-order contract (the real correctness lever the non-MSVC backend
// can silently get wrong); (3) the layout-freeze macros evaluate on both pointer
// widths; (4) the calling-convention / alignment shims are actually referenced (they
// are define-away no-ops off MSVC, so only a test that touches them parses that branch).
//
// What it does NOT prove: memory-ordering correctness under contention. That is
// validated later under the M2 ASan/UBSan + threaded-engine gate on linux_amd64.

#include <universal/kisak_abi.h>
#include <universal/sys_atomic.h>
#include <database/db_disk32.h>

#include <cstdint>
#include <cstdio>

namespace
{
// Force the non-MSVC branches of the calling-convention / alignment shim to parse.
struct KISAK_ALIGNAS(16) AlignProbe
{
    uint32_t a, b, c, d;
};
static int KISAK_CDECL cdeclProbe(int x) { return x + 1; }

// A runtime struct with a real pointer member: 8 bytes on ILP32, 16 on LP64/LLP64.
struct RuntimeProbe
{
    void *p;
    uint32_t n;
};

// Compile-time layout-freeze macros must evaluate on whichever width this leg runs.
ONDISK_SIZE(disk32::PointerToken, 4);
ONDISK_SIZE(disk32::Ptr32<void>, 4);
RUNTIME_SIZE(RuntimeProbe, 8, 16);

int fail(const char *what)
{
    std::fprintf(stderr, "abi-atomics test failed: %s\n", what);
    return 1;
}
} // namespace

int main()
{
    static_assert(alignof(AlignProbe) == 16, "KISAK_ALIGNAS lost alignment");
    static_assert(KISAK_PTR_BITS == 32 || KISAK_PTR_BITS == 64, "KISAK_PTR_BITS unset");
    static_assert(sizeof(void *) * 8 == KISAK_PTR_BITS, "KISAK_PTR_BITS disagrees with pointer size");
    if (cdeclProbe(1) != 2)
        return fail("KISAK_CDECL");

#if !defined(_MSC_VER)
    // On MSVC these names resolve to the <windows.h> intrinsics (not included here),
    // and are covered by the real engine build; the shim under test is the __atomic_* path.
    int32_t i = 0;
    if (InterlockedIncrement(&i) != 1 || i != 1)
        return fail("Increment must return the NEW value");
    if (InterlockedDecrement(&i) != 0 || i != 0)
        return fail("Decrement must return the NEW value");

    uint32_t u = 10;
    if (InterlockedExchangeAdd(&u, 5u) != 10u || u != 15u)
        return fail("ExchangeAdd must return the OLD value");

    volatile int32_t c = 7;
    if (InterlockedCompareExchange(&c, 9, 7) != 7 || c != 9)
        return fail("CompareExchange (dest,exchange,comparand) swap + return OLD");
    if (InterlockedCompareExchange(&c, 1, 7) != 9 || c != 9)
        return fail("CompareExchange no-swap must return OLD and not write");

    int32_t e = 4;
    if (InterlockedExchange(&e, 6) != 4 || e != 6)
        return fail("Exchange must return the OLD value");

    int dummy = 0;
    void *pv = &dummy;
    if (InterlockedExchangePointer(&pv, static_cast<void *>(nullptr)) != &dummy || pv != nullptr)
        return fail("ExchangePointer must swap and return the OLD pointer");
#endif

    return 0;
}
