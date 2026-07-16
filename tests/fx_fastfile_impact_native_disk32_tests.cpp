#include <EffectsCore/fx_fastfile_impact_native_disk32.h>

#include <EffectsCore/fx_runtime.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
namespace fastfile = fx::fastfile;
using Status = fastfile::FxFastFileNativeDisk32Status;

constexpr std::size_t kHandleCount =
    fastfile::kFxFastFileImpactDisk32HandleCount;
constexpr std::size_t kJournalCount =
    fastfile::kFxFastFileImpactDisk32JournalCount;
constexpr std::uint8_t kGuard = UINT8_C(0xA5);

int failures = 0;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

enum class MutationKind : std::uint8_t
{
    None,
    Header,
    Entry,
    Name,
};

struct ProvenanceState final
{
    fastfile::FxImpactTableDisk32 *header = nullptr;
    fastfile::FxImpactEntryDisk32 *entries = nullptr;
    char *name = nullptr;
    std::uint32_t nameBytes = 0;
    std::size_t calls = 0;
    bool failEnabled = false;
    fastfile::FxFastFileDisk32SourceSpanKind failKind =
        fastfile::FxFastFileDisk32SourceSpanKind::ImpactTableHeader;
    bool mutateEnabled = false;
    fastfile::FxFastFileDisk32SourceSpanKind mutateKind =
        fastfile::FxFastFileDisk32SourceSpanKind::ImpactTableHeader;
    MutationKind mutation = MutationKind::None;
    fastfile::FxFastFileDisk32Resolvers *resolverDescriptor = nullptr;
    fastfile::FxFastFileDisk32ResolvedReference *retainedReference = nullptr;
    bool mutateResolverDescriptor = false;
    bool mutateReturnedDescriptor = false;
    const void *returnedDescriptorPointer = nullptr;
    std::uint64_t returnedDescriptorBytes = 0;
    std::uint64_t returnedDescriptorAlignment = 0;
};

struct ResolverObservation final
{
    fastfile::FxFastFileDisk32ReferenceKind kind =
        fastfile::FxFastFileDisk32ReferenceKind::EffectName;
    const disk32::PointerToken *sourceField = nullptr;
    std::uint32_t token = 0;
    const void *result = nullptr;
};

struct ResolverState final
{
    fastfile::FxImpactTableDisk32 *header = nullptr;
    fastfile::FxImpactEntryDisk32 *entries = nullptr;
    char *name = nullptr;
    std::uint32_t nameBytes = 0;
    std::array<ResolverObservation, kJournalCount> observations{};
    std::size_t calls = 0;
    std::size_t failAt = (std::numeric_limits<std::size_t>::max)();
    std::size_t nullAt = (std::numeric_limits<std::size_t>::max)();
    std::size_t zeroStringBytesAt = (std::numeric_limits<std::size_t>::max)();
    std::size_t invalidAssetExtentAt =
        (std::numeric_limits<std::size_t>::max)();
    std::size_t reenterAt = (std::numeric_limits<std::size_t>::max)();
    std::size_t mutateAt = (std::numeric_limits<std::size_t>::max)();
    MutationKind mutation = MutationKind::None;
    bool futureTokenRestoreEvasion = false;
    bool reenterPlanWithInvalidArguments = false;
    bool reenterMaterializeWithInvalidArguments = false;
    const void *forcedAssetIdentity = nullptr;
    bool assetResultUsesOutReference = false;
    bool mutateRetainedNameDescriptor = false;
    bool retainedNameDescriptorMutated = false;
    std::size_t retainedDescriptorOverrideAt =
        (std::numeric_limits<std::size_t>::max)();
    std::uint64_t retainedByteCountOverride = 0;
    std::uint64_t retainedAlignmentOverride = 0;
    alignas(FxEffectDef) std::array<std::uint8_t, sizeof(FxEffectDef)>
        assetStorage{};
    ProvenanceState *provenance = nullptr;
    fastfile::FxFastFileImpactNativeDisk32Workspace *workspace = nullptr;
    const fastfile::FxFastFileImpactTableDisk32View *source = nullptr;
    const fastfile::FxFastFileDisk32Resolvers *resolvers = nullptr;
    bool reentered = false;
    Status nestedStatus = Status::Success;
    fastfile::FxFastFileImpactNativeDisk32Plan nestedPlan{};
};

void MutateSource(const MutationKind mutation,
                  fastfile::FxImpactTableDisk32 *const header,
                  fastfile::FxImpactEntryDisk32 *const entries,
                  char *const name) noexcept
{
    switch (mutation)
    {
    case MutationKind::Header:
        if (header)
            header->table.token.value ^= UINT32_C(0x00000001);
        break;
    case MutationKind::Entry:
        if (entries)
            entries[0].nonflesh[0].token.value ^= UINT32_C(0x00000010);
        break;
    case MutationKind::Name:
        if (name)
            name[0] = name[0] == 'i' ? 'I' : 'i';
        break;
    case MutationKind::None:
        break;
    }
}

bool ValidateSourceSpan(void *const context,
                        const fastfile::FxFastFileDisk32SourceSpanKind kind,
                        const disk32::PointerToken *const sourceField,
                        const disk32::PointerToken token,
                        const void *const address,
                        const std::uint64_t byteCount,
                        const std::size_t alignment) noexcept
{
    auto *const state = static_cast<ProvenanceState *>(context);
    if (!state || !state->header || !state->entries || !state->name)
        return false;
    ++state->calls;
    if (state->failEnabled && state->failKind == kind)
        return false;

    bool valid = false;
    switch (kind)
    {
    case fastfile::FxFastFileDisk32SourceSpanKind::ImpactTableHeader:
        valid = !sourceField && token.isNull() && address == state->header &&
                byteCount == sizeof(*state->header) &&
                alignment == alignof(fastfile::FxImpactTableDisk32);
        break;
    case fastfile::FxFastFileDisk32SourceSpanKind::ImpactEntries:
        valid = sourceField == &state->header->table.token &&
                sourceField->value == token.value &&
                address == state->entries &&
                byteCount == sizeof(fastfile::FxImpactEntryDisk32) *
                                 fastfile::kImpactSurfaceCount &&
                alignment == alignof(fastfile::FxImpactEntryDisk32);
        break;
    case fastfile::FxFastFileDisk32SourceSpanKind::String:
        valid = sourceField == &state->header->name.token &&
                sourceField->value == token.value && address == state->name &&
                byteCount == state->nameBytes && alignment == alignof(char);
        break;
    default:
        valid = false;
        break;
    }
    if (valid && state->mutateEnabled && state->mutateKind == kind)
    {
        MutateSource(
            state->mutation, state->header, state->entries, state->name);
    }
    if (valid && state->mutateResolverDescriptor && state->resolverDescriptor)
    {
        state->resolverDescriptor->context = nullptr;
    }
    if (valid && kind == fastfile::FxFastFileDisk32SourceSpanKind::String &&
        state->mutateReturnedDescriptor && state->retainedReference)
    {
        state->retainedReference->pointer = state->returnedDescriptorPointer;
        state->retainedReference->retainedByteCount =
            state->returnedDescriptorBytes;
        state->retainedReference->retainedAlignment =
            state->returnedDescriptorAlignment;
    }
    return valid;
}

