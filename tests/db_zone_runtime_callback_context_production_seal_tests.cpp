#include <database/db_zone_runtime_callback_context.h>

#include <cstdint>
#include <type_traits>

namespace db::zone_runtime
{
static_assert(sizeof(ZoneRuntimeCallbackContextPhase) == 1u);
static_assert(std::is_same_v<
    std::underlying_type_t<ZoneRuntimeCallbackContextPhase>,
    std::uint8_t>);
static_assert(
    static_cast<std::uint8_t>(ZoneRuntimeCallbackContextPhase::Reserved) == 0u);
static_assert(
    static_cast<std::uint8_t>(ZoneRuntimeCallbackContextPhase::Unbound) == 1u);
static_assert(
    static_cast<std::uint8_t>(ZoneRuntimeCallbackContextPhase::Bound) == 2u);
static_assert(
    static_cast<std::uint8_t>(ZoneRuntimeCallbackContextPhase::Terminal) == 3u);

static_assert(sizeof(ZoneRuntimeCallbackContextStatus) == 1u);
static_assert(std::is_same_v<
    std::underlying_type_t<ZoneRuntimeCallbackContextStatus>,
    std::uint8_t>);
static_assert(static_cast<std::uint8_t>(
    ZoneRuntimeCallbackContextStatus::UnsafeFailure) == 8u);

static_assert(sizeof(ZoneRuntimeCallbackContext) == 0x28u);
static_assert(alignof(ZoneRuntimeCallbackContext) == 0x8u);
static_assert(std::is_standard_layout_v<ZoneRuntimeCallbackContext>);
static_assert(std::is_trivially_destructible_v<ZoneRuntimeCallbackContext>);
static_assert(!std::is_copy_constructible_v<ZoneRuntimeCallbackContext>);
static_assert(!std::is_copy_assignable_v<ZoneRuntimeCallbackContext>);
static_assert(!std::is_move_constructible_v<ZoneRuntimeCallbackContext>);
static_assert(!std::is_move_assignable_v<ZoneRuntimeCallbackContext>);

static_assert(sizeof(ZoneRuntimeCallbackContextBindResult)
    == (sizeof(void *) == 4 ? 0x8u : 0x10u));
static_assert(alignof(ZoneRuntimeCallbackContextBindResult)
    == alignof(const ZoneRuntimeCallbackContext *));
static_assert(std::is_standard_layout_v<
    ZoneRuntimeCallbackContextBindResult>);
static_assert(std::is_trivially_copyable_v<
    ZoneRuntimeCallbackContextBindResult>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimeCallbackContextBindResult::context),
    const ZoneRuntimeCallbackContext *>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimeCallbackContextBindResult::status),
    ZoneRuntimeCallbackContextStatus>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimeCallbackContextBindResult::reserved),
    std::uint8_t[3]>);

static_assert(sizeof(ZoneRuntimeCallbackContextSnapshot) == 0x18u);
static_assert(alignof(ZoneRuntimeCallbackContextSnapshot) == 0x8u);
static_assert(std::is_standard_layout_v<
    ZoneRuntimeCallbackContextSnapshot>);
static_assert(std::is_trivially_copyable_v<
    ZoneRuntimeCallbackContextSnapshot>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimeCallbackContextSnapshot::key),
    zone_load::ZoneLoadContextKey>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimeCallbackContextSnapshot::physicalSlot),
    std::uint32_t>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimeCallbackContextSnapshot::phase),
    ZoneRuntimeCallbackContextPhase>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimeCallbackContextSnapshot::status),
    ZoneRuntimeCallbackContextStatus>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimeCallbackContextSnapshot::reserved),
    std::uint8_t[2]>);

constexpr zone_load::ZoneLoadContextKey kSealKey{1u, 1u, 0u};
constexpr ZoneRuntimeCallbackContextBindResult kDefaultBind{};
static_assert(kDefaultBind.context == nullptr);
static_assert(kDefaultBind.status
    == ZoneRuntimeCallbackContextStatus::InvalidArgument);
static_assert(!static_cast<bool>(kDefaultBind));
constexpr ZoneRuntimeCallbackContextSnapshot kDefaultSnapshot{};
static_assert(kDefaultSnapshot.key == zone_load::ZoneLoadContextKey{});
static_assert(
    kDefaultSnapshot.physicalSlot == zone_load::kInvalidZoneLoadSlot);
static_assert(kDefaultSnapshot.phase
    == ZoneRuntimeCallbackContextPhase::Unbound);
static_assert(kDefaultSnapshot.status
    == ZoneRuntimeCallbackContextStatus::InvalidArgument);
static_assert(!static_cast<bool>(kDefaultSnapshot));
constexpr ZoneRuntimeCallbackContextSnapshot kBoundSnapshot{
    kSealKey,
    1u,
    ZoneRuntimeCallbackContextPhase::Bound,
    ZoneRuntimeCallbackContextStatus::Success,
    {0, 0}};
static_assert(static_cast<bool>(kBoundSnapshot));
constexpr ZoneRuntimeCallbackContextSnapshot kTerminalSnapshot{
    kSealKey,
    1u,
    ZoneRuntimeCallbackContextPhase::Terminal,
    ZoneRuntimeCallbackContextStatus::Success,
    {0, 0}};
