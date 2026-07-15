#include <EffectsCore/fx_archive_reader_disk32.h>
#include <EffectsCore/fx_archive_restore_workspace.h>

#include <universal/memfile.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

// The production executable supplies this table from fx_random.cpp.  These
// fixtures use zero-amplitude semantic ranges, so no sampled result depends on
// a particular table entry; the complete symbol is still required by the
// production semantic implementation linked into this isolated executable.
extern const float fx_randomTable[507]{};

namespace
{
namespace archive = fx::archive;

constexpr std::uint32_t DEFINITION_KEY = UINT32_C(0x00013579);
constexpr char DEFINITION_NAME[] = "fx/reader_test";
constexpr std::uint32_t BUFFER_BASE = UINT32_C(0x24000000);
constexpr std::uint32_t ELEMS_OFFSET = UINT32_C(0x00020000);
constexpr std::uint32_t TRAILS_OFFSET = UINT32_C(0x00034000);
constexpr std::uint32_t TRAIL_ELEMS_OFFSET = UINT32_C(0x00034400);
constexpr std::uint32_t VIS_STATE_OFFSET = UINT32_C(0x00044400);
constexpr std::uint32_t DEFERRED_ELEMS_OFFSET = UINT32_C(0x00046420);
constexpr std::uint32_t VIS_STATE_STRIDE = UINT32_C(0x00001010);
constexpr std::uint32_t SELF_OWNED_STATUS_BIT = UINT32_C(0x10000000);
constexpr std::uint32_t DOBJ_HANDLE_NONE = UINT32_C(4095);
constexpr std::uint32_t BONE_INDEX_NONE = UINT32_C(2047);
constexpr std::uint8_t ELEM_TYPE_MODEL = UINT8_C(5);
constexpr std::uint16_t INVALID_HANDLE =
    (std::numeric_limits<std::uint16_t>::max)();
constexpr std::size_t DISK_EFFECT_HANDLE_STRIDE =
    sizeof(archive::FxEffectDisk32) / FxEffect::HANDLE_SCALE;
constexpr std::size_t ELEM_HANDLE_STRIDE =
    sizeof(archive::FxElemDisk32) / FxElem::HANDLE_SCALE;

int failures = 0;
int unexpectedReports = 0;

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

constexpr std::uint16_t DiskEffectHandle(
    const std::size_t index) noexcept
{
    return static_cast<std::uint16_t>(
        index * DISK_EFFECT_HANDLE_STRIDE);
}

constexpr std::uint16_t ElemHandle(const std::size_t index) noexcept
{
    return static_cast<std::uint16_t>(index * ELEM_HANDLE_STRIDE);
}

template <std::size_t SIZE>
void StoreFreeLink(
    std::uint8_t (&bytes)[SIZE],
    const std::int32_t nextFree) noexcept
{
    static_assert(SIZE >= sizeof(std::uint32_t));
    const std::uint32_t value = static_cast<std::uint32_t>(nextFree);
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8u);
    bytes[2] = static_cast<std::uint8_t>(value >> 16u);
    bytes[3] = static_cast<std::uint8_t>(value >> 24u);
}

template <typename RECORD, std::size_t SIZE>
void StoreRecord(
    const RECORD &record,
    std::uint8_t (&bytes)[SIZE]) noexcept
{
    static_assert(sizeof(RECORD) == SIZE);
    std::memcpy(bytes, &record, sizeof(record));
}

void AppendLittleEndianU32(
    std::vector<std::uint8_t> *const output,
    const std::uint32_t value)
{
    CHECK(output != nullptr);
    if (!output)
        return;
    output->push_back(static_cast<std::uint8_t>(value));
    output->push_back(static_cast<std::uint8_t>(value >> 8u));
    output->push_back(static_cast<std::uint8_t>(value >> 16u));
    output->push_back(static_cast<std::uint8_t>(value >> 24u));
}

template <typename RECORD>
void AppendRecord(
    std::vector<std::uint8_t> *const output,
    const RECORD &record)
{
    static_assert(std::is_trivially_copyable_v<RECORD>);
    CHECK(output != nullptr);
    if (!output)
        return;
    const auto *const bytes =
        reinterpret_cast<const std::uint8_t *>(&record);
    output->insert(output->end(), bytes, bytes + sizeof(record));
}

std::vector<std::uint8_t> MakeEffectTablePrefix()
{
    std::vector<std::uint8_t> prefix;
    const auto *const nameBytes =
        reinterpret_cast<const std::uint8_t *>(DEFINITION_NAME);
    prefix.insert(
        prefix.end(),
        nameBytes,
        nameBytes + sizeof(DEFINITION_NAME));
    AppendLittleEndianU32(&prefix, DEFINITION_KEY);
    prefix.push_back(0);
    return prefix;
}

std::vector<std::uint8_t> WriteArchive(
    const std::vector<std::uint8_t> &payload,
    const bool compress)
{
    CHECK(!payload.empty());
    CHECK(payload.size()
          <= static_cast<std::size_t>(
              (std::numeric_limits<std::int32_t>::max)()));
    const std::size_t capacity = payload.size() * 3u + 4096u;
    CHECK(capacity
          <= static_cast<std::size_t>(
              (std::numeric_limits<std::int32_t>::max)()));
    std::vector<std::uint8_t> archiveBytes(capacity);
    MemoryFile writer{};
    MemFile_InitForWriting(
        &writer,
        static_cast<int>(archiveBytes.size()),
        archiveBytes.data(),
        false,
        compress);
    MemFile_WriteData(
        &writer,
        static_cast<int>(payload.size()),
        payload.data());
    MemFile_StartSegment(&writer, -1);
    CHECK(!writer.memoryOverflow);
    CHECK(writer.bufferSize >= 0);
    archiveBytes.resize(static_cast<std::size_t>(writer.bufferSize));
    MemFile_Shutdown(&writer);
    return archiveBytes;
}