bool ResolveReference(
    void *const context,
    const fastfile::FxFastFileDisk32ReferenceKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    fastfile::FxFastFileDisk32ResolvedReference *const outReference) noexcept
{
    auto *const state = static_cast<ResolverState *>(context);
    if (!state || !sourceField || !outReference ||
        sourceField->value != token.value)
    {
        return false;
    }

    const std::size_t callIndex = state->calls++;
    if (state->futureTokenRestoreEvasion && state->entries &&
        (callIndex == 1u || callIndex == 2u))
    {
        state->entries[0].nonflesh[1].token.value ^= UINT32_C(0x00000020);
    }
    if (callIndex == state->reenterAt && !state->reentered)
    {
        state->reentered = true;
        if (!state->workspace || !state->source || !state->resolvers)
            return false;
        if (state->reenterPlanWithInvalidArguments)
        {
            state->nestedStatus = fastfile::TryPlanFxImpactTableDisk32(
                state->workspace, *state->source, *state->resolvers, nullptr);
        }
        else if (state->reenterMaterializeWithInvalidArguments)
        {
            state->nestedStatus = fastfile::TryMaterializeFxImpactTableDisk32(
                state->workspace, state->nestedPlan, nullptr, 0, nullptr);
        }
        else
        {
            state->nestedStatus =
                fastfile::TryPlanFxImpactTableDisk32(state->workspace,
                                                     *state->source,
                                                     *state->resolvers,
                                                     &state->nestedPlan);
        }
    }
    if (callIndex == state->mutateAt)
    {
        MutateSource(
            state->mutation, state->header, state->entries, state->name);
    }
    if (callIndex == 1u && state->mutateRetainedNameDescriptor &&
        state->provenance && state->provenance->retainedReference)
    {
        state->provenance->retainedReference->pointer =
            reinterpret_cast<const void *>(static_cast<std::uintptr_t>(1u));
        state->provenance->retainedReference->retainedByteCount =
            (std::numeric_limits<std::uint64_t>::max)();
        state->provenance->retainedReference->retainedAlignment =
            alignof(char);
        state->retainedNameDescriptorMutated = true;
    }
    if (callIndex == state->failAt)
        return false;

    fastfile::FxFastFileDisk32ResolvedReference resolved{};
    if (kind == fastfile::FxFastFileDisk32ReferenceKind::EffectName)
    {
        resolved.pointer = state->name;
        resolved.retainedByteCount = state->nameBytes;
        resolved.retainedAlignment = alignof(char);
    }
    else if (kind == fastfile::FxFastFileDisk32ReferenceKind::EffectAssetHandle)
    {
        resolved.pointer = state->assetResultUsesOutReference
            ? outReference
            : state->forcedAssetIdentity
                ? state->forcedAssetIdentity
                : state->assetStorage.data();
        resolved.retainedByteCount = sizeof(FxEffectDef);
        resolved.retainedAlignment = alignof(FxEffectDef);
    }
    else
    {
        return false;
    }

    if (callIndex == state->nullAt)
        resolved.pointer = nullptr;
    if (callIndex == state->zeroStringBytesAt)
        resolved.retainedByteCount = 0;
    if (callIndex == state->invalidAssetExtentAt)
        resolved.retainedByteCount = 0;
    if (callIndex == state->retainedDescriptorOverrideAt)
    {
        resolved.retainedByteCount = state->retainedByteCountOverride;
        resolved.retainedAlignment = state->retainedAlignmentOverride;
    }
    if (callIndex < state->observations.size())
    {
        state->observations[callIndex] = {
            kind,
            sourceField,
            token.value,
            resolved.pointer,
        };
    }
    *outReference = resolved;
    if (kind == fastfile::FxFastFileDisk32ReferenceKind::EffectName &&
        state->provenance)
    {
        state->provenance->retainedReference = outReference;
    }
    return true;
}

struct alignas(8) Fixture final
{
    alignas(8) fastfile::FxImpactTableDisk32 header{};
    alignas(8) std::array<fastfile::FxImpactEntryDisk32,
                          fastfile::kImpactSurfaceCount> entries{};
    alignas(8) std::array<char, 64> name{};
    ProvenanceState provenance{};
    ResolverState resolver{};
    fastfile::FxFastFileImpactTableDisk32View view{};
    fastfile::FxFastFileDisk32Resolvers resolvers{};

    Fixture()
    {
        header.name.token.value = UINT32_C(0x40000001);
        header.table.token.value = disk32::kInline;
        SetName("impact/native_disk32_test");
        SetAllHandles(UINT32_C(0x12345678));
        RefreshBindings();
    }

    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;

    void SetName(const std::string_view value)
    {
        CHECK(value.size() + 1u <= name.size());
        name.fill('\0');
        if (value.size() + 1u > name.size())
            return;
        std::memcpy(name.data(), value.data(), value.size());
        name[value.size()] = '\0';
        provenance.nameBytes = static_cast<std::uint32_t>(value.size() + 1u);
        resolver.nameBytes = provenance.nameBytes;
    }

    fastfile::FxEffectDefHandleDisk32 &Handle(const std::size_t flatIndex)
    {
        CHECK(flatIndex < kHandleCount);
        const std::size_t perEntry = fastfile::kImpactNonFleshEffectCount +
                                     fastfile::kImpactFleshEffectCount;
        const std::size_t entryIndex = flatIndex / perEntry;
        const std::size_t effectIndex = flatIndex % perEntry;
        if (effectIndex < fastfile::kImpactNonFleshEffectCount)
            return entries[entryIndex].nonflesh[effectIndex];
        return entries[entryIndex]
            .flesh[effectIndex - fastfile::kImpactNonFleshEffectCount];
    }

    void SetAllHandles(const std::uint32_t token)
    {
        for (std::size_t index = 0; index < kHandleCount; ++index)
            Handle(index).token.value = token;
    }