static_assert(static_cast<bool>(kTerminalSnapshot));
constexpr ZoneRuntimeCallbackContextSnapshot kUnboundSnapshot{
    kSealKey,
    1u,
    ZoneRuntimeCallbackContextPhase::Unbound,
    ZoneRuntimeCallbackContextStatus::Success,
    {0, 0}};
static_assert(!static_cast<bool>(kUnboundSnapshot));

// Production includes do not opt in to TestAccess. Recreating its public name
// must confer access to neither the context representation nor owner methods.
struct ZoneRuntimeCallbackContextTestAccess
{
    template <typename Context>
    static constexpr bool CanCallCanonical = requires(
        const Context *const context)
    {
        context->canonical(1u);
    };

    template <typename Context>
    static constexpr bool CanCallInitialize = requires(Context *const context)
    {
        context->initialize(1u);
    };

    template <typename Context>
    static constexpr bool CanCallBind = requires(
        Context *const context,
        const zone_load::ZoneLoadContextKey &key)
    {
        context->bind(key);
    };

    template <typename Context>
    static constexpr bool CanCallSetPhase = requires(Context *const context)
    {
        context->setPhase(ZoneRuntimeCallbackContextPhase::Terminal);
    };

    template <typename Context>
    static constexpr bool CanCallRefreshWitness = requires(Context *const context)
    {
        context->refreshWitness();
    };

    template <typename Context>
    static constexpr bool CanReadKey = requires(Context *const context)
    {
        &context->key_;
    };

    template <typename Context>
    static constexpr bool CanMutateSelf = requires(Context *const context)
    {
        context->self_ = nullptr;
    };

    template <typename Context>
    static constexpr bool CanMutatePointerAlignmentPadding = requires(
        Context *const context)
    {
        context->pointerAlignmentPadding_[0] = 1u;
    };

    template <typename Context>
    static constexpr bool CanMutateWitness = requires(Context *const context)
    {
        context->witness_ = 0;
    };

    template <typename Context>
    static constexpr bool CanMutateSlot = requires(Context *const context)
    {
        context->physicalSlot_ = 1u;
    };

    template <typename Context>
    static constexpr bool CanMutatePhase = requires(Context *const context)
    {
        context->phase_ = ZoneRuntimeCallbackContextPhase::Terminal;
    };

    template <typename Context>
    static constexpr bool CanMutateReserved = requires(Context *const context)
    {
        context->reserved_[0] = 1u;
    };

    template <typename Owner>
    static constexpr bool CanClassify = requires(
        const ZoneRuntimeCallbackContext *const context)
    {
        Owner::TryClassifyStorage(context);
    };

    template <typename Owner>
    static constexpr bool CanOwnerBind = requires(
        const zone_load::ZoneLoadContextKey &key)
    {
        Owner::TryBind(1u, key);
    };

    template <typename Owner>
    static constexpr bool CanResolve = requires(
        const zone_load::ZoneLoadContextKey &key)
    {
        Owner::TryResolve(1u, key);
    };

    template <typename Owner>
    static constexpr bool CanAdvance = requires(
        const ZoneRuntimeCallbackContext *const context,
        const zone_load::ZoneLoadContextKey &key)
    {
        Owner::TryAdvance(
            context, key, ZoneRuntimeCallbackContextPhase::Terminal);
    };

    template <typename Owner>
    static constexpr bool CanAuthenticate = requires(
        const ZoneRuntimeCallbackContext *const context,
        const zone_load::ZoneLoadContextKey &key)
    {
        Owner::TryAuthenticate(
            context, key, ZoneRuntimeCallbackContextPhase::Bound);
    };

    template <typename Owner>
    static constexpr bool CanAuthenticateStructural =
        requires(const ZoneRuntimeCallbackContext *const context,
                 const zone_load::ZoneLoadContextKey &key)
    {
        Owner::TryAuthenticateStructural(
            context, key, ZoneRuntimeCallbackContextPhase::Bound);
    };

    template <typename Owner>
    static constexpr bool CanAuthenticateUnused =
        requires { Owner::TryAuthenticateUnused(1u); };

    template <typename Owner>
    static constexpr bool CanAuthenticateStore =
        requires { Owner::TryAuthenticateStore(); };

    template <typename Owner>
    static constexpr bool CanCapture = requires(
        const ZoneRuntimeCallbackContext *const context,
        const zone_load::ZoneLoadContextKey &key)
    {
        Owner::TryCapture(context, key);
    };

    template <typename Owner>
    static constexpr bool CanSeparateSpan = requires(const void *storage)
    {
        Owner::SpanIsSeparated(storage, 1u, 1u);
    };
};

static_assert(!ZoneRuntimeCallbackContextTestAccess::CanCallCanonical<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanCallInitialize<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanCallBind<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanCallSetPhase<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanCallRefreshWitness<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanReadKey<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanMutateSelf<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanMutatePointerAlignmentPadding<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanMutateWitness<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanMutateSlot<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanMutatePhase<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanMutateReserved<
    ZoneRuntimeCallbackContext>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanClassify<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanOwnerBind<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanResolve<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanAdvance<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanAuthenticate<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanAuthenticateStructural<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanAuthenticateUnused<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanAuthenticateStore<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanCapture<
    ZoneRuntimeCallbackContextOwner>);
static_assert(!ZoneRuntimeCallbackContextTestAccess::CanSeparateSpan<
    ZoneRuntimeCallbackContextOwner>);
} // namespace db::zone_runtime

int main()
{
    return 0;
}
