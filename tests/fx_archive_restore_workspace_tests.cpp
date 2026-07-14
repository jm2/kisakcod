#include <EffectsCore/fx_archive_restore_workspace.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace restore = fx::archive;

namespace
{
enum class Event : std::uint8_t
{
    Allocate,
    Construct,
    Destroy,
    Free,
};

struct TestState
{
    std::array<Event, 8> events{};
    std::size_t eventCount = 0;
    void *storage = nullptr;
    void *freedStorage = nullptr;
    int requestedByteCount = -1;
    std::size_t allocateCalls = 0;
    std::size_t freeCalls = 0;
};

TestState *activeState = nullptr;

void Record(const Event event) noexcept
{
    if (activeState && activeState->eventCount < activeState->events.size())
        activeState->events[activeState->eventCount++] = event;
}

struct Workspace
{
    std::uint64_t marker = 0;

    Workspace() noexcept
        : marker(0xC0DEC0DEu)
    {
        Record(Event::Construct);
    }

    ~Workspace() noexcept
    {
        Record(Event::Destroy);
        marker = 0;
    }
};

struct ThrowingConstructor
{
    ThrowingConstructor() noexcept(false);
};

struct ThrowingDestructor
{
    ~ThrowingDestructor() noexcept(false);
};

struct alignas(alignof(std::max_align_t) * 2) OverAlignedWorkspace
{
    std::array<std::byte, alignof(std::max_align_t) * 2> storage{};
};

void *Allocate(void *const context, const int byteCount) noexcept
{
    auto *const state = static_cast<TestState *>(context);
    ++state->allocateCalls;
    state->requestedByteCount = byteCount;
    Record(Event::Allocate);
    return state->storage;
}

void Free(void *const context, void *const storage) noexcept
{
    auto *const state = static_cast<TestState *>(context);
    ++state->freeCalls;
    state->freedStorage = storage;
    Record(Event::Free);
}

restore::ArchiveRestoreWorkspaceMemoryCallbacks Callbacks(
    TestState *const state) noexcept
{
    return {state, Allocate, Free};
}

static_assert(std::is_standard_layout_v<
              restore::ArchiveRestoreWorkspaceMemoryCallbacks>);
static_assert(std::is_trivially_copyable_v<
              restore::ArchiveRestoreWorkspaceMemoryCallbacks>);
static_assert(std::is_nothrow_invocable_r_v<
              void *,
              restore::ArchiveRestoreWorkspaceAllocateCallback,
              void *,
              int>);
static_assert(std::is_nothrow_invocable_r_v<
              void,
              restore::ArchiveRestoreWorkspaceFreeCallback,
              void *,
              void *>);
static_assert(restore::SupportedArchiveRestoreWorkspace<Workspace>);
static_assert(!restore::SupportedArchiveRestoreWorkspace<Workspace[2]>);
static_assert(!restore::SupportedArchiveRestoreWorkspace<ThrowingConstructor>);
static_assert(!restore::SupportedArchiveRestoreWorkspace<ThrowingDestructor>);
static_assert(sizeof(OverAlignedWorkspace) == alignof(OverAlignedWorkspace));
static_assert(!restore::SupportedArchiveRestoreWorkspace<OverAlignedWorkspace>);
static_assert(noexcept(restore::TryNarrowArchiveRestoreWorkspaceSize(0, nullptr)));
static_assert(noexcept(
    restore::TryGetArchiveRestoreWorkspaceAllocationSize<Workspace>(nullptr)));
static_assert(noexcept(restore::AllocateArchiveRestoreWorkspace<Workspace>(
    std::declval<const restore::ArchiveRestoreWorkspaceMemoryCallbacks &>())));
static_assert(noexcept(restore::DestroyArchiveRestoreWorkspace<Workspace>(
    nullptr,
    std::declval<const restore::ArchiveRestoreWorkspaceMemoryCallbacks &>())));

int failures = 0;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void TestCheckedSizeNarrowing()
{
    int narrowed = -1;
    Expect(
        restore::TryNarrowArchiveRestoreWorkspaceSize(0, &narrowed)
            && narrowed == 0,
        "zero must be representable by the signed allocation ABI");

    const std::size_t intMaximum = static_cast<std::size_t>(
        (std::numeric_limits<int>::max)());
    narrowed = -1;
    Expect(
        restore::TryNarrowArchiveRestoreWorkspaceSize(
            intMaximum, &narrowed)
            && narrowed == (std::numeric_limits<int>::max)(),
        "INT_MAX must remain an exact successful boundary");

    narrowed = 73;
    Expect(
        !restore::TryNarrowArchiveRestoreWorkspaceSize(
            intMaximum + 1, &narrowed)
            && narrowed == 73,
        "INT_MAX plus one must fail without changing the output");
    Expect(
        !restore::TryNarrowArchiveRestoreWorkspaceSize(1, nullptr),
        "a null narrowing output must fail");

    narrowed = -1;
    Expect(
        restore::TryGetArchiveRestoreWorkspaceAllocationSize<Workspace>(
            &narrowed)
            && narrowed == static_cast<int>(sizeof(Workspace)),
        "typed allocation size must exactly narrow sizeof(Workspace)");
}

void TestRequiredCallbacks()
{
    alignas(Workspace) std::array<std::byte, sizeof(Workspace)> storage{};
    TestState state{};
    state.storage = storage.data();
    activeState = &state;

    restore::ArchiveRestoreWorkspaceMemoryCallbacks callbacks =
        Callbacks(&state);
    callbacks.allocate = nullptr;
    Expect(
        restore::AllocateArchiveRestoreWorkspace<Workspace>(callbacks)
            == nullptr,
        "a null allocate callback must be rejected");

    callbacks = Callbacks(&state);
    callbacks.free = nullptr;
    Expect(
        restore::AllocateArchiveRestoreWorkspace<Workspace>(callbacks)
            == nullptr,
        "a null free callback must be rejected before allocation");
    Expect(
        state.allocateCalls == 0 && state.freeCalls == 0
            && state.eventCount == 0,
        "missing callbacks must not allocate, construct, or free");

    activeState = nullptr;
}

void TestNullAllocation()
{
    TestState state{};
    activeState = &state;

    Expect(
        restore::AllocateArchiveRestoreWorkspace<Workspace>(Callbacks(&state))
            == nullptr,
        "a null allocation result must be reported");
    Expect(
        state.allocateCalls == 1
            && state.requestedByteCount == static_cast<int>(sizeof(Workspace))
            && state.freeCalls == 0
            && state.eventCount == 1
            && state.events[0] == Event::Allocate,
        "allocation failure must not construct or free null storage");

    activeState = nullptr;
}

void TestMisalignedStorageIsReleasedWithoutConstruction()
{
    alignas(Workspace) std::array<
        std::byte,
        sizeof(Workspace) + alignof(Workspace)> raw{};
    TestState state{};
    state.storage = raw.data() + 1;
    activeState = &state;

    Expect(
        reinterpret_cast<std::uintptr_t>(state.storage) % alignof(Workspace)
            != 0,
        "test storage must actually be misaligned");
    Expect(
        restore::AllocateArchiveRestoreWorkspace<Workspace>(Callbacks(&state))
            == nullptr,
        "misaligned storage must be rejected");
    Expect(
        state.allocateCalls == 1 && state.freeCalls == 1
            && state.freedStorage == state.storage,
        "rejected storage must be returned through the matching callback");
    Expect(
        state.eventCount == 2
            && state.events[0] == Event::Allocate
            && state.events[1] == Event::Free,
        "misaligned storage must be freed without construction");

    activeState = nullptr;
}

void TestConstructionAndDestructionOrder()
{
    alignas(Workspace) std::array<std::byte, sizeof(Workspace)> storage{};
    TestState state{};
    state.storage = storage.data();
    activeState = &state;
    const auto callbacks = Callbacks(&state);

    Workspace *const workspace =
        restore::AllocateArchiveRestoreWorkspace<Workspace>(callbacks);
    Expect(workspace != nullptr, "aligned storage must construct a workspace");
    Expect(
        workspace && workspace->marker == 0xC0DEC0DEu,
        "the workspace default constructor must run");
    Expect(
        state.eventCount == 2
            && state.events[0] == Event::Allocate
            && state.events[1] == Event::Construct,
        "allocation must precede construction");

    Expect(
        restore::DestroyArchiveRestoreWorkspace(workspace, callbacks),
        "a live workspace with a free callback must finalize");
    Expect(
        state.freeCalls == 1 && state.freedStorage == storage.data(),
        "finalization must free the original storage address");
    Expect(
        state.eventCount == 4
            && state.events[2] == Event::Destroy
            && state.events[3] == Event::Free,
        "destruction must precede storage release");

    activeState = nullptr;
}

void TestMissingFreePreservesLiveWorkspace()
{
    alignas(Workspace) std::array<std::byte, sizeof(Workspace)> storage{};
    TestState state{};
    activeState = &state;
    Workspace *const workspace = ::new (storage.data()) Workspace();

    auto callbacks = Callbacks(&state);
    callbacks.free = nullptr;
    Expect(
        !restore::DestroyArchiveRestoreWorkspace(workspace, callbacks),
        "finalization must report a missing free callback");
    Expect(
        state.eventCount == 1 && state.events[0] == Event::Construct,
        "a missing free callback must leave the workspace alive");

    callbacks.free = Free;
    Expect(
        restore::DestroyArchiveRestoreWorkspace(workspace, callbacks),
        "the preserved workspace must support a later valid finalization");
    Expect(
        state.eventCount == 3
            && state.events[1] == Event::Destroy
            && state.events[2] == Event::Free,
        "retry finalization must destroy and then free");

    activeState = nullptr;
}

void TestNullWorkspaceFinalization()
{
    const restore::ArchiveRestoreWorkspaceMemoryCallbacks callbacks{};
    Expect(
        restore::DestroyArchiveRestoreWorkspace<Workspace>(nullptr, callbacks),
        "finalizing a null workspace must be an idempotent success");
}
} // namespace

int main()
{
    TestCheckedSizeNarrowing();
    TestRequiredCallbacks();
    TestNullAllocation();
    TestMisalignedStorageIsReleasedWithoutConstruction();
    TestConstructionAndDestructionOrder();
    TestMissingFreePreservesLiveWorkspace();
    TestNullWorkspaceFinalization();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d restore workspace test(s) failed\n", failures);
        return 1;
    }

    std::puts("FX archive restore workspace tests passed");
    return 0;
}