    void RefreshBindings()
    {
        provenance.header = &header;
        provenance.entries = entries.data();
        provenance.name = name.data();
        resolver.header = &header;
        resolver.entries = entries.data();
        resolver.name = name.data();
        resolver.provenance = &provenance;
        view = {
            &header,
            {entries.data(), static_cast<std::uint32_t>(entries.size())},
            {&provenance, ValidateSourceSpan},
        };
        resolvers = {&resolver, ResolveReference};
        provenance.resolverDescriptor = &resolvers;
        resolver.source = &view;
        resolver.resolvers = &resolvers;
    }
};

class AlignedStorage final
{
  public:
    AlignedStorage(const std::size_t bytes, const std::size_t alignment)
        : raw_(bytes + alignment + 2u, kGuard), bytes_(bytes)
    {
        CHECK(alignment && (alignment & (alignment - 1u)) == 0);
        const std::uintptr_t raw =
            reinterpret_cast<std::uintptr_t>(raw_.data());
        const std::size_t offset =
            static_cast<std::size_t>(-raw) & (alignment - 1u);
        aligned_ = raw_.data() + offset;
        CHECK(reinterpret_cast<std::uintptr_t>(aligned_) % alignment == 0);
        CHECK(offset + bytes <= raw_.size());
    }

    void *data()
    {
        return aligned_;
    }

    std::uint8_t *bytes()
    {
        return aligned_;
    }

    std::size_t size() const
    {
        return bytes_;
    }

    std::vector<std::uint8_t> Snapshot() const
    {
        return {aligned_, aligned_ + bytes_};
    }

  private:
    std::vector<std::uint8_t> raw_{};
    std::uint8_t *aligned_ = nullptr;
    std::size_t bytes_ = 0;
};

const void *FindResolved(const ResolverState &resolver,
                         const disk32::PointerToken *const sourceField)
{
    for (std::size_t index = 0; index < resolver.calls; ++index)
    {
        if (resolver.observations[index].sourceField == sourceField)
            return resolver.observations[index].result;
    }
    return nullptr;
}

const FxEffectDef *NativeHandle(const FxImpactTable &table,
                                const std::size_t flatIndex)
{
    const std::size_t perEntry = fastfile::kImpactNonFleshEffectCount +
                                 fastfile::kImpactFleshEffectCount;
    const std::size_t entryIndex = flatIndex / perEntry;
    const std::size_t effectIndex = flatIndex % perEntry;
    if (effectIndex < fastfile::kImpactNonFleshEffectCount)
        return table.table[entryIndex].nonflesh[effectIndex];
    return table.table[entryIndex]
        .flesh[effectIndex - fastfile::kImpactNonFleshEffectCount];
}

Status Plan(Fixture *const fixture,
            fastfile::FxFastFileImpactNativeDisk32Workspace *const workspace,
            fastfile::FxFastFileImpactNativeDisk32Plan *const plan)
{
    CHECK(fixture != nullptr);
    CHECK(workspace != nullptr);
    CHECK(plan != nullptr);
    if (!fixture || !workspace || !plan)
        return Status::InvalidArgument;
    fixture->resolver.workspace = workspace;
    return fastfile::TryPlanFxImpactTableDisk32(
        workspace, fixture->view, fixture->resolvers, plan);
}

std::size_t ExpectedEntriesOffset()
{
    const std::size_t alignment = alignof(FxImpactEntry);
    return (sizeof(FxImpactTable) + alignment - 1u) & ~(alignment - 1u);
}

void TestHappyPathAndFullWidthIdentities()
{
    Fixture fixture{};
    fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
    fastfile::FxFastFileImpactNativeDisk32Plan plan{};
    CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
    CHECK(workspace.phase() == fastfile::FxFastFileNativeDisk32Phase::Planned);
    CHECK(static_cast<bool>(plan));
    CHECK(plan.entryCount() == fastfile::kImpactSurfaceCount);
    CHECK(plan.resolvedReferenceCount() == kJournalCount);
    CHECK(fixture.provenance.calls == 3u);
    CHECK(fixture.resolver.calls == kJournalCount);
    CHECK(plan.outputAlignment() ==
          (std::max)(alignof(FxImpactTable), alignof(FxImpactEntry)));

    const std::size_t entriesOffset = ExpectedEntriesOffset();
    const std::size_t nameOffset =
        entriesOffset + sizeof(FxImpactEntry) * fastfile::kImpactSurfaceCount;
    CHECK(plan.outputBytes() == nameOffset + fixture.resolver.nameBytes);
    for (std::size_t call = 1; call < fixture.resolver.calls; ++call)
    {
        CHECK(fixture.resolver.observations[call].kind ==
              fastfile::FxFastFileDisk32ReferenceKind::EffectAssetHandle);
        CHECK(fixture.resolver.observations[call].token ==
              UINT32_C(0x12345678));
        for (std::size_t prior = 1; prior < call; ++prior)
        {
            CHECK(fixture.resolver.observations[call].sourceField !=
                  fixture.resolver.observations[prior].sourceField);
        }
    }

    AlignedStorage storage(plan.outputBytes(), plan.outputAlignment());
    FxImpactTable *output = reinterpret_cast<FxImpactTable *>(
        static_cast<std::uintptr_t>(UINT32_C(0x1234)));
    const std::size_t resolverCalls = fixture.resolver.calls;
    const std::size_t provenanceCalls = fixture.provenance.calls;
    CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
              &workspace, plan, storage.data(), storage.size(), &output) ==
          Status::Success);
    CHECK(output == storage.data());
    CHECK(output->table ==
          reinterpret_cast<FxImpactEntry *>(storage.bytes() + entriesOffset));
    CHECK(output->name ==
          reinterpret_cast<const char *>(storage.bytes() + nameOffset));
    CHECK(output->name != fixture.name.data());
    CHECK(std::strcmp(output->name, fixture.name.data()) == 0);
    CHECK(fixture.resolver.calls == resolverCalls);
    CHECK(fixture.provenance.calls == provenanceCalls);
    CHECK(workspace.phase() == fastfile::FxFastFileNativeDisk32Phase::Empty);

    for (std::size_t flat = 0; flat < kHandleCount; ++flat)
    {
        const disk32::PointerToken *const field = &fixture.Handle(flat).token;
        const void *const expected = FindResolved(fixture.resolver, field);
        CHECK(NativeHandle(*output, flat) == expected);
        if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t))
        {
            CHECK(reinterpret_cast<std::uintptr_t>(expected) >
                  (std::numeric_limits<std::uint32_t>::max)());
        }
    }

    fixture.name[0] = 'X';
    CHECK(output->name[0] == 'i');
    FxImpactTable *second = output;
    const auto before = storage.Snapshot();
    CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
              &workspace, plan, storage.data(), storage.size(), &second) ==
          Status::InvalidPhase);
    CHECK(second == output);
    CHECK(storage.Snapshot() == before);
}

