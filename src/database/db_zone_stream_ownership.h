#pragma once

#include <database/db_relocation.h>
#include <database/db_zone_load_context.h>
#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

struct XZoneMemory;

namespace db::zone_stream_ownership
{
class ActiveZoneStreamBinding;
class ZoneStreamGenerationReceipt;
} // namespace db::zone_stream_ownership

namespace db::zone_runtime::detail
{
[[nodiscard]] bool IsPristineRuntimeReceipt(
    const zone_stream_ownership::ZoneStreamGenerationReceipt &receipt)
    noexcept;
} // namespace db::zone_runtime::detail

namespace db::zone_stream_ownership
{
// Durable, allocation-independent evidence for one zone-load generation's
// use of the process-wide stream and relocation singletons.  The receipt must
// live outside the zone allocation and outlive cleanup.  It is non-copyable
// and authenticates its stable address, lifecycle address, and exact key.
//
// This foundation is intentionally not enrolled in the loader yet.  Every
// operation is report-free and noexcept, but callers must still retain the
// database/lifecycle serializer across each call and all stream mutations.
enum class ZoneStreamGenerationPhase : std::uint8_t
{
    NeverBound,
    Bound,
    Invalidated,
    UnsafeFailure,
};

enum class ActiveZoneStreamPhase : std::uint8_t
{
    Idle,
    Bound,
    UnsafeFailure,
};

enum class ZoneStreamOwnershipStatus : std::uint8_t
{
    Success,
    AlreadyComplete,
    Busy,
    InvalidArgument,
    InvalidState,
    InvalidKey,
    StaleKey,
    InvalidPhase,
    InvalidLayout,
    GenerationExhausted,
    UnsafeFailure,
};

class ActiveZoneStreamBinding;

// Authenticates the complete passive process-wide stream topology: the exact
// binding object must be pristine, no other binding may own the hidden
// singleton, both relocation contexts must be inactive, and every legacy
// stream cursor/delay/stack field must be scrubbed. This report-free predicate
// grants no mutable stream or relocation capability.
[[nodiscard]] bool AuthenticatePassiveZoneStreamSingleton(
    const ActiveZoneStreamBinding &binding) noexcept;

class alignas(8) ZoneStreamGenerationReceipt final
{
public:
    ZoneStreamGenerationReceipt() noexcept = default;
    ~ZoneStreamGenerationReceipt() noexcept = default;

    ZoneStreamGenerationReceipt(const ZoneStreamGenerationReceipt &) = delete;
    ZoneStreamGenerationReceipt &operator=(
        const ZoneStreamGenerationReceipt &) = delete;
    ZoneStreamGenerationReceipt(ZoneStreamGenerationReceipt &&) = delete;
    ZoneStreamGenerationReceipt &operator=(
        ZoneStreamGenerationReceipt &&) = delete;

