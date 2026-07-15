#include <EffectsCore/fx_archive_reader_disk32.h>

#include <universal/memfile.h>

#include <cstddef>
#include <cstdint>

namespace fx::archive
{
namespace
{
[[nodiscard]] FxArchiveDisk32ReaderLeaseIdentity SnapshotLease(
    const EffectTableRestoreLease &lease) noexcept
{
    return {
        lease.identity,
        lease.ownerCookie,
        lease.lifecycleGeneration,
        static_cast<std::uint32_t>(lease.serial),
        static_cast<std::uint32_t>(lease.serial >> 32u)};
}

[[nodiscard]] bool LeaseMatches(
    const FxArchiveDisk32ReaderLeaseIdentity &stored,
    const EffectTableRestoreLease &lease) noexcept
{
    const FxArchiveDisk32ReaderLeaseIdentity candidate =
        SnapshotLease(lease);
    return stored.identity == candidate.identity
        && stored.ownerCookie == candidate.ownerCookie
        && stored.lifecycleGeneration == candidate.lifecycleGeneration
        && stored.serialLow == candidate.serialLow
        && stored.serialHigh == candidate.serialHigh;
}

[[nodiscard]] FxArchiveDisk32ReaderStatus ReadExact(
    MemoryFile *const memFile,
    const std::size_t byteCount,
    void *const output) noexcept
{
    const MemFileReadStatus status = MemFile_TryReadDataNoReport(
        memFile,
        static_cast<int>(byteCount),
        static_cast<std::uint8_t *>(output));
    switch (status)
    {
    case MemFileReadStatus::Success:
        return FxArchiveDisk32ReaderStatus::Success;
    case MemFileReadStatus::InvalidArgument:
        return FxArchiveDisk32ReaderStatus::InvalidArgument;
    case MemFileReadStatus::InvalidState:
        return FxArchiveDisk32ReaderStatus::InvalidMemoryFile;
    case MemFileReadStatus::Overflow:
    case MemFileReadStatus::OutputTooSmall:
        return FxArchiveDisk32ReaderStatus::TruncatedInput;
    }
    return FxArchiveDisk32ReaderStatus::InvalidMemoryFile;
}

struct DefinitionResolverContext
{
    const EffectTableRestoreLease *lease = nullptr;
    bool leaseRejected = false;
};

[[nodiscard]] const FxEffectDef *ResolveDefinition(
    void *const opaqueContext,
    const EffectDefinitionKey32 key) noexcept
{
    auto *const context =
        static_cast<DefinitionResolverContext *>(opaqueContext);
    if (!context || !context->lease)
        return nullptr;
    const void *const definition =
        EffectTableRestoreFind(*context->lease, key);
    if (!definition
        && ValidateEffectTableRestoreLease(*context->lease)
            != EffectTableRestoreStatus::Success)
    {
        context->leaseRejected = true;
    }
    return static_cast<const FxEffectDef *>(definition);
}

struct PhysicsSinkContext
{
    FxArchiveDisk32ReaderPhysicsBody *bodies = nullptr;
    std::size_t acceptedCount = 0;
};

[[nodiscard]] bool AcceptPhysics(
    void *const opaqueContext,
    const FxArchiveDisk32ReadyPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    auto *const context =
        static_cast<PhysicsSinkContext *>(opaqueContext);
    if (!context || !context->bodies
        || physicsIndex != context->acceptedCount
        || physicsIndex >= FX_ARCHIVE_PHYSICS_BODY_LIMIT
        || !descriptor.elem || !descriptor.model
        || descriptor.ownerIndex >= MAX_ELEMS
        || descriptor.token == FX_ARCHIVE_INVALID_PHYSICS_TOKEN)
    {
        return false;
    }

    FxArchiveDisk32ReaderPhysicsBody &body =
        context->bodies[physicsIndex];
    body.descriptor = descriptor;
    body.state = BodyState{};
    ++context->acceptedCount;
    return true;
}
} // namespace

FxArchiveDisk32ReaderStatus TryReadFxArchiveDisk32NoReport(
    MemoryFile *const memFile,
    const EffectTableRestoreLease &lease,
    FxArchiveDisk32ReaderWorkspace *const workspace) noexcept
{
    if (!workspace)
        return FxArchiveDisk32ReaderStatus::InvalidArgument;
    if (workspace->operating_)
        return FxArchiveDisk32ReaderStatus::Busy;

    const EffectTableRestoreLease leaseSnapshot = lease;
    // Every idle attempt invalidates all earlier views before a lifecycle
    // callback or staged byte can change.
    workspace->phase_ = FxArchiveDisk32ReaderPhase::Empty;
    workspace->physicsBodyCount_ = 0;
    workspace->archivedSystemAddress_ = ArchiveAddress32{};
    workspace->lease_ = FxArchiveDisk32ReaderLeaseIdentity{};
    if (!memFile)
        return FxArchiveDisk32ReaderStatus::InvalidArgument;

    workspace->operating_ = true;
    const auto finish = [workspace](
                            const FxArchiveDisk32ReaderStatus status)
        noexcept {
            workspace->operating_ = false;
            return status;
        };

    if (ValidateEffectTableRestoreLease(leaseSnapshot)
        != EffectTableRestoreStatus::Success)
    {
        return finish(FxArchiveDisk32ReaderStatus::InvalidLease);
    }

    FxArchiveDisk32ReaderStatus readStatus = ReadExact(
        memFile, sizeof(workspace->sourceSystem_),
        &workspace->sourceSystem_);
    if (readStatus != FxArchiveDisk32ReaderStatus::Success)
        return finish(readStatus);
    readStatus = ReadExact(
        memFile, sizeof(workspace->sourceBuffers_),
        &workspace->sourceBuffers_);
    if (readStatus != FxArchiveDisk32ReaderStatus::Success)
        return finish(readStatus);

    DefinitionResolverContext resolverContext{&leaseSnapshot, false};
    const FxArchiveDisk32Resolver resolver{
        &resolverContext, ResolveDefinition};
    if (TryBuildFxArchiveDisk32StructuralImage(
            workspace->sourceSystem_,
            workspace->sourceBuffers_,
            resolver,
            &workspace->nativeWorkspace_)
        != FxArchiveDisk32StructuralStatus::Success)
    {
        if (resolverContext.leaseRejected
            || ValidateEffectTableRestoreLease(leaseSnapshot)
                != EffectTableRestoreStatus::Success)
        {
            return finish(FxArchiveDisk32ReaderStatus::InvalidLease);
        }
        return finish(
            FxArchiveDisk32ReaderStatus::InvalidStructuralImage);
    }

    if (ValidateEffectTableRestoreLease(leaseSnapshot)
        != EffectTableRestoreStatus::Success)
    {
        return finish(FxArchiveDisk32ReaderStatus::InvalidLease);
    }
    if (TryFinalizeFxArchiveDisk32NativeImage(
            &workspace->nativeWorkspace_)
        != FxArchiveDisk32ReadyStatus::Success)
    {
        if (ValidateEffectTableRestoreLease(leaseSnapshot)
            != EffectTableRestoreStatus::Success)
        {
            return finish(FxArchiveDisk32ReaderStatus::InvalidLease);
        }
        return finish(
            FxArchiveDisk32ReaderStatus::InvalidSemanticImage);
    }

    FxArchiveDisk32ReadyView graphView{};
    if (!TryGetFxArchiveDisk32ReadyView(
            &workspace->nativeWorkspace_, &graphView)
        || !graphView.system
        || graphView.physicsBodyCount > FX_ARCHIVE_PHYSICS_BODY_LIMIT)
    {
        return finish(
            FxArchiveDisk32ReaderStatus::InvalidSemanticImage);
    }

    PhysicsSinkContext sink{workspace->physicsBodies_, 0};
    if (!TryEnumerateFxArchiveDisk32ReadyPhysics(
            &workspace->nativeWorkspace_, &sink, AcceptPhysics)
        || sink.acceptedCount != graphView.physicsBodyCount)
    {
        if (ValidateEffectTableRestoreLease(leaseSnapshot)
            != EffectTableRestoreStatus::Success)
        {
            return finish(FxArchiveDisk32ReaderStatus::InvalidLease);
        }
        return finish(
            FxArchiveDisk32ReaderStatus::InvalidSemanticImage);
    }

    readStatus = ReadExact(
        memFile, sizeof(workspace->archivedSystemAddress_),
        &workspace->archivedSystemAddress_);
    if (readStatus != FxArchiveDisk32ReaderStatus::Success)
        return finish(readStatus);
    if (workspace->archivedSystemAddress_.value == 0)
    {
        return finish(
            FxArchiveDisk32ReaderStatus::InvalidRelocation);
    }

    for (std::size_t index = 0; index < sink.acceptedCount; ++index)
    {
        readStatus = ReadExact(
            memFile, sizeof(workspace->bodyScratch_),
            &workspace->bodyScratch_);
        if (readStatus != FxArchiveDisk32ReaderStatus::Success)
            return finish(readStatus);
        if (!TryUnpackBodyStateDisk32(
                workspace->bodyScratch_,
                graphView.system->msecNow,
                &workspace->physicsBodies_[index].state))
        {
            return finish(
                FxArchiveDisk32ReaderStatus::InvalidBodyState);
        }
    }

    if (ValidateEffectTableRestoreLease(leaseSnapshot)
        != EffectTableRestoreStatus::Success)
    {
        return finish(FxArchiveDisk32ReaderStatus::InvalidLease);
    }
    FxArchiveDisk32ReadyView finalGraphView{};
    if (!TryGetFxArchiveDisk32ReadyView(
            &workspace->nativeWorkspace_, &finalGraphView)
        || finalGraphView.system != graphView.system
        || finalGraphView.buffers != graphView.buffers
        || finalGraphView.poolStates != graphView.poolStates
        || finalGraphView.metadata != graphView.metadata
        || finalGraphView.physicsBodyCount != sink.acceptedCount
        || workspace->archivedSystemAddress_.value == 0)
    {
        return finish(
            FxArchiveDisk32ReaderStatus::InvalidSemanticImage);
    }

    workspace->lease_ = SnapshotLease(leaseSnapshot);
    workspace->physicsBodyCount_ =
        static_cast<std::uint32_t>(sink.acceptedCount);
    workspace->operating_ = false;
    // Ready publication is deliberately the final workspace mutation.
    workspace->phase_ = FxArchiveDisk32ReaderPhase::Ready;
    return FxArchiveDisk32ReaderStatus::Success;
}

bool TryGetFxArchiveDisk32ReaderReadyView(
    const FxArchiveDisk32ReaderWorkspace *const workspace,
    const EffectTableRestoreLease &lease,
    FxArchiveDisk32ReaderReadyView *const outView) noexcept
{
    if (!workspace || !outView || workspace->operating_
        || workspace->phase_ != FxArchiveDisk32ReaderPhase::Ready
        || !LeaseMatches(workspace->lease_, lease))
    {
        return false;
    }

    workspace->operating_ = true;
    const auto finish = [workspace](const bool result) noexcept {
        workspace->operating_ = false;
        return result;
    };
    if (ValidateEffectTableRestoreLease(lease)
        != EffectTableRestoreStatus::Success)
    {
        return finish(false);
    }

    FxArchiveDisk32ReadyView graphView{};
    if (!TryGetFxArchiveDisk32ReadyView(
            &workspace->nativeWorkspace_, &graphView)
        || graphView.physicsBodyCount != workspace->physicsBodyCount_
        || workspace->physicsBodyCount_ > FX_ARCHIVE_PHYSICS_BODY_LIMIT
        || workspace->archivedSystemAddress_.value == 0)
    {
        return finish(false);
    }

    const FxArchiveDisk32ReaderReadyView view{
        graphView,
        workspace->archivedSystemAddress_,
        workspace->physicsBodyCount_ != 0
            ? &workspace->physicsBodies_[0]
            : nullptr,
        workspace->physicsBodyCount_};
    workspace->operating_ = false;
    *outView = view;
    return true;
}
} // namespace fx::archive
