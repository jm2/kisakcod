#include <database/db_zone_runtime_callback_context.h>
#include <database/db_zone_slots.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace db::zone_runtime
{
namespace
{
inline constexpr std::uint64_t kWitnessSeed = UINT64_C(0x97F4A7C15D2E8B63);
inline constexpr std::size_t kInvalidStoreIndex =
    (std::numeric_limits<std::size_t>::max)();
constinit std::array<
    ZoneRuntimeCallbackContext,
    zone_slots::kPhysicalZoneSlotCount> g_contextStore{};

[[nodiscard]] constexpr bool IsKnownPhase(
    const ZoneRuntimeCallbackContextPhase phase) noexcept
{
    return phase >= ZoneRuntimeCallbackContextPhase::Reserved
        && phase <= ZoneRuntimeCallbackContextPhase::Terminal;
}

[[nodiscard]] constexpr bool IsRetainedPhase(
    const ZoneRuntimeCallbackContextPhase phase) noexcept
{
    return phase == ZoneRuntimeCallbackContextPhase::Bound
        || phase == ZoneRuntimeCallbackContextPhase::Terminal;
}

[[nodiscard]] constexpr std::uint64_t RotateLeft(
    const std::uint64_t value,
    const unsigned int amount) noexcept
{
    return (value << amount) | (value >> (64u - amount));
}

[[nodiscard]] constexpr std::uint64_t ContextWitness(
    const zone_load::ZoneLoadContextKey &key,
    const std::uint32_t physicalSlot,
    const ZoneRuntimeCallbackContextPhase phase) noexcept
{
    std::uint64_t value = kWitnessSeed ^ key.generation;
    value = RotateLeft(value, 17u)
        ^ (static_cast<std::uint64_t>(key.slot) << 32u)
        ^ static_cast<std::uint64_t>(key.reserved);
    value = RotateLeft(value, 23u)
        ^ (static_cast<std::uint64_t>(physicalSlot) << 8u)
        ^ static_cast<std::uint8_t>(phase);
    return value ^ UINT64_C(0xC6A4A7935BD1E995);
}

[[nodiscard]] constexpr bool IsAllowedAdvance(
    const ZoneRuntimeCallbackContextPhase current,
    const ZoneRuntimeCallbackContextPhase next) noexcept
{
    return current == ZoneRuntimeCallbackContextPhase::Bound
        && next == ZoneRuntimeCallbackContextPhase::Terminal;
}

[[nodiscard]] ZoneRuntimeCallbackContextStatus MapExactStorageStatus(
    const pmem_runtime::StorageIsolationStatus status) noexcept
{
    switch (status)
    {
    case pmem_runtime::StorageIsolationStatus::Success:
    case pmem_runtime::StorageIsolationStatus::Uninitialized:
        return ZoneRuntimeCallbackContextStatus::Success;
    case pmem_runtime::StorageIsolationStatus::Busy:
        return ZoneRuntimeCallbackContextStatus::Busy;
    case pmem_runtime::StorageIsolationStatus::InvalidArgument:
    case pmem_runtime::StorageIsolationStatus::ProtectedStorageOverlap:
    case pmem_runtime::StorageIsolationStatus::Poisoned:
    case pmem_runtime::StorageIsolationStatus::CorruptState:
    default:
        return ZoneRuntimeCallbackContextStatus::UnsafeFailure;
    }
}

[[nodiscard]] std::array<
    ZoneRuntimeCallbackContext,
    zone_slots::kPhysicalZoneSlotCount> &ContextStore() noexcept
{
    return g_contextStore;
}

[[nodiscard]] std::size_t ExactStoreIndex(
    const ZoneRuntimeCallbackContext *const candidate) noexcept
{
    if (!candidate)
        return kInvalidStoreIndex;

    const auto &store = ContextStore();
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(candidate);
    const std::uintptr_t begin =
        reinterpret_cast<std::uintptr_t>(store.data());
    const std::size_t totalBytes = sizeof(store);
    if (address < begin || address - begin >= totalBytes)
        return kInvalidStoreIndex;

    const std::uintptr_t offset = address - begin;
    if (offset % sizeof(ZoneRuntimeCallbackContext) != 0)
        return kInvalidStoreIndex;

    const std::size_t index =
        static_cast<std::size_t>(offset)
        / sizeof(ZoneRuntimeCallbackContext);
    if (index >= store.size() || &store[index] != candidate)
        return kInvalidStoreIndex;
    return index;
}
} // namespace

bool ZoneRuntimeCallbackContext::canonical(
    const std::size_t exactStoreIndex) const noexcept
{
    if (exactStoreIndex >= zone_slots::kPhysicalZoneSlotCount
        || physicalSlot_ != exactStoreIndex
        || self_ != this
        || !IsKnownPhase(phase_)
#if !KISAK_ARCH_64BIT
        || pointerAlignmentPadding_[0] != 0
        || pointerAlignmentPadding_[1] != 0
        || pointerAlignmentPadding_[2] != 0
        || pointerAlignmentPadding_[3] != 0
#endif
        || reserved_[0] != 0 || reserved_[1] != 0 || reserved_[2] != 0
        || witness_ != ContextWitness(key_, physicalSlot_, phase_))
    {
        return false;
    }

    if (exactStoreIndex == zone_slots::kDefaultZoneSlot)
    {
        return phase_ == ZoneRuntimeCallbackContextPhase::Reserved
            && key_ == zone_load::ZoneLoadContextKey{};
    }

    if (!zone_slots::IsUsableZoneSlot(exactStoreIndex)
        || phase_ == ZoneRuntimeCallbackContextPhase::Reserved)
    {
        return false;
    }
    if (phase_ == ZoneRuntimeCallbackContextPhase::Unbound)
        return key_ == zone_load::ZoneLoadContextKey{};
    return IsRetainedPhase(phase_) && static_cast<bool>(key_)
        && key_.slot == exactStoreIndex;
}

void ZoneRuntimeCallbackContext::initialize(
    const std::uint32_t physicalSlot) noexcept
{
    key_ = {};
    self_ = this;
#if !KISAK_ARCH_64BIT
    pointerAlignmentPadding_[0] = 0;
    pointerAlignmentPadding_[1] = 0;
    pointerAlignmentPadding_[2] = 0;
    pointerAlignmentPadding_[3] = 0;
#endif
    physicalSlot_ = physicalSlot;
    phase_ = physicalSlot == zone_slots::kDefaultZoneSlot
        ? ZoneRuntimeCallbackContextPhase::Reserved
        : ZoneRuntimeCallbackContextPhase::Unbound;
    reserved_[0] = 0;
    reserved_[1] = 0;
    reserved_[2] = 0;
    refreshWitness();
}

void ZoneRuntimeCallbackContext::bind(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    key_ = key;
    phase_ = ZoneRuntimeCallbackContextPhase::Bound;
    refreshWitness();
}

void ZoneRuntimeCallbackContext::setPhase(
    const ZoneRuntimeCallbackContextPhase phase) noexcept
{
    phase_ = phase;
    refreshWitness();
}

void ZoneRuntimeCallbackContext::refreshWitness() noexcept
{
    witness_ = ContextWitness(key_, physicalSlot_, phase_);
}

pmem_runtime::StorageIsolationStatus
ZoneRuntimeCallbackContextOwner::TryClassifyStorage(
    const ZoneRuntimeCallbackContext *const context) noexcept
{
    // ContextStore construction never consults PMem. This separate
    // thread-safe initializer publishes all 33 self/index/state witnesses
    // before the first full-object classification.
    static const bool initialized = []() noexcept
    {
        auto &store = ContextStore();
        for (std::uint32_t slot = 0;
             slot < zone_slots::kPhysicalZoneSlotCount;
             ++slot)
        {
            store[slot].initialize(slot);
        }
        return true;
    }();
    (void)initialized;

    const std::size_t exactIndex = ExactStoreIndex(context);
    if (exactIndex == kInvalidStoreIndex)
        return pmem_runtime::StorageIsolationStatus::InvalidArgument;
    const ZoneRuntimeCallbackContext *const exact =
        &ContextStore()[exactIndex];
    return pmem_runtime::TryClassifyStorageIsolation(
        exact, sizeof(*exact));
}

ZoneRuntimeCallbackContextBindResult
ZoneRuntimeCallbackContextOwner::TryBind(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    ZoneRuntimeCallbackContextBindResult result{};
    const zone_load::ZoneLoadContextKey inputKey = key;
    if (!zone_slots::IsUsableZoneSlot(physicalSlot))
    {
        result.status = ZoneRuntimeCallbackContextStatus::InvalidSlot;
        return result;
    }
    if (!static_cast<bool>(inputKey))
    {
        result.status = ZoneRuntimeCallbackContextStatus::InvalidKey;
        return result;
    }
    if (inputKey.slot != physicalSlot)
    {
        result.status = ZoneRuntimeCallbackContextStatus::StaleKey;
        return result;
    }

    auto &store = ContextStore();
    ZoneRuntimeCallbackContext *const context = &store[physicalSlot];
    const ZoneRuntimeCallbackContextStatus storageStatus =
        MapExactStorageStatus(TryClassifyStorage(context));
    if (storageStatus != ZoneRuntimeCallbackContextStatus::Success)
    {
        result.status = storageStatus;
        return result;
    }
    if (!context->canonical(physicalSlot))
    {
        result.status = ZoneRuntimeCallbackContextStatus::UnsafeFailure;
        return result;
    }

    if (context->phase_ == ZoneRuntimeCallbackContextPhase::Unbound)
    {
        context->bind(inputKey);
    }
    else if (context->phase_ == ZoneRuntimeCallbackContextPhase::Terminal)
    {
        if (context->key_.generation
            == (std::numeric_limits<std::uint64_t>::max)())
        {
            result.status =
                ZoneRuntimeCallbackContextStatus::GenerationExhausted;
            return result;
        }
        if (inputKey.generation == context->key_.generation)
        {
            result.status = ZoneRuntimeCallbackContextStatus::InvalidPhase;
            return result;
        }
        if (inputKey.generation != context->key_.generation + 1u)
        {
            result.status = ZoneRuntimeCallbackContextStatus::StaleKey;
            return result;
        }
        context->bind(inputKey);
    }
    else
    {
        if (inputKey != context->key_)
        {
            result.status = ZoneRuntimeCallbackContextStatus::StaleKey;
            return result;
        }
        if (context->phase_ != ZoneRuntimeCallbackContextPhase::Bound)
        {
            result.status = ZoneRuntimeCallbackContextStatus::InvalidPhase;
            return result;
        }
    }

    if (!context->canonical(physicalSlot))
    {
        result.status = ZoneRuntimeCallbackContextStatus::UnsafeFailure;
        return result;
    }
    result.context = context;
    result.status = ZoneRuntimeCallbackContextStatus::Success;
    return result;
}

ZoneRuntimeCallbackContextBindResult
ZoneRuntimeCallbackContextOwner::TryResolve(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &expectedKey) noexcept
{
    ZoneRuntimeCallbackContextBindResult result{};
    const zone_load::ZoneLoadContextKey inputKey = expectedKey;
    if (!zone_slots::IsUsableZoneSlot(physicalSlot))
    {
        result.status = ZoneRuntimeCallbackContextStatus::InvalidSlot;
        return result;
    }
    if (!static_cast<bool>(inputKey))
    {
        result.status = ZoneRuntimeCallbackContextStatus::InvalidKey;
        return result;
    }
    if (inputKey.slot != physicalSlot)
    {
        result.status = ZoneRuntimeCallbackContextStatus::StaleKey;
        return result;
    }

    const ZoneRuntimeCallbackContext *const context =
        &ContextStore()[physicalSlot];
    const ZoneRuntimeCallbackContextStatus storageStatus =
        MapExactStorageStatus(TryClassifyStorage(context));
    if (storageStatus != ZoneRuntimeCallbackContextStatus::Success)
    {
        result.status = storageStatus;
        return result;
    }
    if (!context->canonical(physicalSlot))
    {
        result.status = ZoneRuntimeCallbackContextStatus::UnsafeFailure;
        return result;
    }
    if (!IsRetainedPhase(context->phase_))
    {
        result.status = ZoneRuntimeCallbackContextStatus::InvalidPhase;
        return result;
    }
    if (inputKey != context->key_)
    {
        result.status = ZoneRuntimeCallbackContextStatus::StaleKey;
        return result;
    }

    result.context = context;
    result.status = ZoneRuntimeCallbackContextStatus::Success;
    return result;
}

ZoneRuntimeCallbackContextStatus
ZoneRuntimeCallbackContextOwner::TryAdvance(
    const ZoneRuntimeCallbackContext *const context,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeCallbackContextPhase nextPhase) noexcept
{
    const zone_load::ZoneLoadContextKey inputKey = key;
    if (!static_cast<bool>(inputKey))
        return ZoneRuntimeCallbackContextStatus::InvalidKey;
    if (!IsRetainedPhase(nextPhase))
        return ZoneRuntimeCallbackContextStatus::InvalidPhase;

    const std::size_t exactIndex = ExactStoreIndex(context);
    if (exactIndex == kInvalidStoreIndex)
        return ZoneRuntimeCallbackContextStatus::InvalidArgument;
    const ZoneRuntimeCallbackContextStatus storageStatus =
        MapExactStorageStatus(TryClassifyStorage(context));
    if (storageStatus != ZoneRuntimeCallbackContextStatus::Success)
        return storageStatus;

    ZoneRuntimeCallbackContext *const mutableContext =
        &ContextStore()[exactIndex];
    if (!mutableContext->canonical(exactIndex))
        return ZoneRuntimeCallbackContextStatus::UnsafeFailure;
    if (inputKey.slot != exactIndex)
        return ZoneRuntimeCallbackContextStatus::StaleKey;
    if (!IsRetainedPhase(mutableContext->phase_))
        return ZoneRuntimeCallbackContextStatus::InvalidPhase;
    if (inputKey != mutableContext->key_)
        return ZoneRuntimeCallbackContextStatus::StaleKey;
    if (mutableContext->phase_ == nextPhase)
        return ZoneRuntimeCallbackContextStatus::Success;
    if (!IsAllowedAdvance(mutableContext->phase_, nextPhase))
        return ZoneRuntimeCallbackContextStatus::InvalidPhase;

    mutableContext->setPhase(nextPhase);
    return mutableContext->canonical(exactIndex)
        ? ZoneRuntimeCallbackContextStatus::Success
        : ZoneRuntimeCallbackContextStatus::UnsafeFailure;
}

ZoneRuntimeCallbackContextStatus
ZoneRuntimeCallbackContextOwner::TryAuthenticate(
    const ZoneRuntimeCallbackContext *const context,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeCallbackContextPhase expectedPhase) noexcept
{
    const zone_load::ZoneLoadContextKey inputKey = key;
    if (!static_cast<bool>(inputKey))
        return ZoneRuntimeCallbackContextStatus::InvalidKey;
    if (!IsRetainedPhase(expectedPhase))
        return ZoneRuntimeCallbackContextStatus::InvalidPhase;

    const std::size_t exactIndex = ExactStoreIndex(context);
    if (exactIndex == kInvalidStoreIndex)
        return ZoneRuntimeCallbackContextStatus::InvalidArgument;
    const ZoneRuntimeCallbackContextStatus storageStatus =
        MapExactStorageStatus(TryClassifyStorage(context));
    if (storageStatus != ZoneRuntimeCallbackContextStatus::Success)
        return storageStatus;

    const ZoneRuntimeCallbackContext *const exact =
        &ContextStore()[exactIndex];
    if (!exact->canonical(exactIndex))
        return ZoneRuntimeCallbackContextStatus::UnsafeFailure;
    if (inputKey.slot != exactIndex)
        return ZoneRuntimeCallbackContextStatus::StaleKey;
    if (!IsRetainedPhase(exact->phase_))
        return ZoneRuntimeCallbackContextStatus::InvalidPhase;
    if (inputKey != exact->key_)
        return ZoneRuntimeCallbackContextStatus::StaleKey;
    return exact->phase_ == expectedPhase
        ? ZoneRuntimeCallbackContextStatus::Success
        : ZoneRuntimeCallbackContextStatus::InvalidPhase;
}

ZoneRuntimeCallbackContextSnapshot
ZoneRuntimeCallbackContextOwner::TryCapture(
    const ZoneRuntimeCallbackContext *const context,
    const zone_load::ZoneLoadContextKey &expectedKey) noexcept
{
    ZoneRuntimeCallbackContextSnapshot snapshot{};
    const zone_load::ZoneLoadContextKey inputKey = expectedKey;
    if (!static_cast<bool>(inputKey))
    {
        snapshot.status = ZoneRuntimeCallbackContextStatus::InvalidKey;
        return snapshot;
    }

    const std::size_t exactIndex = ExactStoreIndex(context);
    if (exactIndex == kInvalidStoreIndex)
        return snapshot;
    const ZoneRuntimeCallbackContextStatus storageStatus =
        MapExactStorageStatus(TryClassifyStorage(context));
    if (storageStatus != ZoneRuntimeCallbackContextStatus::Success)
    {
        snapshot.status = storageStatus;
        return snapshot;
    }

    const ZoneRuntimeCallbackContext *const exact =
        &ContextStore()[exactIndex];
    if (!exact->canonical(exactIndex))
    {
        snapshot.status = ZoneRuntimeCallbackContextStatus::UnsafeFailure;
        return snapshot;
    }
    if (inputKey.slot != exactIndex)
    {
        snapshot.status = ZoneRuntimeCallbackContextStatus::StaleKey;
        return snapshot;
    }
    if (!IsRetainedPhase(exact->phase_))
    {
        snapshot.status = ZoneRuntimeCallbackContextStatus::InvalidPhase;
        return snapshot;
    }
    if (inputKey != exact->key_)
    {
        snapshot.status = ZoneRuntimeCallbackContextStatus::StaleKey;
        return snapshot;
    }

    snapshot.key = exact->key_;
    snapshot.physicalSlot = exact->physicalSlot_;
    snapshot.phase = exact->phase_;
    snapshot.status = ZoneRuntimeCallbackContextStatus::Success;
    return snapshot;
}

bool ZoneRuntimeCallbackContextOwner::SpanIsSeparated(
    const void *const storage,
    const std::size_t size,
    const std::size_t alignment) noexcept
{
    if (!storage || size == 0 || alignment == 0)
        return false;

    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(storage);
    if (start % alignment != 0)
        return false;

    const std::uintptr_t maximum =
        (std::numeric_limits<std::uintptr_t>::max)();
    if (size > maximum - start)
        return false;
    const std::uintptr_t end = start + size;

    const std::uintptr_t bankBegin =
        reinterpret_cast<std::uintptr_t>(g_contextStore.data());
    const std::size_t bankSize = sizeof(g_contextStore);
    if (bankSize > maximum - bankBegin)
        return false;
    const std::uintptr_t bankEnd = bankBegin + bankSize;
    return end <= bankBegin || bankEnd <= start;
}

#ifdef KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING
const ZoneRuntimeCallbackContext *
ZoneRuntimeCallbackContextTestAccess::ContextForPhysicalSlot(
    const std::uint32_t physicalSlot) noexcept
{
    if (physicalSlot >= zone_slots::kPhysicalZoneSlotCount)
        return nullptr;
    const ZoneRuntimeCallbackContext *const context =
        &ContextStore()[physicalSlot];
    (void)TryClassifyStorage(context);
    return context;
}

void ZoneRuntimeCallbackContextTestAccess::CorruptSelf(
    const ZoneRuntimeCallbackContext *const context) noexcept
{
    const std::size_t index = ExactStoreIndex(context);
    if (index != kInvalidStoreIndex)
        ContextStore()[index].self_ = nullptr;
}

void ZoneRuntimeCallbackContextTestAccess::CorruptWitness(
    const ZoneRuntimeCallbackContext *const context) noexcept
{
    const std::size_t index = ExactStoreIndex(context);
    if (index != kInvalidStoreIndex)
        ContextStore()[index].witness_ ^= 1u;
}

void ZoneRuntimeCallbackContextTestAccess::CorruptReserved(
    const ZoneRuntimeCallbackContext *const context) noexcept
{
    const std::size_t index = ExactStoreIndex(context);
    if (index != kInvalidStoreIndex)
        ContextStore()[index].reserved_[1] = 1u;
}

#if !KISAK_ARCH_64BIT
void ZoneRuntimeCallbackContextTestAccess::CorruptPointerAlignmentPadding(
    const ZoneRuntimeCallbackContext *const context) noexcept
{
    const std::size_t index = ExactStoreIndex(context);
    if (index != kInvalidStoreIndex)
        ContextStore()[index].pointerAlignmentPadding_[2] = 1u;
}
#endif

void ZoneRuntimeCallbackContextTestAccess::Restore(
    const ZoneRuntimeCallbackContext *const context,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeCallbackContextPhase phase) noexcept
{
    const std::size_t index = ExactStoreIndex(context);
    if (index == kInvalidStoreIndex)
        return;
    ZoneRuntimeCallbackContext &mutableContext = ContextStore()[index];
    mutableContext.initialize(physicalSlot);
    if (IsRetainedPhase(phase))
    {
        mutableContext.key_ = key;
        mutableContext.phase_ = phase;
        mutableContext.refreshWitness();
    }
}
#endif
} // namespace db::zone_runtime
