#include <database/db_registry_ownership_coordi\
nator.h>

#include <cstdint>
#include <type_traits>
#include <utility>

using db/**/::/**/registry_ownership/**/::/**/
    FinishRegistryOwnershipCoordinator;
using SplicedFinishPointer = decltype(&FinishRegistryOwnershipCoordi\
nator);
using CommentQualifiedFinishPointer = decltype(
    &db// phase-3 line-comment separator
    ::/**/registry_ownership/**/::/**/FinishRegistryOwnershipCoordinator);
static_assert(
    std::is_same_v<SplicedFinishPointer, CommentQualifiedFinishPointer>);

namespace/**/db { namespace/**/registry_ownership
{
struct CommentNamespaceProbe;
}} // namespace db::registry_ownership

namespace script_string
{
// The public type name is not authority: production cannot construct, copy,
// mutate, authenticate, or use the test-only admission factory. The seven
// bare function-address checks also prove that no OwnershipBatch overload can
// coexist with the exact token-required scalar/bulk API.
struct RegistryOwnershipAdmissionTestAccess final
{
    template <typename Admission>
    static constexpr bool CanCallForTesting = requires(OwnershipBatch &batch)
    {
        Admission::ForTesting(batch);
    };

    template <typename Admission>
    static constexpr bool CanAuthenticateBatch = requires
    {
        &Admission::tryAuthenticateBatchLocked;
    };

    template <typename Admission>
    static constexpr bool CanMutateCoordinatorAddress = requires
    {
        &Admission::coordinatorAddress_;
    };

    template <typename Admission>
    static constexpr bool CanMutateCoordinatorAddressMirror = requires
    {
        &Admission::coordinatorAddressMirror_;
    };

    template <typename Admission>
    static constexpr bool CanMutateCoordinatorSerial = requires
    {
        &Admission::coordinatorSerial_;
    };

    template <typename Admission>
    static constexpr bool CanMutateCoordinatorSerialMirror = requires
    {
        &Admission::coordinatorSerialMirror_;
    };

    template <typename Admission>
    static constexpr bool CanMutateBatchAddress = requires
    {
        &Admission::batchAddress_;
    };

    template <typename Admission>
    static constexpr bool CanMutateBatchAddressMirror = requires
    {
        &Admission::batchAddressMirror_;
    };

    template <typename Admission>
    static constexpr bool CanMutateBatchSerial = requires
    {
        &Admission::batchSerial_;
    };

