#pragma once

#include <database/db_zone_load_context.h>
#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

namespace db::script_string_journal
{
class ScriptStringJournal;
struct ScriptStringJournalEntry;
} // namespace db::script_string_journal

namespace fx::fastfile
{
class FxFastFileNativeArena;
class FxFastFileZoneAdapterDisk32Workspace;
} // namespace fx::fastfile

namespace db::zone_runtime_storage
{
class ZoneRuntimeStorageBinding;
} // namespace db::zone_runtime_storage

namespace db::zone_runtime::detail
{
[[nodiscard]] bool IsPristineRuntimeReceipt(
    const zone_runtime_storage::ZoneRuntimeStorageBinding &binding) noexcept;
} // namespace db::zone_runtime::detail

namespace db::zone_runtime_storage
{
// Every planned address is a 32-bit byte displacement from one caller-owned
// slab. The planner accepts a wider arena request so truncation can never turn
// an unrepresentable request into a smaller successful allocation.
struct ZoneRuntimeStorageExtent final
{
    std::uint32_t offset = 0;
    std::uint32_t byteCount = 0;

    [[nodiscard]] constexpr bool operator==(
        const ZoneRuntimeStorageExtent &) const noexcept = default;
};

struct ZoneRuntimeStoragePlan final
{
    std::uint32_t expectedStringCount = 0;
    std::uint32_t arenaBudget = 0;
    ZoneRuntimeStorageExtent scriptStringJournal{};
    ZoneRuntimeStorageExtent scriptStringEntries{};
    ZoneRuntimeStorageExtent fxNativeArena{};
    ZoneRuntimeStorageExtent fxZoneAdapterWorkspace{};
    ZoneRuntimeStorageExtent fxArenaBacking{};
    std::uint32_t totalBytes = 0;

    [[nodiscard]] constexpr bool operator==(
        const ZoneRuntimeStoragePlan &) const noexcept = default;
};

enum class ZoneRuntimeStorageStatus : std::uint8_t
{
    Success,
    AlreadyComplete,
    Busy,
    InvalidArgument,
    InvalidCount,
    InvalidPlan,
    InvalidBinding,
    InvalidPhase,
    MisalignedStorage,
    SizeOverflow,
    InsufficientCapacity,
    OverlappingStorage,
    ArenaFailed,
};

// Public const-authentication vocabulary. The private representation remains
// hidden so callers can prove an expected binding phase without gaining a
// mutation path into the placement owner.
enum class ZoneRuntimeStorageBindingPhase : std::uint8_t
{
    Pristine,
    Bound,
    Destroyed,
};

// Stable, externally owned expectations for one exact-key storage
// composition.  The journal/entry addresses and count survive terminal
// teardown in the outer script-string controller; this value grants no
// mutation authority over either object.  Pristine and Destroyed require a
// null key and arena identity because their component objects cannot be
// associated with a live generation.
struct alignas(8) ZoneRuntimeStorageCompositionExpectation final
{
    zone_load::ZoneLoadContextKey key{};
    std::uint64_t arenaZoneIdentity = 0;
    const script_string_journal::ScriptStringJournal *journal = nullptr;
    const script_string_journal::ScriptStringJournalEntry *entries = nullptr;
    std::uint32_t capacity = 0;
    std::uint32_t expectedCount = 0;
};

RUNTIME_SIZE(ZoneRuntimeStorageCompositionExpectation, 0x28, 0x30);

enum class ZoneRuntimeStorageCompositionMode : std::uint8_t
{
    Pristine,
    Placed,
    Active,
    Detached,
    Destroyed,
};

// Stable-address handle for the placement-constructed objects in one slab.
// It must live outside that slab and outlive every typed pointer it exposes.
// The handle is neither a zone-generation authority nor a physical-memory
// receipt: future production integration must keep the checked PMem receipt
// and authoritative zone identity in its outer controller.
//
// Binding deliberately leaves FxFastFileNativeArena unbound. The controller
// binds it to fxArenaBacking() with the already-authoritative zone identity.
// Before journal use, that controller must call
// TryInitializeScriptStringJournal with exactly scriptStringEntries(),
// plan()->expectedStringCount for both capacity and expected count, and its
// authoritative key. The zero-count case must use null storage and zero
// capacity/count. This layer never mints or substitutes that key.
// Destruction accepts either that arena or the pristine unbound arena, and
// unbinds it only after the journal and adapter are safe to tear down.
class ZoneRuntimeStorageBinding final
{
public:
    ZoneRuntimeStorageBinding() noexcept = default;
    // User-provided so byte-copying/replaying an owning handle is not a
    // supported trivially-copyable operation. Cleanup remains explicit.
    ~ZoneRuntimeStorageBinding() noexcept {}