void CloseReader(MemoryFile *const reader)
{
    if (!reader)
        return;
    if (!reader->memoryOverflow && reader->segmentIndex >= 0)
        MemFile_MoveToSegment(reader, -1);
    MemFile_Shutdown(reader);
}

struct alignas(FX_ELEM_DEF_RUNTIME_ALIGNMENT)
    SemanticElemDefinition final
{
    std::array<std::uint8_t, archive::layout::ELEM_DEF_STRIDE> bytes{};
};
static_assert(
    sizeof(SemanticElemDefinition)
    == archive::layout::ELEM_DEF_STRIDE);
static_assert(
    alignof(SemanticElemDefinition)
    == FX_ELEM_DEF_RUNTIME_ALIGNMENT);

template <typename VALUE>
void StoreDefinitionField(
    SemanticElemDefinition *const definition,
    const std::size_t offset,
    const VALUE &value) noexcept
{
    CHECK(definition != nullptr);
    const bool rangeIsValid = definition
        && offset <= definition->bytes.size()
        && sizeof(value) <= definition->bytes.size() - offset;
    CHECK(rangeIsValid);
    if (rangeIsValid)
    {
        std::memcpy(
            definition->bytes.data() + offset,
            &value,
            sizeof(value));
    }
}

struct SemanticDefinition final
{
    SemanticElemDefinition elemDef{};
    FxEffectDef effect{};
    const XModel *model = reinterpret_cast<const XModel *>(
        static_cast<std::uintptr_t>(UINT32_C(0x00135790)));

    SemanticDefinition() noexcept
    {
        effect.msecLoopingLife = 0;
        effect.elemDefCountOneShot = 1;
        effect.elemDefs = reinterpret_cast<const FxElemDef *>(&elemDef);
        const std::int32_t oneShotCount = 1;
        const std::int32_t lifespan = 1000;
        const std::int32_t flags = 0x08000000;
        const std::uint8_t visualCount = 1;
        StoreDefinitionField(
            &elemDef,
            archive::layout::ELEM_DEF_SPAWN_OFFSET,
            oneShotCount);
        StoreDefinitionField(
            &elemDef,
            archive::layout::ELEM_DEF_LIFE_SPAN_OFFSET,
            lifespan);
        StoreDefinitionField(
            &elemDef,
            archive::layout::ELEM_DEF_FLAGS_OFFSET,
            flags);
        StoreDefinitionField(
            &elemDef,
            archive::layout::ELEM_DEF_ELEM_TYPE_OFFSET,
            ELEM_TYPE_MODEL);
        StoreDefinitionField(
            &elemDef,
            archive::layout::ELEM_DEF_VISUAL_COUNT_OFFSET,
            visualCount);
        StoreDefinitionField(
            &elemDef,
            archive::layout::ELEM_DEF_VISUALS_OFFSET,
            model);
    }
};

struct ImageFixture final
{
    std::unique_ptr<archive::FxSystemDisk32> system =
        std::make_unique<archive::FxSystemDisk32>();
    std::unique_ptr<archive::FxSystemBuffersDisk32> buffers =
        std::make_unique<archive::FxSystemBuffersDisk32>();
    std::vector<archive::BodyStateDisk32> bodies;

    explicit ImageFixture(const std::size_t physicsBodyCount)
        : bodies(physicsBodyCount)
    {
        CHECK(system != nullptr);
        CHECK(buffers != nullptr);
        CHECK(physicsBodyCount <= archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT);
        if (!system || !buffers
            || physicsBodyCount > archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT)
        {
            return;
        }

        system->effects = archive::ArchiveAddress32{BUFFER_BASE};
        system->elems =
            archive::ArchiveAddress32{BUFFER_BASE + ELEMS_OFFSET};
        system->trails =
            archive::ArchiveAddress32{BUFFER_BASE + TRAILS_OFFSET};
        system->trailElems = archive::ArchiveAddress32{
            BUFFER_BASE + TRAIL_ELEMS_OFFSET};
        system->deferredElems = archive::ArchiveAddress32{
            BUFFER_BASE + DEFERRED_ELEMS_OFFSET};
        system->visState =
            archive::ArchiveAddress32{BUFFER_BASE + VIS_STATE_OFFSET};
        system->visStateBufferRead = system->visState;
        system->visStateBufferWrite = archive::ArchiveAddress32{
            system->visState.value + VIS_STATE_STRIDE};
        system->msecNow = 1000;
        // Canonical invalid cameras are permitted only before the first draw.
        system->msecDraw = -1;
        system->frameCount = 7;
        system->isInitialized = 1;
        system->isArchiving = 1;
        for (std::size_t index = 0; index < MAX_EFFECTS; ++index)
            system->allEffectHandles[index] = DiskEffectHandle(index);

        ConfigureFreePools(physicsBodyCount);
        if (physicsBodyCount != 0)
            ConfigurePhysicsGraph(physicsBodyCount);
    }

private:
    void ConfigureFreePools(const std::size_t activeElemCount) noexcept
    {
        system->activeElemCount =
            static_cast<std::int32_t>(activeElemCount);
        system->firstFreeElem = activeElemCount == MAX_ELEMS
            ? -1
            : static_cast<std::int32_t>(activeElemCount);
        system->firstFreeTrail = 0;
        system->firstFreeTrailElem = 0;
        for (std::size_t index = activeElemCount;
             index < MAX_ELEMS;
             ++index)
        {
            StoreFreeLink(
                buffers->elems[index].bytes,
                index + 1u < MAX_ELEMS
                    ? static_cast<std::int32_t>(index + 1u)
                    : -1);
        }
        for (std::size_t index = 0; index < MAX_TRAILS; ++index)
        {
            StoreFreeLink(
                buffers->trails[index].bytes,
                index + 1u < MAX_TRAILS
                    ? static_cast<std::int32_t>(index + 1u)
                    : -1);
        }
        for (std::size_t index = 0; index < MAX_TRAIL_ELEMS; ++index)
        {
            StoreFreeLink(
                buffers->trailElems[index].bytes,
                index + 1u < MAX_TRAIL_ELEMS
                    ? static_cast<std::int32_t>(index + 1u)
                    : -1);
        }
    }

