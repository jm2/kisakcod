// M1 portable compile-check + contract test for the new ABI headers.
//
// This standalone target rides the portable-tests leg on all five runners under
// kisakcod_test_warnings() (-Wall -Wextra -Wpedantic -Werror / MSVC /W4 /WX). It
// exercises the canonical fixed-width atomic API through both the MSVC intrinsic
// backend and the GCC/Clang __atomic backend.
//
// What it proves: (1) kisak_abi.h / sys_atomic.h / platform_compat.h / db_disk32.h all
// parse clean under GCC, Clang and MSVC; (2) the atomics wrappers honor the Win32
// return-value / arg-order contract (the real correctness lever the non-MSVC backend
// can silently get wrong); (3) the layout-freeze macros evaluate on both pointer
// widths; (4) the calling-convention / alignment shims are actually referenced (they
// are define-away no-ops off MSVC, so only a test that touches them parses that branch).
//
// What it does NOT prove: every engine call site has migrated or every shared field
// is free of non-atomic access. Source guards and subsystem stress tests cover those
// properties one coherent field family at a time.

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

    int32_t i = 0;
    if (Sys_AtomicIncrement(&i) != 1 || i != 1)
        return fail("Increment must return the NEW value");
    if (Sys_AtomicDecrement(&i) != 0 || i != 0)
        return fail("Decrement must return the NEW value");

    uint32_t u = 10;
    if (Sys_AtomicFetchAdd(&u, 5u) != 10u || u != 15u)
        return fail("FetchAdd must return the OLD value");

    volatile int32_t c = 7;
    if (Sys_AtomicCompareExchange(&c, 9, 7) != 7 || Sys_AtomicLoad(&c) != 9)
        return fail("CompareExchange (dest,exchange,comparand) swap + return OLD");
    if (Sys_AtomicCompareExchange(&c, 1, 7) != 9 || Sys_AtomicLoad(&c) != 9)
        return fail("CompareExchange no-swap must return OLD and not write");

    int32_t e = 4;
    if (Sys_AtomicExchange(&e, 6) != 4 || Sys_AtomicLoad(&e) != 6)
        return fail("Exchange must return the OLD value");

    Sys_AtomicStore(&u, UINT32_C(0xffffffff));
    if (Sys_AtomicLoad(&u) != UINT32_C(0xffffffff))
        return fail("Load/Store must preserve every 32-bit pattern");
    if (Sys_AtomicIncrement(&u) != 0 || Sys_AtomicLoad(&u) != 0)
        return fail("unsigned Increment must wrap at 32 bits");
    if (Sys_AtomicCompareExchange(&u, UINT32_C(0x80000000), 0u) != 0
        || Sys_AtomicLoad(&u) != UINT32_C(0x80000000))
    {
        return fail("CompareExchange must preserve the unsigned high bit");
    }

    int dummy = 0;
    int *pv = &dummy;
    if (Sys_AtomicExchangePointer(&pv, static_cast<int *>(nullptr)) != &dummy
        || pv != nullptr)
    {
        return fail("ExchangePointer must swap and return the OLD pointer");
    }

    int replacement = 0;
    pv = &dummy;
    if (Sys_AtomicCompareExchangePointer(&pv, &replacement, &dummy) != &dummy
        || pv != &replacement)
    {
        return fail("CompareExchangePointer must swap and return OLD on match");
    }
    if (Sys_AtomicCompareExchangePointer(&pv, static_cast<int *>(nullptr), &dummy)
            != &replacement
        || pv != &replacement)
    {
        return fail("CompareExchangePointer must return OLD without swapping on mismatch");
    }

    return 0;
}