void TestNullHandlesBypassResolver()
{
    Fixture fixture{};
    fixture.SetAllHandles(0);
    fixture.Handle(0).token.value = disk32::kInline;
    fixture.Handle(kHandleCount - 1u).token.value = disk32::kSharedInline;

    fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
    fastfile::FxFastFileImpactNativeDisk32Plan plan{};
    CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
    CHECK(fixture.resolver.calls == 3u);
    CHECK(plan.resolvedReferenceCount() == 3u);
    CHECK(fixture.resolver.observations[1].sourceField ==
          &fixture.Handle(0).token);
    CHECK(fixture.resolver.observations[2].sourceField ==
          &fixture.Handle(kHandleCount - 1u).token);

    AlignedStorage storage(plan.outputBytes(), plan.outputAlignment());
    FxImpactTable *output = nullptr;
    CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
              &workspace, plan, storage.data(), storage.size(), &output) ==
          Status::Success);
    for (std::size_t flat = 0; flat < kHandleCount; ++flat)
    {
        const void *const expected =
            fixture.Handle(flat).token.isNull()
                ? nullptr
                : FindResolved(fixture.resolver, &fixture.Handle(flat).token);
        CHECK(NativeHandle(*output, flat) == expected);
    }
}

template <typename MUTATOR>
void ExpectPlanFailure(const Status expected, MUTATOR mutate)
{
    Fixture fixture{};
    fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
    fastfile::FxFastFileImpactNativeDisk32Plan plan{};
    mutate(fixture, workspace, plan);
    fixture.resolver.workspace = &workspace;
    const Status status = fastfile::TryPlanFxImpactTableDisk32(
        &workspace, fixture.view, fixture.resolvers, &plan);
    CHECK(status == expected);
    CHECK(workspace.phase() == fastfile::FxFastFileNativeDisk32Phase::Empty);
    CHECK(!static_cast<bool>(plan));
}

void TestPlanStructuralAndCallbackFailures()
{
    Fixture fixture{};
    fastfile::FxFastFileImpactNativeDisk32Plan plan{};
    CHECK(fastfile::TryPlanFxImpactTableDisk32(
              nullptr, fixture.view, fixture.resolvers, &plan) ==
          Status::InvalidArgument);
    fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
    CHECK(fastfile::TryPlanFxImpactTableDisk32(
              &workspace, fixture.view, fixture.resolvers, nullptr) ==
          Status::InvalidArgument);

    ExpectPlanFailure(Status::InvalidArgument,
                      [](auto &f, auto &, auto &)
                      { f.resolvers.resolve = nullptr; });
    ExpectPlanFailure(Status::InvalidArgument,
                      [](auto &f, auto &, auto &)
                      { f.view.provenance.validateSpan = nullptr; });
    ExpectPlanFailure(Status::InvalidCount,
                      [](auto &f, auto &, auto &)
                      { f.view.entries.count = 11; });
    ExpectPlanFailure(Status::InvalidPointerCount,
                      [](auto &f, auto &, auto &)
                      { f.view.entries.data = nullptr; });
    ExpectPlanFailure(Status::InvalidPointerCount,
                      [](auto &f, auto &, auto &)
                      { f.header.table.token.value = 0; });
    ExpectPlanFailure(Status::InvalidString,
                      [](auto &f, auto &, auto &)
                      { f.header.name.token.value = 0; });
    ExpectPlanFailure(
        Status::InvalidSourceLayout,
        [](auto &f, auto &, auto &)
        {
            f.view.impactTable =
                reinterpret_cast<const fastfile::FxImpactTableDisk32 *>(
                    reinterpret_cast<const std::uint8_t *>(&f.header) + 1u);
        });
    ExpectPlanFailure(
        Status::InvalidSourceLayout,
        [](auto &f, auto &, auto &)
        {
            f.view.entries.data =
                reinterpret_cast<const fastfile::FxImpactEntryDisk32 *>(
                    &f.header);
        });
    ExpectPlanFailure(
        Status::OverlappingStorage,
        [](auto &f, auto &w, auto &)
        {
            f.view.impactTable =
                reinterpret_cast<const fastfile::FxImpactTableDisk32 *>(&w);
        });
    ExpectPlanFailure(
        Status::OverlappingStorage,
        [](auto &f, auto &w, auto &)
        {
            f.view.entries.data =
                reinterpret_cast<const fastfile::FxImpactEntryDisk32 *>(&w);
        });
    ExpectPlanFailure(Status::OverlappingStorage,
                      [](auto &f, auto &w, auto &)
                      {
                          f.resolver.name = reinterpret_cast<char *>(&w);
                          f.resolver.nameBytes = 2;
                          f.provenance.name = reinterpret_cast<char *>(&w);
                          f.provenance.nameBytes = 2;
                      });

    for (const auto kind : {
             fastfile::FxFastFileDisk32SourceSpanKind::ImpactTableHeader,
             fastfile::FxFastFileDisk32SourceSpanKind::ImpactEntries,
             fastfile::FxFastFileDisk32SourceSpanKind::String,
         })
    {
        ExpectPlanFailure(Status::InvalidProvenance,
                          [kind](auto &f, auto &, auto &)
                          {
                              f.provenance.failEnabled = true;
                              f.provenance.failKind = kind;
                          });
    }

    ExpectPlanFailure(Status::UnresolvedReference,
                      [](auto &f, auto &, auto &) { f.resolver.failAt = 0; });
    ExpectPlanFailure(Status::InvalidString,
                      [](auto &f, auto &, auto &) { f.resolver.nullAt = 0; });
    ExpectPlanFailure(Status::InvalidString,
                      [](auto &f, auto &, auto &)
                      { f.resolver.zeroStringBytesAt = 0; });
    ExpectPlanFailure(
        Status::InvalidString,
        [](auto &f, auto &, auto &)
        {
            f.resolver.retainedDescriptorOverrideAt = 0;
            f.resolver.retainedByteCountOverride = f.resolver.nameBytes;
            f.resolver.retainedAlignmentOverride = 2;
        });
    ExpectPlanFailure(Status::UnresolvedReference,
                      [](auto &f, auto &, auto &) { f.resolver.failAt = 1; });
    ExpectPlanFailure(Status::UnresolvedReference,
                      [](auto &f, auto &, auto &) { f.resolver.nullAt = 1; });
    ExpectPlanFailure(Status::InvalidArgument,
                      [](auto &f, auto &, auto &)
                      { f.resolver.invalidAssetExtentAt = 1; });
    ExpectPlanFailure(
        Status::InvalidArgument,
        [](auto &f, auto &, auto &)
        {
            f.resolver.retainedDescriptorOverrideAt = 1;
            f.resolver.retainedByteCountOverride = sizeof(FxEffectDef) - 1u;
            f.resolver.retainedAlignmentOverride = alignof(FxEffectDef);
        });
    ExpectPlanFailure(
        Status::InvalidArgument,
        [](auto &f, auto &, auto &)
        {
            f.resolver.retainedDescriptorOverrideAt = 1;
            f.resolver.retainedByteCountOverride = sizeof(FxEffectDef);
            f.resolver.retainedAlignmentOverride =
                alignof(FxEffectDef) * 2u;
        });
    ExpectPlanFailure(
        Status::InvalidArgument,
        [](auto &f, auto &, auto &)
        {
            f.resolver.retainedDescriptorOverrideAt = 1;
            f.resolver.retainedByteCountOverride =
                (std::numeric_limits<std::uint64_t>::max)();
            f.resolver.retainedAlignmentOverride = alignof(FxEffectDef);
        });
    ExpectPlanFailure(
        Status::InvalidArgument,
        [](auto &f, auto &, auto &)
        {
            const std::uintptr_t wrapped =
                (std::numeric_limits<std::uintptr_t>::max)() &
                ~(static_cast<std::uintptr_t>(alignof(FxEffectDef)) - 1u);
            f.resolver.forcedAssetIdentity =
                reinterpret_cast<const void *>(wrapped);
        });
    ExpectPlanFailure(
        Status::InvalidArgument,
        [](auto &f, auto &, auto &)
        {
            f.resolver.forcedAssetIdentity =
                reinterpret_cast<const std::uint8_t *>(&f.header) + 1u;
        });
    ExpectPlanFailure(
        Status::OverlappingStorage,
        [](auto &f, auto &, auto &)
        { f.resolver.assetResultUsesOutReference = true; });

    ExpectPlanFailure(Status::InvalidString,
                      [](auto &f, auto &, auto &)
                      {
                          f.name.fill('\0');
                          f.provenance.nameBytes = 1;
                          f.resolver.nameBytes = 1;
                      });
    ExpectPlanFailure(Status::InvalidString,
                      [](auto &f, auto &, auto &)
                      { f.name[f.provenance.nameBytes - 1u] = 'X'; });
    ExpectPlanFailure(Status::InvalidString,
                      [](auto &f, auto &, auto &) { f.name[1] = '\0'; });
}