    void ConfigurePhysicsGraph(const std::size_t physicsBodyCount) noexcept
    {
        system->firstActiveEffect = 0;
        system->firstNewEffect = 1;
        system->firstFreeEffect = 1;

        archive::FxEffectDisk32 effect{};
        effect.definitionKey =
            archive::EffectDefinitionKey32{DEFINITION_KEY};
        effect.status = static_cast<std::int32_t>(
            SELF_OWNED_STATUS_BIT
            | static_cast<std::uint32_t>(physicsBodyCount));
        effect.firstElemHandle[0] = INVALID_HANDLE;
        effect.firstElemHandle[1] = ElemHandle(0);
        effect.firstElemHandle[2] = INVALID_HANDLE;
        effect.firstSortedElemHandle = INVALID_HANDLE;
        effect.firstTrailHandle = INVALID_HANDLE;
        effect.owner = DiskEffectHandle(0);
        effect.boltAndSortOrder =
            DOBJ_HANDLE_NONE | (BONE_INDEX_NONE << 13u);
        effect.msecBegin = 700;
        effect.msecLastUpdate = 900;
        effect.frameAtSpawn.quat[0] = 1.0f;
        effect.frameNow.quat[0] = 1.0f;
        effect.framePrev.quat[0] = 1.0f;
        buffers->effects[0] = effect;

        for (std::size_t index = 0;
             index < physicsBodyCount;
             ++index)
        {
            archive::FxElemDisk32 elem{};
            elem.defIndex = 0;
            elem.sequence = static_cast<std::uint8_t>(index & 0xFFu);
            elem.nextElemHandleInEffect = index + 1u < physicsBodyCount
                ? ElemHandle(index + 1u)
                : INVALID_HANDLE;
            elem.prevElemHandleInEffect = index == 0
                ? INVALID_HANDLE
                : ElemHandle(index - 1u);
            elem.msecBegin = 800;
            const std::uint32_t token =
                static_cast<std::uint32_t>(index + 1u);
            std::memcpy(elem.payload, &token, sizeof(token));
            StoreRecord(elem, buffers->elems[index].bytes);

            archive::BodyStateDisk32 &body = bodies[index];
            body.position[0] = static_cast<float>(index) + 0.25f;
            body.position[1] = -2.5f;
            body.position[2] = 3.75f;
            body.rotation[0][0] = 1.0f;
            body.rotation[1][1] = 1.0f;
            body.rotation[2][2] = 1.0f;
            body.velocity[0] = static_cast<float>(index) * 0.5f;
            body.angVelocity[1] = -4.0f;
            body.centerOfMassOffset[2] = 1.25f;
            body.mass = 10.0f + static_cast<float>(index);
            body.friction = 0.5f;
            body.bounce = 0.25f;
            body.state = static_cast<std::int32_t>(index % 3u);
            body.timeLastAsleep = -2000000000
                + static_cast<std::int32_t>(index);
            body.type = static_cast<std::int32_t>(index % 50u);
            body.underwater = UINT32_C(0xA5B6C700)
                | static_cast<std::uint32_t>(index & 1u);
        }
    }
};

std::vector<std::uint8_t> MakeTail(
    const ImageFixture &fixture,
    const std::uint32_t archivedSystemAddress,
    const std::vector<std::uint8_t> &trailing = {})
{
    std::vector<std::uint8_t> tail;
    CHECK(fixture.system != nullptr);
    CHECK(fixture.buffers != nullptr);
    if (!fixture.system || !fixture.buffers)
        return tail;
    tail.reserve(
        sizeof(*fixture.system) + sizeof(*fixture.buffers)
        + sizeof(archive::ArchiveAddress32)
        + fixture.bodies.size() * sizeof(archive::BodyStateDisk32)
        + trailing.size());
    AppendRecord(&tail, *fixture.system);
    AppendRecord(&tail, *fixture.buffers);
    AppendLittleEndianU32(&tail, archivedSystemAddress);
    for (const archive::BodyStateDisk32 &body : fixture.bodies)
        AppendRecord(&tail, body);
    tail.insert(tail.end(), trailing.begin(), trailing.end());
    return tail;
}

std::vector<std::uint8_t> MakeCombinedPayload(
    const ImageFixture &fixture,
    const std::uint32_t archivedSystemAddress,
    const std::vector<std::uint8_t> &trailing = {})
{
    std::vector<std::uint8_t> payload = MakeEffectTablePrefix();
    const std::vector<std::uint8_t> tail = MakeTail(
        fixture, archivedSystemAddress, trailing);
    payload.insert(payload.end(), tail.begin(), tail.end());
    return payload;
}

struct AllocationState final
{
    std::size_t allocateCalls = 0;
    std::size_t freeCalls = 0;
    int requestedByteCount = -1;
};

void *AllocateWorkspace(
    void *const opaqueContext,
    const int byteCount) noexcept
{
    auto *const state = static_cast<AllocationState *>(opaqueContext);
    if (!state || byteCount <= 0)
        return nullptr;
    ++state->allocateCalls;
    state->requestedByteCount = byteCount;
    return ::operator new(
        static_cast<std::size_t>(byteCount), std::nothrow);
}

void FreeWorkspace(
    void *const opaqueContext,
    void *const storage) noexcept
{
    auto *const state = static_cast<AllocationState *>(opaqueContext);
    if (state)
        ++state->freeCalls;
    ::operator delete(storage);
}

class WorkspaceOwner final
{
public:
    explicit WorkspaceOwner(AllocationState *const state) noexcept
        : callbacks_{state, AllocateWorkspace, FreeWorkspace},
          workspace_(archive::AllocateArchiveRestoreWorkspace<
                     archive::FxArchiveDisk32ReaderWorkspace>(callbacks_))
    {
    }