    template <typename Admission>
    static constexpr bool CanMutateBatchSerialMirror = requires
    {
        &Admission::batchSerialMirror_;
    };
};

using Admission = RegistryOwnershipAdmission;
static_assert(!RegistryOwnershipAdmissionTestAccess::CanCallForTesting<
    Admission>);
static_assert(!RegistryOwnershipAdmissionTestAccess::CanAuthenticateBatch<
    Admission>);
static_assert(
    !RegistryOwnershipAdmissionTestAccess::CanMutateCoordinatorAddress<
        Admission>);
static_assert(
    !RegistryOwnershipAdmissionTestAccess::CanMutateCoordinatorAddressMirror<
        Admission>);
static_assert(
    !RegistryOwnershipAdmissionTestAccess::CanMutateCoordinatorSerial<
        Admission>);
static_assert(
    !RegistryOwnershipAdmissionTestAccess::CanMutateCoordinatorSerialMirror<
        Admission>);
static_assert(!RegistryOwnershipAdmissionTestAccess::CanMutateBatchAddress<
    Admission>);
static_assert(
    !RegistryOwnershipAdmissionTestAccess::CanMutateBatchAddressMirror<
        Admission>);
static_assert(!RegistryOwnershipAdmissionTestAccess::CanMutateBatchSerial<
    Admission>);
static_assert(
    !RegistryOwnershipAdmissionTestAccess::CanMutateBatchSerialMirror<
        Admission>);

static_assert(!std::is_default_constructible_v<Admission>);
static_assert(!std::is_constructible_v<
    Admission,
    std::uintptr_t,
    std::uint64_t,
    std::uintptr_t,
    std::uint64_t>);
static_assert(!std::is_copy_constructible_v<Admission>);
static_assert(!std::is_copy_assignable_v<Admission>);
static_assert(!std::is_move_constructible_v<Admission>);
static_assert(!std::is_move_assignable_v<Admission>);
static_assert(std::is_nothrow_destructible_v<Admission>);
static_assert(std::is_standard_layout_v<Admission>);
static_assert(std::is_final_v<Admission>);

using RegistryAddFunction = DatabaseUserAddStatus (*)(
    const Admission &, std::uint32_t) noexcept;
using RegistryAddBulkFunction = DatabaseUserAddBulkResult (*)(
    const Admission &, const std::uint32_t *, std::uint32_t) noexcept;
using RegistryInternFunction = DatabaseNameResult (*)(
    const Admission &, const char *, std::uint32_t, int) noexcept;
using RegistryReAddFunction = DatabaseNameStatus (*)(
    const Admission &, const char *) noexcept;
using RegistryReAddBulkFunction = DatabaseUserAddBulkResult (*)(
    const Admission &, const char *const *, std::uint32_t) noexcept;
using RegistrySweepFunction = DatabaseSweepStatus (*)(
    const Admission &) noexcept;

static_assert(std::is_same_v<
    decltype(&TryAddDatabaseUser4Reference), RegistryAddFunction>);
static_assert(std::is_same_v<
    decltype(&TryAddDatabaseUser4References), RegistryAddBulkFunction>);
static_assert(std::is_same_v<
    decltype(&TryInternDatabaseUser4Name), RegistryInternFunction>);
static_assert(std::is_same_v<
    decltype(&TryReAddRetainedDatabaseName), RegistryReAddFunction>);
static_assert(std::is_same_v<
    decltype(&TryReAddRetainedDatabaseNames), RegistryReAddBulkFunction>);
static_assert(std::is_same_v<
    decltype(&TryTransferDatabaseUsers4To8), RegistrySweepFunction>);
static_assert(std::is_same_v<
    decltype(&TryShutdownDatabaseUser8), RegistrySweepFunction>);

static_assert(noexcept(TryAddDatabaseUser4Reference(
    std::declval<const Admission &>(), 0)));
static_assert(noexcept(TryAddDatabaseUser4References(
    std::declval<const Admission &>(), nullptr, 0)));
static_assert(noexcept(TryInternDatabaseUser4Name(
    std::declval<const Admission &>(), nullptr, 0, 0)));
static_assert(noexcept(TryReAddRetainedDatabaseName(
    std::declval<const Admission &>(), nullptr)));
static_assert(noexcept(TryReAddRetainedDatabaseNames(
    std::declval<const Admission &>(), nullptr, 0)));
static_assert(noexcept(TryTransferDatabaseUsers4To8(
    std::declval<const Admission &>())));
static_assert(noexcept(TryShutdownDatabaseUser8(
    std::declval<const Admission &>())));
} // namespace script_string

namespace db::registry_ownership
{
// A normal production include leaves the testing macro off. Recreating the
// helper's public name must not recover friendship or any private lifecycle,
// transaction, lock-receipt, capability, or reset authority. Dependent
// requires-expressions turn access denial into positive compile contracts and
// remain valid if an implementation-private member is later renamed/removed.
struct RegistryOwnershipCoordinatorAdmissionTestAccess final
{
    template <typename Admission>
    static constexpr bool CanCallForTesting = requires
    {
        Admission::ForTesting();
    };

    template <typename Admission>
    static constexpr bool CanAuthenticate = requires
    {
        &Admission::authenticates;
    };

    template <typename Admission>
    static constexpr bool CanMutateSeal = requires
    {
        &Admission::seal_;
    };

