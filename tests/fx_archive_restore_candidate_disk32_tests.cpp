#include <EffectsCore/fx_archive_restore_candidate_disk32.h>
#include <EffectsCore/fx_archive_restore_workspace.h>

#include <universal/memfile.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

extern const float fx_randomTable[507]{};

namespace
{
namespace archive = fx::archive;

constexpr std::uint32_t DEFINITION_KEY = UINT32_C(0x0002468A);
constexpr char DEFINITION_NAME[] = "fx/restore_candidate_test";
constexpr std::uint32_t BUFFER_BASE = UINT32_C(0x31000000);
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

std::vector<std::uint8_t> WriteArchive(
    const std::vector<std::uint8_t> &payload)
{
    CHECK(!payload.empty());
    const std::size_t capacity = payload.size() * 2u + 4096u;
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
        false);
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

template <typename VALUE>
void StoreDefinitionField(
    SemanticElemDefinition *const definition,
    const std::size_t offset,
    const VALUE &value) noexcept
{
    CHECK(definition != nullptr);
    const bool valid = definition
        && offset <= definition->bytes.size()
        && sizeof(value) <= definition->bytes.size() - offset;
    CHECK(valid);
    if (valid)
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
        static_cast<std::uintptr_t>(UINT32_C(0x002468A0)));