    ~WorkspaceOwner() noexcept
    {
        CHECK(archive::DestroyArchiveRestoreWorkspace(
            workspace_, callbacks_));
    }

    WorkspaceOwner(const WorkspaceOwner &) = delete;
    WorkspaceOwner &operator=(const WorkspaceOwner &) = delete;

    archive::FxArchiveDisk32ReaderWorkspace *get() const noexcept
    {
        return workspace_;
    }

private:
    archive::ArchiveRestoreWorkspaceMemoryCallbacks callbacks_{};
    archive::FxArchiveDisk32ReaderWorkspace *workspace_ = nullptr;
};

struct RestoreState final
{
    SemanticDefinition *definition = nullptr;
    const void *identity = nullptr;
    std::uint32_t generation = 17;
    bool lifecycleValid = true;
    std::size_t validationCalls = 0;
    std::size_t invalidateAfterValidationCall = 0;
    std::size_t registrationCalls = 0;
    bool reenterReader = false;
    bool reenterReadyView = false;
    const archive::EffectTableRestoreLease *nestedLease = nullptr;
    archive::FxArchiveDisk32ReaderWorkspace *nestedWorkspace = nullptr;
    archive::FxArchiveDisk32ReaderStatus nestedStatus =
        archive::FxArchiveDisk32ReaderStatus::Success;
    bool nestedViewStatus = true;
};

bool ValidateLifecycle(
    void *const opaqueContext,
    const void *const identity,
    const std::uint32_t generation) noexcept
{
    auto *const state = static_cast<RestoreState *>(opaqueContext);
    if (!state)
        return false;
    ++state->validationCalls;
    if (state->reenterReader && state->nestedLease
        && state->nestedWorkspace)
    {
        state->reenterReader = false;
        state->nestedStatus = archive::TryReadFxArchiveDisk32NoReport(
            nullptr, *state->nestedLease, state->nestedWorkspace);
    }
    if (state->reenterReadyView && state->nestedLease
        && state->nestedWorkspace)
    {
        state->reenterReadyView = false;
        archive::FxArchiveDisk32ReaderReadyView nestedView{};
        state->nestedViewStatus =
            archive::TryGetFxArchiveDisk32ReaderReadyView(
                state->nestedWorkspace,
                *state->nestedLease,
                &nestedView);
    }
    const bool valid = state->lifecycleValid
        && identity == state->identity
        && generation == state->generation;
    if (valid
        && state->validationCalls
            == state->invalidateAfterValidationCall)
    {
        // Model a lifecycle transition immediately after a successful
        // handshake.  The caller that triggered this validation may finish
        // its current step, but every subsequent lease use must reject it.
        state->lifecycleValid = false;
    }
    return valid;
}

const void *RegisterDefinition(
    void *const opaqueContext,
    const char *const name) noexcept
{
    auto *const state = static_cast<RestoreState *>(opaqueContext);
    if (!state || !state->definition || !name)
        return nullptr;
    ++state->registrationCalls;
    return std::strcmp(name, DEFINITION_NAME) == 0
        ? static_cast<const void *>(&state->definition->effect)
        : nullptr;
}

archive::EffectTableRestoreResult RestoreTableFromReader(
    MemoryFile *const reader,
    RestoreState *const state) noexcept
{
    CHECK(reader != nullptr);
    CHECK(state != nullptr);
    if (!reader || !state)
        return {};
    const archive::EffectTableRestoreCallbacks callbacks{
        state, ValidateLifecycle, RegisterDefinition};
    return archive::RestoreEffectTableNoReport(
        reader, state->identity, state->generation, callbacks);
}

archive::EffectTableRestoreResult AcquireTableLease(
    RestoreState *const state,
    const bool compress = false)
{
    const std::vector<std::uint8_t> payload = MakeEffectTablePrefix();
    std::vector<std::uint8_t> archiveBytes =
        WriteArchive(payload, compress);
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archiveBytes.size()),
        archiveBytes.data(),
        compress);
    const archive::EffectTableRestoreResult result =
        RestoreTableFromReader(&reader, state);
    CloseReader(&reader);
    CHECK(result.status == archive::EffectTableRestoreStatus::Success);
    return result;
}

void ReleaseLease(
    const archive::EffectTableRestoreLease &lease,
    const archive::EffectTableRestoreStatus expected =
        archive::EffectTableRestoreStatus::Success)
{
    CHECK(archive::ReleaseEffectTableRestore(lease) == expected);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());
}

archive::FxArchiveDisk32ReaderReadyView PoisonView() noexcept
{
    const auto *const poisonSystem = reinterpret_cast<const FxSystem *>(
        static_cast<std::uintptr_t>(UINT32_C(0x10)));
    const auto *const poisonBuffers =
        reinterpret_cast<const FxSystemBuffers *>(
            static_cast<std::uintptr_t>(UINT32_C(0x20)));
    const auto *const poisonStates = reinterpret_cast<const
        archive::FxSystemBuffersDisk32PoolStates *>(
            static_cast<std::uintptr_t>(UINT32_C(0x30)));
    const auto *const poisonMetadata = reinterpret_cast<const
        archive::FxSystemDisk32Metadata *>(
            static_cast<std::uintptr_t>(UINT32_C(0x40)));
    const auto *const poisonBodies = reinterpret_cast<const
        archive::FxArchiveDisk32ReaderPhysicsBody *>(
            static_cast<std::uintptr_t>(UINT32_C(0x50)));
    return {
        {poisonSystem, poisonBuffers, poisonStates, poisonMetadata, 71},
        archive::ArchiveAddress32{UINT32_C(0xDEADBEEF)},
        poisonBodies,
        93};
}

void CheckSameViewBytes(
    const archive::FxArchiveDisk32ReaderReadyView &left,
    const archive::FxArchiveDisk32ReaderReadyView &right)
{
    CHECK(std::memcmp(&left, &right, sizeof(left)) == 0);
}