    template <typename Admission>
    static constexpr bool CanMutateSealMirror = requires
    {
        &Admission::sealMirror_;
    };
};

using CoordinatorAdmission = RegistryOwnershipCoordinatorAdmission;
using CoordinatorFacade = RegistryOwnershipCoordinatorFacade;
static_assert(!std::is_default_constructible_v<CoordinatorFacade>);
static_assert(!std::is_copy_constructible_v<CoordinatorFacade>);
static_assert(!std::is_copy_assignable_v<CoordinatorFacade>);
static_assert(!std::is_move_constructible_v<CoordinatorFacade>);
static_assert(!std::is_move_assignable_v<CoordinatorFacade>);
static_assert(std::is_nothrow_destructible_v<CoordinatorFacade>);
static_assert(std::is_empty_v<CoordinatorFacade>);
static_assert(std::is_final_v<CoordinatorFacade>);
static_assert(
    !RegistryOwnershipCoordinatorAdmissionTestAccess::CanCallForTesting<
        CoordinatorAdmission>);
static_assert(
    !RegistryOwnershipCoordinatorAdmissionTestAccess::CanAuthenticate<
        CoordinatorAdmission>);
static_assert(!RegistryOwnershipCoordinatorAdmissionTestAccess::CanMutateSeal<
    CoordinatorAdmission>);
static_assert(
    !RegistryOwnershipCoordinatorAdmissionTestAccess::CanMutateSealMirror<
        CoordinatorAdmission>);
static_assert(!std::is_default_constructible_v<CoordinatorAdmission>);
static_assert(!std::is_constructible_v<
    CoordinatorAdmission, std::uint64_t, std::uint64_t>);
static_assert(!std::is_copy_constructible_v<CoordinatorAdmission>);
static_assert(!std::is_copy_assignable_v<CoordinatorAdmission>);
static_assert(!std::is_move_constructible_v<CoordinatorAdmission>);
static_assert(!std::is_move_assignable_v<CoordinatorAdmission>);
static_assert(std::is_nothrow_destructible_v<CoordinatorAdmission>);
static_assert(std::is_standard_layout_v<CoordinatorAdmission>);
static_assert(std::is_final_v<CoordinatorAdmission>);

struct RegistryOwnershipCoordinatorTestAccess final
{
    template <typename>
    struct DependentScalar final
    {
        template <typename Integer>
            requires std::is_integral_v<Integer>
        constexpr operator Integer() const noexcept
        {
            return Integer{0};
        }
    };

    template <typename Coordinator>
    static constexpr bool CanCallBoundarySetter = requires
    {
        SetRegistryOwnershipCoordinatorBoundaryForTesting(
            DependentScalar<Coordinator>{},
            UINT64_C(0),
            std::uintptr_t{0},
            UINT64_C(0),
            std::uint8_t{0},
            std::uint8_t{0});
    };

    template <typename Coordinator>
    static constexpr bool CanCallNextSerialSetter = requires
    {
        SetNextRegistryOwnershipCoordinatorSerialForTesting(
            DependentScalar<Coordinator>{});
    };

    template <typename Coordinator>
    static constexpr bool CanCallGlobalMirrorsSetter = requires
    {
        SetRegistryOwnershipCoordinatorGlobalMirrorsForTesting(
            DependentScalar<Coordinator>{},
            std::uintptr_t{0},
            std::uintptr_t{0},
            std::uint32_t{0},
            std::uint32_t{0},
            std::uint8_t{0},
            std::uint8_t{0},
            std::uint8_t{0},
            std::uint8_t{0},
            false,
            false);
    };

    template <typename Coordinator>
    static constexpr bool CanReachBatch = requires
    {
        &Coordinator::operationBatch_;
    };

    template <typename Coordinator>
    static constexpr bool CanReachStandaloneToken = requires
    {
        &Coordinator::standaloneTransaction_;
    };

