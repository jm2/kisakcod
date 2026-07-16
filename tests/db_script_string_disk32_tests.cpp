#include <database/db_xasset_disk32.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace
{
namespace xasset = db::xasset;

int failures = 0;

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

template <typename TYPE>
std::array<std::uint8_t, sizeof(TYPE)> ObjectBytes(const TYPE &object)
{
    static_assert(std::is_trivially_copyable_v<TYPE>);
    std::array<std::uint8_t, sizeof(TYPE)> bytes{};
    std::memcpy(bytes.data(), &object, bytes.size());
    return bytes;
}

template <std::size_t SIZE>
void StoreU32(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset <= SIZE && SIZE - offset >= 4u);
    if (!bytes || offset > SIZE || SIZE - offset < 4u)
        return;
    (*bytes)[offset + 0u] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1u] =
        static_cast<std::uint8_t>(value >> 8u);
    (*bytes)[offset + 2u] =
        static_cast<std::uint8_t>(value >> 16u);
    (*bytes)[offset + 3u] =
        static_cast<std::uint8_t>(value >> 24u);
}

xasset::ScriptStringListDisk32 MakeList(
    const std::int32_t count,
    const std::uint32_t token)
{
    xasset::ScriptStringListDisk32 list{};
    list.count = count;
    list.strings.token.value = token;
    return list;
}

xasset::ScriptStringTokenDisk32 MakeToken(
    const std::uint32_t value)
{
    xasset::ScriptStringTokenDisk32 token{};
    token.token.value = value;
    return token;
}

xasset::ScriptStringListDisk32Layout SentinelLayout()
{
    return {
        UINT32_C(0xDEADBEEF),
        INT32_C(0x13572468)};
}

void CheckLayoutUnchanged(
    const xasset::ScriptStringListDisk32Layout &layout)
{
    CHECK(layout.tokenBytes == UINT32_C(0xDEADBEEF));
    CHECK(layout.stringCount == INT32_C(0x13572468));
}

void TestExactTokenSchema()
{
    static_assert(
        sizeof(xasset::ScriptStringTokenDisk32) == 0x04);
    static_assert(
        alignof(xasset::ScriptStringTokenDisk32) == 4);
    static_assert(
        std::is_trivial_v<xasset::ScriptStringTokenDisk32>);
    static_assert(
        std::is_standard_layout_v<
            xasset::ScriptStringTokenDisk32>);
    static_assert(
        std::is_trivially_copyable_v<
            xasset::ScriptStringTokenDisk32>);

    const xasset::ScriptStringTokenDisk32 token =
        MakeToken(UINT32_C(0xFEDCBA98));
    std::array<std::uint8_t, 4> expected{};
    StoreU32(&expected, 0, UINT32_C(0xFEDCBA98));
    CHECK(ObjectBytes(token) == expected);
}

