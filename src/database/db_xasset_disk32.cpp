#include <database/db_xasset_disk32.h>

#include <cstring>
#include <limits>

namespace db::xasset
{
namespace
{
[[nodiscard]] ScriptStringListDisk32Status CheckedScriptStringTokenBytes(
    const std::int32_t count,
    std::uint32_t *const outBytes) noexcept
{
    if (!outBytes)
        return ScriptStringListDisk32Status::InvalidArgument;
    if (count < 0)
        return ScriptStringListDisk32Status::InvalidStringCount;

    constexpr std::uint32_t stride =
        sizeof(ScriptStringTokenDisk32);
    const std::uint32_t unsignedCount =
        static_cast<std::uint32_t>(count);
    if (unsignedCount
        > (std::numeric_limits<std::uint32_t>::max)() / stride)
    {
        return ScriptStringListDisk32Status::SizeOverflow;
    }
    if (count > kMaxScriptStringListStrings)
        return ScriptStringListDisk32Status::InvalidStringCount;

    *outBytes = unsignedCount * stride;
    return ScriptStringListDisk32Status::Success;
}

[[nodiscard]] ScriptStringListDisk32Status ValidateScriptStringToken(
    const ScriptStringTokenDisk32 &token) noexcept
{
    if (token.token.isSharedInline())
        return ScriptStringListDisk32Status::UnsupportedSharedInline;
    return ScriptStringListDisk32Status::Success;
}

[[nodiscard]] ScriptStringListDisk32Status
ValidateScriptStringTokenSpan(
    const ScriptStringListDisk32Layout &layout,
    const void *const tokenRecords,
    const std::size_t tokenRecordBytes) noexcept
{
    const bool hasRecords = tokenRecords != nullptr;
    const bool hasRequiredRecords = layout.tokenBytes != 0;
    if (hasRecords != hasRequiredRecords)
        return ScriptStringListDisk32Status::InvalidTokenSpan;
    if (!hasRequiredRecords && tokenRecordBytes != 0)
        return ScriptStringListDisk32Status::InvalidTokenSpan;
    if (tokenRecordBytes < layout.tokenBytes)
        return ScriptStringListDisk32Status::TruncatedTokenSpan;
    return ScriptStringListDisk32Status::Success;
}

[[nodiscard]] ScriptStringTokenDisk32 ReadScriptStringToken(
    const std::uint8_t *const records,
    const std::int32_t index) noexcept
{
    ScriptStringTokenDisk32 token{};
    const std::size_t offset =
        static_cast<std::size_t>(index)
        * sizeof(ScriptStringTokenDisk32);
    std::memcpy(&token, records + offset, sizeof(token));
    return token;
}

[[nodiscard]] bool TypePolicyIsValid(
    const XAssetTypeDisk32Policy &policy) noexcept
{
    return policy.typeCount > 0 && policy.admitType != nullptr;
}

[[nodiscard]] XAssetListDisk32Status CheckedAssetBytes(
    const std::int32_t count,
    std::uint32_t *const outBytes) noexcept
{
    if (!outBytes)
        return XAssetListDisk32Status::InvalidArgument;
    if (count < 0)
        return XAssetListDisk32Status::InvalidAssetCount;

    constexpr std::uint32_t stride = sizeof(XAssetDisk32);
    const std::uint32_t unsignedCount =
        static_cast<std::uint32_t>(count);
    if (unsignedCount
        > (std::numeric_limits<std::uint32_t>::max)() / stride)
    {
        return XAssetListDisk32Status::SizeOverflow;
    }
    if (count > kMaxXAssetListAssets)
        return XAssetListDisk32Status::InvalidAssetCount;

    *outBytes = unsignedCount * stride;
    return XAssetListDisk32Status::Success;
}

[[nodiscard]] XAssetListDisk32Status ValidateAssetType(
    const XAssetDisk32 &asset,
    const XAssetTypeDisk32Policy &policy) noexcept
{
    if (!TypePolicyIsValid(policy))
        return XAssetListDisk32Status::InvalidTypePolicy;
    if (asset.type < 0 || asset.type >= policy.typeCount)
        return XAssetListDisk32Status::InvalidAssetType;
    if (!policy.admitType(policy.context, asset.type))
        return XAssetListDisk32Status::UnsupportedAssetType;
    return XAssetListDisk32Status::Success;
}

[[nodiscard]] XAssetListDisk32Status ValidateAssetSpan(
    const XAssetListDisk32Layout &layout,
    const void *const assetRecords,
    const std::size_t assetRecordBytes) noexcept
{
    const bool hasRecords = assetRecords != nullptr;
    const bool hasRequiredRecords = layout.assetBytes != 0;
    if (hasRecords != hasRequiredRecords)
        return XAssetListDisk32Status::InvalidAssetSpan;
    if (!hasRequiredRecords && assetRecordBytes != 0)
        return XAssetListDisk32Status::InvalidAssetSpan;
    if (assetRecordBytes < layout.assetBytes)
        return XAssetListDisk32Status::TruncatedAssetSpan;
    return XAssetListDisk32Status::Success;
}

[[nodiscard]] XAssetDisk32 ReadAsset(
    const std::uint8_t *const records,
    const std::int32_t index) noexcept
{
    XAssetDisk32 asset{};
    const std::size_t offset =
        static_cast<std::size_t>(index) * sizeof(XAssetDisk32);
    std::memcpy(&asset, records + offset, sizeof(asset));
    return asset;
}

} // namespace

bool ScriptStringListDisk32Iterator::isValid() const noexcept
{
    if (!valid_
        || stringCount_ < 0
        || stringCount_ > kMaxScriptStringListStrings
        || nextIndex_ < 0
        || nextIndex_ > stringCount_)
    {
        return false;
    }

    std::uint32_t expectedBytes = 0;
    if (CheckedScriptStringTokenBytes(stringCount_, &expectedBytes)
            != ScriptStringListDisk32Status::Success
        || requiredBytes_ != expectedBytes)
    {
        return false;
    }
    return (records_ != nullptr) == (expectedBytes != 0);
}

ScriptStringListDisk32Status
TryValidateScriptStringListDisk32Header(
    const ScriptStringListDisk32 *const list,
    ScriptStringListDisk32Layout *const outLayout) noexcept
{
    if (!list || !outLayout)
        return ScriptStringListDisk32Status::InvalidArgument;

    ScriptStringListDisk32Layout candidate{};
    const ScriptStringListDisk32Status status =
        CheckedScriptStringTokenBytes(
            list->count, &candidate.tokenBytes);
    if (status != ScriptStringListDisk32Status::Success)
        return status;

    const bool hasStrings = !list->strings.token.isNull();
    if (hasStrings != (list->count != 0))
    {
        return ScriptStringListDisk32Status::
            InvalidStringPointerCount;
    }

    candidate.stringCount = list->count;
    *outLayout = candidate;
    return ScriptStringListDisk32Status::Success;
}

ScriptStringListDisk32Status
TryValidateScriptStringListDisk32Span(
    const ScriptStringListDisk32 *const list,
    const void *const tokenRecords,
    const std::size_t tokenRecordBytes,
    ScriptStringListDisk32Layout *const outLayout) noexcept
{
    if (!list || !outLayout)
        return ScriptStringListDisk32Status::InvalidArgument;

    ScriptStringListDisk32Layout candidate{};
    ScriptStringListDisk32Status status =
        TryValidateScriptStringListDisk32Header(
            list, &candidate);
    if (status != ScriptStringListDisk32Status::Success)
        return status;

    status = ValidateScriptStringTokenSpan(
        candidate, tokenRecords, tokenRecordBytes);
    if (status != ScriptStringListDisk32Status::Success)
        return status;

    const auto *const records =
        static_cast<const std::uint8_t *>(tokenRecords);
    for (std::int32_t index = 0;
         index < candidate.stringCount;
         ++index)
    {
        const ScriptStringTokenDisk32 token =
            ReadScriptStringToken(records, index);
        status = ValidateScriptStringToken(token);
        if (status != ScriptStringListDisk32Status::Success)
            return status;
    }

    *outLayout = candidate;
    return ScriptStringListDisk32Status::Success;
}

ScriptStringListDisk32Status TryBeginScriptStringListDisk32(
    const ScriptStringListDisk32 *const list,
    const void *const tokenRecords,
    const std::size_t tokenRecordBytes,
    ScriptStringListDisk32Iterator *const outIterator) noexcept
{
    if (!outIterator)
        return ScriptStringListDisk32Status::InvalidArgument;

    ScriptStringListDisk32Layout layout{};
    const ScriptStringListDisk32Status status =
        TryValidateScriptStringListDisk32Span(
            list, tokenRecords, tokenRecordBytes, &layout);
    if (status != ScriptStringListDisk32Status::Success)
        return status;

    ScriptStringListDisk32Iterator candidate{};
    candidate.records_ =
        static_cast<const std::uint8_t *>(tokenRecords);
    candidate.requiredBytes_ = layout.tokenBytes;
    candidate.stringCount_ = layout.stringCount;
    candidate.nextIndex_ = 0;
    candidate.valid_ = true;
    *outIterator = candidate;
    return ScriptStringListDisk32Status::Success;
}

ScriptStringListDisk32Status TryNextScriptStringTokenDisk32(
    ScriptStringListDisk32Iterator *const iterator,
    ScriptStringTokenDisk32 *const outToken) noexcept
{
    if (!iterator || !outToken)
        return ScriptStringListDisk32Status::InvalidArgument;
    if (!iterator->isValid())
        return ScriptStringListDisk32Status::InvalidIterator;
    if (iterator->nextIndex_ == iterator->stringCount_)
        return ScriptStringListDisk32Status::End;

    const ScriptStringTokenDisk32 token =
        ReadScriptStringToken(
            iterator->records_, iterator->nextIndex_);
    const ScriptStringListDisk32Status status =
        ValidateScriptStringToken(token);
    if (status != ScriptStringListDisk32Status::Success)
        return status;

    *outToken = token;
    ++iterator->nextIndex_;
    return ScriptStringListDisk32Status::Success;
}

bool XAssetListDisk32Iterator::isValid() const noexcept
{
    if (!valid_
        || assetCount_ < 0
        || assetCount_ > kMaxXAssetListAssets
        || nextIndex_ < 0
        || nextIndex_ > assetCount_
        || !TypePolicyIsValid(policy_))
    {
        return false;
    }

    std::uint32_t expectedBytes = 0;
    if (CheckedAssetBytes(assetCount_, &expectedBytes)
            != XAssetListDisk32Status::Success
        || requiredBytes_ != expectedBytes)
    {
        return false;
    }
    return (records_ != nullptr) == (expectedBytes != 0);
}

XAssetListDisk32Status TryValidateXAssetListDisk32Header(
    const XAssetListDisk32 *const list,
    XAssetListDisk32Layout *const outLayout) noexcept
{
    if (!list || !outLayout)
        return XAssetListDisk32Status::InvalidArgument;

    XAssetListDisk32Layout candidate{};
    XAssetListDisk32Status status =
        CheckedAssetBytes(list->assetCount, &candidate.assetBytes);
    if (status != XAssetListDisk32Status::Success)
        return status;

    const bool hasAssets = !list->assets.token.isNull();
    if (hasAssets != (list->assetCount != 0))
        return XAssetListDisk32Status::InvalidAssetPointerCount;

    if (list->stringList.count < 0
        || list->stringList.count > kMaxScriptStringListStrings)
    {
        return XAssetListDisk32Status::InvalidScriptStringCount;
    }
    const bool hasStrings = !list->stringList.strings.token.isNull();
    if (hasStrings != (list->stringList.count != 0))
    {
        return XAssetListDisk32Status::InvalidScriptStringPointerCount;
    }

    candidate.assetCount = list->assetCount;
    candidate.scriptStringCount = list->stringList.count;
    *outLayout = candidate;
    return XAssetListDisk32Status::Success;
}

XAssetListDisk32Status TryValidateXAssetListDisk32Span(
    const XAssetListDisk32 *const list,
    const void *const assetRecords,
    const std::size_t assetRecordBytes,
    const XAssetTypeDisk32Policy &policy,
    XAssetListDisk32Layout *const outLayout) noexcept
{
    if (!list || !outLayout)
        return XAssetListDisk32Status::InvalidArgument;
    if (!TypePolicyIsValid(policy))
        return XAssetListDisk32Status::InvalidTypePolicy;

    XAssetListDisk32Layout candidate{};
    XAssetListDisk32Status status =
        TryValidateXAssetListDisk32Header(list, &candidate);
    if (status != XAssetListDisk32Status::Success)
        return status;

    status = ValidateAssetSpan(candidate, assetRecords, assetRecordBytes);
    if (status != XAssetListDisk32Status::Success)
        return status;

    const auto *const records =
        static_cast<const std::uint8_t *>(assetRecords);
    for (std::int32_t index = 0; index < candidate.assetCount; ++index)
    {
        const XAssetDisk32 asset = ReadAsset(records, index);
        status = ValidateAssetType(asset, policy);
        if (status != XAssetListDisk32Status::Success)
            return status;
    }

    *outLayout = candidate;
    return XAssetListDisk32Status::Success;
}

XAssetListDisk32Status TryBeginXAssetListDisk32(
    const XAssetListDisk32 *const list,
    const void *const assetRecords,
    const std::size_t assetRecordBytes,
    const XAssetTypeDisk32Policy &policy,
    XAssetListDisk32Iterator *const outIterator) noexcept
{
    if (!outIterator)
        return XAssetListDisk32Status::InvalidArgument;

    XAssetListDisk32Layout layout{};
    const XAssetListDisk32Status status =
        TryValidateXAssetListDisk32Span(
            list,
            assetRecords,
            assetRecordBytes,
            policy,
            &layout);
    if (status != XAssetListDisk32Status::Success)
        return status;

    XAssetListDisk32Iterator candidate{};
    candidate.records_ =
        static_cast<const std::uint8_t *>(assetRecords);
    candidate.requiredBytes_ = layout.assetBytes;
    candidate.assetCount_ = layout.assetCount;
    candidate.nextIndex_ = 0;
    candidate.policy_ = policy;
    candidate.valid_ = true;
    *outIterator = candidate;
    return XAssetListDisk32Status::Success;
}

XAssetListDisk32Status TryNextXAssetDisk32(
    XAssetListDisk32Iterator *const iterator,
    XAssetDisk32 *const outAsset) noexcept
{
    if (!iterator || !outAsset)
        return XAssetListDisk32Status::InvalidArgument;
    if (!iterator->isValid())
        return XAssetListDisk32Status::InvalidIterator;
    if (iterator->nextIndex_ == iterator->assetCount_)
        return XAssetListDisk32Status::End;

    const XAssetDisk32 asset =
        ReadAsset(iterator->records_, iterator->nextIndex_);
    const XAssetListDisk32Status status =
        ValidateAssetType(asset, iterator->policy_);
    if (status != XAssetListDisk32Status::Success)
        return status;

    *outAsset = asset;
    ++iterator->nextIndex_;
    return XAssetListDisk32Status::Success;
}
} // namespace db::xasset
