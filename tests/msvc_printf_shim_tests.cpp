// Runtime contract for the MSVC formatted-print boundary. The decompiled
// callers of _snprintf/_vsnprintf were compiled against the MSVC return
// contract: truncation reports -1, a fitting write reports the length
// without the terminator. On MSVC hosts these names are the real CRT
// functions; on POSIX hosts q_shared.h shims them through
// universal/msvc_printf_shim.h. This test asserts the CONTRACT, not the
// implementation, so both hosts must satisfy it identically.
#include <universal/msvc_printf_shim.h>

#include <string.h>

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
    memset(buffer, 0x7F, sizeof(buffer));
    Expect(_snprintf(buffer, sizeof(buffer), "%s %s\n", "name", "value")
            == 11,
        "fitting _snprintf returns the unterminated length");
    Expect(strcmp(buffer, "name value\n") == 0,
        "fitting _snprintf writes the full output");

    // Truncation reports -1 — the return value Com_SaveDvarsToBuffer
    // relies on to stop before advancing past the allocation.
    memset(buffer, 0x7F, sizeof(buffer));
    Expect(_snprintf(buffer, sizeof(buffer), "%s \"%s\"\n",
                "a_dvar_name_much_longer_than_the_buffer", "value")
            == -1,
        "truncating _snprintf returns -1");

    // Exact-fit boundary: count-1 characters of output fit, count
    // characters do not (the terminator never fits on truncation).
    char exact[6] = {};
    Expect(_snprintf(exact, sizeof(exact), "%s", "12345") == 5,
        "output of count-1 characters fits");
    Expect(_snprintf(exact, sizeof(exact), "%s", "123456") == -1,
        "output of count characters truncates to -1");

    // vsnprintf through va_list keeps the same contract.
    memset(buffer, 0x7F, sizeof(buffer));
    Expect(FormatForward(buffer, sizeof(buffer), "%d/%d", 12, 34) == 5,
        "fitting _vsnprintf returns the unterminated length");
    Expect(FormatForward(buffer, sizeof(buffer), "%0500d", 7) == -1,
        "truncating _vsnprintf returns -1");

    // POSIX-only guarantee, asserted where it applies: the shim keeps
    // vsnprintf's terminator within count on truncation (MSVC is allowed
    // to leave the buffer unterminated, so it is not asserted there).
#if defined(__GNUC__)
    memset(buffer, 0x7F, sizeof(buffer));
    (void)_snprintf(buffer, sizeof(buffer), "%s", "0123456789");
    Expect(memchr(buffer, '\0', sizeof(buffer)) != nullptr,
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
