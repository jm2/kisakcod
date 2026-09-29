// Byte-order helpers of universal/q_shared.h: BigShort/BigLong (host to
// big-endian) and LittleShort/LittleLong/LittleFloat (host to little-endian).
//
// Issue #231: these were defined only inside q_shared.h's WIN32 block while
// BigShort was declared for every target, so the POSIX headless callers
// compiled and then failed to link. They are now one constexpr set keyed on
// KISAK_LITTLE_ENDIAN from kisak_abi.h; this test pins the retail
// ShortSwap/LongSwap values and the Little* identity the Windows x86 build
// shipped with.
#include <universal/q_shared.h>

#include <cstdio>

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
    // Big* swap exactly where the retail ShortSwap/LongSwap did: the two
    // bytes of a short, the four bytes of an int.
    Expect(BigShort(0x1234) == 0x3412, "BigShort swaps the two bytes");
    Expect(BigShort(-2) == -257, "BigShort preserves the short width");
    Expect(BigLong(0x12345678) == 0x78563412, "BigLong reverses four bytes");
    Expect(BigLong(static_cast<int>(0x80000001u)) == 0x01000080,
        "BigLong keeps its full-width arithmetic");

#if KISAK_LITTLE_ENDIAN
    // Every target the ABI header accepts is little-endian: the Little* forms
    // are identity, what the old empty WIN32 macros expanded to.
    Expect(LittleShort(0x1234) == 0x1234, "LittleShort is identity");
    Expect(LittleLong(0x12345678) == 0x12345678, "LittleLong is identity");
    Expect(LittleFloat(1.5f) == 1.5f, "LittleFloat is identity");
    Expect(BigShort(BigShort(0x1234)) == 0x1234,
        "BigShort round-trips through both byte orders");
    Expect(BigLong(BigLong(0x12345678)) == 0x12345678,
        "BigLong round-trips through both byte orders");
#else
    Expect(LittleShort(0x1234) == 0x3412, "LittleShort swaps on big-endian");
    Expect(LittleLong(0x12345678) == 0x78563412,
        "LittleLong swaps on big-endian");
#endif

    // Usable in constant expressions, so wire formats can freeze values.
    static_assert(BigShort(0x1234) == 0x3412);
    static_assert(LittleLong(0x12345678) == 0x12345678);

    if (Failures != 0)
        fprintf(stderr, "%d failure(s)\n", Failures);
    return Failures == 0 ? 0 : 1;
}