void TestPlanAliasChecksPrecedeCallbacks()
{
    {
        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        const fastfile::FxImpactTableDisk32 headerBefore = fixture.header;
        auto *const aliasedPlan =
            reinterpret_cast<fastfile::FxFastFileImpactNativeDisk32Plan *>(
                &fixture.header);

        CHECK(fastfile::TryPlanFxImpactTableDisk32(
                  &workspace, fixture.view, fixture.resolvers, aliasedPlan) ==
              Status::OverlappingStorage);
        CHECK(fixture.provenance.calls == 0u);
        CHECK(fixture.resolver.calls == 0u);
        CHECK(std::memcmp(
                  &fixture.header, &headerBefore, sizeof(headerBefore)) == 0);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        const auto entriesBefore = fixture.entries;
        auto *const aliasedPlan =
            reinterpret_cast<fastfile::FxFastFileImpactNativeDisk32Plan *>(
                fixture.entries.data());

        CHECK(fastfile::TryPlanFxImpactTableDisk32(
                  &workspace, fixture.view, fixture.resolvers, aliasedPlan) ==
              Status::OverlappingStorage);
        CHECK(fixture.provenance.calls == 0u);
        CHECK(fixture.resolver.calls == 0u);
        CHECK(std::memcmp(fixture.entries.data(),
                          entriesBefore.data(),
                          sizeof(entriesBefore)) == 0);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        alignas(8) fastfile::FxFastFileImpactTableDisk32View sourceView =
            fixture.view;
        std::array<std::uint8_t, sizeof(sourceView)> sourceBefore{};
        std::memcpy(sourceBefore.data(), &sourceView, sizeof(sourceView));
        auto *const aliasedPlan =
            reinterpret_cast<fastfile::FxFastFileImpactNativeDisk32Plan *>(
                &sourceView);

        CHECK(fastfile::TryPlanFxImpactTableDisk32(
                  &workspace, sourceView, fixture.resolvers, aliasedPlan) ==
              Status::OverlappingStorage);
        CHECK(fixture.provenance.calls == 0u);
        CHECK(fixture.resolver.calls == 0u);
        CHECK(std::memcmp(
                  &sourceView, sourceBefore.data(), sizeof(sourceView)) == 0);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        alignas(8) fastfile::FxFastFileDisk32Resolvers resolvers =
            fixture.resolvers;
        std::array<std::uint8_t, sizeof(resolvers)> resolversBefore{};
        std::memcpy(resolversBefore.data(), &resolvers, sizeof(resolvers));
        auto *const aliasedPlan =
            reinterpret_cast<fastfile::FxFastFileImpactNativeDisk32Plan *>(
                &resolvers);

        CHECK(fastfile::TryPlanFxImpactTableDisk32(
                  &workspace, fixture.view, resolvers, aliasedPlan) ==
              Status::OverlappingStorage);
        CHECK(fixture.provenance.calls == 0u);
        CHECK(fixture.resolver.calls == 0u);
        CHECK(std::memcmp(
                  &resolvers, resolversBefore.data(), sizeof(resolvers)) == 0);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        alignas(8) fastfile::FxFastFileImpactTableDisk32View sourceView =
            fixture.view;
        fixture.resolver.name = reinterpret_cast<char *>(&sourceView);
        fixture.resolver.nameBytes = 2;
        std::array<std::uint8_t, sizeof(sourceView)> sourceBefore{};
        std::memcpy(sourceBefore.data(), &sourceView, sizeof(sourceView));

        CHECK(fastfile::TryPlanFxImpactTableDisk32(
                  &workspace, sourceView, fixture.resolvers, &plan) ==
              Status::InvalidSourceLayout);
        CHECK(fixture.provenance.calls == 2u);
        CHECK(fixture.resolver.calls == 1u);
        CHECK(std::memcmp(
                  &sourceView, sourceBefore.data(), sizeof(sourceView)) == 0);
        CHECK(!static_cast<bool>(plan));
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        alignas(8) fastfile::FxFastFileDisk32Resolvers resolvers =
            fixture.resolvers;
        fixture.resolver.name = reinterpret_cast<char *>(&resolvers);
        fixture.resolver.nameBytes = 2;
        std::array<std::uint8_t, sizeof(resolvers)> resolversBefore{};
        std::memcpy(resolversBefore.data(), &resolvers, sizeof(resolvers));

        CHECK(fastfile::TryPlanFxImpactTableDisk32(
                  &workspace, fixture.view, resolvers, &plan) ==
              Status::InvalidSourceLayout);
        CHECK(fixture.provenance.calls == 2u);
        CHECK(fixture.resolver.calls == 1u);
        CHECK(std::memcmp(
                  &resolvers, resolversBefore.data(), sizeof(resolvers)) == 0);
        CHECK(!static_cast<bool>(plan));
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        const auto nameBefore = fixture.name;
        auto *const aliasedPlan =
            reinterpret_cast<fastfile::FxFastFileImpactNativeDisk32Plan *>(
                fixture.name.data());

        CHECK(fastfile::TryPlanFxImpactTableDisk32(
                  &workspace, fixture.view, fixture.resolvers, aliasedPlan) ==
              Status::OverlappingStorage);
        CHECK(fixture.provenance.calls == 2u);
        CHECK(fixture.resolver.calls == 1u);
        CHECK(fixture.name == nameBefore);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }
}