    SemanticDefinition() noexcept
    {
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

    ImageFixture(
        const std::size_t physicsBodyCount,
        const bool reverseSelectors)
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
        system->visStateBufferRead = archive::ArchiveAddress32{
            system->visState.value
            + (reverseSelectors ? VIS_STATE_STRIDE : 0u)};
        system->visStateBufferWrite = archive::ArchiveAddress32{
            system->visState.value
            + (reverseSelectors ? 0u : VIS_STATE_STRIDE)};
        system->msecNow = 1000;
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

    void ConfigurePhysicsGraph(const std::size_t bodyCount) noexcept
    {
        system->firstActiveEffect = 0;
        system->firstNewEffect = 1;
        system->firstFreeEffect = 1;

        archive::FxEffectDisk32 effect{};
        effect.definitionKey =
            archive::EffectDefinitionKey32{DEFINITION_KEY};
        effect.status = static_cast<std::int32_t>(
            SELF_OWNED_STATUS_BIT
            | static_cast<std::uint32_t>(bodyCount));
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

        for (std::size_t index = 0; index < bodyCount; ++index)
        {
            archive::FxElemDisk32 elem{};
            elem.defIndex = 0;
            elem.sequence = static_cast<std::uint8_t>(index & 0xFFu);
            elem.nextElemHandleInEffect = index + 1u < bodyCount
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

std::vector<std::uint8_t> MakeTablePrefix()
{
    std::vector<std::uint8_t> prefix;
    const auto *const name =
        reinterpret_cast<const std::uint8_t *>(DEFINITION_NAME);
    prefix.insert(prefix.end(), name, name + sizeof(DEFINITION_NAME));
    AppendLittleEndianU32(&prefix, DEFINITION_KEY);
    prefix.push_back(0);
    return prefix;
}

std::vector<std::uint8_t> MakeTail(
    const ImageFixture &fixture,
    const std::uint32_t archivedAddress)
{
    std::vector<std::uint8_t> tail;
    CHECK(fixture.system != nullptr);
    CHECK(fixture.buffers != nullptr);
    if (!fixture.system || !fixture.buffers)
        return tail;
    tail.reserve(
        sizeof(*fixture.system) + sizeof(*fixture.buffers)
        + sizeof(archive::ArchiveAddress32)
        + fixture.bodies.size() * sizeof(archive::BodyStateDisk32));
    AppendRecord(&tail, *fixture.system);
    AppendRecord(&tail, *fixture.buffers);
    AppendLittleEndianU32(&tail, archivedAddress);
    for (const archive::BodyStateDisk32 &body : fixture.bodies)
        AppendRecord(&tail, body);
    return tail;
}

struct AllocationState final
{
    std::size_t allocations = 0;
    std::size_t frees = 0;
};

void *AllocateWorkspace(
    void *const opaqueContext,
    const int byteCount) noexcept
{
    auto *const state = static_cast<AllocationState *>(opaqueContext);
    if (!state || byteCount <= 0)
        return nullptr;
    ++state->allocations;
    return ::operator new(
        static_cast<std::size_t>(byteCount), std::nothrow);
}

void FreeWorkspace(
    void *const opaqueContext,
    void *const storage) noexcept
{
    auto *const state = static_cast<AllocationState *>(opaqueContext);
    if (state)
        ++state->frees;
    ::operator delete(storage);
}

struct RestoreState final
{
    SemanticDefinition *definition = nullptr;
    const void *identity = nullptr;
    std::uint32_t generation = 23;
    bool lifecycleValid = true;
    std::size_t validationCalls = 0;
    bool reenter = false;
    archive::FxArchiveDisk32ReaderWorkspace *reader = nullptr;
    archive::FxArchiveRestoreCandidateDisk32Workspace *candidate = nullptr;
    const archive::EffectTableRestoreLease *lease = nullptr;
    archive::FxArchiveDisk32ReaderStatus nestedRead =
        archive::FxArchiveDisk32ReaderStatus::Success;
    bool nestedReaderView = true;
    archive::FxArchiveRestoreCandidateDisk32Status nestedCandidate =
        archive::FxArchiveRestoreCandidateDisk32Status::Success;
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
    if (state->reenter && state->reader && state->candidate && state->lease)
    {
        state->reenter = false;
        state->nestedRead = archive::TryReadFxArchiveDisk32NoReport(
            nullptr, *state->lease, state->reader);
        archive::FxArchiveDisk32ReaderReadyView nestedView{};
        state->nestedReaderView =
            archive::TryGetFxArchiveDisk32ReaderReadyView(
                state->reader, *state->lease, &nestedView);
        state->nestedCandidate =
            archive::TryBuildFxArchiveRestoreCandidateDisk32(
                state->reader, *state->lease, state->candidate);
    }
    return state->lifecycleValid && identity == state->identity
        && generation == state->generation;
}

const void *RegisterDefinition(
    void *const opaqueContext,
    const char *const name) noexcept
{
    auto *const state = static_cast<RestoreState *>(opaqueContext);
    if (!state || !state->definition || !name)
        return nullptr;
    return std::strcmp(name, DEFINITION_NAME) == 0
        ? static_cast<const void *>(&state->definition->effect)
        : nullptr;
}

archive::EffectTableRestoreResult AcquireLease(
    RestoreState *const state)
{
    std::vector<std::uint8_t> bytes = WriteArchive(MakeTablePrefix());
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(bytes.size()),
        bytes.data(),
        false);
    const archive::EffectTableRestoreCallbacks callbacks{
        state, ValidateLifecycle, RegisterDefinition};
    const archive::EffectTableRestoreResult result =
        archive::RestoreEffectTableNoReport(
            &reader, state->identity, state->generation, callbacks);
    CloseReader(&reader);
    CHECK(result.status == archive::EffectTableRestoreStatus::Success);
    return result;
}

class PreparedFixture final
{
public:
    PreparedFixture(
        const std::size_t bodyCount,
        const bool reverseSelectors = false)
        : image_(bodyCount, reverseSelectors),
          callbacks_{&allocation_, AllocateWorkspace, FreeWorkspace}
    {
        restore_.definition = &definition_;
        restore_.identity = &restore_;
        table_ = AcquireLease(&restore_);
        if (table_.status != archive::EffectTableRestoreStatus::Success)
            return;

        reader_ = archive::AllocateArchiveRestoreWorkspace<
            archive::FxArchiveDisk32ReaderWorkspace>(callbacks_);
        candidate_ = archive::AllocateArchiveRestoreWorkspace<
            archive::FxArchiveRestoreCandidateDisk32Workspace>(callbacks_);
        CHECK(reader_ != nullptr);
        CHECK(candidate_ != nullptr);
        if (!reader_ || !candidate_)
            return;

        const std::vector<std::uint8_t> tail =
            MakeTail(image_, UINT32_C(0xF1234567));
        std::vector<std::uint8_t> bytes = WriteArchive(tail);
        MemoryFile memoryFile{};
        MemFile_InitForReading(
            &memoryFile,
            static_cast<int>(bytes.size()),
            bytes.data(),
            false);
        CHECK(archive::TryReadFxArchiveDisk32NoReport(
                  &memoryFile, table_.lease, reader_)
              == archive::FxArchiveDisk32ReaderStatus::Success);
        CloseReader(&memoryFile);
        ready_ = reader_->phase()
            == archive::FxArchiveDisk32ReaderPhase::Ready;
        if (ready_)
        {
            CHECK(archive::TryGetFxArchiveDisk32ReaderReadyView(
                reader_, table_.lease, &readerView_));
        }
    }

    ~PreparedFixture() noexcept
    {
        CHECK(archive::DestroyArchiveRestoreWorkspace(
            candidate_, callbacks_));
        CHECK(archive::DestroyArchiveRestoreWorkspace(reader_, callbacks_));
        if (table_.lease.ownerCookie)
        {
            CHECK(archive::ReleaseEffectTableRestore(table_.lease)
                  == archive::EffectTableRestoreStatus::Success);
        }
        CHECK(allocation_.allocations == allocation_.frees);
    }

    PreparedFixture(const PreparedFixture &) = delete;
    PreparedFixture &operator=(const PreparedFixture &) = delete;

    [[nodiscard]] bool ready() const noexcept
    {
        return ready_;
    }

    SemanticDefinition definition_{};
    ImageFixture image_;
    AllocationState allocation_{};
    archive::ArchiveRestoreWorkspaceMemoryCallbacks callbacks_{};
    RestoreState restore_{};
    archive::EffectTableRestoreResult table_{};
    archive::FxArchiveDisk32ReaderWorkspace *reader_ = nullptr;
    archive::FxArchiveRestoreCandidateDisk32Workspace *candidate_ = nullptr;
    archive::FxArchiveDisk32ReaderReadyView readerView_{};
    bool ready_ = false;
};

archive::FxArchiveRestoreCandidateDisk32ReadyView PoisonCandidateView()
    noexcept
{
    return {
        reinterpret_cast<FxSystem *>(static_cast<std::uintptr_t>(0x10)),
        reinterpret_cast<FxSystemBuffers *>(
            static_cast<std::uintptr_t>(0x20)),
        archive::ArchiveAddress32{UINT32_C(0xDEADBEEF)},
        reinterpret_cast<
            archive::FxArchiveRestoreCandidateDisk32PhysicsBody *>(
            static_cast<std::uintptr_t>(0x30)),
        99};
}

void CheckCandidateHidden(
    archive::FxArchiveRestoreCandidateDisk32Workspace *const workspace)
{
    CHECK(workspace != nullptr);
    if (!workspace)
        return;
    CHECK(workspace->phase()
          == archive::FxArchiveRestoreCandidateDisk32Phase::Empty);
    auto output = PoisonCandidateView();
    const auto before = output;
    CHECK(!archive::TryGetFxArchiveRestoreCandidateDisk32ReadyView(
        workspace, &output));
    CHECK(std::memcmp(&output, &before, sizeof(output)) == 0);
}

archive::FxArchiveRestoreCandidateDisk32Status BuildCandidate(
    PreparedFixture *const fixture)
{
    CHECK(fixture != nullptr);
    if (!fixture)
        return archive::FxArchiveRestoreCandidateDisk32Status::InvalidArgument;
    return archive::TryBuildFxArchiveRestoreCandidateDisk32(
        fixture->reader_, fixture->table_.lease, fixture->candidate_);
}

void CheckReadyCandidate(
    PreparedFixture *const fixture,
    const std::size_t expectedCount,
    const bool reverseSelectors)
{
    CHECK(fixture != nullptr);
    if (!fixture)
        return;
    CHECK(BuildCandidate(fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::Success);
    CHECK(fixture->candidate_->phase()
          == archive::FxArchiveRestoreCandidateDisk32Phase::Ready);

    archive::FxArchiveRestoreCandidateDisk32ReadyView candidate{};
    CHECK(archive::TryGetFxArchiveRestoreCandidateDisk32ReadyView(
        fixture->candidate_, &candidate));
    CHECK(candidate.system != nullptr);
    CHECK(candidate.buffers != nullptr);
    CHECK(candidate.system != fixture->readerView_.graph.system);
    CHECK(candidate.buffers != fixture->readerView_.graph.buffers);
    CHECK(candidate.archivedSystemAddress.value == UINT32_C(0xF1234567));
    CHECK(candidate.physicsBodyCount == expectedCount);
    CHECK((candidate.physicsBodies == nullptr) == (expectedCount == 0));
    if (!candidate.system || !candidate.buffers)
        return;

    CHECK(candidate.system->effects == candidate.buffers->effects);
    CHECK(candidate.system->elems == candidate.buffers->elems);
    CHECK(candidate.system->trails == candidate.buffers->trails);
    CHECK(candidate.system->trailElems == candidate.buffers->trailElems);
    CHECK(candidate.system->deferredElems == candidate.buffers->deferredElems);
    CHECK(candidate.system->visState == candidate.buffers->visState);
    CHECK(candidate.system->visStateBufferRead
          == &candidate.buffers->visState[reverseSelectors ? 1 : 0]);
    CHECK(candidate.system->visStateBufferWrite
          == &candidate.buffers->visState[reverseSelectors ? 0 : 1]);
    CHECK(std::memcmp(
              candidate.buffers,
              fixture->readerView_.graph.buffers,
              sizeof(*candidate.buffers))
          == 0);

    for (std::size_t index = 0; index < expectedCount; ++index)
    {
        const auto &source = fixture->readerView_.physicsBodies[index];
        const auto &body = candidate.physicsBodies[index];
        CHECK(body.elem == &candidate.buffers->elems[index].item);
        CHECK(body.elem != source.descriptor.elem);
        CHECK(body.model == source.descriptor.model);
        CHECK(body.ownerIndex == index);
        CHECK(body.token == index + 1u);
        CHECK(std::memcmp(&body.state, &source.state, sizeof(body.state)) == 0);
    }

    const int sourceTime = fixture->readerView_.graph.system->msecNow;
    candidate.system->msecNow = sourceTime + 7;
    CHECK(fixture->readerView_.graph.system->msecNow == sourceTime);
    if (expectedCount != 0)
    {
        const float sourceMass =
            fixture->readerView_.physicsBodies[0].state.mass;
        candidate.physicsBodies[0].state.mass = sourceMass + 1.0f;
        CHECK(fixture->readerView_.physicsBodies[0].state.mass == sourceMass);
    }
}

void TestWorkspaceABIAndArgumentGates()
{
    static_assert(archive::IsSupportedArchiveRestoreWorkspace<
                  archive::FxArchiveRestoreCandidateDisk32Workspace>);
    static_assert(!std::is_copy_constructible_v<
                  archive::FxArchiveRestoreCandidateDisk32Workspace>);
    static_assert(!std::is_copy_assignable_v<
                  archive::FxArchiveRestoreCandidateDisk32Workspace>);
    static_assert(!std::is_move_constructible_v<
                  archive::FxArchiveRestoreCandidateDisk32Workspace>);
    static_assert(!std::is_move_assignable_v<
                  archive::FxArchiveRestoreCandidateDisk32Workspace>);
    static_assert(std::is_trivially_destructible_v<
                  archive::FxArchiveDisk32ReaderWorkspace>);
    static_assert(std::is_trivially_destructible_v<
                  archive::FxArchiveRestoreCandidateDisk32Workspace>);
    static_assert(alignof(
                      archive::FxArchiveRestoreCandidateDisk32Workspace)
                  == 8);
    static_assert(sizeof(
                      archive::FxArchiveRestoreCandidateDisk32PhysicsBody)
                  == (KISAK_ARCH_64BIT ? 0x90u : 0x80u));
    static_assert(sizeof(
                      archive::FxArchiveRestoreCandidateDisk32ReadyView)
                  == (KISAK_ARCH_64BIT ? 0x28u : 0x14u));
    static_assert(sizeof(
                      archive::FxArchiveRestoreCandidateDisk32Workspace)
                  == (KISAK_ARCH_64BIT ? 0x61DE8u : 0x5BD98u));

    AllocationState allocation{};
    const archive::ArchiveRestoreWorkspaceMemoryCallbacks callbacks{
        &allocation, AllocateWorkspace, FreeWorkspace};
    auto *const workspace = archive::AllocateArchiveRestoreWorkspace<
        archive::FxArchiveRestoreCandidateDisk32Workspace>(callbacks);
    CHECK(workspace != nullptr);
    if (!workspace)
        return;
    CHECK(workspace->phase()
          == archive::FxArchiveRestoreCandidateDisk32Phase::Empty);
    const archive::EffectTableRestoreLease emptyLease{};
    CHECK(archive::TryBuildFxArchiveRestoreCandidateDisk32(
              nullptr, emptyLease, workspace)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidArgument);
    CheckCandidateHidden(workspace);
    CHECK(archive::TryBuildFxArchiveRestoreCandidateDisk32(
              nullptr, emptyLease, nullptr)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidArgument);
    auto output = PoisonCandidateView();
    const auto before = output;
    CHECK(!archive::TryGetFxArchiveRestoreCandidateDisk32ReadyView(
        nullptr, &output));
    CHECK(std::memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(!archive::TryGetFxArchiveRestoreCandidateDisk32ReadyView(
        workspace, nullptr));
    CHECK(archive::DestroyArchiveRestoreWorkspace(workspace, callbacks));
    CHECK(allocation.allocations == 1);
    CHECK(allocation.frees == 1);
}

void TestSuccessfulCopiesAndSelectors()
{
    for (const bool reverse : {false, true})
    {
        for (const std::size_t count : {std::size_t{0}, std::size_t{3}})
        {
            PreparedFixture fixture{count, reverse};
            CHECK(fixture.ready());
            if (fixture.ready())
                CheckReadyCandidate(&fixture, count, reverse);
        }
    }
}

void TestMaximumPhysicsCapacity()
{
    PreparedFixture fixture{archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT, true};
    CHECK(fixture.ready());
    if (!fixture.ready())
        return;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::Success);
    archive::FxArchiveRestoreCandidateDisk32ReadyView candidate{};
    CHECK(archive::TryGetFxArchiveRestoreCandidateDisk32ReadyView(
        fixture.candidate_, &candidate));
    CHECK(candidate.physicsBodyCount
          == archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT);
    if (!candidate.physicsBodies || !candidate.buffers)
        return;
    for (const std::size_t index : {
             std::size_t{0},
             archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT / 2u,
             archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT - 1u})
    {
        CHECK(candidate.physicsBodies[index].elem
              == &candidate.buffers->elems[index].item);
        CHECK(candidate.physicsBodies[index].ownerIndex == index);
        CHECK(candidate.physicsBodies[index].token == index + 1u);
        CHECK(candidate.physicsBodies[index].state.position[0]
              == static_cast<float>(index) + 0.25f);
    }
}

void TestDescriptorBodyAndGraphForgery()
{
    PreparedFixture fixture{3};
    CHECK(fixture.ready());
    if (!fixture.ready() || !fixture.readerView_.physicsBodies)
        return;
    auto *const bodies = const_cast<archive::FxArchiveDisk32ReaderPhysicsBody *>(
        fixture.readerView_.physicsBodies);
    const auto originalBody = bodies[0];

    bodies[0].descriptor.elem = bodies[1].descriptor.elem;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    CheckCandidateHidden(fixture.candidate_);
    bodies[0] = originalBody;

    bodies[0].descriptor.ownerIndex = 1;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    bodies[0] = originalBody;

    ++bodies[0].descriptor.token;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    bodies[0] = originalBody;

    bodies[0].descriptor.model = reinterpret_cast<const XModel *>(
        static_cast<std::uintptr_t>(UINT32_C(0x00444440)));
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    bodies[0] = originalBody;

    bodies[0].state.mass =
        (std::numeric_limits<float>::infinity)();
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    bodies[0] = originalBody;

    ++bodies[0].state.timeLastAsleep;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    bodies[0] = originalBody;

    auto *const metadata = const_cast<archive::FxSystemDisk32Metadata *>(
        fixture.readerView_.graph.metadata);
    const auto originalMetadata = *metadata;
    metadata->writeVisibilitySelector = metadata->readVisibilitySelector;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    *metadata = originalMetadata;

    metadata->activeEffectSlots[0] = 0;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    *metadata = originalMetadata;

    metadata->activeEffectSlots[1] = 2;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    *metadata = originalMetadata;

    auto *const sourceSystem =
        const_cast<FxSystem *>(fixture.readerView_.graph.system);
    FxEffect *const originalEffects = sourceSystem->effects;
    sourceSystem->effects = originalEffects + 1;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    sourceSystem->effects = originalEffects;

    auto *const states = const_cast<
        archive::FxSystemBuffersDisk32PoolStates *>(
        fixture.readerView_.graph.poolStates);
    const bool originalInitialized = states->elems.initialized;
    states->elems.initialized = false;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    states->elems.initialized = originalInitialized;

    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::Success);
}

void TestLeaseBindingReentryAndFailureAtomicity()
{
    PreparedFixture fixture{2};
    CHECK(fixture.ready());
    if (!fixture.ready())
        return;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::Success);

    archive::EffectTableRestoreLease forged = fixture.table_.lease;
    ++forged.serial;
    CHECK(archive::TryBuildFxArchiveRestoreCandidateDisk32(
              fixture.reader_, forged, fixture.candidate_)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidLease);
    CheckCandidateHidden(fixture.candidate_);

    fixture.restore_.reader = fixture.reader_;
    fixture.restore_.candidate = fixture.candidate_;
    fixture.restore_.lease = &fixture.table_.lease;
    fixture.restore_.reenter = true;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::Success);
    CHECK(fixture.restore_.nestedRead
          == archive::FxArchiveDisk32ReaderStatus::Busy);
    CHECK(!fixture.restore_.nestedReaderView);
    CHECK(fixture.restore_.nestedCandidate
          == archive::FxArchiveRestoreCandidateDisk32Status::Busy);
    CHECK(fixture.reader_->phase()
          == archive::FxArchiveDisk32ReaderPhase::Ready);

    auto *const bodies = const_cast<archive::FxArchiveDisk32ReaderPhysicsBody *>(
        fixture.readerView_.physicsBodies);
    const auto original = bodies[0];
    bodies[0].descriptor.token = 0;
    CHECK(BuildCandidate(&fixture)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    CheckCandidateHidden(fixture.candidate_);
    bodies[0] = original;

    CHECK(archive::TryReadFxArchiveDisk32NoReport(
              nullptr, fixture.table_.lease, fixture.reader_)
          == archive::FxArchiveDisk32ReaderStatus::InvalidArgument);
    CHECK(archive::TryBuildFxArchiveRestoreCandidateDisk32(
              fixture.reader_, fixture.table_.lease, fixture.candidate_)
          == archive::FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    CheckCandidateHidden(fixture.candidate_);
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
    TestWorkspaceABIAndArgumentGates();
    TestSuccessfulCopiesAndSelectors();
    TestMaximumPhysicsCapacity();
    TestDescriptorBodyAndGraphForgery();
    TestLeaseBindingReentryAndFailureAtomicity();
    CHECK(unexpectedReports == 0);

    if (archive::EffectTableRestoreLeaseIsActive())
    {
        archive::AbandonCurrentThreadEffectTableRestoreForError();
        CHECK(false);
    }
    if (failures != 0)
    {
        std::fprintf(
            stderr,
            "%d restore candidate Disk32 test(s) failed\n",
            failures);
        return 1;
    }
    std::puts("fx archive restore candidate Disk32 tests passed");
    return 0;
}
