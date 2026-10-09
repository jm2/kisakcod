// Runtime contract for universal/msvc_crt_compat.h, the POSIX side of the
// MSVC CRT / winnt spellings the decompiled engine calls directly (#218 for
// _BitScanReverse, PLATFORM_POSIX.md's "MSVC CRT names" list for the rest).
// Like tests/msvc_printf_shim_tests.cpp this asserts the CONTRACT, not the
// implementation: on MSVC hosts these names are the real CRT/intrinsic
// spellings and the compat header stays guarded out, so the same assertions
// must hold there against the real thing.
//
// The include must come first: the header renames glibc's basename()
// declaration while <string.h> is first parsed, and that only wins the
// include race when it is included first (universal/q_shared.h does the same).
//
// Scope (NOW.md row 23, split 4a): ARRAYSIZE, _isnan, _BitScanReverse,
// _time64, _TRUNCATE. The _vsnprintf_s and _localtime64 companions are
// included because _TRUNCATE and _time64 are unusable without them.
// Split 4b's _strlwr and basename halves (landed via PR #319) are asserted
// here too; split 4c (IsValidSeed, BigShort) is a separate bead and is not
// tested here.
#include <universal/msvc_crt_compat.h>

#if defined(_MSC_VER)
// The compat header is correctly guarded out on MSVC: every name it provides
// is the real CRT/SDK spelling there, so the assertions below resolve against
// the real thing. Pull the headers that declare them on MSVC — ARRAYSIZE from
// winnt.h (reached through <windows.h>), _time64/_localtime64 from the CRT's
// <time.h>, _isnan from <float.h>, _BitScanReverse from <intrin.h>, _strlwr
// from the CRT's <string.h>.
#pragma warning(push)
#pragma warning(disable : 4996) // legacy CRT names are the test surface
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string.h>
#include <time.h>
#include <float.h>
#include <intrin.h>
#endif // _MSC_VER

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

// qcommon/files.cpp's file-scope `basename` buffer must keep its retail name;
// glibc's C++ basename() overload in <string.h> used to collide with it. A
// namespace-scope declaration is the engine's exact shape, so this stops
// compiling the moment the rename stops working.
char basename[64];

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

// Reached through a real varargs forwarder, the way BG_AnimParseError calls
// _vsnprintf_s.
int FormatForward(char *const buffer, const size_t count,
    const char *const format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = _vsnprintf_s(buffer, count, _TRUNCATE, format, args);
    va_end(args);
    return written;
}

// _TRUNCATE + _vsnprintf_s: fill the buffer (terminator included) and
// report truncation as -1, the MSVC contract the only call site sits on.
void CheckVsnprintfSContract()
{
    char buffer[8];
    std::memset(buffer, 0x7F, sizeof(buffer));
    Expect(FormatForward(buffer, sizeof(buffer), "%s", "ab") == 2,
        "a fitting _vsnprintf_s returns the unterminated length");
    Expect(std::strcmp(buffer, "ab") == 0,
        "a fitting _vsnprintf_s writes the full output");
    std::memset(buffer, 0x7F, sizeof(buffer));
    Expect(FormatForward(buffer, sizeof(buffer), "%s", "abcdefg") == 7,
        "an exact-fit _vsnprintf_s returns without truncating");
    Expect(std::strcmp(buffer, "abcdefg") == 0,
        "an exact-fit _vsnprintf_s writes every character");
    std::memset(buffer, 0x7F, sizeof(buffer));
    Expect(FormatForward(buffer, sizeof(buffer), "%s", "abcdefghij") == -1,
        "a truncated _vsnprintf_s reports -1");
    Expect(std::strcmp(buffer, "abcdefg") == 0,
        "a truncated _vsnprintf_s keeps the terminator inside the buffer");
}

