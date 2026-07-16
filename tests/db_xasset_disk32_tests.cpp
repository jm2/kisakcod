#include <database/db_asset_mode.h>
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

xasset::XAssetListDisk32 MakeList(
    const std::int32_t assetCount,
    const std::uint32_t assetToken,
    const std::int32_t stringCount = 0,
    const std::uint32_t stringToken = 0)
{
    xasset::XAssetListDisk32 list{};
    list.stringList.count = stringCount;
    list.stringList.strings.token.value = stringToken;
    list.assetCount = assetCount;
    list.assets.token.value = assetToken;
    return list;
}

xasset::XAssetDisk32 MakeAsset(
    const std::int32_t type,
    const std::uint32_t token)
{
    xasset::XAssetDisk32 asset{};
    asset.type = type;
    asset.header.token.value = token;
    return asset;
}

struct AdmissionProbe final
{
    std::int32_t rejectedType = -1;
    std::int32_t calls = 0;
    std::int32_t lastType = -1;
};

bool AdmitType(
    void *const opaqueContext,
    const std::int32_t rawType) noexcept
{
    auto *const probe =
        static_cast<AdmissionProbe *>(opaqueContext);
    if (!probe)
        return true;
    ++probe->calls;
    probe->lastType = rawType;
    return rawType != probe->rejectedType;
}

xasset::XAssetTypeDisk32Policy MakePolicy(
    AdmissionProbe *const probe,
    const std::int32_t typeCount =
        db::asset_mode::kAssetTypeCount)
{
    return {typeCount, probe, AdmitType};
}

bool AdmitBuildType(
    void *const opaqueContext,
    const std::int32_t rawType) noexcept
{
    if (!opaqueContext)
        return false;
    const auto mode =
        *static_cast<const db::asset_mode::BuildMode *>(
            opaqueContext);
    return db::asset_mode::IsAssetTypeSupported(mode, rawType);
}

void CheckLayoutUnchanged(
    const xasset::XAssetListDisk32Layout &layout)
{
    CHECK(layout.assetBytes == UINT32_C(0xDEADBEEF));
    CHECK(layout.assetCount == INT32_C(0x13572468));
    CHECK(layout.scriptStringCount == INT32_C(0x24681357));
}

xasset::XAssetListDisk32Layout SentinelLayout()
{
    return {
        UINT32_C(0xDEADBEEF),
        INT32_C(0x13572468),
        INT32_C(0x24681357)};
}

void TestExactSchemaBytes()
{
    static_assert(sizeof(xasset::XAssetHeaderDisk32) == 0x04);
    static_assert(sizeof(xasset::XAssetDisk32) == 0x08);
    static_assert(sizeof(xasset::ScriptStringListDisk32) == 0x08);
    static_assert(sizeof(xasset::XAssetListDisk32) == 0x10);
    static_assert(alignof(xasset::XAssetHeaderDisk32) == 4);
    static_assert(alignof(xasset::XAssetDisk32) == 4);
    static_assert(alignof(xasset::ScriptStringListDisk32) == 4);
    static_assert(alignof(xasset::XAssetListDisk32) == 4);

    xasset::XAssetHeaderDisk32 header{};
    header.token.value = UINT32_C(0xFEDCBA98);
    constexpr std::array<std::uint8_t, 4> headerExpected{
        0x98, 0xBA, 0xDC, 0xFE};
    CHECK(ObjectBytes(header) == headerExpected);

    const xasset::XAssetDisk32 asset =
        MakeAsset(INT32_C(0x01020304), UINT32_C(0x89ABCDEF));
    std::array<std::uint8_t, 8> assetExpected{};
    StoreU32(&assetExpected, 0x00, UINT32_C(0x01020304));
    StoreU32(&assetExpected, 0x04, UINT32_C(0x89ABCDEF));
    CHECK(ObjectBytes(asset) == assetExpected);

    xasset::ScriptStringListDisk32 strings{};
    strings.count = INT32_C(0x10203040);
    strings.strings.token.value = UINT32_C(0xF1020304);
    std::array<std::uint8_t, 8> stringsExpected{};
    StoreU32(&stringsExpected, 0x00, UINT32_C(0x10203040));
    StoreU32(&stringsExpected, 0x04, UINT32_C(0xF1020304));
    CHECK(ObjectBytes(strings) == stringsExpected);

    const xasset::XAssetListDisk32 list = MakeList(
        INT32_C(0x01020304),
        UINT32_C(0x87654321),
        INT32_C(0x11223344),
        UINT32_C(0xFEDCBA98));
    std::array<std::uint8_t, 16> listExpected{};
    StoreU32(&listExpected, 0x00, UINT32_C(0x11223344));
    StoreU32(&listExpected, 0x04, UINT32_C(0xFEDCBA98));
    StoreU32(&listExpected, 0x08, UINT32_C(0x01020304));
    StoreU32(&listExpected, 0x0C, UINT32_C(0x87654321));
    CHECK(ObjectBytes(list) == listExpected);
}