void TestCallbackMutationAndReentry()
{
    for (const auto &pair : {
             std::pair{
                 fastfile::FxFastFileDisk32SourceSpanKind::ImpactTableHeader,
                 MutationKind::Header},
             std::pair{fastfile::FxFastFileDisk32SourceSpanKind::ImpactEntries,
                       MutationKind::Entry},
             std::pair{fastfile::FxFastFileDisk32SourceSpanKind::String,
                       MutationKind::Name},
         })
    {
        ExpectPlanFailure(Status::SourceChanged,
                          [pair](auto &f, auto &, auto &)
                          {
                              f.provenance.mutateEnabled = true;
                              f.provenance.mutateKind = pair.first;
                              f.provenance.mutation = pair.second;
                          });
    }

    for (const auto mutation : {
             MutationKind::Header,
             MutationKind::Entry,
             MutationKind::Name,
         })
    {
        ExpectPlanFailure(Status::SourceChanged,
                          [mutation](auto &f, auto &, auto &)
                          {
                              f.resolver.mutateAt =
                                  mutation == MutationKind::Header ? 0u : 1u;
                              f.resolver.mutation = mutation;
                          });
    }

    {
        Fixture fixture{};
        fixture.provenance.mutateResolverDescriptor = true;
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
        CHECK(fixture.resolvers.context == nullptr);
        CHECK(fixture.provenance.calls == 3u);
        CHECK(fixture.resolver.calls == kJournalCount);

        AlignedStorage storage(plan.outputBytes(), plan.outputAlignment());
        FxImpactTable *output = nullptr;
        CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
                  &workspace, plan, storage.data(), storage.size(), &output) ==
              Status::Success);
        CHECK(output == storage.data());
    }

    {
        Fixture fixture{};
        fixture.provenance.mutateReturnedDescriptor = true;
        fixture.provenance.returnedDescriptorPointer =
            reinterpret_cast<const void *>(static_cast<std::uintptr_t>(1u));
        fixture.provenance.returnedDescriptorBytes =
            (std::numeric_limits<std::uint64_t>::max)();
        fixture.provenance.returnedDescriptorAlignment =
            (std::numeric_limits<std::uint64_t>::max)();
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        CHECK(Plan(&fixture, &workspace, &plan) == Status::SourceChanged);
        CHECK(fixture.provenance.calls == 3u);
        CHECK(fixture.resolver.calls == 1u);
        CHECK(!static_cast<bool>(plan));
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        Fixture fixture{};
        fixture.resolver.mutateRetainedNameDescriptor = true;
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
        CHECK(fixture.resolver.retainedNameDescriptorMutated);
        CHECK(fixture.resolver.calls == kJournalCount);
        CHECK(static_cast<bool>(plan));
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Planned);

        AlignedStorage storage(plan.outputBytes(), plan.outputAlignment());
        FxImpactTable *output = nullptr;
        CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
                  &workspace, plan, storage.data(), storage.size(), &output) ==
              Status::Success);
        CHECK(output == storage.data());
        CHECK(output && output->name);
        if (output && output->name)
            CHECK(std::strcmp(output->name, fixture.name.data()) == 0);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        Fixture fixture{};
        fixture.resolver.futureTokenRestoreEvasion = true;
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        CHECK(Plan(&fixture, &workspace, &plan) == Status::SourceChanged);
        CHECK(fixture.resolver.calls == 2u);
        CHECK(!static_cast<bool>(plan));
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    for (const std::size_t reenterAt : {0u, 1u})
    {
        Fixture fixture{};
        fixture.resolver.reenterAt = reenterAt;
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
        CHECK(fixture.resolver.reentered);
        CHECK(fixture.resolver.nestedStatus == Status::Busy);
        CHECK(!static_cast<bool>(fixture.resolver.nestedPlan));
        CHECK(fixture.resolver.calls == kJournalCount);
    }

    for (const bool materialize : {false, true})
    {
        Fixture fixture{};
        fixture.resolver.reenterAt = 0;
        fixture.resolver.reenterPlanWithInvalidArguments = !materialize;
        fixture.resolver.reenterMaterializeWithInvalidArguments = materialize;
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
        CHECK(fixture.resolver.reentered);
        CHECK(fixture.resolver.nestedStatus == Status::Busy);
        CHECK(!static_cast<bool>(fixture.resolver.nestedPlan));
        CHECK(fixture.resolver.calls == kJournalCount);
    }

    Fixture fixture{};
    fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
    fastfile::FxFastFileImpactNativeDisk32Plan plan{};
    fixture.resolver.forcedAssetIdentity = &plan;
    CHECK(Plan(&fixture, &workspace, &plan) == Status::OverlappingStorage);
    CHECK(!static_cast<bool>(plan));
    CHECK(workspace.phase() == fastfile::FxFastFileNativeDisk32Phase::Empty);
}

void TestLegacyTokenCompatibility()
{
    for (const std::uint32_t tableToken : {
             disk32::kInline,
             disk32::kSharedInline,
             UINT32_C(0x41234567),
         })
    {
        for (const std::uint32_t nameToken : {
                 disk32::kInline,
                 disk32::kSharedInline,
                 UINT32_C(0x456789AB),
             })
        {
            Fixture fixture{};
            fixture.header.table.token.value = tableToken;
            fixture.header.name.token.value = nameToken;
            fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
            fastfile::FxFastFileImpactNativeDisk32Plan plan{};
            CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
        }
    }
}