void TestHeaderValidationAndCheckedExtents()
{
    xasset::ScriptStringListDisk32Layout layout =
        SentinelLayout();
    const xasset::ScriptStringListDisk32 empty =
        MakeList(0, 0);
    CHECK(
        xasset::TryValidateScriptStringListDisk32Header(
            &empty, &layout)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(layout.tokenBytes == 0);
    CHECK(layout.stringCount == 0);

    const xasset::ScriptStringListDisk32 highRoot =
        MakeList(1, UINT32_C(0xF1234567));
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateScriptStringListDisk32Header(
            &highRoot, &layout)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(
        layout.tokenBytes
        == sizeof(xasset::ScriptStringTokenDisk32));
    CHECK(layout.stringCount == 1);

    const xasset::ScriptStringListDisk32 maximum =
        MakeList(
            xasset::kMaxScriptStringListStrings,
            disk32::kSharedInline);
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateScriptStringListDisk32Header(
            &maximum, &layout)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(layout.tokenBytes == UINT32_C(262144));
    CHECK(
        layout.stringCount
        == xasset::kMaxScriptStringListStrings);

    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateScriptStringListDisk32Header(
            nullptr, &layout)
        == xasset::ScriptStringListDisk32Status::
            InvalidArgument);
    CheckLayoutUnchanged(layout);
    CHECK(
        xasset::TryValidateScriptStringListDisk32Header(
            &empty, nullptr)
        == xasset::ScriptStringListDisk32Status::
            InvalidArgument);

    constexpr std::int32_t largestNonoverflowingCount =
        static_cast<std::int32_t>(
            (std::numeric_limits<std::uint32_t>::max)()
            / sizeof(xasset::ScriptStringTokenDisk32));
    for (const std::int32_t count : {
             -1,
             xasset::kMaxScriptStringListStrings + 1,
             largestNonoverflowingCount})
    {
        const xasset::ScriptStringListDisk32 invalid =
            MakeList(count, disk32::kInline);
        layout = SentinelLayout();
        CHECK(
            xasset::TryValidateScriptStringListDisk32Header(
                &invalid, &layout)
            == xasset::ScriptStringListDisk32Status::
                InvalidStringCount);
        CheckLayoutUnchanged(layout);
    }

    for (const std::int32_t count : {
             largestNonoverflowingCount + 1,
             (std::numeric_limits<std::int32_t>::max)()})
    {
        const xasset::ScriptStringListDisk32 overflowing =
            MakeList(count, disk32::kInline);
        layout = SentinelLayout();
        CHECK(
            xasset::TryValidateScriptStringListDisk32Header(
                &overflowing, &layout)
            == xasset::ScriptStringListDisk32Status::
                SizeOverflow);
        CheckLayoutUnchanged(layout);
    }

    for (const xasset::ScriptStringListDisk32 invalid : {
             MakeList(0, disk32::kInline),
             MakeList(1, 0)})
    {
        layout = SentinelLayout();
        CHECK(
            xasset::TryValidateScriptStringListDisk32Header(
                &invalid, &layout)
            == xasset::ScriptStringListDisk32Status::
                InvalidStringPointerCount);
        CheckLayoutUnchanged(layout);
    }
}

void TestSpanBoundsAndEmptyIteration()
{
    const xasset::ScriptStringListDisk32 one =
        MakeList(1, disk32::kInline);
    const xasset::ScriptStringTokenDisk32 record =
        MakeToken(disk32::kInline);
    xasset::ScriptStringListDisk32Layout layout =
        SentinelLayout();

    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &one, nullptr, sizeof(record), &layout)
        == xasset::ScriptStringListDisk32Status::
            InvalidTokenSpan);
    CheckLayoutUnchanged(layout);
    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &one, &record, sizeof(record) - 1u, &layout)
        == xasset::ScriptStringListDisk32Status::
            TruncatedTokenSpan);
    CheckLayoutUnchanged(layout);

    const std::array<xasset::ScriptStringTokenDisk32, 2>
        trailing{
            MakeToken(UINT32_C(0xF1234567)),
            MakeToken(disk32::kSharedInline)};
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &one, trailing.data(), sizeof(trailing), &layout)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(layout.tokenBytes == sizeof(trailing[0]));
    CHECK(layout.stringCount == 1);

    const xasset::ScriptStringListDisk32 empty =
        MakeList(0, 0);
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &empty, nullptr, 0, &layout)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(layout.tokenBytes == 0);
    CHECK(layout.stringCount == 0);

    xasset::ScriptStringListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &empty, nullptr, 0, &iterator)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(iterator.nextIndex() == 0);
    CHECK(iterator.remaining() == 0);

    xasset::ScriptStringTokenDisk32 output =
        MakeToken(UINT32_C(0xDEADBEEF));
    const auto outputBeforeEnd = ObjectBytes(output);
    const auto iteratorBeforeEnd = ObjectBytes(iterator);
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &iterator, &output)
        == xasset::ScriptStringListDisk32Status::End);
    CHECK(ObjectBytes(output) == outputBeforeEnd);
    CHECK(ObjectBytes(iterator) == iteratorBeforeEnd);

    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &empty, &record, 0, &layout)
        == xasset::ScriptStringListDisk32Status::
            InvalidTokenSpan);
    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &empty, nullptr, 1, &layout)
        == xasset::ScriptStringListDisk32Status::
            InvalidTokenSpan);

    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            nullptr, &record, sizeof(record), &layout)
        == xasset::ScriptStringListDisk32Status::
            InvalidArgument);
    CheckLayoutUnchanged(layout);
    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &one, &record, sizeof(record), nullptr)
        == xasset::ScriptStringListDisk32Status::
            InvalidArgument);
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &one, &record, sizeof(record), nullptr)
        == xasset::ScriptStringListDisk32Status::
            InvalidArgument);
}