void TestHeaderValidationAndLimits()
{
    xasset::XAssetListDisk32Layout layout = SentinelLayout();
    const xasset::XAssetListDisk32 empty = MakeList(0, 0);
    CHECK(
        xasset::TryValidateXAssetListDisk32Header(
            &empty, &layout)
        == xasset::XAssetListDisk32Status::Success);
    CHECK(layout.assetBytes == 0);
    CHECK(layout.assetCount == 0);
    CHECK(layout.scriptStringCount == 0);

    const xasset::XAssetListDisk32 highTokens = MakeList(
        1,
        UINT32_C(0x80000001),
        1,
        UINT32_C(0xF0000001));
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateXAssetListDisk32Header(
            &highTokens, &layout)
        == xasset::XAssetListDisk32Status::Success);
    CHECK(layout.assetBytes == sizeof(xasset::XAssetDisk32));
    CHECK(layout.assetCount == 1);
    CHECK(layout.scriptStringCount == 1);

    const xasset::XAssetListDisk32 maximums = MakeList(
        xasset::kMaxXAssetListAssets,
        disk32::kInline,
        xasset::kMaxScriptStringListStrings,
        disk32::kSharedInline);
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateXAssetListDisk32Header(
            &maximums, &layout)
        == xasset::XAssetListDisk32Status::Success);
    CHECK(layout.assetBytes == UINT32_C(262144));
    CHECK(layout.assetCount == xasset::kMaxXAssetListAssets);
    CHECK(
        layout.scriptStringCount
        == xasset::kMaxScriptStringListStrings);

    CHECK(
        xasset::TryValidateXAssetListDisk32Header(
            nullptr, &layout)
        == xasset::XAssetListDisk32Status::InvalidArgument);
    CHECK(
        xasset::TryValidateXAssetListDisk32Header(
            &empty, nullptr)
        == xasset::XAssetListDisk32Status::InvalidArgument);

    for (const std::int32_t count : {
             -1,
             xasset::kMaxXAssetListAssets + 1,
             (std::numeric_limits<std::int32_t>::max)()})
    {
        const xasset::XAssetListDisk32 invalid =
            MakeList(count, disk32::kInline);
        layout = SentinelLayout();
        CHECK(
            xasset::TryValidateXAssetListDisk32Header(
                &invalid, &layout)
            == xasset::XAssetListDisk32Status::InvalidAssetCount);
        CheckLayoutUnchanged(layout);
    }

    for (const xasset::XAssetListDisk32 invalid : {
             MakeList(0, disk32::kInline),
             MakeList(1, 0)})
    {
        layout = SentinelLayout();
        CHECK(
            xasset::TryValidateXAssetListDisk32Header(
                &invalid, &layout)
            == xasset::XAssetListDisk32Status::
                InvalidAssetPointerCount);
        CheckLayoutUnchanged(layout);
    }

    for (const std::int32_t count : {
             -1,
             xasset::kMaxScriptStringListStrings + 1,
             (std::numeric_limits<std::int32_t>::max)()})
    {
        const xasset::XAssetListDisk32 invalid =
            MakeList(0, 0, count, disk32::kInline);
        layout = SentinelLayout();
        CHECK(
            xasset::TryValidateXAssetListDisk32Header(
                &invalid, &layout)
            == xasset::XAssetListDisk32Status::
                InvalidScriptStringCount);
        CheckLayoutUnchanged(layout);
    }

    for (const xasset::XAssetListDisk32 invalid : {
             MakeList(0, 0, 0, disk32::kInline),
             MakeList(0, 0, 1, 0)})
    {
        layout = SentinelLayout();
        CHECK(
            xasset::TryValidateXAssetListDisk32Header(
                &invalid, &layout)
            == xasset::XAssetListDisk32Status::
                InvalidScriptStringPointerCount);
        CheckLayoutUnchanged(layout);
    }
}