    ZoneRuntimeStorageBinding(const ZoneRuntimeStorageBinding &) = delete;
    ZoneRuntimeStorageBinding &operator=(
        const ZoneRuntimeStorageBinding &) = delete;
    ZoneRuntimeStorageBinding(ZoneRuntimeStorageBinding &&) = delete;
    ZoneRuntimeStorageBinding &operator=(ZoneRuntimeStorageBinding &&) = delete;

    [[nodiscard]] bool bound() const noexcept;
    [[nodiscard]] bool destroyed() const noexcept;
    [[nodiscard]] void *slab() const noexcept;
    [[nodiscard]] std::size_t slabCapacity() const noexcept;
    [[nodiscard]] const ZoneRuntimeStoragePlan *plan() const noexcept;
    [[nodiscard]] script_string_journal::ScriptStringJournal *
    scriptStringJournal() const noexcept;
    [[nodiscard]] script_string_journal::ScriptStringJournalEntry *
    scriptStringEntries() const noexcept;
    [[nodiscard]] fx::fastfile::FxFastFileNativeArena *
    fxNativeArena() const noexcept;
    [[nodiscard]] fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *
    fxZoneAdapterWorkspace() const noexcept;
    [[nodiscard]] void *fxArenaBacking() const noexcept;

private:
    friend bool AuthenticateZoneRuntimeStorageBinding(
        const ZoneRuntimeStorageBinding &,
        ZoneRuntimeStorageBindingPhase) noexcept;
    friend bool AuthenticateZoneRuntimeStorageComposition(
        const ZoneRuntimeStorageBinding &,
        const ZoneRuntimeStorageCompositionExpectation &,
        ZoneRuntimeStorageCompositionMode) noexcept;
    friend ZoneRuntimeStorageStatus TryBindZoneRuntimeStorage(
        void *,
        std::size_t,
        const ZoneRuntimeStoragePlan *,
        ZoneRuntimeStorageBinding *) noexcept;
    friend ZoneRuntimeStorageStatus TryDestroyZoneRuntimeStorage(
        ZoneRuntimeStorageBinding *) noexcept;
    // Exact const-only friendship for passive runtime-table authentication.
    friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt(
        const ZoneRuntimeStorageBinding &binding) noexcept;

    enum class State : std::uint8_t
    {
        Pristine,
        Bound,
        Destroyed,
    };

    [[nodiscard]] bool isPristine() const noexcept
    {
        return self_ == nullptr && slab_ == nullptr && slabCapacity_ == 0
            && plan_ == ZoneRuntimeStoragePlan{} && journal_ == nullptr
            && entries_ == nullptr && arena_ == nullptr
            && workspace_ == nullptr && arenaBacking_ == nullptr
            && state_ == State::Pristine;
    }
    [[nodiscard]] bool isSelfAuthenticating(State state) const noexcept;
    [[nodiscard]] bool hasCanonicalPlacementMetadata(State state) const
        noexcept;
    [[nodiscard]] bool hasCanonicalBoundMetadata() const noexcept;