void TestTokenClassesAndRawPreservation()
{
    constexpr std::array<std::uint32_t, 5> rawTokens{
        0,
        disk32::kInline,
        UINT32_C(0x00000001),
        UINT32_C(0xF1234567),
        UINT32_C(0xFFFFFFFD)};
    std::array<
        xasset::ScriptStringTokenDisk32,
        rawTokens.size()>
        records{};
    for (std::size_t index = 0; index < records.size(); ++index)
        records[index] = MakeToken(rawTokens[index]);

    const xasset::ScriptStringListDisk32 list =
        MakeList(
            static_cast<std::int32_t>(records.size()),
            UINT32_C(0x80000001));
    xasset::ScriptStringListDisk32Layout layout{};
    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &list, records.data(), sizeof(records), &layout)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(
        layout.tokenBytes
        == records.size() * sizeof(records[0]));
    CHECK(
        layout.stringCount
        == static_cast<std::int32_t>(records.size()));

    xasset::ScriptStringListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &list, records.data(), sizeof(records), &iterator)
        == xasset::ScriptStringListDisk32Status::Success);
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        xasset::ScriptStringTokenDisk32 output{};
        CHECK(
            xasset::TryNextScriptStringTokenDisk32(
                &iterator, &output)
            == xasset::ScriptStringListDisk32Status::Success);
        CHECK(output.token.value == rawTokens[index]);
    }

    CHECK(records[0].token.isNull());
    CHECK(records[1].token.isInline());
    CHECK(records[2].token.isOffset());
    CHECK(records[3].token.isOffset());
    CHECK(records[4].token.isOffset());
}

void TestUnalignedIteratorAndGuardBytes()
{
    constexpr std::uint8_t prefixGuard = UINT8_C(0xA5);
    constexpr std::uint8_t suffixGuard0 = UINT8_C(0x5A);
    constexpr std::uint8_t suffixGuard1 = UINT8_C(0xC3);
    constexpr std::size_t recordCount = 4;
    std::array<
        std::uint8_t,
        1u
            + recordCount
                * sizeof(xasset::ScriptStringTokenDisk32)
            + 2u>
        bytes{};
    bytes.fill(UINT8_C(0xCC));
    bytes.front() = prefixGuard;
    bytes[bytes.size() - 2u] = suffixGuard0;
    bytes[bytes.size() - 1u] = suffixGuard1;

    const std::array<
        xasset::ScriptStringTokenDisk32,
        recordCount>
        records{
            MakeToken(0),
            MakeToken(disk32::kInline),
            MakeToken(UINT32_C(0x80000001)),
            MakeToken(UINT32_C(0xFEDCBA98))};
    std::memcpy(
        bytes.data() + 1u,
        records.data(),
        sizeof(records));

    const xasset::ScriptStringListDisk32 list =
        MakeList(
            static_cast<std::int32_t>(records.size()),
            disk32::kInline);
    xasset::ScriptStringListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &list,
            bytes.data() + 1u,
            bytes.size() - 1u,
            &iterator)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(iterator.nextIndex() == 0);
    CHECK(
        iterator.remaining()
        == static_cast<std::int32_t>(recordCount));

    xasset::ScriptStringTokenDisk32 output{};
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &iterator, &output)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(output.token.value == records[0].token.value);

    xasset::ScriptStringListDisk32Iterator copy = iterator;
    for (std::size_t index = 1; index < records.size(); ++index)
    {
        CHECK(
            xasset::TryNextScriptStringTokenDisk32(
                &iterator, &output)
            == xasset::ScriptStringListDisk32Status::Success);
        CHECK(output.token.value == records[index].token.value);
    }
    CHECK(
        copy.nextIndex() == 1
        && copy.remaining()
            == static_cast<std::int32_t>(recordCount - 1u));

    output = MakeToken(UINT32_C(0xDEADBEEF));
    const auto outputBeforeEnd = ObjectBytes(output);
    const auto iteratorBeforeEnd = ObjectBytes(iterator);
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &iterator, &output)
        == xasset::ScriptStringListDisk32Status::End);
    CHECK(ObjectBytes(output) == outputBeforeEnd);
    CHECK(ObjectBytes(iterator) == iteratorBeforeEnd);

    CHECK(bytes.front() == prefixGuard);
    CHECK(bytes[bytes.size() - 2u] == suffixGuard0);
    CHECK(bytes[bytes.size() - 1u] == suffixGuard1);
}