void TestSpanBoundsAndTypePolicy()
{
    AdmissionProbe probe{};
    const xasset::XAssetTypeDisk32Policy policy =
        MakePolicy(&probe);
    const xasset::XAssetListDisk32 one = MakeList(
        1, UINT32_C(0x80000001));
    const xasset::XAssetDisk32 record =
        MakeAsset(db::asset_mode::kAssetTypeCount - 1,
                  UINT32_C(0xF1234567));

    xasset::XAssetListDisk32Layout layout = SentinelLayout();
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one, nullptr, sizeof(record), policy, &layout)
        == xasset::XAssetListDisk32Status::InvalidAssetSpan);
    CheckLayoutUnchanged(layout);
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one,
            &record,
            sizeof(record) - 1u,
            policy,
            &layout)
        == xasset::XAssetListDisk32Status::TruncatedAssetSpan);
    CheckLayoutUnchanged(layout);

    const xasset::XAssetListDisk32 empty = MakeList(0, 0);
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &empty, &record, 0, policy, &layout)
        == xasset::XAssetListDisk32Status::InvalidAssetSpan);
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &empty, nullptr, 1, policy, &layout)
        == xasset::XAssetListDisk32Status::InvalidAssetSpan);

    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one, &record, sizeof(record), policy, nullptr)
        == xasset::XAssetListDisk32Status::InvalidArgument);

    xasset::XAssetTypeDisk32Policy invalidPolicy = policy;
    invalidPolicy.typeCount = 0;
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one,
            &record,
            sizeof(record),
            invalidPolicy,
            &layout)
        == xasset::XAssetListDisk32Status::InvalidTypePolicy);
    CheckLayoutUnchanged(layout);
    invalidPolicy = policy;
    invalidPolicy.admitType = nullptr;
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one,
            &record,
            sizeof(record),
            invalidPolicy,
            &layout)
        == xasset::XAssetListDisk32Status::InvalidTypePolicy);

    probe.calls = 0;
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one,
            &record,
            sizeof(record),
            policy,
            &layout)
        == xasset::XAssetListDisk32Status::Success);
    CHECK(layout.assetBytes == sizeof(record));
    CHECK(probe.calls == 1);
    CHECK(probe.lastType == db::asset_mode::kAssetTypeCount - 1);

    for (const std::int32_t invalidType : {
             -1,
             db::asset_mode::kAssetTypeCount,
             (std::numeric_limits<std::int32_t>::max)()})
    {
        const xasset::XAssetDisk32 invalid =
            MakeAsset(invalidType, UINT32_C(0xF0000001));
        probe.calls = 0;
        layout = SentinelLayout();
        CHECK(
            xasset::TryValidateXAssetListDisk32Span(
                &one,
                &invalid,
                sizeof(invalid),
                policy,
                &layout)
            == xasset::XAssetListDisk32Status::InvalidAssetType);
        CHECK(probe.calls == 0);
        CheckLayoutUnchanged(layout);
    }

    const xasset::XAssetDisk32 unsupported =
        MakeAsset(25, disk32::kSharedInline);
    probe.rejectedType = 25;
    probe.calls = 0;
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one,
            &unsupported,
            sizeof(unsupported),
            policy,
            &layout)
        == xasset::XAssetListDisk32Status::UnsupportedAssetType);
    CHECK(probe.calls == 1);
    CheckLayoutUnchanged(layout);
}