// _BitScanReverse: MSVC contract — return 1 and write the bit position of
// the most significant set bit (bit 0 least significant) when Mask is
// nonzero; return 0 when Mask is zero. On a zero mask *Index is undefined:
// Microsoft documents only the return value; the intrinsic lowers to `bsr`,
// which leaves the destination undefined for a zero source (Intel SDM
// Vol. 2B). The index variables use the type MSVC declares the parameter
// with, so this compiles against the real one.
void CheckBitScanReverseContract()
{
    unsigned long index = 0xDEADBEEFu;
    Expect(_BitScanReverse(&index, 0x80000001u) != 0 && index == 31,
        "_BitScanReverse finds the most significant set bit");
    index = 0xDEADBEEFu;
    Expect(_BitScanReverse(&index, 0x00010000u) != 0 && index == 16,
        "_BitScanReverse indexes from the least significant bit");
    index = 0xDEADBEEFu;
    Expect(_BitScanReverse(&index, 0x00000001u) != 0 && index == 0,
        "_BitScanReverse reports bit 0 for a unit mask");
    // MSVC contract: the return value is 0 for a zero mask. What happens
    // to *Index is undefined — MSVC does not guarantee it is untouched.
    index = 0xDEADBEEFu;
    Expect(_BitScanReverse(&index, 0u) == 0,
        "_BitScanReverse reports failure for a zero mask");
#if !defined(_MSC_VER)
    // The POSIX shim is stricter than MSVC and stores nothing on failure.
    // This extra guarantee is NOT part of the MSVC contract (the real
    // intrinsic lowers to BSR, which leaves the destination undefined for
    // a zero source) and is pinned here as shim-only behavior. Every
    // engine call site (cg_snapshot.cpp, r_dpvs*.cpp, r_model_lighting.cpp,
    // r_primarylights.cpp, ...) reads the index only on success, so the
    // shim's stricter guarantee is safe but not required.
    Expect(index == 0xDEADBEEFu,
        "_BitScanReverse (POSIX shim) stores nothing for a zero mask");
#endif

    // The whole index object is written: on LP64 `unsigned long` is 64 bits,
    // and a 32-bit store would leave its upper half as it was.
    unsigned long wide = ~0ul;
    Expect(_BitScanReverse(&wide, 0x10u) != 0 && wide == 4ul,
        "_BitScanReverse writes the full width of an unsigned long index");
}
} // namespace