void TestSharedInlinePreflightIsAtomic()
{
    std::array<xasset::ScriptStringTokenDisk32, 4> rejected{
        MakeToken(0),
        MakeToken(disk32::kInline),
        MakeToken(disk32::kSharedInline),
        MakeToken(UINT32_C(0xF1234567))};
    const xasset::ScriptStringListDisk32 list =
        MakeList(
            static_cast<std::int32_t>(rejected.size()),
            disk32::kSharedInline);

    xasset::ScriptStringListDisk32Layout layout =
        SentinelLayout();
    CHECK(
        xasset::TryValidateScriptStringListDisk32Span(
            &list, rejected.data(), sizeof(rejected), &layout)
        == xasset::ScriptStringListDisk32Status::
            UnsupportedSharedInline);
    CheckLayoutUnchanged(layout);

    xasset::ScriptStringTokenDisk32 accepted =
        MakeToken(disk32::kInline);
    const xasset::ScriptStringListDisk32 acceptedList =
        MakeList(1, disk32::kInline);
    xasset::ScriptStringListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &acceptedList,
            &accepted,
            sizeof(accepted),
            &iterator)
        == xasset::ScriptStringListDisk32Status::Success);
    const auto iteratorBeforeRejectedBegin =
        ObjectBytes(iterator);
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &list,
            rejected.data(),
            sizeof(rejected),
            &iterator)
        == xasset::ScriptStringListDisk32Status::
            UnsupportedSharedInline);
    CHECK(
        ObjectBytes(iterator)
        == iteratorBeforeRejectedBegin);

    xasset::ScriptStringListDisk32Iterator invalid{};
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &list,
            rejected.data(),
            sizeof(rejected),
            &invalid)
        == xasset::ScriptStringListDisk32Status::
            UnsupportedSharedInline);
    xasset::ScriptStringTokenDisk32 output{};
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &invalid, &output)
        == xasset::ScriptStringListDisk32Status::
            InvalidIterator);
}

