#include <database/db_zone_stream_ownership.h>

#include <database/db_stream_state.h>
#include <database/db_zone_memory.h>
#include <database/db_zone_slots.h>
#include <database/db_zone_stream_ownership_internal.h>

#include <algorithm>
#include <limits>

// These are the exact legacy singleton objects consumed by db_stream.cpp.
// Keeping their definitions beside the receipt/controller ensures teardown
// scrubs the real production state rather than a test-only model.
std::uint32_t g_streamDelayIndex;
std::uint8_t *g_streamPosArray[db::relocation::kBlockCount];
static_assert(
    db::relocation::kBlockCount == 9,
    "legacy stream and ownership block counts must match");
StreamDelayInfo g_streamDelayArray[4096];
std::uint32_t g_streamPosIndex;
StreamPosInfo g_streamPosStack[64];
XZoneMemory *g_streamZoneMem;
std::uint8_t *g_streamPos;
std::uint32_t g_streamPosStackIndex;

namespace db::zone_stream_ownership
{
namespace
{
constexpr std::uint32_t kPhaseMask = UINT32_C(0xFF);

relocation::AliasRegistry g_aliasRegistry;
relocation::DirectResolver g_directResolver;
ActiveZoneStreamBinding *g_activeOwner = nullptr;

[[nodiscard]] constexpr bool IsNullKey(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return key.generation == 0
        && key.slot == zone_load::kInvalidZoneLoadSlot
        && key.reserved == 0;
}

[[nodiscard]] constexpr bool IsUsableKey(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return static_cast<bool>(key)
        && zone_slots::IsUsableZoneSlot(key.slot);
}

[[nodiscard]] constexpr bool IsReceiptPhase(
    const ZoneStreamGenerationPhase phase) noexcept
{
    switch (phase)
    {
    case ZoneStreamGenerationPhase::NeverBound:
    case ZoneStreamGenerationPhase::Bound:
    case ZoneStreamGenerationPhase::Invalidated:
    case ZoneStreamGenerationPhase::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsActivePhase(
    const ActiveZoneStreamPhase phase) noexcept
{
    switch (phase)
    {
    case ActiveZoneStreamPhase::Idle:
    case ActiveZoneStreamPhase::Bound:
    case ActiveZoneStreamPhase::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ObjectSpan(
    const void *const object,
    const std::size_t size,
    std::uintptr_t *const begin,
    std::uintptr_t *const end) noexcept
{
    if (!object || !begin || !end || !size)
        return false;
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(object);
    if (size > (std::numeric_limits<std::uintptr_t>::max)() - start)
        return false;
    *begin = start;
    *end = start + size;
    return true;
}

[[nodiscard]] constexpr bool SpansOverlap(
    const std::uintptr_t leftBegin,
    const std::uintptr_t leftEnd,
    const std::uintptr_t rightBegin,
    const std::uintptr_t rightEnd) noexcept
{
    return leftBegin < rightEnd && rightBegin < leftEnd;
}

[[nodiscard]] bool ObjectsDisjoint(
    const void *const left,
    const std::size_t leftSize,
    const void *const right,
    const std::size_t rightSize) noexcept
{
    std::uintptr_t leftBegin = 0;
    std::uintptr_t leftEnd = 0;
    std::uintptr_t rightBegin = 0;
    std::uintptr_t rightEnd = 0;
    return ObjectSpan(left, leftSize, &leftBegin, &leftEnd)
        && ObjectSpan(right, rightSize, &rightBegin, &rightEnd)
        && !SpansOverlap(leftBegin, leftEnd, rightBegin, rightEnd);
}

[[nodiscard]] bool ObjectIsAligned(
    const void *const object,
    const std::size_t alignment) noexcept
{
    return object && alignment != 0
        && reinterpret_cast<std::uintptr_t>(object) % alignment == 0;
}

[[nodiscard]] bool BlockLayoutsEqual(
    const relocation::BlockView *const left,
    const relocation::BlockView *const right) noexcept
{
    if (!left || !right)
        return false;
    for (std::size_t i = 0; i < relocation::kBlockCount; ++i)
    {
        if (left[i].base != right[i].base
            || left[i].size != right[i].size)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateBlockLayout(
    const relocation::BlockView *const blocks,
    const ActiveZoneStreamBinding *const active,
    const ZoneStreamGenerationReceipt *const receipt,
    const zone_load::ZoneLoadContextSlot *const lifecycle,
    const XZoneMemory *const zone) noexcept
{
    if (!blocks || !active || !receipt || !lifecycle
        || !ObjectIsAligned(zone, alignof(XZoneMemory)))
        return false;

    std::uintptr_t controlBegins[4]{};
    std::uintptr_t controlEnds[4]{};
    const void *const controls[4] = {active, receipt, lifecycle, zone};
    const std::size_t controlSizes[4] = {
        sizeof(*active), sizeof(*receipt), sizeof(*lifecycle), sizeof(*zone)};
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (!ObjectSpan(
                controls[i],
                controlSizes[i],
                &controlBegins[i],
                &controlEnds[i]))
        {
            return false;
        }
        for (std::size_t prior = 0; prior < i; ++prior)
        {
            if (SpansOverlap(
                    controlBegins[i],
                    controlEnds[i],
                    controlBegins[prior],
                    controlEnds[prior]))
            {
                return false;
            }
        }
    }

    std::uintptr_t blockEnds[relocation::kBlockCount]{};
    for (std::size_t i = 0; i < relocation::kBlockCount; ++i)
    {
        const relocation::BlockView &block = blocks[i];
        const bool hasPointer = block.base != 0;
        const bool hasSize = block.size != 0;
        if (hasPointer != hasSize)
            return false;
        if (!hasPointer)
            continue;
        if ((block.base & (alignof(std::uint32_t) - 1)) != 0
            || block.size
                > (std::numeric_limits<std::uintptr_t>::max)() - block.base)
        {
            return false;
        }
        blockEnds[i] = block.base + block.size;
        for (std::size_t control = 0; control < 4; ++control)
        {
            if (SpansOverlap(
                    block.base,
                    blockEnds[i],
                    controlBegins[control],
                    controlEnds[control]))
            {
                return false;
            }
        }
        for (std::size_t prior = 0; prior < i; ++prior)
        {
            if (blocks[prior].size != 0
                && SpansOverlap(
                    block.base,
                    blockEnds[i],
                    blocks[prior].base,
                    blockEnds[prior]))
            {
                return false;
            }
        }
    }
    return blocks[0].base != 0 && blocks[0].size != 0;
}

[[nodiscard]] bool ZoneMatchesLayout(
    const XZoneMemory *const zone,
    const relocation::BlockView *const blocks) noexcept
{
    if (!zone || !blocks)
        return false;
    for (std::size_t i = 0; i < relocation::kBlockCount; ++i)
    {
        if (reinterpret_cast<std::uintptr_t>(zone->blocks[i].data)
                != blocks[i].base
            || zone->blocks[i].size != blocks[i].size)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool LifecycleMatchesLoading(
    const zone_load::ZoneLoadContextSlot *const lifecycle,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return lifecycle && lifecycle->canonical()
        && lifecycle->slotIndex() == key.slot
        && lifecycle->generation() == key.generation
        && lifecycle->phase() == zone_load::ZoneLoadContextPhase::Loading
        && lifecycle->terminalKind()
            == zone_load::ZoneLoadTerminalKind::None
        && zone_load::ZoneLoadContextKeyMatches(lifecycle, key);
}

[[nodiscard]] bool LifecycleOwnsActiveKey(
    const zone_load::ZoneLoadContextSlot *const lifecycle,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return lifecycle && lifecycle->canonical()
        && lifecycle->slotIndex() == key.slot
        && lifecycle->generation() == key.generation
        && zone_load::ZoneLoadContextKeyMatches(lifecycle, key);
}

void ScrubStreamArrays() noexcept
{
    for (StreamDelayInfo &delay : g_streamDelayArray)
    {
        delay.ptr = nullptr;
        delay.size = 0;
    }
    for (StreamPosInfo &saved : g_streamPosStack)
    {
        saved.pos = nullptr;
        saved.index = 0;
    }
    for (std::uint8_t *&position : g_streamPosArray)
        position = nullptr;
}

void ScrubStreamScalars() noexcept
{
    g_streamDelayIndex = 0;
    g_streamPosIndex = 0;
    g_streamPosStackIndex = 0;
    g_streamZoneMem = nullptr;
    g_streamPos = nullptr;
}

[[nodiscard]] bool SingletonIsIdle() noexcept
{
    if (g_aliasRegistry.contextValid()
        || g_directResolver.contextValid()
        || g_streamDelayIndex != 0
        || g_streamPosIndex != 0
        || g_streamPosStackIndex != 0
        || g_streamZoneMem != nullptr
        || g_streamPos != nullptr)
    {
        return false;
    }
    for (const std::uint8_t *const position : g_streamPosArray)
    {
        if (position)
            return false;
    }
    for (const StreamDelayInfo &delay : g_streamDelayArray)
    {
        if (delay.ptr || delay.size != 0)
            return false;
    }
    for (const StreamPosInfo &saved : g_streamPosStack)
    {
        if (saved.pos || saved.index != 0)
            return false;
    }
    return true;
}

[[nodiscard]] ZoneStreamOwnershipStatus ValidateRequestedKey(
    const zone_load::ZoneLoadContextKey &stored,
    const zone_load::ZoneLoadContextKey &requested) noexcept
{
    if (!IsUsableKey(requested))
        return ZoneStreamOwnershipStatus::InvalidKey;
    if (stored != requested)
        return ZoneStreamOwnershipStatus::StaleKey;
    return ZoneStreamOwnershipStatus::Success;
}
} // namespace

ZoneStreamGenerationPhase ZoneStreamGenerationReceipt::phase() const noexcept
{
    return static_cast<ZoneStreamGenerationPhase>(phaseWord_ & kPhaseMask);
}

const zone_load::ZoneLoadContextKey &ZoneStreamGenerationReceipt::key()
    const noexcept
{
    return key_;
}

const zone_load::ZoneLoadContextSlot *
ZoneStreamGenerationReceipt::lifecycle() const noexcept
{
    return lifecycle_;
}

bool ZoneStreamGenerationReceipt::isPristine() const noexcept
{
    return phaseWord_ == static_cast<std::uint32_t>(
            ZoneStreamGenerationPhase::NeverBound)
        && self_ == nullptr && lifecycle_ == nullptr && IsNullKey(key_);
}

bool ZoneStreamGenerationReceipt::canonical() const noexcept
{
    if ((phaseWord_ & ~kPhaseMask) != 0 || !IsReceiptPhase(phase()))
        return false;
    if (self_ == nullptr)
    {
        return phase() == ZoneStreamGenerationPhase::NeverBound
            && lifecycle_ == nullptr && IsNullKey(key_);
    }
    return self_ == this
        && phase() != ZoneStreamGenerationPhase::UnsafeFailure
        && lifecycle_ != nullptr && IsUsableKey(key_);
}

ActiveZoneStreamPhase ActiveZoneStreamBinding::phase() const noexcept
{
    return static_cast<ActiveZoneStreamPhase>(phaseWord_ & kPhaseMask);
}

const zone_load::ZoneLoadContextKey &ActiveZoneStreamBinding::key()
    const noexcept
{
    return key_;
}

const ZoneStreamGenerationReceipt *ActiveZoneStreamBinding::receipt()
    const noexcept
{
    return receipt_;
}

const XZoneMemory *ActiveZoneStreamBinding::zoneIdentity() const noexcept
{
    return zoneIdentity_;
}

bool ActiveZoneStreamBinding::block(
    const std::size_t index,
    relocation::BlockView *const outBlock) const noexcept
{
    if (outBlock)
        *outBlock = {};
    if (!outBlock || index >= relocation::kBlockCount)
        return false;
    *outBlock = blocks_[index];
    return true;
}

bool ActiveZoneStreamBinding::isPristine() const noexcept
{
    if (phaseWord_ != static_cast<std::uint32_t>(
            ActiveZoneStreamPhase::Idle)
        || self_ != nullptr || receipt_ != nullptr
        || zoneIdentity_ != nullptr || !IsNullKey(key_))
    {
        return false;
    }
    for (const relocation::BlockView &blockView : blocks_)
    {
        if (blockView.base != 0 || blockView.size != 0)
            return false;
    }
    return true;
}

bool ActiveZoneStreamBinding::canonical() const noexcept
{
    if ((phaseWord_ & ~kPhaseMask) != 0 || !IsActivePhase(phase()))
        return false;
    if (phase() == ActiveZoneStreamPhase::Idle)
    {
        if (self_ != nullptr || receipt_ != nullptr || zoneIdentity_ != nullptr
            || !IsNullKey(key_))
        {
            return false;
        }
        for (const relocation::BlockView &blockView : blocks_)
        {
            if (blockView.base != 0 || blockView.size != 0)
                return false;
        }
        return true;
    }
    if (phase() == ActiveZoneStreamPhase::UnsafeFailure)
        return false;
    return self_ == this && receipt_ && zoneIdentity_ && IsUsableKey(key_)
        && receipt_->canonical()
        && receipt_->phase() == ZoneStreamGenerationPhase::Bound
        && receipt_->key() == key_
        && ValidateBlockLayout(
            blocks_, this, receipt_, receipt_->lifecycle_, zoneIdentity_)
        && ZoneMatchesLayout(zoneIdentity_, blocks_);
}

bool AuthenticatePassiveZoneStreamSingleton(
    const ActiveZoneStreamBinding &binding) noexcept
{
    return binding.isPristine()
        && g_activeOwner == nullptr
        && SingletonIsIdle();
}

ZoneStreamOwnershipStatus TryBeginZoneStreamGeneration(
    ZoneStreamGenerationReceipt *const receipt,
    zone_load::ZoneLoadContextSlot *const lifecycle,
    const zone_load::ZoneLoadContextKey &keyArgument) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!receipt || !lifecycle)
        return ZoneStreamOwnershipStatus::InvalidArgument;
    if (!ObjectsDisjoint(
            receipt,
            sizeof(*receipt),
            lifecycle,
            sizeof(*lifecycle)))
    {
        return ZoneStreamOwnershipStatus::InvalidArgument;
    }
    if (!IsUsableKey(key))
        return ZoneStreamOwnershipStatus::InvalidKey;
    if (!receipt->canonical())
        return ZoneStreamOwnershipStatus::UnsafeFailure;

    if (receipt->self_ != nullptr)
    {
        const ZoneStreamOwnershipStatus keyStatus =
            ValidateRequestedKey(receipt->key_, key);
        if (keyStatus != ZoneStreamOwnershipStatus::Success)
            return keyStatus;
        if (receipt->lifecycle_ != lifecycle)
            return ZoneStreamOwnershipStatus::StaleKey;
        if (receipt->phase() == ZoneStreamGenerationPhase::Invalidated)
            return ZoneStreamOwnershipStatus::AlreadyComplete;
        return receipt->phase() == ZoneStreamGenerationPhase::NeverBound
                || receipt->phase() == ZoneStreamGenerationPhase::Bound
            ? ZoneStreamOwnershipStatus::Success
            : ZoneStreamOwnershipStatus::UnsafeFailure;
    }

    if (!LifecycleMatchesLoading(lifecycle, key))
        return ZoneStreamOwnershipStatus::InvalidPhase;

    receipt->key_ = key;
    receipt->lifecycle_ = lifecycle;
    receipt->phaseWord_ = static_cast<std::uint32_t>(
        ZoneStreamGenerationPhase::NeverBound);
    receipt->self_ = receipt;
    return ZoneStreamOwnershipStatus::Success;
}

ZoneStreamOwnershipStatus TryBindZoneStreams(
    ActiveZoneStreamBinding *const active,
    ZoneStreamGenerationReceipt *const receipt,
    const zone_load::ZoneLoadContextKey &keyArgument,
    const XZoneMemory *const zoneIdentity,
    const relocation::BlockView *const blockArgument,
    const std::size_t blockCount) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!active || !receipt || !zoneIdentity || !blockArgument
        || !ObjectIsAligned(zoneIdentity, alignof(XZoneMemory)))
        return ZoneStreamOwnershipStatus::InvalidArgument;
    if (blockCount != relocation::kBlockCount)
        return ZoneStreamOwnershipStatus::InvalidLayout;
    if (!ObjectsDisjoint(active, sizeof(*active), receipt, sizeof(*receipt)))
        return ZoneStreamOwnershipStatus::InvalidArgument;

    relocation::BlockView blocks[relocation::kBlockCount]{};
    std::copy_n(blockArgument, relocation::kBlockCount, blocks);
    const XZoneMemory *const zone = zoneIdentity;
    if (!IsUsableKey(key))
        return ZoneStreamOwnershipStatus::InvalidKey;
    if (!receipt->canonical())
        return ZoneStreamOwnershipStatus::UnsafeFailure;
    const ZoneStreamOwnershipStatus keyStatus =
        ValidateRequestedKey(receipt->key_, key);
    if (keyStatus != ZoneStreamOwnershipStatus::Success)
        return keyStatus;
    if (!receipt->lifecycle_)
        return ZoneStreamOwnershipStatus::UnsafeFailure;

    if (receipt->phase() == ZoneStreamGenerationPhase::Invalidated)
        return ZoneStreamOwnershipStatus::InvalidState;
    if (receipt->phase() == ZoneStreamGenerationPhase::UnsafeFailure)
        return ZoneStreamOwnershipStatus::UnsafeFailure;

    if (!ValidateBlockLayout(
            blocks, active, receipt, receipt->lifecycle_, zone)
        || !ZoneMatchesLayout(zone, blocks))
    {
        return ZoneStreamOwnershipStatus::InvalidLayout;
    }

    if (receipt->phase() == ZoneStreamGenerationPhase::Bound)
    {
        if (g_activeOwner != active || !active->canonical()
            || active->receipt_ != receipt || active->key_ != key)
        {
            return ZoneStreamOwnershipStatus::UnsafeFailure;
        }
        if (active->zoneIdentity_ != zoneIdentity
            || !BlockLayoutsEqual(active->blocks_, blocks))
        {
            return ZoneStreamOwnershipStatus::InvalidLayout;
        }
        if (!g_aliasRegistry.ContextMatches(
                active->blocks_, relocation::kBlockCount)
            || !g_directResolver.ContextMatches(
                active->blocks_, relocation::kBlockCount)
            || g_streamZoneMem != zone)
        {
            return ZoneStreamOwnershipStatus::UnsafeFailure;
        }
        return ZoneStreamOwnershipStatus::AlreadyComplete;
    }

    if (!LifecycleMatchesLoading(receipt->lifecycle_, key))
        return ZoneStreamOwnershipStatus::InvalidPhase;
    if (!active->canonical())
        return ZoneStreamOwnershipStatus::UnsafeFailure;
    if (g_activeOwner)
    {
        return g_activeOwner->canonical() && g_activeOwner->key_ != key
            ? ZoneStreamOwnershipStatus::Busy
            : ZoneStreamOwnershipStatus::UnsafeFailure;
    }
    if (active->phase() != ActiveZoneStreamPhase::Idle)
        return ZoneStreamOwnershipStatus::UnsafeFailure;
    if (!SingletonIsIdle())
        return ZoneStreamOwnershipStatus::Busy;
    if (!g_aliasRegistry.CanReset())
        return ZoneStreamOwnershipStatus::GenerationExhausted;

    const relocation::Status aliasStatus = g_aliasRegistry.Reset(
        blocks, relocation::kBlockCount);
    if (aliasStatus != relocation::Status::Ok)
    {
        return aliasStatus == relocation::Status::GenerationExhausted
            ? ZoneStreamOwnershipStatus::GenerationExhausted
            : ZoneStreamOwnershipStatus::UnsafeFailure;
    }
    g_directResolver.Reset(blocks, relocation::kBlockCount);

    ScrubStreamScalars();
    ScrubStreamArrays();
    g_streamZoneMem = const_cast<XZoneMemory *>(zone);
    g_streamPos = reinterpret_cast<std::uint8_t *>(blocks[0].base);
    for (std::size_t i = 0; i < relocation::kBlockCount; ++i)
    {
        g_streamPosArray[i] =
            reinterpret_cast<std::uint8_t *>(blocks[i].base);
    }

    active->key_ = key;
    active->receipt_ = receipt;
    active->zoneIdentity_ = zoneIdentity;
    std::copy_n(blocks, relocation::kBlockCount, active->blocks_);
    active->phaseWord_ = static_cast<std::uint32_t>(
        ActiveZoneStreamPhase::Bound);
    active->self_ = active;
    g_activeOwner = active;

    // Publish ownership evidence only after every infallible singleton write.
    receipt->phaseWord_ = static_cast<std::uint32_t>(
        ZoneStreamGenerationPhase::Bound);
    return ZoneStreamOwnershipStatus::Success;
}

ZoneStreamOwnershipStatus TryInvalidateZoneStreams(
    ActiveZoneStreamBinding *const active,
    ZoneStreamGenerationReceipt *const receipt,
    const zone_load::ZoneLoadContextKey &keyArgument) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!receipt)
        return ZoneStreamOwnershipStatus::InvalidArgument;
    if (!IsUsableKey(key))
        return ZoneStreamOwnershipStatus::InvalidKey;
    if (!receipt->canonical())
        return ZoneStreamOwnershipStatus::UnsafeFailure;
    const ZoneStreamOwnershipStatus keyStatus =
        ValidateRequestedKey(receipt->key_, key);
    if (keyStatus != ZoneStreamOwnershipStatus::Success)
        return keyStatus;

    // The terminal receipt is authoritative without consulting the singleton:
    // its lifecycle slot may already hold a newer generation and that newer
    // generation must remain completely untouched.
    if (receipt->phase() == ZoneStreamGenerationPhase::Invalidated)
        return ZoneStreamOwnershipStatus::AlreadyComplete;
    if (receipt->phase() == ZoneStreamGenerationPhase::UnsafeFailure)
        return ZoneStreamOwnershipStatus::UnsafeFailure;
    if (!active)
        return ZoneStreamOwnershipStatus::InvalidArgument;
    if (!ObjectsDisjoint(active, sizeof(*active), receipt, sizeof(*receipt)))
        return ZoneStreamOwnershipStatus::InvalidArgument;

    if (receipt->phase() == ZoneStreamGenerationPhase::NeverBound)
    {
        receipt->phaseWord_ = static_cast<std::uint32_t>(
            ZoneStreamGenerationPhase::Invalidated);
        return ZoneStreamOwnershipStatus::Success;
    }
    if (receipt->phase() != ZoneStreamGenerationPhase::Bound
        || !LifecycleOwnsActiveKey(receipt->lifecycle_, key)
        || g_activeOwner != active
        || !active->canonical()
        || active->receipt_ != receipt
        || active->key_ != key
        || !g_aliasRegistry.ContextMatches(
            active->blocks_, relocation::kBlockCount)
        || !g_directResolver.ContextMatches(
            active->blocks_, relocation::kBlockCount)
        || g_streamZoneMem != active->zoneIdentity_)
    {
        return ZoneStreamOwnershipStatus::UnsafeFailure;
    }

    g_aliasRegistry.Invalidate();
    g_directResolver.Invalidate();
    ScrubStreamScalars();
    ScrubStreamArrays();
    active->key_ = {};
    active->receipt_ = nullptr;
    active->zoneIdentity_ = nullptr;
    for (relocation::BlockView &block : active->blocks_)
        block = {};
    active->self_ = nullptr;
    active->phaseWord_ = 0;
    g_activeOwner = nullptr;

    // Terminal publication is last so an exact retry can safely return before
    // observing whatever generation subsequently owns the singleton.
    receipt->phaseWord_ = static_cast<std::uint32_t>(
        ZoneStreamGenerationPhase::Invalidated);
    return ZoneStreamOwnershipStatus::Success;
}

namespace detail
{
relocation::AliasRegistry &AliasRegistryForLegacyStream() noexcept
{
    return g_aliasRegistry;
}

relocation::DirectResolver &DirectResolverForLegacyStream() noexcept
{
    return g_directResolver;
}

bool OwnershipBindingActive() noexcept
{
    return g_activeOwner != nullptr;
}
} // namespace detail
} // namespace db::zone_stream_ownership
