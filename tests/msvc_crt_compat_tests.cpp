// Runtime contract for universal/msvc_crt_compat.h, the POSIX side of the
// MSVC CRT spellings the decompiled engine calls directly
// (PLATFORM_POSIX.md's "MSVC CRT names" list). Like
// tests/msvc_printf_shim_tests.cpp this asserts the CONTRACT, not the
// implementation: on MSVC hosts these names are the real CRT spellings and
// the compat header stays guarded out, so the same assertions must hold
// there against the real thing.
//
// The include must come first: the header renames glibc's basename()
// declaration while <string.h> is first parsed, and that only wins the
// include race when it is included first (universal/q_shared.h does the same).
#include <universal/msvc_crt_compat.h>

#if defined(_MSC_VER)
// The compat header is correctly guarded out on MSVC: every name it provides
// is the real CRT spelling there, so the assertions below resolve against
// the real thing. Pull the headers that declare them on MSVC — _strlwr from
// the CRT's <string.h>. The shims themselves stay guarded out.
//
// MSVC deprecates the legacy CRT names under test (_strlwr) with C4996, and
// /WX escalates that warning to C2220 (CI failure on the hosted Windows
// legs). Suppress the deprecation for this translation unit only — same
// idiom as tests/msvc_printf_shim_tests.cpp. This does not adopt the
// spellings anywhere new: the deprecated-but-contractual names ARE the test
// surface, and the *_s replacements have different contracts and would not
// exercise what production was built against.
#pragma warning(push)
#pragma warning(disable : 4996) // legacy CRT names are the test surface
#include <string.h>
#endif // _MSC_VER

#include <cstdio>
#include <cstring>

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
} // namespace

int main()
{
    // _strlwr: in-place ASCII lowercase, returns its argument (I_strlwr's
    // contract, which the asset/path call sites assume).
    char mixed[] = "AbC-9z";
    Expect(_strlwr(mixed) == mixed, "_strlwr returns the buffer it lowered");
    Expect(std::strcmp(mixed, "abc-9z") == 0,
        "_strlwr lowers A-Z and leaves the rest alone");

    // basename: the engine's buffer is the identifier, not glibc's function.
    // Bounded copy — same content, explicit destination extent.
    std::snprintf(basename, sizeof(basename), "%s", "mp_shipment");
    Expect(std::strcmp(basename, "mp_shipment") == 0,
        "the engine's basename buffer keeps its name");

    if (Failures != 0)
        fprintf(stderr, "%d failure(s)\n", Failures);
    return Failures == 0 ? 0 : 1;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