void TestMutationRevalidationAndFailureAtomicity()
{
    std::array<xasset::ScriptStringTokenDisk32, 2> records{
        MakeToken(disk32::kInline),
        MakeToken(UINT32_C(0x80000001))};
    const xasset::ScriptStringListDisk32 list =
        MakeList(2, disk32::kInline);
    xasset::ScriptStringListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &list, records.data(), sizeof(records), &iterator)
        == xasset::ScriptStringListDisk32Status::Success);

    xasset::ScriptStringTokenDisk32 output =
        MakeToken(UINT32_C(0xDEADBEEF));
    records[0] = MakeToken(disk32::kSharedInline);
    auto outputBefore = ObjectBytes(output);
    auto iteratorBefore = ObjectBytes(iterator);
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &iterator, &output)
        == xasset::ScriptStringListDisk32Status::
            UnsupportedSharedInline);
    CHECK(ObjectBytes(output) == outputBefore);
    CHECK(ObjectBytes(iterator) == iteratorBefore);

    records[0] = MakeToken(disk32::kInline);
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &iterator, &output)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(output.token.value == disk32::kInline);
    CHECK(iterator.nextIndex() == 1);
    CHECK(iterator.remaining() == 1);

    records[1] = MakeToken(disk32::kSharedInline);
    output = MakeToken(UINT32_C(0xDEADBEEF));
    outputBefore = ObjectBytes(output);
    iteratorBefore = ObjectBytes(iterator);
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &iterator, &output)
        == xasset::ScriptStringListDisk32Status::
            UnsupportedSharedInline);
    CHECK(ObjectBytes(output) == outputBefore);
    CHECK(ObjectBytes(iterator) == iteratorBefore);

    records[1] = MakeToken(UINT32_C(0xF7654321));
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &iterator, &output)
        == xasset::ScriptStringListDisk32Status::Success);
    CHECK(output.token.value == UINT32_C(0xF7654321));
    CHECK(iterator.nextIndex() == 2);
    CHECK(iterator.remaining() == 0);

    iteratorBefore = ObjectBytes(iterator);
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            nullptr, &output)
        == xasset::ScriptStringListDisk32Status::
            InvalidArgument);
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &iterator, nullptr)
        == xasset::ScriptStringListDisk32Status::
            InvalidArgument);
    CHECK(ObjectBytes(iterator) == iteratorBefore);

    xasset::ScriptStringListDisk32Iterator invalid{};
    outputBefore = ObjectBytes(output);
    CHECK(
        xasset::TryNextScriptStringTokenDisk32(
            &invalid, &output)
        == xasset::ScriptStringListDisk32Status::
            InvalidIterator);
    CHECK(ObjectBytes(output) == outputBefore);
}

void TestMaximumTokenIteration()
{
    std::vector<xasset::ScriptStringTokenDisk32> records(
        static_cast<std::size_t>(
            xasset::kMaxScriptStringListStrings));
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        switch (index % 4u)
        {
        case 0:
            records[index] = MakeToken(0);
            break;
        case 1:
            records[index] = MakeToken(disk32::kInline);
            break;
        case 2:
            records[index] = MakeToken(
                static_cast<std::uint32_t>(index + 1u));
            break;
        default:
            records[index] = MakeToken(
                UINT32_C(0x80000001)
                | static_cast<std::uint32_t>(index));
            break;
        }
    }

    const xasset::ScriptStringListDisk32 list = MakeList(
        xasset::kMaxScriptStringListStrings,
        disk32::kInline);
    xasset::ScriptStringListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginScriptStringListDisk32(
            &list,
            records.data(),
            records.size() * sizeof(records[0]),
            &iterator)
        == xasset::ScriptStringListDisk32Status::Success);

    for (std::size_t index = 0; index < records.size(); ++index)
    {
        xasset::ScriptStringTokenDisk32 output{};
        CHECK(
            xasset::TryNextScriptStringTokenDisk32(
                &iterator, &output)
            == xasset::ScriptStringListDisk32Status::Success);
        CHECK(output.token.value == records[index].token.value);
    }
    CHECK(
        iterator.nextIndex()
        == xasset::kMaxScriptStringListStrings);
    CHECK(iterator.remaining() == 0);
}
} // namespace

int main()
{
    TestExactTokenSchema();
    TestHeaderValidationAndCheckedExtents();
    TestSpanBoundsAndEmptyIteration();
    TestTokenClassesAndRawPreservation();
    TestUnalignedIteratorAndGuardBytes();
    TestSharedInlinePreflightIsAtomic();
    TestMutationRevalidationAndFailureAtomicity();
    TestMaximumTokenIteration();

    if (failures != 0)
        return 1;
    std::puts("Disk32 script-string token walk tests passed");
    return 0;
}
