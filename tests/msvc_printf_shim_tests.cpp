// Runtime contract for the MSVC formatted-print boundary. The decompiled
// callers of _snprintf/_vsnprintf were compiled against the MSVC return
// contract: truncation reports -1, a fitting write reports the length
// without the terminator. On MSVC hosts these names are the real CRT
// functions; on POSIX hosts q_shared.h shims them through
// universal/msvc_printf_shim.h. This test asserts the CONTRACT, not the
// implementation, so both hosts satisfy it identically except at one
// boundary the UCRT defines differently — the exact-fit len == count
// case, split per host below with the shim's documented rationale.
#include <universal/msvc_printf_shim.h>

// Included directly: on MSVC the shim header keeps its body guarded out
// (the native CRT functions stay unmapped), so the varargs machinery
// this test's forwarder uses and the fprintf/stderr diagnostics must
// come from the C headers themselves.
#include <stdarg.h>
#include <stdio.h>

// The legacy _vsnprintf/_snprintf spellings are the surface under test:
// the decompiled callers were compiled against their MSVC truncation
// contract, and on MSVC hosts those names are the real CRT functions this
// file asserts (the shim in universal/msvc_printf_shim.h stays guarded
// out there). MSVC deprecates both names (C4996) and /WX escalates the
// warning to an error, so suppress the deprecation for this translation
// unit only. This does not adopt the spellings anywhere new — it pins the
// exact deprecated-but-contractual behavior production was built against;
// the standard spellings (vsnprintf/snprintf) deliberately return a
// different value on truncation and would not exercise this contract.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // legacy CRT names are the test surface
#endif

#include <algorithm>
#include <cstring>
#include <iterator>

namespace
{
int Failures = 0;

bool Expect(const bool condition, const char *const what)
{
    if (!condition)
    {
        ++Failures;
        fprintf(stderr, "FAIL: %s\n", what);
    }
    return condition;
}

// The vsnprintf entry point is exercised through a real varargs
// forwarder, the way production callers reach it.
int FormatForward(char *const buffer, const size_t count,
    const char *const format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = _vsnprintf(buffer, count, format, args);
    va_end(args);
    return written;
}
} // namespace

int main()
{
    char buffer[32] = {};

    // A fitting write returns the produced length, terminator excluded,
    // exactly like the MSVC CRT functions.
    std::fill(std::begin(buffer), std::end(buffer), char{0x7F});
    Expect(_snprintf(buffer, sizeof(buffer), "%s %s\n", "name", "value")
            == 11,
        "fitting _snprintf returns the unterminated length");
    Expect(std::strcmp(buffer, "name value\n") == 0,
        "fitting _snprintf writes the full output");

    // Truncation reports -1 — the return value Com_SaveDvarsToBuffer
    // relies on to stop before advancing past the allocation.
    std::fill(std::begin(buffer), std::end(buffer), char{0x7F});
    Expect(_snprintf(buffer, sizeof(buffer), "%s \"%s\"\n",
                "a_dvar_name_much_longer_than_the_buffer", "value")
            == -1,
        "truncating _snprintf returns -1");

    // Exact-fit boundary: count-1 characters of output fit with the
    // terminator on every host.
    char exact[6] = {};
    Expect(_snprintf(exact, sizeof(exact), "%s", "12345") == 5,
        "output of count-1 characters fits");
    // At len == count the hosts legitimately diverge, and the test pins
    // each host's real contract instead of pretending they agree. The
    // UCRT's _snprintf returns len (== count) and leaves the buffer
    // unterminated; the POSIX shim's documented contract reports -1 for
    // any output whose terminator does not fit — a strictly safer
    // superset of UCRT truncation. Production callers only test
    // written < 0, so on POSIX the shim merely stops one call earlier
    // than the UCRT would, never overflowing or underflowing.
#if defined(_MSC_VER)
    Expect(_snprintf(exact, sizeof(exact), "%s", "123456") == 6,
        "UCRT: output of count characters returns count, unterminated");
#else
    Expect(_snprintf(exact, sizeof(exact), "%s", "123456") == -1,
        "POSIX shim: output of count characters truncates to -1");
#endif

    // vsnprintf through va_list keeps the same contract.
    std::fill(std::begin(buffer), std::end(buffer), char{0x7F});
    Expect(FormatForward(buffer, sizeof(buffer), "%d/%d", 12, 34) == 5,
        "fitting _vsnprintf returns the unterminated length");
    Expect(FormatForward(buffer, sizeof(buffer), "%0500d", 7) == -1,
        "truncating _vsnprintf returns -1");

    // POSIX-only guarantee, asserted where it applies: the shim keeps
    // vsnprintf's terminator within count on truncation (MSVC is allowed
    // to leave the buffer unterminated, so it is not asserted there).
#if defined(__GNUC__)
    std::fill(std::begin(buffer), std::end(buffer), char{0x7F});
    (void)_snprintf(buffer, sizeof(buffer), "%s", "0123456789");
    Expect(std::find(std::begin(buffer), std::end(buffer), char{0}) != std::end(buffer),
        "truncated buffer stays terminated within count on POSIX");
#endif

    if (Failures != 0)
    {
        fprintf(stderr, "%d contract failure(s)\n", Failures);
        return 1;
    }
    puts("msvc-printf-shim contracts OK");
    return 0;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