    [[nodiscard]] ZoneStreamGenerationPhase phase() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextKey &key() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextSlot *lifecycle()
        const noexcept;
    [[nodiscard]] bool canonical() const noexcept;

private:
    friend ZoneStreamOwnershipStatus TryBeginZoneStreamGeneration(
        ZoneStreamGenerationReceipt *,
        zone_load::ZoneLoadContextSlot *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneStreamOwnershipStatus TryBindZoneStreams(
        ActiveZoneStreamBinding *,
        ZoneStreamGenerationReceipt *,
        const zone_load::ZoneLoadContextKey &,
        const XZoneMemory *,
        const relocation::BlockView *,
        std::size_t) noexcept;
    friend ZoneStreamOwnershipStatus TryInvalidateZoneStreams(
        ActiveZoneStreamBinding *,
        ZoneStreamGenerationReceipt *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend class ActiveZoneStreamBinding;
    friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt(
        const ZoneStreamGenerationReceipt &receipt) noexcept;

    [[nodiscard]] bool isPristine() const noexcept;

    zone_load::ZoneLoadContextKey key_{};
    zone_load::ZoneLoadContextSlot *lifecycle_ = nullptr;
    const ZoneStreamGenerationReceipt *self_ = nullptr;
    std::uint32_t phaseWord_ = 0;
};

RUNTIME_SIZE(ZoneStreamGenerationReceipt, 0x20, 0x28);

// The sole controller for the process-wide stream state.  It is reusable only
// after exact invalidation has cleared all binding metadata.  A second object
// cannot acquire the singleton while this one is active.  The active object,
// bound receipt, lifecycle slot, opaque zone identity, and all nine backing
// spans must remain alive under the external serializer until invalidation.
class alignas(8) ActiveZoneStreamBinding final
{
public:
    ActiveZoneStreamBinding() noexcept = default;
    ~ActiveZoneStreamBinding() noexcept = default;

    ActiveZoneStreamBinding(const ActiveZoneStreamBinding &) = delete;
    ActiveZoneStreamBinding &operator=(
        const ActiveZoneStreamBinding &) = delete;
    ActiveZoneStreamBinding(ActiveZoneStreamBinding &&) = delete;
    ActiveZoneStreamBinding &operator=(ActiveZoneStreamBinding &&) = delete;

    [[nodiscard]] ActiveZoneStreamPhase phase() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextKey &key() const noexcept;
    [[nodiscard]] const ZoneStreamGenerationReceipt *receipt() const noexcept;
    [[nodiscard]] const XZoneMemory *zoneIdentity() const noexcept;
    [[nodiscard]] bool block(
        std::size_t index,
        relocation::BlockView *outBlock) const noexcept;
    [[nodiscard]] bool canonical() const noexcept;

private:
    friend ZoneStreamOwnershipStatus TryBindZoneStreams(
        ActiveZoneStreamBinding *,
        ZoneStreamGenerationReceipt *,
        const zone_load::ZoneLoadContextKey &,
        const XZoneMemory *,
        const relocation::BlockView *,
        std::size_t) noexcept;
    friend ZoneStreamOwnershipStatus TryInvalidateZoneStreams(
        ActiveZoneStreamBinding *,
        ZoneStreamGenerationReceipt *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend bool AuthenticatePassiveZoneStreamSingleton(
        const ActiveZoneStreamBinding &binding) noexcept;

    [[nodiscard]] bool isPristine() const noexcept;

    zone_load::ZoneLoadContextKey key_{};
    ZoneStreamGenerationReceipt *receipt_ = nullptr;
    const XZoneMemory *zoneIdentity_ = nullptr;
    relocation::BlockView blocks_[relocation::kBlockCount]{};
    const ActiveZoneStreamBinding *self_ = nullptr;
    std::uint32_t phaseWord_ = 0;
};

RUNTIME_SIZE(ActiveZoneStreamBinding, 0x68, 0xC0);

// Establishes a stable NeverBound receipt for an exact usable lifecycle slot
// in Loading.  An exact retry is idempotent; output is unchanged on failure.
[[nodiscard]] ZoneStreamOwnershipStatus TryBeginZoneStreamGeneration(
    ZoneStreamGenerationReceipt *receipt,
    zone_load::ZoneLoadContextSlot *lifecycle,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Acquires the singleton for exactly nine authenticated XZoneMemory blocks.
// Every check completes before any singleton mutation, and the receipt's
// NeverBound -> Bound publication is the final write.  zoneIdentity is an
// aligned, stable typed identity; blocks must be its exact descriptor snapshot,
// retained by value so later retries cannot substitute a layout.
[[nodiscard]] ZoneStreamOwnershipStatus TryBindZoneStreams(
    ActiveZoneStreamBinding *active,
    ZoneStreamGenerationReceipt *receipt,
    const zone_load::ZoneLoadContextKey &key,
    const XZoneMemory *zoneIdentity,
    const relocation::BlockView *blocks,
    std::size_t blockCount) noexcept;

// NeverBound -> Invalidated is a no-op abandonment.  Bound invalidation first
// drops alias/direct provenance, then scrubs every stream cursor, delayed span,
// stack record, zone pointer, and logical count before publishing the terminal
// receipt.  An exact terminal retry returns before inspecting active state, so
// an old generation can never clear a newer singleton binding.
[[nodiscard]] ZoneStreamOwnershipStatus TryInvalidateZoneStreams(
    ActiveZoneStreamBinding *active,
    ZoneStreamGenerationReceipt *receipt,
    const zone_load::ZoneLoadContextKey &key) noexcept;
} // namespace db::zone_stream_ownership