void CheckViewGated(
    const archive::FxArchiveDisk32ReaderWorkspace *const workspace,
    const archive::EffectTableRestoreLease &lease)
{
    archive::FxArchiveDisk32ReaderReadyView view = PoisonView();
    const auto before = view;
    CHECK(!archive::TryGetFxArchiveDisk32ReaderReadyView(
        workspace, lease, &view));
    CheckSameViewBytes(view, before);
}

archive::FxArchiveDisk32ReaderStatus ReadTail(
    archive::FxArchiveDisk32ReaderWorkspace *const workspace,
    const archive::EffectTableRestoreLease &lease,
    const std::vector<std::uint8_t> &tail,
    const bool compress)
{
    std::vector<std::uint8_t> archiveBytes = WriteArchive(tail, compress);
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archiveBytes.size()),
        archiveBytes.data(),
        compress);
    const archive::FxArchiveDisk32ReaderStatus status =
        archive::TryReadFxArchiveDisk32NoReport(
            &reader, lease, workspace);
    CloseReader(&reader);
    return status;
}

void CheckBody(
    const archive::FxArchiveDisk32ReaderPhysicsBody &body,
    const archive::BodyStateDisk32 &source,
    const std::size_t index,
    const XModel *const expectedModel)
{
    CHECK(body.descriptor.elem != nullptr);
    CHECK(body.descriptor.model == expectedModel);
    CHECK(body.descriptor.ownerIndex == index);
    CHECK(body.descriptor.token == index + 1u);
    CHECK(body.state.position[0] == source.position[0]);
    CHECK(body.state.position[1] == source.position[1]);
    CHECK(body.state.position[2] == source.position[2]);
    CHECK(body.state.mass == source.mass);
    CHECK(body.state.friction == source.friction);
    CHECK(body.state.bounce == source.bounce);
    CHECK(body.state.state == source.state);
    CHECK(body.state.timeLastAsleep == 1000);
    CHECK(body.state.type == source.type);
    CHECK(body.state.underwater
          == static_cast<int>(source.underwater & UINT32_C(0xFF)));
}

void TestWorkspaceAndViewGates()
{
    static_assert(archive::IsSupportedArchiveRestoreWorkspace<
                  archive::FxArchiveDisk32ReaderWorkspace>);
    static_assert(std::is_nothrow_default_constructible_v<
                  archive::FxArchiveDisk32ReaderWorkspace>);
    static_assert(std::is_nothrow_destructible_v<
                  archive::FxArchiveDisk32ReaderWorkspace>);
    static_assert(!std::is_copy_constructible_v<
                  archive::FxArchiveDisk32ReaderWorkspace>);
    static_assert(!std::is_copy_assignable_v<
                  archive::FxArchiveDisk32ReaderWorkspace>);
    static_assert(!std::is_move_constructible_v<
                  archive::FxArchiveDisk32ReaderWorkspace>);
    static_assert(!std::is_move_assignable_v<
                  archive::FxArchiveDisk32ReaderWorkspace>);
    static_assert(alignof(archive::FxArchiveDisk32ReaderWorkspace) == 8);
    static_assert(sizeof(archive::FxArchiveDisk32ReaderWorkspace)
                  > 0xA0000);
    static_assert(sizeof(archive::FxArchiveDisk32ReaderWorkspace)
                  < 0xB0000);

    AllocationState allocation{};
    {
        WorkspaceOwner owner{&allocation};
        CHECK(owner.get() != nullptr);
        CHECK(allocation.allocateCalls == 1);
        CHECK(allocation.requestedByteCount
              == static_cast<int>(sizeof(
                  archive::FxArchiveDisk32ReaderWorkspace)));
        if (!owner.get())
            return;
        CHECK(owner.get()->phase()
              == archive::FxArchiveDisk32ReaderPhase::Empty);

        const archive::EffectTableRestoreLease emptyLease{};
        CheckViewGated(owner.get(), emptyLease);
        archive::FxArchiveDisk32ReaderReadyView view = PoisonView();
        const auto before = view;
        CHECK(!archive::TryGetFxArchiveDisk32ReaderReadyView(
            nullptr, emptyLease, &view));
        CheckSameViewBytes(view, before);
        CHECK(!archive::TryGetFxArchiveDisk32ReaderReadyView(
            owner.get(), emptyLease, nullptr));
        CHECK(archive::TryReadFxArchiveDisk32NoReport(
                  nullptr, emptyLease, nullptr)
              == archive::FxArchiveDisk32ReaderStatus::InvalidArgument);
        CHECK(archive::TryReadFxArchiveDisk32NoReport(
                  nullptr, emptyLease, owner.get())
              == archive::FxArchiveDisk32ReaderStatus::InvalidArgument);
    }
    CHECK(allocation.freeCalls == 1);
}

