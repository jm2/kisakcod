#pragma once

#include <database/db_zone_load_context.h>
#include <database/db_zone_script_string_ownership.h>
#include <database/db_zone_slots.h>
#include <universal/kisak_abi.h>

#include <array>
#include <cstdint>

namespace db::zone_runtime
{
// This table is the durable, allocation-independent owner for the metadata of
// every native zone-registry slot.  It is deliberately separate from XZone:
// the legacy loader clears XZone with memset and frees its PMem allocation
// before lifecycle cleanup can publish an empty slot.  Journal entries, FX
// workspaces, native arenas, and other per-generation storage do not belong in
// this table; later wiring binds those resources by exact generation key.
//
// Physical slot zero remains the engine's reserved/default slot.  The table
// has stable storage for all 33 physical slots.  Only slots 1..32 are usable.

enum class ZoneRuntimeTableStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidSlot,
    InvalidState,
    InvalidKey,
    StaleKey,
    InvalidPhase,
    GenerationExhausted,
    UnsafeFailure,
};

class ZoneRuntimeEntry;
class ZoneRuntimeTable;

// An authenticated, read-only observation of one active generation.  The key
// is copied by value so a retained old view cannot silently become an authority
// for a later generation at the same stable table address.  Mutable lifecycle
// and ownership operations stay private until table adapters can reauthenticate
// this key immediately before every operation.  Callers must retain the same
// external per-slot serializer across lookup and use.
struct ZoneRuntimeGenerationView final
{
    zone_load::ZoneLoadContextKey key{};
    const ZoneRuntimeEntry *entry = nullptr;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return static_cast<bool>(key) && entry;
    }
};

RUNTIME_SIZE(ZoneRuntimeGenerationView, 0x18, 0x18);

#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
struct ZoneRuntimeTableTestAccess;
#endif

class alignas(8) ZoneRuntimeEntry final
{
public:
    ZoneRuntimeEntry() noexcept = default;
    ~ZoneRuntimeEntry() noexcept = default;

    ZoneRuntimeEntry(const ZoneRuntimeEntry &) = delete;
    ZoneRuntimeEntry &operator=(const ZoneRuntimeEntry &) = delete;
    ZoneRuntimeEntry(ZoneRuntimeEntry &&) = delete;
    ZoneRuntimeEntry &operator=(ZoneRuntimeEntry &&) = delete;

    [[nodiscard]] const zone_load::ZoneLoadContextKey &key() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextSlot &lifecycle()
        const noexcept;
    [[nodiscard]] const zone_script_string_ownership::
        ZoneScriptStringOwnershipController &scriptStringOwnership()
        const noexcept;

private:
    friend ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable(
        ZoneRuntimeTable *table) noexcept;
    friend ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const ZoneRuntimeEntry **outEntry) noexcept;
    friend ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        zone_load::ZoneLoadContextKey *inOutKey) noexcept;
    friend ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeGenerationView *outView) noexcept;
#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
    friend struct ZoneRuntimeTableTestAccess;
#endif

    zone_load::ZoneLoadContextSlot lifecycle_{};
    zone_script_string_ownership::ZoneScriptStringOwnershipController
        scriptStringOwnership_{};
    zone_load::ZoneLoadContextKey key_{};
};

RUNTIME_SIZE(ZoneRuntimeEntry, 0x60, 0x78);

class alignas(8) ZoneRuntimeTable final
{
public:
    ZoneRuntimeTable() noexcept = default;
    ~ZoneRuntimeTable() noexcept = default;

    ZoneRuntimeTable(const ZoneRuntimeTable &) = delete;
    ZoneRuntimeTable &operator=(const ZoneRuntimeTable &) = delete;
    ZoneRuntimeTable(ZoneRuntimeTable &&) = delete;
    ZoneRuntimeTable &operator=(ZoneRuntimeTable &&) = delete;

    // This is a diagnostic hint, not a synchronization primitive.  Every
    // initialization, lookup, claim, accessor, and capability use requires
    // external serialization.  DB_Init initializes the production object
    // before the database thread can observe it.
    [[nodiscard]] bool initialized() const noexcept;

private:
    friend ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable(
        ZoneRuntimeTable *table) noexcept;
    friend ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const ZoneRuntimeEntry **outEntry) noexcept;
    friend ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        zone_load::ZoneLoadContextKey *inOutKey) noexcept;
    friend ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeGenerationView *outView) noexcept;
#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
    friend struct ZoneRuntimeTableTestAccess;
#endif

    [[nodiscard]] ZoneRuntimeTableStatus
    validateInitializedHeader() noexcept;
    void poison() noexcept;

    std::array<ZoneRuntimeEntry, zone_slots::kPhysicalZoneSlotCount>
        entries_{};
    std::uint32_t state_ = 0;
    std::uint32_t reserved_ = 0;
};

RUNTIME_SIZE(ZoneRuntimeTable, 0xC68, 0xF80);

#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
// Tests opt in before including this header.  Production callers cannot reach
// unkeyed mutable entry state.
struct ZoneRuntimeTableTestAccess final
{
    static zone_load::ZoneLoadContextSlot *Lifecycle(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot].lifecycle_;
    }

    static zone_script_string_ownership::
        ZoneScriptStringOwnershipController *Ownership(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot].scriptStringOwnership_;
    }

    static void SetKey(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept
    {
        if (table && physicalSlot < table->entries_.size())
            table->entries_[physicalSlot].key_ = key;
    }

    static void SetReserved(
        ZoneRuntimeTable *const table,
        const std::uint32_t reserved) noexcept
    {
        if (table)
            table->reserved_ = reserved;
    }
};
#endif

// Returns the process-lifetime production table.  The object owns only durable
// control metadata and therefore outlives every per-generation PMem region.
[[nodiscard]] ZoneRuntimeTable &ProductionZoneRuntimeTable() noexcept;

// Initializes all usable lifecycle slots exactly once.  Repeating this call
// succeeds only for the exact pristine initialized representation.  A table
// that is partially initialized or corrupt is poisoned; a valid table already
// rebound to a generation is rejected without resetting any ownership.
[[nodiscard]] ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable(
    ZoneRuntimeTable *table) noexcept;

// Checked, read-only physical lookup.  Slot zero and out-of-range slots are
// rejected.  The caller's output is unchanged on every non-Success result.
[[nodiscard]] ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const ZoneRuntimeEntry **outEntry) noexcept;

// Claims one usable slot through its embedded generation lifecycle.  This
// function is implemented for the future loader adapter, but the legacy loader
// does not call it yet.  Production wiring must first add exact-key adapters
// for every mutable ownership operation, validate the controller/lifecycle
// phase matrix, and provide exact terminal-receipt reset/unload adapters; a
// claimed slot intentionally cannot otherwise be rebound after abandonment.
// The durable entry key and caller output publish only after the lifecycle
// claim succeeds.
[[nodiscard]] ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    zone_load::ZoneLoadContextKey *inOutKey) noexcept;

// Authenticates both physical slot and generation and returns a read-only
// observation only for an active Loading/Live/Abandoning slot.  Mutable
// controller operations require future keyed table adapters; callers cannot
// retain raw mutable authority across generation reuse.  The caller's output
// is unchanged on every non-Success result.
[[nodiscard]] ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    ZoneRuntimeGenerationView *outView) noexcept;

} // namespace db::zone_runtime
