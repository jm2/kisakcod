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
#include <universal/msvc_crt_compat.h>

#if defined(_MSC_VER)
// The compat header is correctly guarded out on MSVC: every name it provides
// is the real CRT/SDK spelling there, so the assertions below resolve against
// the real thing. Pull the headers that declare them on MSVC — ARRAYSIZE from
// winnt.h (reached through <windows.h>), _time64/_localtime64 from the CRT's
// <time.h>, _isnan from <float.h>, _BitScanReverse from <intrin.h>. The
// shims themselves stay guarded out.
//
// MSVC deprecates the legacy CRT names under test (_strlwr, _localtime64)
// with C4996, and /WX escalates that warning to C2220 (CI failure on the
// hosted Windows legs). Suppress the deprecation for this translation unit
// only — same idiom as tests/msvc_printf_shim_tests.cpp. This does not adopt
// the spellings anywhere new: the deprecated-but-contractual names ARE the
// test surface, and the *_s replacements have different contracts and would
// not exercise what production was built against.
#pragma warning(push)
#pragma warning(disable : 4996) // legacy CRT names are the test surface
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <time.h>
#include <float.h>
#include <intrin.h>
#endif // _MSC_VER

#include <cstdarg>
#include <cstdio>
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

// _BitScanReverse: most significant set bit, bit 0 least significant;
// a zero mask reports failure (return 0). The index variables use the type
// MSVC declares the parameter with, so this compiles against the real one.
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
    index = 0xDEADBEEFu;
    Expect(_BitScanReverse(&index, 0u) == 0,
        "_BitScanReverse reports failure for a zero mask");
#if !defined(_MSC_VER)
    // What the real intrinsic leaves in *Index on a zero mask is
    // architecturally undefined: it compiles to BSR, and Intel's BSR leaves
    // the destination undefined when the source is 0 (the hosted Windows
    // legs demonstrably write it), so only the return value is part of the
    // portable contract. Every engine call site (cg_snapshot.cpp,
    // r_dpvs*.cpp, r_model_lighting.cpp, r_primarylights.cpp, ...) reads
    // the index only on success. The POSIX shim is stricter and stores
    // nothing; that extra guarantee is pinned on the shim side only.
    Expect(index == 0xDEADBEEFu,
        "_BitScanReverse (POSIX shim) stores nothing for a zero mask");
#endif

    // MSVC's Index is a DWORD*: the write stays 32 bits even where a call
    // site reaches it through a wider pointer (msg_bits_mp.cpp casts an int*
    // to unsigned long*), so the neighbouring word must survive.
    struct Overhang
    {
        unsigned int index;
        unsigned int guard;
    } overhang = {0xDEADBEEFu, 0xCAFEBABEu};
    Expect(_BitScanReverse(
                reinterpret_cast<unsigned long *>(&overhang.index), 4u) != 0
            && overhang.index == 2,
        "_BitScanReverse accepts the wider-pointer call shape");
    Expect(overhang.guard == 0xCAFEBABEu,
        "_BitScanReverse stores only the 32-bit index");
}
} // namespace

int main()
{
    // ARRAYSIZE (winnt.h): the array-extent spelling ~11 call sites use.
    char arr[5] = {};
    Expect(ARRAYSIZE(arr) == 5, "ARRAYSIZE reports the element count");
    Expect(ARRAYSIZE("wide") == 5, "ARRAYSIZE counts a literal's terminator");

    // _strlwr: in-place ASCII lowercase, returns its argument (I_strlwr's
    // contract, which the asset/path call sites assume).
    char mixed[] = "AbC-9z";
    Expect(_strlwr(mixed) == mixed, "_strlwr returns the buffer it lowered");
    Expect(std::strcmp(mixed, "abc-9z") == 0,
        "_strlwr lowers A-Z and leaves the rest alone");

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

    // basename: the engine's buffer is the identifier, not glibc's function.
    // Bounded copy — same content, explicit destination extent.
    std::snprintf(basename, sizeof(basename), "%s", "mp_shipment");
    Expect(std::strcmp(basename, "mp_shipment") == 0,
        "the engine's basename buffer keeps its name");

    CheckBitScanReverseContract();

    if (Failures != 0)
        fprintf(stderr, "%d failure(s)\n", Failures);
    return Failures == 0 ? 0 : 1;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