void TestRawAndCompressedEmptyAndPhysics()
{
    for (const bool compress : {false, true})
    {
        for (const std::size_t bodyCount : {std::size_t{0}, std::size_t{2}})
        {
            SemanticDefinition definition{};
            ImageFixture fixture{bodyCount};
            const std::uint32_t archivedAddress = compress
                ? UINT32_C(0xFFFFFFF0)
                : UINT32_C(0x00000001);
            const std::vector<std::uint8_t> trailing{
                UINT8_C(0xD1), UINT8_C(0xE2), UINT8_C(0xF3)};
            const std::vector<std::uint8_t> payload = MakeCombinedPayload(
                fixture, archivedAddress, trailing);
            std::vector<std::uint8_t> archiveBytes =
                WriteArchive(payload, compress);
            MemoryFile reader{};
            MemFile_InitForReading(
                &reader,
                static_cast<int>(archiveBytes.size()),
                archiveBytes.data(),
                compress);

            RestoreState restoreState{};
            restoreState.definition = &definition;
            restoreState.identity = &restoreState;
            const archive::EffectTableRestoreResult table =
                RestoreTableFromReader(&reader, &restoreState);
            CHECK(table.status
                  == archive::EffectTableRestoreStatus::Success);
            CHECK(restoreState.registrationCalls == 1);

            AllocationState allocation{};
            WorkspaceOwner owner{&allocation};
            CHECK(owner.get() != nullptr);
            if (owner.get()
                && table.status
                    == archive::EffectTableRestoreStatus::Success)
            {
                const archive::FxArchiveDisk32ReaderStatus readStatus =
                    archive::TryReadFxArchiveDisk32NoReport(
                        &reader, table.lease, owner.get());
                if (readStatus
                    != archive::FxArchiveDisk32ReaderStatus::Success)
                {
                    std::fprintf(
                        stderr,
                        "reader status %u for compress=%d bodies=%zu\n",
                        static_cast<unsigned>(readStatus),
                        compress ? 1 : 0,
                        bodyCount);
                }
                CHECK(readStatus
                      == archive::FxArchiveDisk32ReaderStatus::Success);
                CHECK(owner.get()->phase()
                      == archive::FxArchiveDisk32ReaderPhase::Ready);
                archive::FxArchiveDisk32ReaderReadyView view{};
                const bool gotView =
                    archive::TryGetFxArchiveDisk32ReaderReadyView(
                        owner.get(), table.lease, &view);
                CHECK(gotView);
                if (readStatus
                        == archive::FxArchiveDisk32ReaderStatus::Success
                    && gotView)
                {
                    CHECK(view.graph.system != nullptr);
                    CHECK(view.graph.buffers != nullptr);
                    CHECK(view.archivedSystemAddress.value
                          == archivedAddress);
                    CHECK(view.physicsBodyCount == bodyCount);
                    CHECK(view.graph.physicsBodyCount == bodyCount);
                    CHECK((view.physicsBodies == nullptr)
                          == (bodyCount == 0));
                    for (std::size_t index = 0;
                         index < bodyCount;
                         ++index)
                    {
                        CheckBody(
                            view.physicsBodies[index],
                            fixture.bodies[index],
                            index,
                            definition.model);
                    }

                    std::array<std::uint8_t, 3> restoredTrailing{};
                    CHECK(MemFile_TryReadDataNoReport(
                              &reader,
                              static_cast<int>(restoredTrailing.size()),
                              restoredTrailing.data())
                          == MemFileReadStatus::Success);
                    CHECK(std::equal(
                        restoredTrailing.begin(),
                        restoredTrailing.end(),
                        trailing.begin(),
                        trailing.end()));
                }

                archive::EffectTableRestoreLease forged = table.lease;
                ++forged.serial;
                CheckViewGated(owner.get(), forged);
            }

            if (table.lease.ownerCookie)
                ReleaseLease(table.lease);
            if (owner.get())
                CheckViewGated(owner.get(), table.lease);
            CloseReader(&reader);
        }
    }
}

void TestMaximumPhysicsCapacity()
{
    for (const bool compress : {false, true})
    {
        SemanticDefinition definition{};
        ImageFixture fixture{archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT};
        const std::vector<std::uint8_t> payload = MakeCombinedPayload(
            fixture, UINT32_C(0x81234567));
        std::vector<std::uint8_t> archiveBytes =
            WriteArchive(payload, compress);
        MemoryFile reader{};
        MemFile_InitForReading(
            &reader,
            static_cast<int>(archiveBytes.size()),
            archiveBytes.data(),
            compress);
        RestoreState restoreState{};
        restoreState.definition = &definition;
        restoreState.identity = &restoreState;
        const archive::EffectTableRestoreResult table =
            RestoreTableFromReader(&reader, &restoreState);

        AllocationState allocation{};
        WorkspaceOwner owner{&allocation};
        CHECK(owner.get() != nullptr);
        if (owner.get()
            && table.status == archive::EffectTableRestoreStatus::Success)
        {
            CHECK(archive::TryReadFxArchiveDisk32NoReport(
                      &reader, table.lease, owner.get())
                  == archive::FxArchiveDisk32ReaderStatus::Success);
            archive::FxArchiveDisk32ReaderReadyView view{};
            CHECK(archive::TryGetFxArchiveDisk32ReaderReadyView(
                owner.get(), table.lease, &view));
            CHECK(view.physicsBodyCount
                  == archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT);
            if (view.physicsBodies)
            {
                CheckBody(
                    view.physicsBodies[0],
                    fixture.bodies[0],
                    0,
                    definition.model);
                const std::size_t last =
                    archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT - 1u;
                CheckBody(
                    view.physicsBodies[last],
                    fixture.bodies[last],
                    last,
                    definition.model);
            }
        }
        if (table.lease.ownerCookie)
            ReleaseLease(table.lease);
        CloseReader(&reader);
    }
}

