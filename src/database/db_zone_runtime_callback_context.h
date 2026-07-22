#pragma once

#include <database/db_zone_load_context.h>
#include <universal/kisak_abi.h>
#include <universal/physicalmemory_runtime.h>

#include <cstddef>
#include <cstdint>

namespace db::zone_runtime
{
class ZoneRuntimeFacade;
class ZoneRuntimeTable;
class ZoneRuntimeCallbackContextOwner;

#ifdef KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING
struct ZoneRuntimeCallbackContextTestAccess;
#endif

// These phases describe callback identity only. Loading, admission, live, and
// cleanup execution modes remain solely owned by ZoneRuntimeTable, avoiding a
// second lifecycle authority that could drift from the durable generation.
enum class ZoneRuntimeCallbackContextPhase : std::uint8_t
{
    Reserved,
    Unbound,
    Bound,
    Terminal,
};

enum class ZoneRuntimeCallbackContextStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidSlot,
    InvalidKey,
    StaleKey,
    InvalidPhase,
    GenerationExhausted,
    UnsafeFailure,
};

// One typed context occupies each physical registry slot for the lifetime of
// the process. Slot zero is represented but permanently reserved. The default
// constructor creates no authority: only the owner-private 33-entry store can
// initialize a canonical self/index/key witness, and fabricated addresses are
// rejected before representation access. Copy and move stay forbidden.
//
// The 0x28 layout is target-neutral: 0x10 key, one native pointer, an explicit
// zero-checked 0x4 ILP32-only pointer-alignment gap, 0x8 witness, 0x4 slot, and
// one phase plus three reserved bytes. It changes no legacy owner ABI.
class alignas(8) ZoneRuntimeCallbackContext final
{
public:
    constexpr ZoneRuntimeCallbackContext() noexcept = default;
    ~ZoneRuntimeCallbackContext() noexcept = default;

    ZoneRuntimeCallbackContext(
        const ZoneRuntimeCallbackContext &) = delete;
    ZoneRuntimeCallbackContext &operator=(
        const ZoneRuntimeCallbackContext &) = delete;
    ZoneRuntimeCallbackContext(ZoneRuntimeCallbackContext &&) = delete;
    ZoneRuntimeCallbackContext &operator=(
        ZoneRuntimeCallbackContext &&) = delete;

private:
    friend class ZoneRuntimeCallbackContextOwner;
#ifdef KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING
    friend struct ZoneRuntimeCallbackContextTestAccess;
#endif

    [[nodiscard]] bool canonical(std::size_t exactStoreIndex) const noexcept;
    void initialize(std::uint32_t physicalSlot) noexcept;
    void bind(const zone_load::ZoneLoadContextKey &key) noexcept;
    void setPhase(ZoneRuntimeCallbackContextPhase phase) noexcept;
    void refreshWitness() noexcept;

    zone_load::ZoneLoadContextKey key_{};
    const ZoneRuntimeCallbackContext *self_ = nullptr;
#if !KISAK_ARCH_64BIT
    std::uint8_t pointerAlignmentPadding_[4]{};
#endif
    std::uint64_t witness_ = 0;
    std::uint32_t physicalSlot_ = zone_load::kInvalidZoneLoadSlot;
    ZoneRuntimeCallbackContextPhase phase_ =
        ZoneRuntimeCallbackContextPhase::Unbound;
    std::uint8_t reserved_[3]{};
};

RUNTIME_SIZE(ZoneRuntimeCallbackContext, 0x28, 0x28);

// This value is public only as a typed, immutable transfer between the two
// friend owners and their future typed callbacks. Production callers cannot
// invoke the private bind operation that produces a store address.
struct ZoneRuntimeCallbackContextBindResult final
{
    const ZoneRuntimeCallbackContext *context = nullptr;
    ZoneRuntimeCallbackContextStatus status =
        ZoneRuntimeCallbackContextStatus::InvalidArgument;
    std::uint8_t reserved[3]{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return status == ZoneRuntimeCallbackContextStatus::Success
            && context && reserved[0] == 0 && reserved[1] == 0
            && reserved[2] == 0;
    }
};

RUNTIME_SIZE(ZoneRuntimeCallbackContextBindResult, 0x8, 0x10);

// Capture requires the caller's copied expected key before this pointer-free
// identity can be produced. A stale pointer alone can never disclose or adopt
// the generation installed by a later slot reuse.
struct ZoneRuntimeCallbackContextSnapshot final
{
    zone_load::ZoneLoadContextKey key{};
    std::uint32_t physicalSlot = zone_load::kInvalidZoneLoadSlot;
    ZoneRuntimeCallbackContextPhase phase =
        ZoneRuntimeCallbackContextPhase::Unbound;
    ZoneRuntimeCallbackContextStatus status =
        ZoneRuntimeCallbackContextStatus::InvalidArgument;
    std::uint8_t reserved[2]{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return status == ZoneRuntimeCallbackContextStatus::Success
            && static_cast<bool>(key) && key.slot == physicalSlot
            && (phase == ZoneRuntimeCallbackContextPhase::Bound
                || phase == ZoneRuntimeCallbackContextPhase::Terminal)
            && reserved[0] == 0 && reserved[1] == 0;
    }
};

RUNTIME_SIZE(ZoneRuntimeCallbackContextSnapshot, 0x18, 0x18);

