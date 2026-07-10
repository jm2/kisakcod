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