void TestMalformedTailAndFreshReaderRetry()
{
    SemanticDefinition definition{};
    RestoreState restoreState{};
    restoreState.definition = &definition;
    restoreState.identity = &restoreState;
    const archive::EffectTableRestoreResult table =
        AcquireTableLease(&restoreState);
    if (table.status != archive::EffectTableRestoreStatus::Success)
        return;

    ImageFixture fixture{2};
    const std::vector<std::uint8_t> validTail =
        MakeTail(fixture, UINT32_C(0x76543210));
    const std::size_t systemEnd = sizeof(archive::FxSystemDisk32);
    const std::size_t buffersEnd =
        systemEnd + sizeof(archive::FxSystemBuffersDisk32);
    const std::size_t addressEnd =
        buffersEnd + sizeof(archive::ArchiveAddress32);
    const std::size_t firstBodyEnd =
        addressEnd + sizeof(archive::BodyStateDisk32);
    const std::size_t secondBodyEnd =
        firstBodyEnd + sizeof(archive::BodyStateDisk32);
    CHECK(validTail.size() == secondBodyEnd);

    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
    {
        ReleaseLease(table.lease);
        return;
    }

    MemoryFile invalidReader{};
    CHECK(archive::TryReadFxArchiveDisk32NoReport(
              &invalidReader, table.lease, owner.get())
          == archive::FxArchiveDisk32ReaderStatus::InvalidMemoryFile);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32ReaderPhase::Empty);

    ImageFixture invalidStructuralFixture{0};
    invalidStructuralFixture.system->isInitialized = 0;
    const std::vector<std::uint8_t> invalidStructuralTail = MakeTail(
        invalidStructuralFixture, UINT32_C(0x76543210));
    CHECK(ReadTail(
              owner.get(),
              table.lease,
              invalidStructuralTail,
              false)
          == archive::FxArchiveDisk32ReaderStatus::InvalidStructuralImage);
    CheckViewGated(owner.get(), table.lease);

    ImageFixture invalidSemanticFixture{1};
    const XModel *const missingModel = nullptr;
    StoreDefinitionField(
        &definition.elemDef,
        archive::layout::ELEM_DEF_VISUALS_OFFSET,
        missingModel);
    const std::vector<std::uint8_t> invalidSemanticTail = MakeTail(
        invalidSemanticFixture, UINT32_C(0x76543210));
    CHECK(ReadTail(
              owner.get(), table.lease, invalidSemanticTail, false)
          == archive::FxArchiveDisk32ReaderStatus::InvalidSemanticImage);
    CheckViewGated(owner.get(), table.lease);
    StoreDefinitionField(
        &definition.elemDef,
        archive::layout::ELEM_DEF_VISUALS_OFFSET,
        definition.model);

    std::vector<std::size_t> truncationPoints{
        systemEnd - 1u,
        buffersEnd - 1u};
    for (std::size_t addressBytes = 0; addressBytes < 4; ++addressBytes)
        truncationPoints.push_back(buffersEnd + addressBytes);
    for (std::size_t bodyBytes = 0;
         bodyBytes < sizeof(archive::BodyStateDisk32);
         ++bodyBytes)
    {
        truncationPoints.push_back(addressEnd + bodyBytes);
    }
    truncationPoints.push_back(
        secondBodyEnd - 1u);
    for (const std::size_t truncationPoint : truncationPoints)
    {
        std::vector<std::uint8_t> truncated(
            validTail.begin(),
            validTail.begin()
                + static_cast<std::ptrdiff_t>(truncationPoint));
        CHECK(ReadTail(
                  owner.get(), table.lease, truncated, false)
              == archive::FxArchiveDisk32ReaderStatus::TruncatedInput);
        CHECK(owner.get()->phase()
              == archive::FxArchiveDisk32ReaderPhase::Empty);
        CheckViewGated(owner.get(), table.lease);
    }

    std::vector<std::uint8_t> compressedTruncation(
        validTail.begin(),
        validTail.begin()
            + static_cast<std::ptrdiff_t>(secondBodyEnd - 1u));
    CHECK(ReadTail(
              owner.get(),
              table.lease,
              compressedTruncation,
              true)
          == archive::FxArchiveDisk32ReaderStatus::TruncatedInput);

    std::vector<std::uint8_t> zeroAddress = validTail;
    std::fill(
        zeroAddress.begin()
            + static_cast<std::ptrdiff_t>(buffersEnd),
        zeroAddress.begin()
            + static_cast<std::ptrdiff_t>(addressEnd),
        UINT8_C(0));
    CHECK(ReadTail(owner.get(), table.lease, zeroAddress, false)
          == archive::FxArchiveDisk32ReaderStatus::InvalidRelocation);
    CheckViewGated(owner.get(), table.lease);

    ImageFixture invalidBodyFixture{2};
    invalidBodyFixture.bodies[0].mass = 0.0f;
    const std::vector<std::uint8_t> invalidBodyTail = MakeTail(
        invalidBodyFixture, UINT32_C(0x76543210));
    CHECK(ReadTail(
              owner.get(), table.lease, invalidBodyTail, false)
          == archive::FxArchiveDisk32ReaderStatus::InvalidBodyState);
    CheckViewGated(owner.get(), table.lease);

    // The failed stream cursor is intentionally not rewound.  A newly
    // initialized MemoryFile can reuse the same workspace and exact lease.
    CHECK(ReadTail(owner.get(), table.lease, validTail, false)
          == archive::FxArchiveDisk32ReaderStatus::Success);
    archive::FxArchiveDisk32ReaderReadyView view{};
    CHECK(archive::TryGetFxArchiveDisk32ReaderReadyView(
        owner.get(), table.lease, &view));
    CHECK(view.physicsBodyCount == 2);

    ReleaseLease(table.lease);
}