int main()
{
    // ARRAYSIZE (winnt.h): the array-extent spelling ~11 call sites use.
    char arr[5] = {};
    Expect(ARRAYSIZE(arr) == 5, "ARRAYSIZE reports the element count");
    Expect(ARRAYSIZE("wide") == 5, "ARRAYSIZE counts a literal's terminator");

    // _isnan: nonzero iff NaN.
    Expect(_isnan(0.0) == 0, "_isnan is zero for 0.0");
    Expect(_isnan(1.5) == 0, "_isnan is zero for a normal");
    Expect(_isnan(std::numeric_limits<double>::infinity()) == 0,
        "_isnan is zero for infinity");
    Expect(_isnan(std::numeric_limits<double>::quiet_NaN()) != 0,
        "_isnan is nonzero for NaN");

    // _time64: seconds since the epoch, stored to the pointer when non-null
    // and returned either way (Com_RealTime uses both forms).
    long long stored = 0;
    const long long now = _time64(&stored);
    Expect(now == stored, "_time64 returns what it stores");
    Expect(now > 1600000000LL, "_time64 reports a plausible epoch (after 2020)");
    const long long direct = _time64(nullptr);
    Expect(direct >= now && direct <= now + 5LL,
        "_time64(nullptr) returns the same clock");

    // _localtime64: the companion every _time64 call site pairs it with.
    struct tm *const broken = _localtime64(&stored);
    Expect(broken != nullptr, "_localtime64 converts a stored time");
    if (broken != nullptr)
        Expect(broken->tm_year + 1900 >= 2020,
            "_localtime64 agrees with the _time64 epoch");

    CheckVsnprintfSContract();

    CheckBitScanReverseContract();

    // _strlwr: in-place ASCII lowercase, returns its argument (I_strlwr's
    // contract, which the asset/path call sites assume).
    char mixed[] = "AbC-9z";
    Expect(_strlwr(mixed) == mixed, "_strlwr returns the buffer it lowered");
    Expect(std::strcmp(mixed, "abc-9z") == 0,
        "_strlwr lowers A-Z and leaves the rest alone");

    // _stricmp / _strnicmp: case-insensitive compare (ASCII); callers use the
    // sign (db_registry.cpp sorts reorder entries with it) and zero.
    Expect(_stricmp("Zone_Source", "zone_source") == 0, "_stricmp ignores ASCII case");
    Expect(_stricmp("abc", "ABD") < 0 && _stricmp("ABD", "abc") > 0,
        "_stricmp orders case-insensitively");
    Expect(_stricmp("ab", "abc") < 0, "_stricmp orders a prefix first");
    Expect(_strnicmp("mp_Shipment", "MP_shipyard", 6) == 0,
        "_strnicmp compares only the first count characters");
    Expect(_strnicmp("mp_Shipment", "MP_shipyard", 8) != 0,
        "_strnicmp sees a difference inside count");
    Expect(_strnicmp("x", "y", 0) == 0, "_strnicmp over zero characters is equal");

    // _strdup: a heap copy the caller frees.
    char *const copy = _strdup("fastfile");
    Expect(copy && std::strcmp(copy, "fastfile") == 0, "_strdup copies the string");
    free(copy);

    // basename: the engine's buffer is the identifier, not glibc's function.
    // Bounded copy — same content, explicit destination extent.
    std::snprintf(basename, sizeof(basename), "%s", "mp_shipment");
    Expect(std::strcmp(basename, "mp_shipment") == 0,
        "the engine's basename buffer keeps its name");

    // sprintf_s: bg_misc.cpp's sized form and win_main.cpp's array form.
    char shock[16];
    Expect(sprintf_s(shock, sizeof(shock), "vol_%s", "music") == 9 && std::strcmp(shock, "vol_music") == 0,
        "sprintf_s formats and returns the length");
    char small[4];
    Expect(sprintf_s(small, "%d", 42) == 2 && std::strcmp(small, "42") == 0,
        "sprintf_s's array form takes the extent from the array");

    // fopen_s: 0 and a stream, or an error code and a null stream.
    FILE *missing = stdout;
    Expect(fopen_s(&missing, "kisak_no_such_dir/never/here.txt", "rb") != 0 && missing == nullptr,
        "fopen_s reports a failed open and nulls the stream");
#if !defined(_MSC_VER)
    // MSVC's CRT routes a null argument to its invalid-parameter handler (which
    // aborts by default); the shim returns the EINVAL the handler would report.
    missing = stdout;
    Expect(fopen_s(&missing, nullptr, "rb") == EINVAL && missing == nullptr, "fopen_s rejects a null name");
    Expect(fopen_s(&missing, "x", nullptr) == EINVAL, "fopen_s rejects a null mode");
#endif

    // _putenv: cl_main_mp's setenv command passes "NAME=value" from a stack
    // buffer, so the value must survive the buffer; an empty value removes.
    {
        char assignment[64];
        std::snprintf(assignment, sizeof(assignment), "%s", "KISAK_CRT_COMPAT_TEST=on");
        Expect(_putenv(assignment) == 0, "_putenv sets a variable");
        std::memset(assignment, 'x', sizeof(assignment) - 1);
        const char *const value = std::getenv("KISAK_CRT_COMPAT_TEST");
        Expect(value && std::strcmp(value, "on") == 0, "_putenv copies the value");
        Expect(_putenv("KISAK_CRT_COMPAT_TEST=") == 0 && std::getenv("KISAK_CRT_COMPAT_TEST") == nullptr,
            "_putenv with an empty value removes the variable");
        Expect(_putenv("no equals sign") == -1, "_putenv rejects a string without '='");
    }

    // _itoa: rb_stats.cpp prints counters in radix 10.
    char number[40];
    Expect(std::strcmp(_itoa(0, number, 10), "0") == 0, "_itoa prints zero");
    Expect(std::strcmp(_itoa(65535, number, 10), "65535") == 0, "_itoa prints radix 10");
    Expect(std::strcmp(_itoa(-120, number, 10), "-120") == 0, "_itoa signs radix 10");
    Expect(std::strcmp(_itoa(255, number, 16), "ff") == 0, "_itoa prints lower-case hex");
    Expect(std::strcmp(_itoa(-1, number, 16), "ffffffff") == 0, "_itoa prints other radixes unsigned");

    // _ctime64: ui_shared_obj.cpp slices the month/day at +4 and the year at
    // +20 of the asctime layout.
    const long long y2k = 946728000LL; // 2000-01-01 12:00:00 UTC
    const char *const when = _ctime64(&y2k);
    Expect(when && strnlen(when, 32) == 25 && when[24] == '\n', "_ctime64 uses the 25-character asctime layout");
    if (when)
        Expect(std::strncmp(when + 20, "2000", 4) == 0 || std::strncmp(when + 20, "1999", 4) == 0,
            "_ctime64 puts the year at offset 20");
    if (when)
        Expect(std::strncmp(when + 4, "Jan", 3) == 0 || std::strncmp(when + 4, "Dec", 3) == 0,
            "_ctime64 puts the English month at offset 4");

    if (Failures != 0)
        fprintf(stderr, "%d failure(s)\n", Failures);
    return Failures == 0 ? 0 : 1;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