void TestBuildAdmissionPolicy()
{
    db::asset_mode::BuildMode mode =
        db::asset_mode::BuildMode::Multiplayer;
    const xasset::XAssetTypeDisk32Policy policy{
        db::asset_mode::kAssetTypeCount,
        &mode,
        AdmitBuildType};
    const xasset::XAssetListDisk32 one =
        MakeList(1, disk32::kInline);
    xasset::XAssetListDisk32Layout layout = SentinelLayout();

    xasset::XAssetDisk32 asset =
        MakeAsset(db::asset_mode::kClipMapPvs, disk32::kInline);
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one, &asset, sizeof(asset), policy, &layout)
        == xasset::XAssetListDisk32Status::Success);

    asset = MakeAsset(db::asset_mode::kClipMap, disk32::kInline);
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one, &asset, sizeof(asset), policy, &layout)
        == xasset::XAssetListDisk32Status::UnsupportedAssetType);
    CheckLayoutUnchanged(layout);

    mode = db::asset_mode::BuildMode::SinglePlayer;
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one, &asset, sizeof(asset), policy, &layout)
        == xasset::XAssetListDisk32Status::Success);

    const xasset::XAssetTypeDisk32Policy nullContextPolicy{
        db::asset_mode::kAssetTypeCount,
        nullptr,
        AdmitBuildType};
    layout = SentinelLayout();
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &one,
            &asset,
            sizeof(asset),
            nullContextPolicy,
            &layout)
        == xasset::XAssetListDisk32Status::UnsupportedAssetType);
    CheckLayoutUnchanged(layout);
}

void TestUnalignedIteratorAndGuardBytes()
{
    constexpr std::uint8_t prefixGuard = UINT8_C(0xA5);
    constexpr std::uint8_t suffixGuard0 = UINT8_C(0x5A);
    constexpr std::uint8_t suffixGuard1 = UINT8_C(0xC3);
    std::array<
        std::uint8_t,
        1u + 2u * sizeof(xasset::XAssetDisk32) + 2u>
        bytes{};
    bytes.fill(UINT8_C(0xCC));
    bytes.front() = prefixGuard;
    bytes[bytes.size() - 2u] = suffixGuard0;
    bytes[bytes.size() - 1u] = suffixGuard1;

    const std::array<xasset::XAssetDisk32, 2> records{
        MakeAsset(0, UINT32_C(0x80000001)),
        MakeAsset(
            db::asset_mode::kAssetTypeCount - 1,
            UINT32_C(0xFEDCBA98))};
    std::memcpy(
        bytes.data() + 1u,
        records.data(),
        sizeof(records));

    AdmissionProbe probe{};
    const xasset::XAssetTypeDisk32Policy policy =
        MakePolicy(&probe);
    const xasset::XAssetListDisk32 list =
        MakeList(2, UINT32_C(0xF0000001));

    xasset::XAssetListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginXAssetListDisk32(
            &list,
            bytes.data() + 1u,
            bytes.size() - 1u,
            policy,
            &iterator)
        == xasset::XAssetListDisk32Status::Success);
    CHECK(iterator.nextIndex() == 0);
    CHECK(iterator.remaining() == 2);
    CHECK(probe.calls == 2);

    xasset::XAssetDisk32 output =
        MakeAsset(INT32_C(0x13572468), UINT32_C(0xDEADBEEF));
    CHECK(
        xasset::TryNextXAssetDisk32(&iterator, &output)
        == xasset::XAssetListDisk32Status::Success);
    CHECK(ObjectBytes(output) == ObjectBytes(records[0]));
    CHECK(iterator.nextIndex() == 1);
    CHECK(iterator.remaining() == 1);

    xasset::XAssetListDisk32Iterator copy = iterator;
    CHECK(
        xasset::TryNextXAssetDisk32(&iterator, &output)
        == xasset::XAssetListDisk32Status::Success);
    CHECK(ObjectBytes(output) == ObjectBytes(records[1]));
    CHECK(iterator.nextIndex() == 2);
    CHECK(iterator.remaining() == 0);
    CHECK(copy.nextIndex() == 1);
    CHECK(copy.remaining() == 1);

    output =
        MakeAsset(INT32_C(0x13572468), UINT32_C(0xDEADBEEF));
    const auto outputBeforeEnd = ObjectBytes(output);
    const auto iteratorBeforeEnd = ObjectBytes(iterator);
    CHECK(
        xasset::TryNextXAssetDisk32(&iterator, &output)
        == xasset::XAssetListDisk32Status::End);
    CHECK(ObjectBytes(output) == outputBeforeEnd);
    CHECK(ObjectBytes(iterator) == iteratorBeforeEnd);

    CHECK(bytes.front() == prefixGuard);
    CHECK(bytes[bytes.size() - 2u] == suffixGuard0);
    CHECK(bytes[bytes.size() - 1u] == suffixGuard1);
}