void TestLeaseGatesAndCallbackReentry()
{
    SemanticDefinition definition{};
    ImageFixture fixture{1};
    const std::vector<std::uint8_t> validTail =
        MakeTail(fixture, UINT32_C(0xF0000001));
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    RestoreState firstState{};
    firstState.definition = &definition;
    firstState.identity = &firstState;
    const archive::EffectTableRestoreResult first =
        AcquireTableLease(&firstState);
    if (first.status != archive::EffectTableRestoreStatus::Success)
        return;

    std::vector<std::uint8_t> tailArchive = WriteArchive(validTail, false);
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(tailArchive.size()),
        tailArchive.data(),
        false);

    std::array<archive::EffectTableRestoreLease, 5> forged{
        first.lease,
        first.lease,
        first.lease,
        first.lease,
        first.lease};
    forged[0].identity = &definition;
    ++forged[1].lifecycleGeneration;
    ++forged[2].serial;
    forged[3].ownerCookie = &definition;
    forged[4].serial ^= UINT64_C(1) << 32u;
    for (const archive::EffectTableRestoreLease &lease : forged)
    {
        CHECK(archive::TryReadFxArchiveDisk32NoReport(
                  &reader, lease, owner.get())
              == archive::FxArchiveDisk32ReaderStatus::InvalidLease);
        CheckViewGated(owner.get(), first.lease);
    }

    firstState.reenterReader = true;
    firstState.nestedLease = &first.lease;
    firstState.nestedWorkspace = owner.get();
    CHECK(archive::TryReadFxArchiveDisk32NoReport(
              &reader, first.lease, owner.get())
          == archive::FxArchiveDisk32ReaderStatus::Success);
    CHECK(firstState.nestedStatus
          == archive::FxArchiveDisk32ReaderStatus::Busy);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32ReaderPhase::Ready);
    CloseReader(&reader);

    firstState.reenterReader = true;
    firstState.reenterReadyView = true;
    firstState.nestedStatus =
        archive::FxArchiveDisk32ReaderStatus::Success;
    firstState.nestedViewStatus = true;
    archive::FxArchiveDisk32ReaderReadyView ready = PoisonView();
    CHECK(archive::TryGetFxArchiveDisk32ReaderReadyView(
        owner.get(), first.lease, &ready));
    CHECK(firstState.nestedStatus
          == archive::FxArchiveDisk32ReaderStatus::Busy);
    CHECK(!firstState.nestedViewStatus);
    CHECK(ready.graph.system != nullptr);
    CHECK(ready.physicsBodyCount == 1);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32ReaderPhase::Ready);
    archive::EffectTableRestoreLease stale = first.lease;
    ReleaseLease(first.lease);
    CheckViewGated(owner.get(), stale);
    CHECK(ReadTail(owner.get(), stale, validTail, false)
          == archive::FxArchiveDisk32ReaderStatus::InvalidLease);

    RestoreState secondState{};
    secondState.definition = &definition;
    secondState.identity = &secondState;
    const archive::EffectTableRestoreResult second =
        AcquireTableLease(&secondState);
    if (second.status == archive::EffectTableRestoreStatus::Success)
    {
        CHECK(ReadTail(owner.get(), stale, validTail, false)
              == archive::FxArchiveDisk32ReaderStatus::InvalidLease);
        CHECK(ReadTail(owner.get(), second.lease, validTail, false)
              == archive::FxArchiveDisk32ReaderStatus::Success);

        secondState.lifecycleValid = false;
        CheckViewGated(owner.get(), second.lease);
        CHECK(owner.get()->phase()
              == archive::FxArchiveDisk32ReaderPhase::Ready);
        CHECK(ReadTail(owner.get(), second.lease, validTail, false)
              == archive::FxArchiveDisk32ReaderStatus::InvalidLease);
        CHECK(owner.get()->phase()
              == archive::FxArchiveDisk32ReaderPhase::Empty);
        ReleaseLease(
            second.lease,
            archive::EffectTableRestoreStatus::LifecycleChanged);
    }
}

void TestLateLifecycleInvalidationClassification()
{
    SemanticDefinition definition{};
    // Structural conversion needs only the definition identity.  A physics
    // element with no selected model fails later semantic finalization.
    const XModel *const missingModel = nullptr;
    StoreDefinitionField(
        &definition.elemDef,
        archive::layout::ELEM_DEF_VISUALS_OFFSET,
        missingModel);
    ImageFixture fixture{1};
    const std::vector<std::uint8_t> validTail =
        MakeTail(fixture, UINT32_C(0xF0000011));

    RestoreState restoreState{};
    restoreState.definition = &definition;
    restoreState.identity = &restoreState;
    const archive::EffectTableRestoreResult table =
        AcquireTableLease(&restoreState);
    if (table.status != archive::EffectTableRestoreStatus::Success)
        return;

    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
    {
        ReleaseLease(table.lease);
        return;
    }

    restoreState.validationCalls = 0;
    // Reader validation call 1 gates input, call 2 resolves the definition,
    // and call 3 is the handshake immediately before finalization.  Let that
    // call report success while invalidating every subsequent lease use.
    restoreState.invalidateAfterValidationCall = 3;
    CHECK(ReadTail(owner.get(), table.lease, validTail, false)
          == archive::FxArchiveDisk32ReaderStatus::InvalidLease);
    CHECK(restoreState.validationCalls == 4);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32ReaderPhase::Empty);
    CheckViewGated(owner.get(), table.lease);

    ReleaseLease(
        table.lease,
        archive::EffectTableRestoreStatus::LifecycleChanged);
}
} // namespace

void MyAssertHandler(
    const char *filename,
    int line,
    int type,
    const char *format,
    ...)
{
    (void)filename;
    (void)line;
    (void)type;
    (void)format;
    ++unexpectedReports;
}

void QDECL Com_Printf(const int channel, const char *format, ...)
{
    (void)channel;
    (void)format;
    ++unexpectedReports;
}

void QDECL Com_Error(const errorParm_t code, const char *format, ...)
{
    (void)code;
    (void)format;
    ++unexpectedReports;
}

char *QDECL va(const char *format, ...)
{
    static char result[1]{};
    (void)format;
    return result;
}

bool __cdecl Sys_IsMainThread()
{
    return true;
}

bool __cdecl Sys_IsRenderThread()
{
    return false;
}

bool __cdecl Sys_IsDatabaseThread()
{
    return false;
}

int main()
{
    TestWorkspaceAndViewGates();
    TestRawAndCompressedEmptyAndPhysics();
    TestMaximumPhysicsCapacity();
    TestMalformedTailAndFreshReaderRetry();
    TestLeaseGatesAndCallbackReentry();
    TestLateLifecycleInvalidationClassification();
    CHECK(unexpectedReports == 0);

    if (archive::EffectTableRestoreLeaseIsActive())
    {
        archive::AbandonCurrentThreadEffectTableRestoreForError();
        CHECK(false);
    }
    if (failures != 0)
    {
        std::fprintf(stderr, "%d reader Disk32 test(s) failed\n", failures);
        return 1;
    }
    std::puts("fx archive reader Disk32 tests passed");
    return 0;
}