void TestWorkspaceIdentityIsRejected()
{
    Fixture fixture{};
    fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
    fastfile::FxFastFileImpactNativeDisk32Plan plan{};
    fixture.resolver.forcedAssetIdentity = &workspace;
    CHECK(Plan(&fixture, &workspace, &plan) == Status::OverlappingStorage);
    CHECK(!static_cast<bool>(plan));
    CHECK(workspace.phase() == fastfile::FxFastFileNativeDisk32Phase::Empty);
}

void TestUnconsumedPlanCannotBeReplaced()
{
    Fixture fixture{};
    fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
    fastfile::FxFastFileImpactNativeDisk32Plan plan{};
    CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
    const std::size_t resolverCalls = fixture.resolver.calls;
    const std::size_t provenanceCalls = fixture.provenance.calls;
    fastfile::FxFastFileImpactNativeDisk32Plan replacement{};
    CHECK(Plan(&fixture, &workspace, &replacement) == Status::InvalidPhase);
    CHECK(!static_cast<bool>(replacement));
    CHECK(fixture.resolver.calls == resolverCalls);
    CHECK(fixture.provenance.calls == provenanceCalls);

    AlignedStorage storage(plan.outputBytes(), plan.outputAlignment());
    FxImpactTable *output = nullptr;
    CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
              &workspace, plan, storage.data(), storage.size(), &output) ==
          Status::Success);
}

void CheckRetryableFailure(
    const Status expected,
    fastfile::FxFastFileImpactNativeDisk32Workspace *const workspace,
    const fastfile::FxFastFileImpactNativeDisk32Plan &plan,
    void *const storage,
    const std::size_t capacity,
    FxImpactTable **const outTable,
    const std::uint8_t *const observedStorage,
    const std::size_t observedBytes)
{
    CHECK(workspace != nullptr);
    CHECK(outTable != nullptr);
    std::vector<std::uint8_t> before{};
    if (observedStorage && observedBytes)
        before.assign(observedStorage, observedStorage + observedBytes);
    FxImpactTable *const outputBefore = *outTable;
    CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
              workspace, plan, storage, capacity, outTable) == expected);
    CHECK(*outTable == outputBefore);
    if (observedStorage && observedBytes)
    {
        CHECK(std::equal(before.begin(), before.end(), observedStorage));
    }
    CHECK(workspace->phase() == fastfile::FxFastFileNativeDisk32Phase::Planned);
}

void TestMaterializationPreflightAndRetry()
{
    Fixture fixture{};
    fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
    fastfile::FxFastFileImpactNativeDisk32Plan plan{};
    CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
    AlignedStorage storage(plan.outputBytes() + 1u, plan.outputAlignment());
    FxImpactTable *output = reinterpret_cast<FxImpactTable *>(
        static_cast<std::uintptr_t>(UINT32_C(0x1234)));

    fastfile::FxFastFileImpactNativeDisk32Plan forged{};
    CheckRetryableFailure(Status::InvalidPlan,
                          &workspace,
                          forged,
                          storage.data(),
                          storage.size(),
                          &output,
                          storage.bytes(),
                          storage.size());
    CheckRetryableFailure(Status::MisalignedStorage,
                          &workspace,
                          plan,
                          storage.bytes() + 1u,
                          plan.outputBytes(),
                          &output,
                          storage.bytes(),
                          storage.size());
    CheckRetryableFailure(Status::InsufficientCapacity,
                          &workspace,
                          plan,
                          storage.data(),
                          plan.outputBytes() - 1u,
                          &output,
                          storage.bytes(),
                          storage.size());

    const std::uintptr_t highAddress =
        (std::numeric_limits<std::uintptr_t>::max)() &
        ~(static_cast<std::uintptr_t>(plan.outputAlignment()) - 1u);
    CheckRetryableFailure(Status::SizeOverflow,
                          &workspace,
                          plan,
                          reinterpret_cast<void *>(highAddress),
                          plan.outputBytes(),
                          &output,
                          nullptr,
                          0);

    CheckRetryableFailure(
        Status::OverlappingStorage,
        &workspace,
        plan,
        &fixture.header,
        plan.outputBytes(),
        &output,
        reinterpret_cast<const std::uint8_t *>(&fixture.header),
        sizeof(fixture.header));
    CheckRetryableFailure(Status::OverlappingStorage,
                          &workspace,
                          plan,
                          &workspace,
                          plan.outputBytes(),
                          &output,
                          nullptr,
                          0);
    std::array<std::uint8_t, sizeof(fixture.header)> headerBefore{};
    std::memcpy(headerBefore.data(), &fixture.header, sizeof(fixture.header));
    const auto storageBeforeBadOutput = storage.Snapshot();
    CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
              &workspace,
              plan,
              storage.data(),
              storage.size(),
              reinterpret_cast<FxImpactTable **>(&fixture.header)) ==
          Status::OverlappingStorage);
    CHECK(std::memcmp(headerBefore.data(),
                      &fixture.header,
                      sizeof(fixture.header)) == 0);
    CHECK(storage.Snapshot() == storageBeforeBadOutput);
    CHECK(workspace.phase() == fastfile::FxFastFileNativeDisk32Phase::Planned);

    alignas(8) std::array<std::uint8_t, 16> misalignedOutBytes{};
    auto **const misalignedOut =
        reinterpret_cast<FxImpactTable **>(misalignedOutBytes.data() + 1u);
    CHECK(
        fastfile::TryMaterializeFxImpactTableDisk32(
            &workspace, plan, storage.data(), storage.size(), misalignedOut) ==
        Status::InvalidArgument);
    CHECK(workspace.phase() == fastfile::FxFastFileNativeDisk32Phase::Planned);

    CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
              &workspace, plan, storage.data(), storage.size(), &output) ==
          Status::Success);
    CHECK(output == storage.data());
}

