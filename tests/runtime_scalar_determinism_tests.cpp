// M10 architecture-neutral determinism contract tests.
//
// This standalone target rides the portable-tests leg on all five runners
// under kisakcod_test_warnings() (-Wall -Wextra -Wpedantic -Werror / MSVC
// /W4 /WX). It pins the exact semantics every architecture must produce for
// the runtime::determinism primitives:
//
//   (1) FloatToIntSaturating is defined for every input the script
//       boundaries can produce -- including the out-of-range float-to-int
//       domain where the x86 cvttss2si result (INT32_MIN) and the AArch64
//       FCVTZS result (saturating) disagree -- while remaining bit-identical
//       to the legacy cast for every in-range value (the PR #42 deferred
//       forcedMaterialSpeed producer risk).
//   (2) TotalOrderLess implements the IEEE 754 totalOrder predicate, so
//       sorts keyed on floating-point data produce the same permutation on
//       every architecture even when NaN or signed zero is present.
//   (3) Packed reference fields round-trip byte-exactly through explicit
//       little-endian assembly, independent of host endianness, alignment,
//       or width-typed load behavior.
//   (4) Sign-carrying compressed scalars and trail bytes decode through
//       explicit sign extension, independent of the ABI's plain-char
//       signedness (signed on x86, unsigned on AArch64 Linux).
//
// What it does NOT prove: that every engine producer/consumer already routes
// through these primitives. The source-invariants ctest entry pins the
// material-time producers; other subsystems adopt the layer as ARM64 runs
// expose their scalar gaps.

#include <runtime/scalar_determinism.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>