// The complete capability surface is private. Only the real process facade
// and durable table may bind, resolve, advance, authenticate, classify,
// capture, or compare an arbitrary span with the complete store. The raw
// classifier preserves PMem's precise status; the other lifecycle operations
// convert an impossible overlap/invalid classification for an exact private
// member into UnsafeFailure.
//
// Except for the address-only SpanIsSeparated predicate, these operations
// are intentionally unsynchronized. Future production enrollment must call
// them only while the existing facade/table process-lifetime serializer is
// held across the complete operation and any external callback window.
class ZoneRuntimeCallbackContextOwner final
{
public:
    ZoneRuntimeCallbackContextOwner() = delete;
    ~ZoneRuntimeCallbackContextOwner() = delete;
    ZoneRuntimeCallbackContextOwner(
        const ZoneRuntimeCallbackContextOwner &) = delete;
    ZoneRuntimeCallbackContextOwner &operator=(
        const ZoneRuntimeCallbackContextOwner &) = delete;

private:
    friend class ZoneRuntimeFacade;
    friend class ZoneRuntimeTable;
#ifdef KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING
    friend struct ZoneRuntimeCallbackContextTestAccess;
#endif

    [[nodiscard]] static pmem_runtime::StorageIsolationStatus
    TryClassifyStorage(
        const ZoneRuntimeCallbackContext *context) noexcept;
    [[nodiscard]] static ZoneRuntimeCallbackContextBindResult TryBind(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeCallbackContextBindResult TryResolve(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept;
    [[nodiscard]] static ZoneRuntimeCallbackContextStatus TryAdvance(
        const ZoneRuntimeCallbackContext *context,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeCallbackContextPhase nextPhase) noexcept;
    [[nodiscard]] static ZoneRuntimeCallbackContextStatus TryAuthenticate(
        const ZoneRuntimeCallbackContext *context,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeCallbackContextPhase expectedPhase) noexcept;
    [[nodiscard]] static ZoneRuntimeCallbackContextSnapshot TryCapture(
        const ZoneRuntimeCallbackContext *context,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept;
    [[nodiscard]] static bool SpanIsSeparated(
        const void *storage,
        std::size_t size,
        std::size_t alignment) noexcept;
    [[nodiscard]] static ZoneRuntimeCallbackContextStatus
    TryAuthenticateStructural(
        const ZoneRuntimeCallbackContext *context,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeCallbackContextPhase expectedPhase) noexcept;
    [[nodiscard]] static ZoneRuntimeCallbackContextStatus
    TryAuthenticateUnused(std::uint32_t physicalSlot) noexcept;
    [[nodiscard]] static ZoneRuntimeCallbackContextStatus
    TryAuthenticateStore() noexcept;
};

#ifdef KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING
// Tests opt in before including this header. Production translation units
// have neither these wrappers nor friendship around the private owner.
struct ZoneRuntimeCallbackContextTestAccess final
{
    [[nodiscard]] static pmem_runtime::StorageIsolationStatus
    TryClassifyStorage(
        const ZoneRuntimeCallbackContext *context) noexcept
    {
        return ZoneRuntimeCallbackContextOwner::TryClassifyStorage(context);
    }

    [[nodiscard]] static ZoneRuntimeCallbackContextBindResult TryBind(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept
    {
        return ZoneRuntimeCallbackContextOwner::TryBind(physicalSlot, key);
    }

    [[nodiscard]] static ZoneRuntimeCallbackContextBindResult TryResolve(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept
    {
        return ZoneRuntimeCallbackContextOwner::TryResolve(
            physicalSlot, expectedKey);
    }

    [[nodiscard]] static ZoneRuntimeCallbackContextStatus TryAdvance(
        const ZoneRuntimeCallbackContext *context,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeCallbackContextPhase nextPhase) noexcept
    {
        return ZoneRuntimeCallbackContextOwner::TryAdvance(
            context, key, nextPhase);
    }

    [[nodiscard]] static ZoneRuntimeCallbackContextStatus TryAuthenticate(
        const ZoneRuntimeCallbackContext *context,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeCallbackContextPhase expectedPhase) noexcept
    {
        return ZoneRuntimeCallbackContextOwner::TryAuthenticate(
            context, key, expectedPhase);
    }

    [[nodiscard]] static ZoneRuntimeCallbackContextSnapshot TryCapture(
        const ZoneRuntimeCallbackContext *context,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept
    {
        return ZoneRuntimeCallbackContextOwner::TryCapture(
            context, expectedKey);
    }

    [[nodiscard]] static bool SpanIsSeparated(
        const void *storage,
        std::size_t size,
        std::size_t alignment) noexcept
    {
        return ZoneRuntimeCallbackContextOwner::SpanIsSeparated(
            storage, size, alignment);
    }

    [[nodiscard]] static const ZoneRuntimeCallbackContext *
    ContextForPhysicalSlot(std::uint32_t physicalSlot) noexcept;
    [[nodiscard]] static const zone_load::ZoneLoadContextKey *
    RetainedKey(const ZoneRuntimeCallbackContext *context) noexcept;
    static void CorruptSelf(
        const ZoneRuntimeCallbackContext *context) noexcept;
    static void CorruptWitness(
        const ZoneRuntimeCallbackContext *context) noexcept;
    static void CorruptReserved(
        const ZoneRuntimeCallbackContext *context) noexcept;
#if !KISAK_ARCH_64BIT
    static void CorruptPointerAlignmentPadding(
        const ZoneRuntimeCallbackContext *context) noexcept;
#endif
    static void Restore(
        const ZoneRuntimeCallbackContext *context,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeCallbackContextPhase phase) noexcept;
};
#endif
} // namespace db::zone_runtime