void TestResolvedIdentityAliasing()
{
    {
        const std::size_t alignment =
            (std::max)(alignof(FxEffectDef), alignof(FxImpactEntry));
        AlignedStorage storage(8192 + alignof(FxEffectDef), alignment);
        Fixture fixture{};
        fixture.resolver.forcedAssetIdentity = storage.data();
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
        FxImpactTable *output = nullptr;
        const auto before = storage.Snapshot();
        void *const partiallyOverlappingStorage =
            storage.bytes() + alignof(FxEffectDef);
        CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
                  &workspace,
                  plan,
                  partiallyOverlappingStorage,
                  storage.size() - alignof(FxEffectDef),
                  &output) == Status::OverlappingStorage);
        CHECK(output == nullptr);
        CHECK(storage.Snapshot() == before);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Planned);
    }
    {
        struct alignas(8) OutputCaller final
        {
            std::array<std::uint8_t, sizeof(FxEffectDef)> prefix{};
            FxImpactTable *output = nullptr;
            std::array<std::uint8_t, sizeof(FxEffectDef)> suffix{};
        } caller{};

        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        const auto *const outputAddress =
            reinterpret_cast<const std::uint8_t *>(&caller.output);
        fixture.resolver.forcedAssetIdentity =
            outputAddress - alignof(FxEffectDef);
        CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
        AlignedStorage storage(plan.outputBytes(), plan.outputAlignment());
        const auto storageBefore = storage.Snapshot();
        std::array<std::uint8_t, sizeof(caller)> callerBefore{};
        std::memcpy(callerBefore.data(), &caller, sizeof(caller));
        CHECK(fastfile::TryMaterializeFxImpactTableDisk32(&workspace,
                                                          plan,
                                                          storage.data(),
                                                          storage.size(),
                                                          &caller.output) ==
              Status::OverlappingStorage);
        CHECK(caller.output == nullptr);
        CHECK(std::memcmp(callerBefore.data(), &caller, sizeof(caller)) == 0);
        CHECK(storage.Snapshot() == storageBefore);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Planned);
    }
    {
        struct alignas(8) PlanCaller final
        {
            std::array<std::uint8_t, sizeof(FxEffectDef)> prefix{};
            fastfile::FxFastFileImpactNativeDisk32Plan plan{};
            std::array<std::uint8_t, sizeof(FxEffectDef)> suffix{};
        } caller{};

        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        const auto *const planAddress =
            reinterpret_cast<const std::uint8_t *>(&caller.plan);
        fixture.resolver.forcedAssetIdentity =
            planAddress - alignof(FxEffectDef);
        std::array<std::uint8_t, sizeof(caller)> callerBefore{};
        std::memcpy(callerBefore.data(), &caller, sizeof(caller));

        CHECK(Plan(&fixture, &workspace, &caller.plan) ==
              Status::OverlappingStorage);
        CHECK(!static_cast<bool>(caller.plan));
        CHECK(std::memcmp(callerBefore.data(), &caller, sizeof(caller)) == 0);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
    }
}

void TestSourceMutationConsumesPlan()
{
    for (const MutationKind mutation : {
             MutationKind::Header,
             MutationKind::Entry,
             MutationKind::Name,
         })
    {
        Fixture fixture{};
        fastfile::FxFastFileImpactNativeDisk32Workspace workspace{};
        fastfile::FxFastFileImpactNativeDisk32Plan plan{};
        CHECK(Plan(&fixture, &workspace, &plan) == Status::Success);
        AlignedStorage storage(plan.outputBytes(), plan.outputAlignment());
        const auto before = storage.Snapshot();
        FxImpactTable *output = reinterpret_cast<FxImpactTable *>(
            static_cast<std::uintptr_t>(UINT32_C(0x1234)));
        MutateSource(mutation,
                     &fixture.header,
                     fixture.entries.data(),
                     fixture.name.data());
        CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
                  &workspace, plan, storage.data(), storage.size(), &output) ==
              Status::SourceChanged);
        CHECK(output == reinterpret_cast<FxImpactTable *>(
                            static_cast<std::uintptr_t>(UINT32_C(0x1234))));
        CHECK(storage.Snapshot() == before);
        CHECK(workspace.phase() ==
              fastfile::FxFastFileNativeDisk32Phase::Empty);
        CHECK(fastfile::TryMaterializeFxImpactTableDisk32(
                  &workspace, plan, storage.data(), storage.size(), &output) ==
              Status::InvalidPhase);
    }
}

void TestForeignAndStalePlans()
{
    Fixture firstFixture{};
    Fixture secondFixture{};
    fastfile::FxFastFileImpactNativeDisk32Workspace firstWorkspace{};
    fastfile::FxFastFileImpactNativeDisk32Workspace secondWorkspace{};
    fastfile::FxFastFileImpactNativeDisk32Plan firstPlan{};
    fastfile::FxFastFileImpactNativeDisk32Plan secondPlan{};
    CHECK(Plan(&firstFixture, &firstWorkspace, &firstPlan) == Status::Success);
    CHECK(Plan(&secondFixture, &secondWorkspace, &secondPlan) ==
          Status::Success);
    AlignedStorage storage(firstPlan.outputBytes(),
                           firstPlan.outputAlignment());
    FxImpactTable *output = nullptr;
    const auto before = storage.Snapshot();
    CHECK(fastfile::TryMaterializeFxImpactTableDisk32(&firstWorkspace,
                                                      secondPlan,
                                                      storage.data(),
                                                      storage.size(),
                                                      &output) ==
          Status::InvalidPlan);
    CHECK(output == nullptr);
    CHECK(storage.Snapshot() == before);
    CHECK(firstWorkspace.phase() ==
          fastfile::FxFastFileNativeDisk32Phase::Planned);
}
} // namespace

int main()
{
    static_assert(kHandleCount == 396);
    static_assert(kJournalCount == 397);
    static_assert(
        sizeof(fastfile::FxFastFileDisk32ResolvedReference) == 0x18);
    static_assert(
        alignof(fastfile::FxFastFileDisk32ResolvedReference) == 8);
    static_assert(alignof(fastfile::FxFastFileImpactNativeDisk32Plan) == 8);
    static_assert(alignof(fastfile::FxFastFileImpactNativeDisk32Workspace) ==
                  8);
    static_assert(sizeof(fastfile::FxFastFileImpactNativeDisk32Plan) == 0x38);
    static_assert(sizeof(fastfile::FxFastFileImpactNativeDisk32Workspace) ==
                  (KISAK_PTR_BITS == 32 ? 0x2BD0 : 0x2BE0));

    TestHappyPathAndFullWidthIdentities();
    TestNullHandlesBypassResolver();
    TestPlanStructuralAndCallbackFailures();
    TestPlanAliasChecksPrecedeCallbacks();
    TestCallbackMutationAndReentry();
    TestLegacyTokenCompatibility();
    TestWorkspaceIdentityIsRejected();
    TestUnconsumedPlanCannotBeReplaced();
    TestMaterializationPreflightAndRetry();
    TestResolvedIdentityAliasing();
    TestSourceMutationConsumesPlan();
    TestForeignAndStalePlans();
    return failures == 0 ? 0 : 1;
}