namespace
{
void expect(bool condition, const char *what)
{
    if (!condition)
    {
        std::fprintf(stderr, "runtime-scalar-determinism test failed: %s\n", what);
        std::exit(1);
    }
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr std::int32_t kIntMin = (std::numeric_limits<std::int32_t>::min)();
constexpr std::int32_t kIntMax = (std::numeric_limits<std::int32_t>::max)();

void TestFloatToIntSaturatingInRangeMatchesLegacyCast()
{
    // Every in-range value must convert exactly like the legacy cast --
    // this is what keeps existing savegames/demos bit-identical.
    static_assert(runtime::determinism::FloatToIntSaturating(0.0) == 0);
    static_assert(runtime::determinism::FloatToIntSaturating(-0.0) == 0);
    static_assert(runtime::determinism::FloatToIntSaturating(0.9) == 0);
    static_assert(runtime::determinism::FloatToIntSaturating(-0.9) == 0);
    static_assert(runtime::determinism::FloatToIntSaturating(1.5) == 1);
    static_assert(runtime::determinism::FloatToIntSaturating(-1.5) == -1);
    static_assert(runtime::determinism::FloatToIntSaturating(42.0) == 42);
    static_assert(runtime::determinism::FloatToIntSaturating(-42.0) == -42);
    static_assert(runtime::determinism::FloatToIntSaturating(2147483647.0) == kIntMax);
    static_assert(runtime::determinism::FloatToIntSaturating(-2147483648.0) == kIntMin);

    for (int i = -1000; i <= 1000; ++i)
    {
        const double value = static_cast<double>(i) + 0.75;
        expect(runtime::determinism::FloatToIntSaturating(value)
                == static_cast<std::int32_t>(value),
            "in-range double must match the legacy truncation");
        expect(runtime::determinism::FloatToIntSaturating(static_cast<float>(value))
                == static_cast<std::int32_t>(value),
            "in-range float must match the legacy truncation");
    }
}

void TestFloatToIntSaturatingOutOfRangeIsDefined()
{
    // The out-of-range domain is where x86 (INT32_MIN) and AArch64
    // (saturation) diverge under the bare cast. The primitive defines the
    // saturating answer everywhere.
    static_assert(runtime::determinism::FloatToIntSaturating(2147483647.5) == kIntMax);
    static_assert(runtime::determinism::FloatToIntSaturating(-2147483648.5) == kIntMin);
    static_assert(runtime::determinism::FloatToIntSaturating(2147483648.0) == kIntMax);
    static_assert(runtime::determinism::FloatToIntSaturating(-2147483649.0) == kIntMin);
    static_assert(runtime::determinism::FloatToIntSaturating(3.0e9) == kIntMax);
    static_assert(runtime::determinism::FloatToIntSaturating(-3.0e9) == kIntMin);
    static_assert(runtime::determinism::FloatToIntSaturating(1.0e300) == kIntMax);
    static_assert(runtime::determinism::FloatToIntSaturating(-1.0e300) == kIntMin);
    static_assert(runtime::determinism::FloatToIntSaturating(kInf) == kIntMax);
    static_assert(runtime::determinism::FloatToIntSaturating(-kInf) == kIntMin);

    // forcedMaterialSpeed-shaped inputs: script floats at extreme magnitude.
    static_assert(runtime::determinism::FloatToIntSaturating(1.0e30f) == kIntMax);
    static_assert(runtime::determinism::FloatToIntSaturating(-1.0e30f) == kIntMin);
    static_assert(runtime::determinism::FloatToIntSaturating(3.4028235e38f) == kIntMax);
}

void TestFloatToIntSaturatingNaNMapsToZero()
{
    // NaN -> 0 matches the established bg::vehicle_material_time::Interpolate
    // boundary convention.
    static_assert(runtime::determinism::FloatToIntSaturating(kNaN) == 0);
    static_assert(runtime::determinism::FloatToIntSaturating(
                      -std::numeric_limits<double>::quiet_NaN())
        == 0);
    static_assert(runtime::determinism::FloatToIntSaturating(
                      std::numeric_limits<float>::quiet_NaN())
        == 0);
    static_assert(runtime::determinism::FloatToIntSaturating(
                      std::numeric_limits<float>::signaling_NaN())
        == 0);
}

void TestTotalOrderLess()
{
    constexpr double kPosZero = 0.0;
    constexpr double kNegZero = -0.0;
    constexpr double kNegNaN = -std::numeric_limits<double>::quiet_NaN();

    // Signed zero is ordered: -0.0 < +0.0 (operator< reports false both ways).
    static_assert(runtime::determinism::TotalOrderLess(kNegZero, kPosZero));
    static_assert(!runtime::determinism::TotalOrderLess(kPosZero, kNegZero));

    // Finite extremes bracket every finite value.
    static_assert(
        runtime::determinism::TotalOrderLess(
            (std::numeric_limits<double>::lowest)(), kInf));
    static_assert(
        runtime::determinism::TotalOrderLess(
            -(std::numeric_limits<double>::lowest)(), kInf));

    // NaN orders above +Inf and below -Inf by sign; a NaN compares equal to
    // itself (neither less), unlike operator< which is false in every NaN
    // combination.
    static_assert(!runtime::determinism::TotalOrderLess(kNaN, kInf));
    static_assert(runtime::determinism::TotalOrderLess(kInf, kNaN));
    static_assert(runtime::determinism::TotalOrderLess(kNegNaN, -kInf));
    static_assert(!runtime::determinism::TotalOrderLess(kNaN, kNaN));

    // Normal-value ordering agrees with operator<.
    static_assert(runtime::determinism::TotalOrderLess(-1.5, 1.5));
    static_assert(runtime::determinism::TotalOrderLess(1.0, 1.5));
    static_assert(!runtime::determinism::TotalOrderLess(1.5, 1.0));

    // A sort containing NaN and both signed zeros must produce one exact
    // permutation on every architecture:
    // -Inf < -2.5 < -1.0 < -0.0 < +0.0 = +0.0 < 1.0 < +Inf < +NaN.
    double values[] = { 1.0, kNaN, -2.5, kPosZero, kNegZero, kInf, -kInf, -1.0, 0.0 };
    std::sort(std::begin(values), std::end(values),
        [](double a, double b) { return runtime::determinism::TotalOrderLess(a, b); });
    expect(values[0] == -kInf, "-Inf sorts first");
    expect(values[1] == -2.5, "negative finite ordering");
    expect(values[2] == -1.0, "negative finite ordering");
    expect(std::signbit(values[3]) && values[3] == 0.0, "-0.0 precedes +0.0");
    expect(!std::signbit(values[4]) && values[4] == 0.0, "+0.0 follows -0.0");
    expect(!std::signbit(values[5]) && values[5] == 0.0, "+0.0 stays adjacent to +0.0");
    expect(values[6] == 1.0, "positive finite ordering");
    expect(values[7] == kInf, "+Inf follows positive finite");
    expect(std::isnan(values[8]) && !std::signbit(values[8]),
        "positive NaN sorts last");
}

// The float overload must key the value's OWN 32 bits. Widening through
// double goes through a hardware conversion that can quieten signaling NaNs
// and reposition/canonicalize payloads, so two distinct binary32 NaNs can
// collapse onto one binary64 key (differently per architecture) and lose
// their order. The NaNs here are built by bit_cast and only ever travel
// through bit-reinterpretation and integer compares -- no floating-point
// operation touches them, so the test stays constexpr and signal-free.
void TestTotalOrderLessFloatKeysNativeBinary32Bits()
{
    using runtime::determinism::TotalOrderKey;
    using runtime::determinism::TotalOrderLess;

    // binary32 sNaNs with distinct payloads, plus the canonical qNaN.
    constexpr float kSnanPayload1 = std::bit_cast<float>(std::uint32_t{0x7F800001u});
    constexpr float kSnanPayload2 = std::bit_cast<float>(std::uint32_t{0x7F8055AAu});
    constexpr float kQNaN = std::numeric_limits<float>::quiet_NaN(); // 0x7FC00000
    constexpr float kNegSnanPayload1 = std::bit_cast<float>(std::uint32_t{0xFF800001u});

    // Distinct payloads stay distinct and payload-ordered; the comparison is
    // antisymmetric -- the exact property the widened double path could lose.
    static_assert(TotalOrderLess(kSnanPayload1, kSnanPayload2));
    static_assert(!TotalOrderLess(kSnanPayload2, kSnanPayload1));
    static_assert(TotalOrderKey(kSnanPayload1) != TotalOrderKey(kSnanPayload2));

    // The 32-bit key is the standard bijection applied to the binary32 bits:
    // positive NaN keys sit in the high half ordered by payload, the qNaN
    // (larger payload bits) above both sNaNs, and -NaN in the low half below
    // every finite value.
    static_assert(TotalOrderKey(kSnanPayload1) == std::uint32_t{0xFF800001u});
    static_assert(TotalOrderKey(kSnanPayload2) == std::uint32_t{0xFF8055AAu});
    static_assert(TotalOrderLess(kSnanPayload2, kQNaN));
    static_assert(!TotalOrderLess(kQNaN, kSnanPayload2));
    static_assert(TotalOrderLess(kNegSnanPayload1, kSnanPayload1));
    static_assert(TotalOrderLess(kNegSnanPayload1,
        -(std::numeric_limits<float>::lowest)()));

    // Signed zero and normal-value ordering hold in binary32 as in binary64.
    constexpr float kPosZero = 0.0f;
    constexpr float kNegZero = -0.0f;
    static_assert(TotalOrderLess(kNegZero, kPosZero));
    static_assert(!TotalOrderLess(kPosZero, kNegZero));
    static_assert(TotalOrderLess(-1.5f, 1.5f));
    static_assert(!TotalOrderLess(2.0f, 1.0f));

    // A sort over binary32 NaNs must produce one exact payload-ordered
    // permutation on every architecture.
    float values[] = { kQNaN, kSnanPayload2, kPosZero, kSnanPayload1, kNegZero };
    std::sort(std::begin(values), std::end(values),
        [](float a, float b) { return TotalOrderLess(a, b); });
    expect(std::signbit(values[0]) && values[0] == 0.0f, "float -0.0 sorts first");
    expect(!std::signbit(values[1]) && values[1] == 0.0f, "float +0.0 follows -0.0");
    expect(std::bit_cast<std::uint32_t>(values[2]) == 0x7F800001u,
        "sNaN payload 1 sorts below payload 2");
    expect(std::bit_cast<std::uint32_t>(values[3]) == 0x7F8055AAu,
        "sNaN payload 2 sorts below qNaN");
    expect(std::bit_cast<std::uint32_t>(values[4]) == 0x7FC00000u,
        "qNaN sorts last");
}

// The packed-field checks split into focused helpers so each stays under
// the project complexity budget while covering one contract per function.
void TestLittleEndianWriteByteLayout()
{
    using runtime::determinism::WriteLe16;
    using runtime::determinism::WriteLe32;
    using runtime::determinism::WriteLe64;

    // Byte-level layout is pinned: the buffer must look identical everywhere.
    unsigned char bytes[8] = {};
    WriteLe16(bytes, 0x1234);
    const unsigned char kLe16Layout[] = { 0x34, 0x12 };
    // std::equal's checked (four-iterator, C++14) overload: the second
    // range is bounds-checked (CWE-126), and its length is part of the
    // contract — compare exactly the bytes each Write writes.
    expect(std::equal(kLe16Layout, kLe16Layout + 2, bytes, bytes + 2),
        "WriteLe16 byte order");
    WriteLe32(bytes, 0xDEADBEEFu);
    const unsigned char kLe32Layout[] = { 0xEF, 0xBE, 0xAD, 0xDE };
    expect(std::equal(kLe32Layout, kLe32Layout + 4, bytes, bytes + 4),
        "WriteLe32 byte order");
    WriteLe64(bytes, 0x0102030405060707ull);
    const unsigned char kLe64Layout[] = {
        0x07, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 };
    expect(std::equal(kLe64Layout, kLe64Layout + 8, bytes, bytes + 8),
        "WriteLe64 byte order");
}

void TestLittleEndianBoundaryRoundTrip()
{
    using runtime::determinism::ReadLe16;
    using runtime::determinism::ReadLe32;
    using runtime::determinism::WriteLe32;

    constexpr unsigned char kLe16Bytes[] = { 0x34, 0x12 };
    constexpr unsigned char kLe32Bytes[] = { 0x01, 0x02, 0x03, 0x04 };
    static_assert(ReadLe16(kLe16Bytes) == 0x1234);
    static_assert(ReadLe32(kLe32Bytes) == 0x04030201u);

    // Round-trip over boundary values.
    const std::uint32_t probes[] = { 0u, 1u, 0x7FFFu, 0x8000u, 0xFFFFFFFFu };
    for (const std::uint32_t probe : probes)
    {
        unsigned char bytes[8] = {};
        WriteLe32(bytes, probe);
        expect(ReadLe32(bytes) == probe, "u32 round-trip");
        expect(ReadLe16(bytes) == static_cast<std::uint16_t>(probe & 0xFFFFu),
            "u32 low half readable as u16");
    }
}

void TestLittleEndianMisalignedAccess()
{
    using runtime::determinism::ReadLe32;
    using runtime::determinism::ReadLe64;
    using runtime::determinism::WriteLe32;
    using runtime::determinism::WriteLe64;

    // Alignment independence: deliberately misaligned offsets round-trip too.
    unsigned char misaligned[9] = {};
    WriteLe32(misaligned + 1, 0xCAFEBABEu);
    expect(ReadLe32(misaligned + 1) == 0xCAFEBABEu, "misaligned u32 round-trip");
    WriteLe64(misaligned + 1, 0x1122334455667788ull);
    expect(ReadLe64(misaligned + 1) == 0x1122334455667788ull,
        "misaligned u64 round-trip");
}

void TestSignExtensionAndTrailByteRoundTrip()
{
    using runtime::determinism::ReadLe16;
    using runtime::determinism::SignExtend16;
    using runtime::determinism::SignExtend8;

    static_assert(runtime::determinism::SignExtend8(0x7F) == 127);
    static_assert(runtime::determinism::SignExtend8(0x80) == -128);
    static_assert(runtime::determinism::SignExtend8(0xFF) == -1);
    static_assert(SignExtend16(0x7FFF) == 32767);
    static_assert(SignExtend16(0x8000) == -32768);
    static_assert(SignExtend16(0xFFFF) == -1);
    static_assert(runtime::determinism::SignExtend32(0x7FFFFFFFu) == 2147483647);
    static_assert(runtime::determinism::SignExtend32(0x80000000u) == -2147483648LL);
    static_assert(runtime::determinism::SignExtend32(0xFFFFFFFFu) == -1);

    // Trail-byte signedness round-trip: a sign-carrying compressed scalar
    // must decode identically regardless of the host's plain-char signedness.
    const std::int16_t signedProbes[] = { 0, 1, 127, -128, -1, 255, -255, 32767,
        -32768 };
    for (const std::int16_t probe : signedProbes)
    {
        unsigned char packed[2] = {};
        packed[0] = static_cast<unsigned char>(probe & 0xFF);
        packed[1] = static_cast<unsigned char>((probe >> 8) & 0xFF);
        expect(SignExtend16(ReadLe16(packed)) == probe,
            "sign-carrying compressed scalar must round-trip");
    }
}
} // namespace

int main()
{
    TestFloatToIntSaturatingInRangeMatchesLegacyCast();
    TestFloatToIntSaturatingOutOfRangeIsDefined();
    TestFloatToIntSaturatingNaNMapsToZero();
    TestTotalOrderLess();
    TestTotalOrderLessFloatKeysNativeBinary32Bits();
    TestLittleEndianWriteByteLayout();
    TestLittleEndianBoundaryRoundTrip();
    TestLittleEndianMisalignedAccess();
    TestSignExtensionAndTrailByteRoundTrip();
    return 0;
}