void TestIteratorFailureAtomicity()
{
    AdmissionProbe probe{};
    const xasset::XAssetTypeDisk32Policy policy =
        MakePolicy(&probe);
    xasset::XAssetDisk32 record =
        MakeAsset(25, UINT32_C(0xF7654321));
    const xasset::XAssetListDisk32 list =
        MakeList(1, disk32::kInline);

    xasset::XAssetListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginXAssetListDisk32(
            &list, &record, sizeof(record), policy, &iterator)
        == xasset::XAssetListDisk32Status::Success);

    const auto validIterator = ObjectBytes(iterator);
    const xasset::XAssetListDisk32 badList =
        MakeList(1, 0);
    CHECK(
        xasset::TryBeginXAssetListDisk32(
            &badList,
            &record,
            sizeof(record),
            policy,
            &iterator)
        == xasset::XAssetListDisk32Status::
            InvalidAssetPointerCount);
    CHECK(ObjectBytes(iterator) == validIterator);

    record.type = db::asset_mode::kAssetTypeCount;
    xasset::XAssetDisk32 output =
        MakeAsset(INT32_C(0x13572468), UINT32_C(0xDEADBEEF));
    const auto outputBefore = ObjectBytes(output);
    const auto iteratorBefore = ObjectBytes(iterator);
    CHECK(
        xasset::TryNextXAssetDisk32(&iterator, &output)
        == xasset::XAssetListDisk32Status::InvalidAssetType);
    CHECK(ObjectBytes(output) == outputBefore);
    CHECK(ObjectBytes(iterator) == iteratorBefore);

    record.type = 25;
    probe.rejectedType = 25;
    CHECK(
        xasset::TryNextXAssetDisk32(&iterator, &output)
        == xasset::XAssetListDisk32Status::UnsupportedAssetType);
    CHECK(ObjectBytes(output) == outputBefore);
    CHECK(ObjectBytes(iterator) == iteratorBefore);

    xasset::XAssetListDisk32Iterator invalid{};
    CHECK(
        xasset::TryNextXAssetDisk32(&invalid, &output)
        == xasset::XAssetListDisk32Status::InvalidIterator);
    CHECK(ObjectBytes(output) == outputBefore);
    CHECK(
        xasset::TryNextXAssetDisk32(nullptr, &output)
        == xasset::XAssetListDisk32Status::InvalidArgument);
    CHECK(
        xasset::TryNextXAssetDisk32(&iterator, nullptr)
        == xasset::XAssetListDisk32Status::InvalidArgument);
    CHECK(
        xasset::TryBeginXAssetListDisk32(
            &list, &record, sizeof(record), policy, nullptr)
        == xasset::XAssetListDisk32Status::InvalidArgument);
}

