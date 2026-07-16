#pragma once

#include <database/db_disk32.h>

#include <universal/kisak_abi.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace db::xasset
{
inline constexpr std::int32_t kMaxXAssetListAssets = 32768;
inline constexpr std::int32_t kMaxScriptStringListStrings = 65536;

// XAssetHeader is an opaque serialized asset-handle token. Its pointer grammar
// is selected only after the owning XAsset type has passed the caller's
// portable build-admission policy.
struct XAssetHeaderDisk32 final
{
    disk32::PointerToken token;
};

struct XAssetDisk32 final
{
    std::int32_t type;
    XAssetHeaderDisk32 header;
};

struct ScriptStringListDisk32 final
{
    std::int32_t count;
    disk32::Ptr32<disk32::Ptr32<const char>> strings;
};

struct XAssetListDisk32 final
{
    ScriptStringListDisk32 stringList;
    std::int32_t assetCount;
    disk32::Ptr32<XAssetDisk32> assets;
};

static_assert(
    std::endian::native == std::endian::little,
    "XAsset Disk32 records require a little-endian target");

ONDISK_SIZE(XAssetHeaderDisk32, 0x04);
ONDISK_OFFSET(XAssetHeaderDisk32, token, 0x00);
static_assert(alignof(XAssetHeaderDisk32) == 4);
static_assert(std::is_trivial_v<XAssetHeaderDisk32>);
static_assert(std::is_standard_layout_v<XAssetHeaderDisk32>);
static_assert(std::is_trivially_copyable_v<XAssetHeaderDisk32>);

ONDISK_SIZE(XAssetDisk32, 0x08);
ONDISK_OFFSET(XAssetDisk32, type, 0x00);
ONDISK_OFFSET(XAssetDisk32, header, 0x04);
static_assert(alignof(XAssetDisk32) == 4);
static_assert(std::is_trivial_v<XAssetDisk32>);
static_assert(std::is_standard_layout_v<XAssetDisk32>);
static_assert(std::is_trivially_copyable_v<XAssetDisk32>);

ONDISK_SIZE(ScriptStringListDisk32, 0x08);
ONDISK_OFFSET(ScriptStringListDisk32, count, 0x00);
ONDISK_OFFSET(ScriptStringListDisk32, strings, 0x04);
static_assert(alignof(ScriptStringListDisk32) == 4);
static_assert(std::is_trivial_v<ScriptStringListDisk32>);
static_assert(std::is_standard_layout_v<ScriptStringListDisk32>);
static_assert(std::is_trivially_copyable_v<ScriptStringListDisk32>);

ONDISK_SIZE(XAssetListDisk32, 0x10);
ONDISK_OFFSET(XAssetListDisk32, stringList, 0x00);
ONDISK_OFFSET(XAssetListDisk32, assetCount, 0x08);
ONDISK_OFFSET(XAssetListDisk32, assets, 0x0C);
static_assert(alignof(XAssetListDisk32) == 4);
static_assert(std::is_trivial_v<XAssetListDisk32>);
static_assert(std::is_standard_layout_v<XAssetListDisk32>);
static_assert(std::is_trivially_copyable_v<XAssetListDisk32>);

using XAssetTypeDisk32AdmissionCallback =
    bool (*)(void *context, std::int32_t rawType) noexcept;

// typeCount supplies the serialized enum's exclusive upper bound without
// importing the native XAssetType declaration. admitType applies the caller's
// MP/SP/build-specific support policy after the raw range check. It must be
// deterministic, may be invoked more than once per record, and must not mutate
// the borrowed list/record bytes or iterator outputs.
struct XAssetTypeDisk32Policy final
{
    std::int32_t typeCount = 0;
    void *context = nullptr;
    XAssetTypeDisk32AdmissionCallback admitType = nullptr;
};

enum class XAssetListDisk32Status : std::uint8_t
{
    Success,
    End,
    InvalidArgument,
    InvalidTypePolicy,
    InvalidAssetCount,
    InvalidAssetPointerCount,
    InvalidScriptStringCount,
    InvalidScriptStringPointerCount,
    SizeOverflow,
    InvalidAssetSpan,
    TruncatedAssetSpan,
    InvalidAssetType,
    UnsupportedAssetType,
    InvalidIterator,
};

struct XAssetListDisk32Layout final
{
    std::uint32_t assetBytes = 0;
    std::int32_t assetCount = 0;
    std::int32_t scriptStringCount = 0;
};

// The iterator borrows a validated byte span. The caller must keep that span
// and the admission-policy context alive and immutable until iteration ends.
// Default construction creates an invalid iterator. Copying a live iterator
// creates an independent cursor over the same immutable input.
class XAssetListDisk32Iterator final
{
public:
    XAssetListDisk32Iterator() noexcept = default;

    [[nodiscard]] std::int32_t nextIndex() const noexcept
    {
        return nextIndex_;
    }

    [[nodiscard]] std::int32_t remaining() const noexcept
    {
        return nextIndex_ >= 0 && nextIndex_ <= assetCount_
            ? assetCount_ - nextIndex_
            : 0;
    }

private:
    friend XAssetListDisk32Status TryBeginXAssetListDisk32(
        const XAssetListDisk32 *list,
        const void *assetRecords,
        std::size_t assetRecordBytes,
        const XAssetTypeDisk32Policy &policy,
        XAssetListDisk32Iterator *outIterator) noexcept;
    friend XAssetListDisk32Status TryNextXAssetDisk32(
        XAssetListDisk32Iterator *iterator,
        XAssetDisk32 *outAsset) noexcept;

    [[nodiscard]] bool isValid() const noexcept;

    const std::uint8_t *records_ = nullptr;
    std::uint32_t requiredBytes_ = 0;
    std::int32_t assetCount_ = 0;
    std::int32_t nextIndex_ = 0;
    XAssetTypeDisk32Policy policy_{};
    bool valid_ = false;
};

static_assert(std::is_standard_layout_v<XAssetListDisk32Iterator>);
static_assert(std::is_trivially_copyable_v<XAssetListDisk32Iterator>);

// Validates only the fixed root envelope and computes its exact serialized
// asset-array extent. outLayout is unchanged on every failure.
[[nodiscard]] XAssetListDisk32Status TryValidateXAssetListDisk32Header(
    const XAssetListDisk32 *list,
    XAssetListDisk32Layout *outLayout) noexcept;

// Validates the root, the caller-provided bounded record span, and every raw
// asset type against the portable policy. Extra trailing bytes are permitted
// and never inspected. outLayout is unchanged on every failure.
[[nodiscard]] XAssetListDisk32Status TryValidateXAssetListDisk32Span(
    const XAssetListDisk32 *list,
    const void *assetRecords,
    std::size_t assetRecordBytes,
    const XAssetTypeDisk32Policy &policy,
    XAssetListDisk32Layout *outLayout) noexcept;

// Begins only after the complete span validates. outIterator is unchanged on
// failure, including a rejected type late in the array.
[[nodiscard]] XAssetListDisk32Status TryBeginXAssetListDisk32(
    const XAssetListDisk32 *list,
    const void *assetRecords,
    std::size_t assetRecordBytes,
    const XAssetTypeDisk32Policy &policy,
    XAssetListDisk32Iterator *outIterator) noexcept;

// Reads exactly one 0x8 record with memcpy, revalidates its type to fail closed
// if borrowed input changed, and advances only after publishing the complete
// local copy. Both iterator and outAsset are unchanged on failure or End.
[[nodiscard]] XAssetListDisk32Status TryNextXAssetDisk32(
    XAssetListDisk32Iterator *iterator,
    XAssetDisk32 *outAsset) noexcept;
} // namespace db::xasset