    template <typename Coordinator>
    static constexpr bool CanCallMakeOperationAdmission = requires
    {
        &Coordinator::makeOperationAdmission;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateBorrowedControllerAddress = requires
    {
        &Coordinator::borrowedControllerAddress_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateBorrowedControllerAddressMirror = requires
    {
        &Coordinator::borrowedControllerAddressMirror_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateKey = requires
    {
        &Coordinator::borrowedKey_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateKeyMirror = requires
    {
        &Coordinator::borrowedKeyMirror_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateSerial = requires
    {
        &Coordinator::serial_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateSerialMirror = requires
    {
        &Coordinator::serialMirror_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateBorrowedSerial = requires
    {
        &Coordinator::borrowedTransactionSerial_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateBorrowedSerialMirror = requires
    {
        &Coordinator::borrowedTransactionSerialMirror_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateStandaloneSerial = requires
    {
        &Coordinator::standaloneTransactionSerial_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateStandaloneSerialMirror = requires
    {
        &Coordinator::standaloneTransactionSerialMirror_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutatePhase = requires
    {
        &Coordinator::phase_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutatePhaseMirror = requires
    {
        &Coordinator::phaseMirror_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateMode = requires
    {
        &Coordinator::mode_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateModeMirror = requires
    {
        &Coordinator::modeMirror_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateHashReceipt = requires
    {
        &Coordinator::hashLockRetained_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateHashReceiptMirror = requires
    {
        &Coordinator::hashLockRetainedMirror_;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateReserved = requires
    {
        &Coordinator::reserved_;
    };

    template <typename Coordinator>
    static constexpr bool CanCallCanonicalAfterStandaloneBegin = requires
    {
        &Coordinator::canonicalAfterStandaloneBegin;
    };

    template <typename Coordinator>
    static constexpr bool CanCallOwnsRegistryBoundary = requires
    {
        &Coordinator::ownsRegistryBoundary;
    };

    template <typename Coordinator>
    static constexpr bool CanCallRepresentationConsistent = requires
    {
        &Coordinator::representationConsistent;
    };

    template <typename Coordinator>
    static constexpr bool CanCallAuthenticatesOuterTransaction = requires
    {
        &Coordinator::authenticatesOuterTransaction;
    };

    template <typename Coordinator>
    static constexpr bool CanCallBeginRegistered = requires
    {
        &Coordinator::beginRegistered;
    };

    template <typename Coordinator>
    static constexpr bool CanCallBeginOperation = requires
    {
        &Coordinator::beginOperation;
    };

    template <typename Coordinator>
    static constexpr bool CanCallFinishOperation = requires
    {
        &Coordinator::finishOperation;
    };

    template <typename Coordinator>
    static constexpr bool CanCallPublishPhase = requires
    {
        &Coordinator::publishPhase;
    };

    template <typename Coordinator>
    static constexpr bool CanCallPublishHashLockRetained = requires
    {
        &Coordinator::publishHashLockRetained;
    };

    template <typename Coordinator>
    static constexpr bool CanCallPoisonBoundary = requires
    {
        &Coordinator::poisonBoundary;
    };

    template <typename Coordinator>
    static constexpr bool CanCallResetAfterFinish = requires
    {
        &Coordinator::resetAfterFinish;
    };
};

using Coordinator = RegistryOwnershipCoordinator;
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanCallBoundarySetter<
        Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanCallNextSerialSetter<
        Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanCallGlobalMirrorsSetter<
        Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanReachBatch<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanReachStandaloneToken<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanCallMakeOperationAdmission<
    Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanMutateBorrowedControllerAddress<
        Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::
        CanMutateBorrowedControllerAddressMirror<
        Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateKey<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateKeyMirror<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateSerial<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateSerialMirror<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateBorrowedSerial<
    Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanMutateBorrowedSerialMirror<
        Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateStandaloneSerial<
    Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanMutateStandaloneSerialMirror<
        Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutatePhase<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutatePhaseMirror<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateMode<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateModeMirror<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateHashReceipt<
    Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanMutateHashReceiptMirror<
        Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateReserved<
    Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::
        CanCallCanonicalAfterStandaloneBegin<Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanCallOwnsRegistryBoundary<
        Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanCallRepresentationConsistent<
        Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::
        CanCallAuthenticatesOuterTransaction<Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanCallBeginRegistered<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanCallBeginOperation<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanCallFinishOperation<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanCallPublishPhase<
    Coordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanCallPublishHashLockRetained<
        Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanCallPoisonBoundary<
    Coordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanCallResetAfterFinish<
    Coordinator>);

using BeginStandaloneFunction = RegistryOwnershipStatus (*)(
    const RegistryOwnershipCoordinatorAdmission &,
    RegistryOwnershipCoordinator *) noexcept;
using BorrowFunction = RegistryOwnershipStatus (*)(
    const RegistryOwnershipCoordinatorAdmission &,
    RegistryOwnershipCoordinator *,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *,
    const zone_load::ZoneLoadContextKey &) noexcept;
using FinishFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *) noexcept;
using AddUser4Function = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *, std::uint32_t) noexcept;
using AddUsers4Function = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *,
    const std::uint32_t *,
    std::uint32_t,
    RegistryOwnershipBulkResult *) noexcept;
using InternNameFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *,
    const char *,
    std::uint32_t,
    RegistryOwnershipName *) noexcept;
using RetainedNameFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *, const char *) noexcept;
using RetainedNamesFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *,
    const char *const *,
    std::uint32_t,
    RegistryOwnershipBulkResult *) noexcept;
using SweepFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *) noexcept;
using PhaseAccessor = RegistryOwnershipCoordinatorPhase (
    RegistryOwnershipCoordinator::*)() const noexcept;
using ModeAccessor = RegistryOwnershipCoordinatorMode (
    RegistryOwnershipCoordinator::*)() const noexcept;
using SerialAccessor = std::uint64_t (
    RegistryOwnershipCoordinator::*)() const noexcept;
using BoolAccessor = bool (
    RegistryOwnershipCoordinator::*)() const noexcept;

static_assert(std::is_same_v<
    decltype(&TryBeginStandaloneRegistryOwnershipCoordinator),
    BeginStandaloneFunction>);
static_assert(std::is_same_v<
    decltype(&TryBorrowRegistryOwnershipCoordinator), BorrowFunction>);
static_assert(std::is_same_v<
    decltype(&FinishRegistryOwnershipCoordinator), FinishFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryAddDatabaseUser4), AddUser4Function>);
static_assert(std::is_same_v<
    decltype(&TryRegistryAddDatabaseUsers4), AddUsers4Function>);
static_assert(std::is_same_v<
    decltype(&TryRegistryInternBoundedName), InternNameFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryReAddRetainedDefaultName), RetainedNameFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryReAddRetainedDefaultNames), RetainedNamesFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryTransferDatabaseUsers4To8), SweepFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryShutdownDatabaseUser8), SweepFunction>);
static_assert(std::is_same_v<
    decltype(&RegistryOwnershipCoordinator::phase), PhaseAccessor>);
static_assert(std::is_same_v<
    decltype(&RegistryOwnershipCoordinator::mode), ModeAccessor>);
static_assert(std::is_same_v<
    decltype(&RegistryOwnershipCoordinator::serial), SerialAccessor>);
static_assert(std::is_same_v<
    decltype(&RegistryOwnershipCoordinator::hashLockRetained), BoolAccessor>);
static_assert(std::is_same_v<
    decltype(&RegistryOwnershipCoordinator::poisoned), BoolAccessor>);
static_assert(std::is_same_v<
    decltype(&RegistryOwnershipCoordinator::isEmptyCanonical), BoolAccessor>);

static_assert(noexcept(TryBeginStandaloneRegistryOwnershipCoordinator(
    std::declval<const CoordinatorAdmission &>(), nullptr)));
static_assert(noexcept(TryBorrowRegistryOwnershipCoordinator(
    std::declval<const CoordinatorAdmission &>(),
    nullptr,
    nullptr,
    std::declval<const zone_load::ZoneLoadContextKey &>())));
static_assert(noexcept(FinishRegistryOwnershipCoordinator(nullptr)));
static_assert(noexcept(TryRegistryAddDatabaseUser4(nullptr, 0)));
static_assert(noexcept(TryRegistryAddDatabaseUsers4(
    nullptr, nullptr, 0, nullptr)));
static_assert(noexcept(TryRegistryInternBoundedName(
    nullptr, nullptr, 0, nullptr)));
static_assert(noexcept(TryRegistryReAddRetainedDefaultName(nullptr, nullptr)));
static_assert(noexcept(TryRegistryReAddRetainedDefaultNames(
    nullptr, nullptr, 0, nullptr)));
static_assert(noexcept(TryRegistryTransferDatabaseUsers4To8(nullptr)));
static_assert(noexcept(TryRegistryShutdownDatabaseUser8(nullptr)));
static_assert(noexcept(std::declval<const Coordinator &>().phase()));
static_assert(noexcept(std::declval<const Coordinator &>().mode()));
static_assert(noexcept(std::declval<const Coordinator &>().serial()));
static_assert(noexcept(
    std::declval<const Coordinator &>().hashLockRetained()));
static_assert(noexcept(std::declval<const Coordinator &>().poisoned()));
static_assert(noexcept(
    std::declval<const Coordinator &>().isEmptyCanonical()));

static_assert(std::is_default_constructible_v<Coordinator>);
static_assert(!std::is_copy_constructible_v<Coordinator>);
static_assert(!std::is_copy_assignable_v<Coordinator>);
static_assert(!std::is_move_constructible_v<Coordinator>);
static_assert(!std::is_move_assignable_v<Coordinator>);
static_assert(std::is_nothrow_destructible_v<RegistryOwnershipCoordinator>);
static_assert(std::is_standard_layout_v<Coordinator>);
static_assert(std::is_final_v<Coordinator>);
} // namespace db::registry_ownership

int main()
{
    return 0;
}
