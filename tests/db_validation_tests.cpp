#include <database/db_validation.h>

#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;

void Expect(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main()
{
    Expect(db::validation::CanInternString(1), "empty terminated string can be interned");
    Expect(db::validation::CanInternString(65531), "maximum script-memory string can be interned");
    Expect(!db::validation::CanInternString(0), "zero-byte string extent rejected");
    Expect(!db::validation::CanInternString(65532), "script-memory allocation ceiling enforced");
    Expect(db::validation::PointerCountConsistent(false, 0), "null zero-count span is consistent");
    Expect(db::validation::PointerCountConsistent(true, 0), "present zero-count span is consistent");
    Expect(db::validation::PointerCountConsistent(true, 1), "present nonempty span is consistent");
    Expect(!db::validation::PointerCountConsistent(false, 1), "missing nonempty span rejected");
    Expect(!db::validation::PointerCountConsistent(false, -1), "negative span count rejected");
    Expect(!db::validation::PointerCountConsistent(true, -1), "present negative span count rejected");
    Expect(db::validation::CountInRange(96, 96, 65536), "minimum font glyph count accepted");
    Expect(db::validation::CountInRange(65536, 96, 65536), "maximum font glyph count accepted");
    Expect(!db::validation::CountInRange(95, 96, 65536), "short font glyph table rejected");
    Expect(!db::validation::CountInRange(65537, 96, 65536), "oversized font glyph table rejected");
    Expect(!db::validation::CountInRange(1, 2, 1), "invalid count range rejected");
    Expect(db::validation::OptionalMirroredCountInRange(0, 0, 2, 16), "empty mirrored graph accepted");
    Expect(db::validation::OptionalMirroredCountInRange(2, 2, 2, 16), "minimum mirrored graph accepted");
    Expect(db::validation::OptionalMirroredCountInRange(16, 16, 2, 16), "maximum mirrored graph accepted");
    Expect(!db::validation::OptionalMirroredCountInRange(1, 1, 2, 16), "short mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(17, 17, 2, 16), "oversized mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(4, 5, 2, 16), "mismatched mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(-1, -1, 2, 16), "negative mirrored graph rejected");

    const float minimumGraph[][2] = {{0.0f, 0.2f}, {1.0f, 0.4f}};
    const float validGraph[][2] = {{0.0f, 0.2f}, {0.5f, 0.8f}, {1.0f, 0.4f}};
    const float missingStart[][2] = {{0.1f, 0.2f}, {1.0f, 0.4f}};
    const float missingEnd[][2] = {{0.0f, 0.2f}, {0.9f, 0.4f}};
    const float duplicateX[][2] = {{0.0f, 0.2f}, {0.5f, 0.8f}, {0.5f, 0.4f}, {1.0f, 0.3f}};
    const float decreasingX[][2] = {{0.0f, 0.2f}, {0.7f, 0.8f}, {0.5f, 0.4f}, {1.0f, 0.3f}};
    const float invalidY[][2] = {{0.0f, -0.1f}, {1.0f, 0.4f}};
    const float nanGraph[][2] = {
        {0.0f, 0.2f},
        {(std::numeric_limits<float>::quiet_NaN)(), 0.8f},
        {1.0f, 0.4f}};
    const float infiniteGraph[][2] = {
        {0.0f, 0.2f},
        {0.5f, (std::numeric_limits<float>::infinity)()},
        {1.0f, 0.4f}};
    float maximumGraph[16][2] = {};
    for (std::uint32_t index = 0; index < 16; ++index)
    {
        maximumGraph[index][0] = static_cast<float>(index) / 15.0f;
        maximumGraph[index][1] = 0.5f;
    }
    Expect(db::validation::NormalizedGraphKnots(minimumGraph, 2), "two-knot graph accepted");
    Expect(db::validation::NormalizedGraphKnots(validGraph, 3), "normalized graph accepted");
    Expect(db::validation::NormalizedGraphKnots(maximumGraph, 16), "sixteen-knot graph accepted");
    Expect(!db::validation::NormalizedGraphKnots(nullptr, 3), "missing graph rejected");
    Expect(!db::validation::NormalizedGraphKnots(validGraph, 1), "single-knot graph rejected");
    Expect(!db::validation::NormalizedGraphKnots(missingStart, 2), "graph missing zero endpoint rejected");
    Expect(!db::validation::NormalizedGraphKnots(missingEnd, 2), "graph missing one endpoint rejected");
    Expect(!db::validation::NormalizedGraphKnots(duplicateX, 4), "duplicate graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(decreasingX, 4), "decreasing graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(invalidY, 2), "out-of-range graph value rejected");
    Expect(!db::validation::NormalizedGraphKnots(nanGraph, 3), "NaN graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(infiniteGraph, 3), "infinite graph value rejected");

    const std::uint16_t validIndices[] = {0, 2, 3};
    const std::uint16_t invalidIndices[] = {0, 4};
    Expect(db::validation::AllU16Below(validIndices, 3, 4), "bounded uint16 indices accepted");
    Expect(!db::validation::AllU16Below(invalidIndices, 2, 4), "out-of-range uint16 index rejected");
    Expect(db::validation::AllU16Below(nullptr, 0, 0), "empty uint16 index list accepted");
    Expect(!db::validation::AllU16Below(nullptr, 1, 4), "missing uint16 index list rejected");

    std::uint32_t spanBytes = UINT32_MAX;
    Expect(db::validation::CheckedSpanBytes(0, 20, &spanBytes) && spanBytes == 0, "zero span size");
    Expect(db::validation::CheckedSpanBytes(12, 20, &spanBytes) && spanBytes == 240, "direct span size");
    Expect(db::validation::CheckedSpanBytes(UINT64_C(214748364), 20, &spanBytes)
        && spanBytes == UINT32_C(4294967280), "maximum direct span product");
    Expect(!db::validation::CheckedSpanBytes(UINT64_C(214748365), 20, &spanBytes), "direct span overflow rejected");
    Expect(!db::validation::CheckedSpanBytes((std::numeric_limits<std::uint64_t>::max)(), 1, &spanBytes), "oversized direct span count rejected");
    Expect(!db::validation::CheckedSpanBytes(1, 0, &spanBytes), "zero direct span stride rejected");
    Expect(!db::validation::CheckedSpanBytes(1, 1, nullptr), "null direct span result rejected");

    std::int32_t bytes = -1;
    Expect(db::validation::CheckedArrayBytes(0, 8, &bytes) && bytes == 0, "zero array size");
    Expect(db::validation::CheckedArrayBytes(32768, 8, &bytes) && bytes == 262144, "asset array size");
    Expect(!db::validation::CheckedArrayBytes(-1, 8, &bytes), "negative count rejected");
    Expect(!db::validation::CheckedArrayBytes(1, 0, &bytes), "zero stride rejected");
    Expect(!db::validation::CheckedArrayBytes((std::numeric_limits<std::int32_t>::max)(), 8, &bytes), "multiplication overflow rejected");
    Expect(!db::validation::CheckedArrayBytes(1, 8, nullptr), "null byte result rejected");

    std::int32_t count = -1;
    Expect(db::validation::CheckedCountSum(12, 30, &count) && count == 42, "count sum");
    Expect(!db::validation::CheckedCountSum(-1, 1, &count), "negative sum operand rejected");
    Expect(!db::validation::CheckedCountSum(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count), "count sum overflow rejected");
    Expect(!db::validation::CheckedCountSum(1, 1, nullptr), "null count sum result rejected");
    Expect(db::validation::CheckedCountProduct(6, 7, &count) && count == 42, "count product");
    Expect(db::validation::CheckedCountProduct(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count product");
    Expect(db::validation::CheckedCountProduct(0, 42, &count) && count == 0, "zero product");
    Expect(!db::validation::CheckedCountProduct(
        0, (std::numeric_limits<std::uint32_t>::max)(), &count), "oversized zero-product operand rejected");
    Expect(!db::validation::CheckedCountProduct(-1, 0, &count), "negative product operand rejected");
    Expect(!db::validation::CheckedCountProduct(
        (std::numeric_limits<std::uint32_t>::max)(), 2, &count), "count product overflow rejected");
    Expect(!db::validation::CheckedCountProduct(1, 1, nullptr), "null count product result rejected");
    Expect(db::validation::CheckedCountDifference(10, 3, &count) && count == 7, "count difference");
    Expect(db::validation::CheckedCountDifference(
        (std::numeric_limits<std::int32_t>::max)(), 0, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count difference");
    Expect(!db::validation::CheckedCountDifference(3, 10, &count), "count subtraction underflow rejected");
    Expect(!db::validation::CheckedCountDifference(
        (std::numeric_limits<std::uint32_t>::max)(),
        (std::numeric_limits<std::uint32_t>::max)(),
        &count), "oversized difference operands rejected");
    Expect(!db::validation::CheckedCountDifference(1, 0, nullptr), "null count difference result rejected");
    Expect(db::validation::CheckedCountCeilDiv(33, 32, &count) && count == 2, "count ceiling division");
    Expect(db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::int32_t>::max)(), 32, &count)
        && count == 67108864, "large count ceiling division");
    Expect(db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count ceiling division");
    Expect(!db::validation::CheckedCountCeilDiv(1, 0, &count), "zero divisor rejected");
    Expect(!db::validation::CheckedCountCeilDiv(1, 1, nullptr), "null ceiling-division result rejected");
    Expect(!db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::uint32_t>::max)(), 32, &count), "oversized dividend rejected");

    Expect(db::validation::CanAppendBytes(0x40000, 0x40000, 0x80000), "double-buffer append");
    Expect(db::validation::CanAppendBytes(0x80000, 0, 0x80000), "zero append at capacity");
    Expect(!db::validation::CanAppendBytes(0x80001, 0, 0x80000), "current bytes over capacity rejected");
    Expect(!db::validation::CanAppendBytes(0x40001, 0x40000, 0x80000), "append over capacity rejected");

    Expect(db::validation::IsAlignmentMask(0), "zero alignment mask");
    Expect(db::validation::IsAlignmentMask(1), "two-byte alignment mask");
    Expect(db::validation::IsAlignmentMask(15), "sixteen-byte alignment mask");
    Expect(!db::validation::IsAlignmentMask(5), "noncontiguous alignment mask rejected");
    Expect(!db::validation::IsAlignmentMask((std::numeric_limits<std::uintptr_t>::max)()), "maximum mask rejected");

    std::uintptr_t aligned = 0;
    Expect(db::validation::AlignUp(0x1001, 15, &aligned) && aligned == 0x1010, "alignment rounds up");
    Expect(db::validation::AlignUp(0x1010, 15, &aligned) && aligned == 0x1010, "aligned value preserved");
    Expect(!db::validation::AlignUp((std::numeric_limits<std::uintptr_t>::max)() - 7, 15, &aligned), "alignment overflow rejected");

    constexpr std::uintptr_t base = 0x1000;
    Expect(db::validation::SpanWithinBlock(base, 0x100, base, 0x100), "whole block span");
    Expect(db::validation::SpanWithinBlock(base, 0x100, base + 0x100, 0), "exact end empty span");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base - 1, 1), "position before block rejected");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base + 0x100, 1), "span past end rejected");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base + 0x80, 0x81), "oversized tail span rejected");

    std::uint32_t remaining = 0;
    Expect(db::validation::RemainingInBlock(base, 0x100, base + 0x40, &remaining) && remaining == 0xC0, "remaining bytes computed");
    Expect(db::validation::RemainingInBlock(base, 0x100, base + 0x100, &remaining) && remaining == 0, "zero bytes at block end");
    Expect(!db::validation::RemainingInBlock(base, 0x100, base + 0x101, &remaining), "position after block rejected");

    return failures == 0 ? 0 : 1;
}
