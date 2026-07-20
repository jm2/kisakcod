#include <universal/physicalmemory_checked.h>
#include <universal/physicalmemory_runtime.h>

#include <type_traits>
#include <utility>

namespace
{
using Receipt = physical_memory::AllocationReceipt;

template <typename T>
concept CanReachOwner = requires(T &value) { value.owner_; };

template <typename T>
concept CanReachSelf = requires(T &value) { value.self_; };

template <typename T>
concept CanReachPrim = requires(T &value) { value.prim_; };

template <typename T>
concept CanReachName = requires(T &value) { value.name_; };

template <typename T>
concept CanReachIndex = requires(T &value) { value.index_; };

template <typename T>
concept CanReachAllocType = requires(T &value) { value.allocType_; };

template <typename T>
concept CanReachStartPosition = requires(T &value) { value.startPos_; };

template <typename T>
concept CanReachPhase = requires(T &value) { value.phase_; };

template <typename T>
concept CanReachPhaseWitness = requires(T &value) { value.phaseWitness_; };

template <typename T>
concept CanReachReserved = requires(T &value) { value.reserved_; };

template <typename T>
concept CanReset = requires(T &value) { value.Reset(); };

template <typename T>
concept CanCheckCanonicalTerminalState =
    requires(const T &value) { value.hasCanonicalTerminalState(); };

template <typename T>
concept CanSetCanonicalTerminalState =
    requires(T &value) { value.setCanonicalTerminalState(); };

static_assert(!std::is_copy_constructible_v<Receipt>);
static_assert(!std::is_copy_assignable_v<Receipt>);
static_assert(!std::is_move_constructible_v<Receipt>);
static_assert(!std::is_move_assignable_v<Receipt>);
static_assert(!std::is_trivially_copyable_v<Receipt>);
static_assert(std::is_nothrow_destructible_v<Receipt>);
static_assert(!CanReachSelf<Receipt>);
static_assert(!CanReachOwner<Receipt>);
static_assert(!CanReachPrim<Receipt>);
static_assert(!CanReachName<Receipt>);
static_assert(!CanReachIndex<Receipt>);
static_assert(!CanReachAllocType<Receipt>);
static_assert(!CanReachStartPosition<Receipt>);
static_assert(!CanReachPhase<Receipt>);
static_assert(!CanReachPhaseWitness<Receipt>);
static_assert(!CanReachReserved<Receipt>);
static_assert(!CanReset<Receipt>);
static_assert(!CanCheckCanonicalTerminalState<Receipt>);
static_assert(!CanSetCanonicalTerminalState<Receipt>);
static_assert(noexcept(physical_memory::TryBegin(
    static_cast<PhysicalMemory *>(nullptr),
    0,
    static_cast<const char *>(nullptr),
    static_cast<Receipt *>(nullptr))));
static_assert(noexcept(physical_memory::TryEnd(nullptr)));
static_assert(noexcept(physical_memory::TryFree(nullptr)));
static_assert(noexcept(
    pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
        std::declval<const Receipt &>(),
        std::declval<const PhysicalMemory &>(),
        0,
        0,
        nullptr,
        pmem_runtime::AllocationReceiptPhase::Pristine)));
static_assert(noexcept(
    pmem_runtime::detail::AuthenticateAllocationRangeNoLock(
        std::declval<const Receipt &>(),
        std::declval<const PhysicalMemory &>(),
        0,
        0,
        nullptr,
        nullptr,
        0,
        pmem_runtime::AllocationReceiptPhase::Begun)));
static_assert(std::is_same_v<
    decltype(pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
        std::declval<const Receipt &>(),
        std::declval<const PhysicalMemory &>(),
        0,
        0,
        nullptr,
        pmem_runtime::AllocationReceiptPhase::Pristine)),
    bool>);
} // namespace

int main()
{
    return 0;
}
