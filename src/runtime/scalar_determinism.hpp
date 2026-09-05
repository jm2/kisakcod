#pragma once

// Architecture-neutral scalar determinism layer (M10).
//
// Producers of wire-visible integers from floating-point inputs, consumers of
// packed reference fields, and decoders of sign-carrying compressed bytes must
// behave identically on x86_64 and AArch64. Each primitive below replaces a
// construct whose result differs between those architectures:
//
//   - A bare (int) float-to-int cast is undefined when the value is outside
//     the destination range. x86 cvttss2si/cvttsd2si return INT32_MIN for the
//     whole out-of-range domain, while AArch64 FCVTZS saturates toward
//     +/-INT32_MAX/MIN. Script-supplied scalars (the deferred PR #42
//     forcedMaterialSpeed producer risk) can reach any magnitude, so the two
//     architectures disagree on the same savegame or demo. FloatToIntSaturating
//     defines one behavior -- saturate, with NaN mapped to zero exactly like
//     the existing bg::vehicle_material_time::Interpolate boundary -- while
//     remaining bit-identical to the legacy cast for every in-range value.
//
//   - Floating-point ordering via operator< diverges wherever NaN or -0.0 can
//     appear (x86 and AArch64 raise identical comparisons for NaN, but any
//     sort keyed on unordered values ends in a layout that depends on input
//     order instead of value order). TotalOrderLess implements the IEEE 754
//     totalOrder predicate: -NaN < -Inf < finite < +Inf < +NaN, -0.0 < +0.0.
//
//   - Packed reference fields read through misaligned or width-typed loads
//     embed host endianness/alignment assumptions. ReadLe*/WriteLe* assemble
//     and serialize explicit little-endian bytes, so a field round-trips to
//     the identical bytes on every supported architecture.
//
//   - Compressed values whose high bit carries sign break when plain `char`
//     changes signedness between ABIs (signed on x86 Linux/Windows, unsigned
//     on AArch64 Linux). SignExtend* makes the signedness of a trail byte or
//     compressed scalar explicit at the decode site.

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace runtime::determinism
{
// constexpr-safe NaN test (std::isnan is not constexpr until C++23): exponent
// all ones with a nonzero mantissa, for quiet and signaling NaN alike.
[[nodiscard]] constexpr bool IsNaN(const double value) noexcept
{
    constexpr std::uint64_t kExponentMask = 0x7FF0000000000000ull;
    constexpr std::uint64_t kMantissaMask = 0x000FFFFFFFFFFFFFull;
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    return (bits & kExponentMask) == kExponentMask
        && (bits & kMantissaMask) != 0;
}

// Defined float-to-int32 conversion shared by every producer that feeds a
// wire-visible integer from a floating-point scalar.
//
// Semantics (identical on every IEEE-754 architecture):
//   NaN            -> 0
//   value <= -2^31 -> INT32_MIN
//   value >= 2^31-1 -> INT32_MAX
//   otherwise      -> exact truncation toward zero (the legacy cast result)
//
// The comparisons are exact in double precision and the final cast is only
// ever reached for values inside the int32 range, where the C and C++
// standards require truncation -- so no path relies on the undefined
// out-of-range conversion the bare cast performs.
[[nodiscard]] constexpr std::int32_t FloatToIntSaturating(const double value) noexcept
{
    constexpr double kInt32Min = -2147483648.0; // exactly representable
    constexpr double kInt32Max = 2147483647.0;  // exactly representable

    if (IsNaN(value))
        return 0;
    if (value <= kInt32Min)
        return (std::numeric_limits<std::int32_t>::min)();
    if (value >= kInt32Max)
        return (std::numeric_limits<std::int32_t>::max)();
    return static_cast<std::int32_t>(value);
}

// Single-precision overload: widening float->double is exact, so the float
// entry point observes the same semantics with no extra rounding.
[[nodiscard]] constexpr std::int32_t FloatToIntSaturating(const float value) noexcept
{
    return FloatToIntSaturating(static_cast<double>(value));
}

// monotonic key mapping a double onto the IEEE 754-2008 totalOrder.
// Comparing keys as unsigned integers yields:
//   -NaN < -Inf < negative finite < -0.0 < +0.0 < positive finite < +Inf < +NaN
[[nodiscard]] constexpr std::uint64_t TotalOrderKey(const double value) noexcept
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    constexpr std::uint64_t kSignMask = std::uint64_t{1} << 63;
    if ((bits & kSignMask) != 0)
        return ~bits; // negative values: reverse magnitude order into the low half
    return bits | kSignMask; // non-negative values: ordered by payload in the high half
}

