// Byte-order helpers of universal/q_shared.h: BigShort/BigLong (host to
// big-endian) and LittleShort/LittleLong/LittleFloat (host to little-endian).
//
// Issue #231: these were defined only inside q_shared.h's WIN32 block while
// BigShort was declared for every target, so the POSIX headless callers
// compiled and then failed to link. They are now one constexpr set keyed on
// KISAK_LITTLE_ENDIAN from kisak_abi.h; this test pins the retail
// ShortSwap/LongSwap values and the Little* identity the Windows x86 build
// shipped with.
#include <bit>
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
    // The swap primitives: two bytes of a short, four bytes of an int.
    Expect(KisakSwap16(0x1234) == 0x3412, "KisakSwap16 swaps the two bytes");
    Expect(KisakSwap16(-2) == -257, "KisakSwap16 preserves the short width");
    Expect(KisakSwap32(0x12345678) == 0x78563412, "KisakSwap32 reverses four bytes");
    Expect(KisakSwap32(static_cast<int>(0x80000001u)) == 0x01000080,
        "KisakSwap32 keeps its full-width arithmetic");
    Expect(BigShort(BigShort(0x1234)) == 0x1234 && LittleShort(LittleShort(0x1234)) == 0x1234,
        "the short forms round-trip");
    Expect(BigLong(BigLong(0x12345678)) == 0x12345678 && LittleLong(LittleLong(0x12345678)) == 0x12345678,
        "the long forms round-trip");

#if KISAK_LITTLE_ENDIAN
    // Every target the ABI header accepts today: Big* swap where the retail
    // ShortSwap/LongSwap did, Little* are identity (the old empty WIN32
    // macros).
    Expect(BigShort(0x1234) == 0x3412, "BigShort swaps on little-endian");
    Expect(BigLong(0x12345678) == 0x78563412, "BigLong swaps on little-endian");
    Expect(LittleShort(0x1234) == 0x1234, "LittleShort is identity on little-endian");
    Expect(LittleLong(0x12345678) == 0x12345678, "LittleLong is identity on little-endian");
    Expect(LittleFloat(1.5f) == 1.5f, "LittleFloat is identity on little-endian");
    static_assert(BigShort(0x1234) == 0x3412);
    static_assert(LittleLong(0x12345678) == 0x12345678);
#else
    Expect(BigShort(0x1234) == 0x1234, "BigShort is identity on big-endian");
    Expect(BigLong(0x12345678) == 0x12345678, "BigLong is identity on big-endian");
    Expect(LittleShort(0x1234) == 0x3412, "LittleShort swaps on big-endian");
    Expect(LittleLong(0x12345678) == 0x78563412, "LittleLong swaps on big-endian");
    Expect(std::bit_cast<int>(LittleFloat(1.5f)) == KisakSwap32(std::bit_cast<int>(1.5f)),
        "LittleFloat reverses the float's bytes on big-endian");
    static_assert(BigShort(0x1234) == 0x1234);
    static_assert(LittleLong(0x12345678) == 0x78563412);
#endif
    // Usable in constant expressions, so wire formats can freeze values.
    static_assert(KisakSwap16(0x1234) == 0x3412);

    if (Failures != 0)
        fprintf(stderr, "%d failure(s)\n", Failures);
    return Failures == 0 ? 0 : 1;
}