    const ZoneRuntimeStorageBinding *self_ = nullptr;
    void *slab_ = nullptr;
    std::size_t slabCapacity_ = 0;
    ZoneRuntimeStoragePlan plan_{};
    script_string_journal::ScriptStringJournal *journal_ = nullptr;
    script_string_journal::ScriptStringJournalEntry *entries_ = nullptr;
    fx::fastfile::FxFastFileNativeArena *arena_ = nullptr;
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace_ = nullptr;
    void *arenaBacking_ = nullptr;
    State state_ = State::Pristine;
};

RUNTIME_SIZE(ZoneRuntimeStorageBinding, 0x58, 0x80);

[[nodiscard]] bool AuthenticateZoneRuntimeStorageBinding(
    const ZoneRuntimeStorageBinding &binding,
    ZoneRuntimeStorageBindingPhase expectedPhase) noexcept;

// Authenticates one externally serialized, stable operation boundary without
// allocating, mutating, reporting, or returning a component pointer. Placed
// requires an exact pristine journal; Active requires an exact, nonterminal,
// non-callback journal; Detached requires its exact terminal detached state.
// All three require the planned native arena to be bound to the exact key's
// generation as its supplied nonzero identity, with an Idle adapter and no
// open arena transaction.
// Destroyed compares the retained placement and journal/entry identities but
// never dereferences an object whose lifetime has ended.
[[nodiscard]] bool AuthenticateZoneRuntimeStorageComposition(
    const ZoneRuntimeStorageBinding &binding,
    const ZoneRuntimeStorageCompositionExpectation &expectation,
    ZoneRuntimeStorageCompositionMode mode) noexcept;

// Produces the exact canonical layout. expectedStringCount may be 0 through
// kMaxScriptStringJournalEntries. arenaBudget must be nonzero and has no cap
// other than fitting, together with the fixed objects, in 32-bit offsets and
// totalBytes. Failure never writes outPlan.
[[nodiscard]] ZoneRuntimeStorageStatus TryPlanZoneRuntimeStorage(
    std::uint32_t expectedStringCount,
    std::uint64_t arenaBudget,
    ZoneRuntimeStoragePlan *outPlan) noexcept;

// Validates the complete plan, slab, and pristine stable-address output before
// the first slab write. slabCapacity may exceed plan.totalBytes, but its full
// address range must be representable and disjoint from outBinding. The plan
// is snapshotted before placement, so a plan stored inside the supplied slab
// is safe on success and remains untouched on every failure.
[[nodiscard]] ZoneRuntimeStorageStatus TryBindZoneRuntimeStorage(
    void *slab,
    std::size_t slabCapacity,
    const ZoneRuntimeStoragePlan *plan,
    ZoneRuntimeStorageBinding *outBinding) noexcept;

// Requires a pristine or terminal-detached journal, an Idle adapter, and no
// open arena transaction. If the arena was bound by the outer controller,
// TryUnbind is the sole fallible mutation. Successful destruction runs in
// reverse construction order and terminalizes the handle; repeated calls
// return AlreadyComplete without touching the slab. The handle retains its
// immutable slab, plan, and component addresses after object destruction so
// an outer exact-key controller can authenticate terminal historical identity.
// Its ordinary typed accessors remain Bound-only and never expose a destroyed
// object. Retained identity is cleared only by ending this handle's lifetime
// and reconstructing a pristine handle at the same address.
//
// The caller must externally serialize planning, binding, every binding and
// embedded-object accessor, every journal/arena/adapter operation, and
// destruction. Before destruction, every published consumer must already be
// unreachable, including every arena reservation and adapter result. These
// lifetime facts belong to the future exact-key outer controller and cannot
// be inferred from this production-neutral placement handle.
[[nodiscard]] ZoneRuntimeStorageStatus TryDestroyZoneRuntimeStorage(
    ZoneRuntimeStorageBinding *binding) noexcept;
} // namespace db::zone_runtime_storage