void TestLateRejectionIsAtomic()
{
    std::array<xasset::XAssetDisk32, 3> records{
        MakeAsset(0, UINT32_C(0x80000000)),
        MakeAsset(1, UINT32_C(0x90000000)),
        MakeAsset(2, UINT32_C(0xA0000000))};
    const xasset::XAssetListDisk32 list =
        MakeList(3, disk32::kSharedInline);
    AdmissionProbe probe{2, 0, -1};
    const xasset::XAssetTypeDisk32Policy policy =
        MakePolicy(&probe);

    xasset::XAssetListDisk32Iterator iterator{};
    const auto iteratorBefore = ObjectBytes(iterator);
    CHECK(
        xasset::TryBeginXAssetListDisk32(
            &list,
            records.data(),
            sizeof(records),
            policy,
            &iterator)
        == xasset::XAssetListDisk32Status::UnsupportedAssetType);
    CHECK(probe.calls == 3);
    CHECK(ObjectBytes(iterator) == iteratorBefore);

    xasset::XAssetListDisk32Layout layout = SentinelLayout();
    probe.calls = 0;
    CHECK(
        xasset::TryValidateXAssetListDisk32Span(
            &list,
            records.data(),
            sizeof(records),
            policy,
            &layout)
        == xasset::XAssetListDisk32Status::UnsupportedAssetType);
    CHECK(probe.calls == 3);
    CheckLayoutUnchanged(layout);
}

void TestMaximumAssetIteration()
{
    std::vector<xasset::XAssetDisk32> records(
        static_cast<std::size_t>(xasset::kMaxXAssetListAssets));
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        records[index] = MakeAsset(
            static_cast<std::int32_t>(
                index
                % static_cast<std::size_t>(
                    db::asset_mode::kAssetTypeCount)),
            UINT32_C(0x80000000)
                | static_cast<std::uint32_t>(index));
    }

    const xasset::XAssetListDisk32 list = MakeList(
        xasset::kMaxXAssetListAssets,
        UINT32_C(0xFFFFFFFF));
    AdmissionProbe probe{};
    const xasset::XAssetTypeDisk32Policy policy =
        MakePolicy(&probe);
    xasset::XAssetListDisk32Iterator iterator{};
    CHECK(
        xasset::TryBeginXAssetListDisk32(
            &list,
            records.data(),
            records.size() * sizeof(records[0]),
            policy,
            &iterator)
        == xasset::XAssetListDisk32Status::Success);
    CHECK(probe.calls == xasset::kMaxXAssetListAssets);

    for (std::size_t index = 0; index < records.size(); ++index)
    {
        xasset::XAssetDisk32 output{};
        CHECK(
            xasset::TryNextXAssetDisk32(&iterator, &output)
            == xasset::XAssetListDisk32Status::Success);
        CHECK(ObjectBytes(output) == ObjectBytes(records[index]));
    }
    CHECK(iterator.nextIndex() == xasset::kMaxXAssetListAssets);
    CHECK(iterator.remaining() == 0);
    CHECK(
        probe.calls
        == xasset::kMaxXAssetListAssets * INT32_C(2));
}
} // namespace

int main()
{
    TestExactSchemaBytes();
    TestHeaderValidationAndLimits();
    TestSpanBoundsAndTypePolicy();
    TestBuildAdmissionPolicy();
    TestUnalignedIteratorAndGuardBytes();
    TestIteratorFailureAtomicity();
    TestLateRejectionIsAtomic();
    TestMaximumAssetIteration();

    if (failures != 0)
        return 1;
    std::puts("Disk32 XAsset envelope tests passed");
    return 0;
}