[[nodiscard]] constexpr bool TotalOrderLess(const double lhs, const double rhs) noexcept
{
    return TotalOrderKey(lhs) < TotalOrderKey(rhs);
}

// binary32 key built from the float's OWN bit pattern -- never a widened
// double. Widening float->double is a hardware conversion that can quieten
// signaling NaNs and reposition or canonicalize the payload, so distinct
// binary32 NaN payloads can collapse onto one binary64 pattern (and the
// collapse can differ between x86 and AArch64). Keying the native 32 bits
// keeps the full binary32 totalOrder -- payload included -- on every
// architecture.
[[nodiscard]] constexpr std::uint32_t TotalOrderKey(const float value) noexcept
{
    const auto bits = std::bit_cast<std::uint32_t>(value);
    constexpr std::uint32_t kSignMask = std::uint32_t{1} << 31;
    if ((bits & kSignMask) != 0)
        return ~bits; // negative values: reverse magnitude order into the low half
    return bits | kSignMask; // non-negative values: ordered by payload in the high half
}

[[nodiscard]] constexpr bool TotalOrderLess(const float lhs, const float rhs) noexcept
{
    return TotalOrderKey(lhs) < TotalOrderKey(rhs);
}

// Explicit little-endian packed-field access. These compose the field from
// individual bytes, so neither host endianness nor pointer alignment changes
// the round-trip: WriteLe* then ReadLe* on the same buffer returns the value,
// and the buffer bytes are identical on every architecture.
[[nodiscard]] constexpr std::uint16_t ReadLe16(const unsigned char *bytes) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8));
}

[[nodiscard]] constexpr std::uint32_t ReadLe32(const unsigned char *bytes) noexcept
{
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | (static_cast<std::uint32_t>(bytes[2]) << 16)
        | (static_cast<std::uint32_t>(bytes[3]) << 24));
}

[[nodiscard]] constexpr std::uint64_t ReadLe64(const unsigned char *bytes) noexcept
{
    return static_cast<std::uint64_t>(ReadLe32(bytes))
        | (static_cast<std::uint64_t>(ReadLe32(bytes + 4)) << 32);
}

constexpr void WriteLe16(unsigned char *bytes, const std::uint16_t value) noexcept
{
    bytes[0] = static_cast<unsigned char>(value & 0xFFu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
}

constexpr void WriteLe32(unsigned char *bytes, const std::uint32_t value) noexcept
{
    bytes[0] = static_cast<unsigned char>(value & 0xFFu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xFFu);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xFFu);
}

constexpr void WriteLe64(unsigned char *bytes, const std::uint64_t value) noexcept
{
    WriteLe32(bytes, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    WriteLe32(bytes + 4, static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFu));
}

// Explicit sign extension for compressed scalars and trail bytes whose high
// bit carries the sign. Decoding through these helpers keeps the value
// independent of the ABI's plain-char signedness.
[[nodiscard]] constexpr std::int32_t SignExtend8(const std::uint8_t value) noexcept
{
    return (value & 0x80u) != 0
        ? static_cast<std::int32_t>(value) - 0x100
        : static_cast<std::int32_t>(value);
}

[[nodiscard]] constexpr std::int32_t SignExtend16(const std::uint16_t value) noexcept
{
    return (value & 0x8000u) != 0
        ? static_cast<std::int32_t>(value) - 0x10000
        : static_cast<std::int32_t>(value);
}

[[nodiscard]] constexpr std::int64_t SignExtend32(const std::uint32_t value) noexcept
{
    return (value & 0x80000000u) != 0
        ? static_cast<std::int64_t>(value) - 0x100000000LL
        : static_cast<std::int64_t>(value);
}
} // namespace runtime::determinism
