#include <EffectsCore/fx_archive_native_disk32.h>
#include <EffectsCore/fx_archive_restore_workspace.h>
#include <EffectsCore/fx_archive_semantics.h>

#include <array>
#include <bit>
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

// The production target obtains this deterministic table from fx_random.cpp.
// Preserve its prefix through every offset used by the seed-zero semantic
// fixtures; the remaining entries are zero because this isolated executable
// never selects them.
extern const float fx_randomTable[507]{
    0.4300513f,
    0.58586591f,
    0.14015682f,
    0.3638894f,
    0.87767053f,
    0.67589945f,
    0.18348631f,
    0.28799689f,
    0.68363762f,
    0.071270868f,
    0.94988143f,
    0.45510319f,
    0.87240946f,
    0.84151697f,
    0.37590459f,
    0.64859152f,
    0.85434818f,
    0.0097696204f,
    0.49672353f,
    0.43216714f,
    0.43313825f,
    0.23515075f};

namespace
{
namespace archive = fx::archive;

constexpr std::uint32_t BUFFER_BASE = UINT32_C(0x24000000);
constexpr std::uint32_t ELEMS_OFFSET = UINT32_C(0x00020000);
constexpr std::uint32_t TRAILS_OFFSET = UINT32_C(0x00034000);
constexpr std::uint32_t TRAIL_ELEMS_OFFSET = UINT32_C(0x00034400);
constexpr std::uint32_t VIS_STATE_OFFSET = UINT32_C(0x00044400);
constexpr std::uint32_t DEFERRED_ELEMS_OFFSET = UINT32_C(0x00046420);
constexpr std::uint32_t VIS_STATE_STRIDE = UINT32_C(0x00001010);

constexpr std::size_t DISK_EFFECT_HANDLE_STRIDE =
    sizeof(archive::FxEffectDisk32) / FxEffect::HANDLE_SCALE;
constexpr std::size_t NATIVE_EFFECT_HANDLE_STRIDE =
    sizeof(FxEffect) / FxEffect::HANDLE_SCALE;
constexpr std::size_t ELEM_HANDLE_STRIDE =
    sizeof(archive::FxElemDisk32) / FxElem::HANDLE_SCALE;
constexpr std::size_t TRAIL_HANDLE_STRIDE =
    sizeof(archive::FxTrailDisk32) / FxTrail::HANDLE_SCALE;
constexpr std::size_t TRAIL_ELEM_HANDLE_STRIDE =
    sizeof(archive::FxTrailElemDisk32) / FxTrailElem::HANDLE_SCALE;
constexpr std::uint16_t INVALID_HANDLE =
    (std::numeric_limits<std::uint16_t>::max)();
constexpr std::uint32_t SELF_OWNED_STATUS_BIT = UINT32_C(0x10000000);
constexpr std::uint32_t DEFINITION_KEY = UINT32_C(0x00013579);
constexpr std::uint32_t DOBJ_HANDLE_NONE = UINT32_C(4095);
constexpr std::uint32_t BONE_INDEX_NONE = UINT32_C(2047);
constexpr std::uint8_t ELEM_TYPE_SPRITE_BILLBOARD = UINT8_C(0);
constexpr std::uint8_t ELEM_TYPE_TRAIL = UINT8_C(3);
constexpr std::uint8_t ELEM_TYPE_CLOUD = UINT8_C(4);
constexpr std::uint8_t ELEM_TYPE_MODEL = UINT8_C(5);
constexpr std::uint8_t ELEM_TYPE_SPOT_LIGHT = UINT8_C(7);

int failures = 0;

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

constexpr std::uint16_t DiskEffectHandle(const std::size_t index) noexcept
{
    return static_cast<std::uint16_t>(index * DISK_EFFECT_HANDLE_STRIDE);
}

constexpr std::uint16_t NativeEffectHandle(const std::size_t index) noexcept
{
    return static_cast<std::uint16_t>(index * NATIVE_EFFECT_HANDLE_STRIDE);
}

constexpr std::uint16_t ElemHandle(const std::size_t index) noexcept
{
    return static_cast<std::uint16_t>(index * ELEM_HANDLE_STRIDE);
}

constexpr std::uint16_t TrailHandle(const std::size_t index) noexcept
{
    return static_cast<std::uint16_t>(index * TRAIL_HANDLE_STRIDE);
}

constexpr std::uint16_t TrailElemHandle(const std::size_t index) noexcept
{
    return static_cast<std::uint16_t>(index * TRAIL_ELEM_HANDLE_STRIDE);
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

template <typename SLOT>
void CheckFreeLinkDecoderBoundaries(const std::size_t limit)
{
    SLOT slot{};
    for (std::size_t index = sizeof(std::uint32_t);
         index < sizeof(slot.bytes);
         ++index)
    {
        slot.bytes[index] = static_cast<std::uint8_t>(
            (index * 37u + 0x5Du) & 0xFFu);
    }

    std::int32_t output = 73;
    StoreFreeLink(slot.bytes, -1);
    CHECK(archive::TryDecodeFxPoolSlotFreeLinkDisk32(slot, &output));
    CHECK(output == -1);

    output = 73;
    StoreFreeLink(
        slot.bytes, static_cast<std::int32_t>(limit - 1u));
    CHECK(archive::TryDecodeFxPoolSlotFreeLinkDisk32(slot, &output));
    CHECK(output == static_cast<std::int32_t>(limit - 1u));

    output = 73;
    StoreFreeLink(slot.bytes, static_cast<std::int32_t>(limit));
    CHECK(!archive::TryDecodeFxPoolSlotFreeLinkDisk32(slot, &output));
    CHECK(output == 73);

    output = 73;
    StoreFreeLink(
        slot.bytes, (std::numeric_limits<std::int32_t>::min)());
    CHECK(!archive::TryDecodeFxPoolSlotFreeLinkDisk32(slot, &output));
    CHECK(output == 73);

    StoreFreeLink(slot.bytes, 0);
    CHECK(!archive::TryDecodeFxPoolSlotFreeLinkDisk32(slot, nullptr));
}

struct AllocationState final
{
    std::size_t allocateCalls = 0;
    std::size_t freeCalls = 0;
    int requestedByteCount = -1;
};

void *AllocateWorkspace(
    void *const context,
    const int byteCount) noexcept
{
    auto *const state = static_cast<AllocationState *>(context);
    if (!state || byteCount <= 0)
        return nullptr;
    ++state->allocateCalls;
    state->requestedByteCount = byteCount;
    return ::operator new(
        static_cast<std::size_t>(byteCount), std::nothrow);
}

void FreeWorkspace(void *const context, void *const storage) noexcept
{
    auto *const state = static_cast<AllocationState *>(context);
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
                     archive::FxArchiveDisk32NativeWorkspace>(callbacks_))
    {
    }

    ~WorkspaceOwner() noexcept
    {
        CHECK(archive::DestroyArchiveRestoreWorkspace(
            workspace_, callbacks_));
    }

    WorkspaceOwner(const WorkspaceOwner &) = delete;
    WorkspaceOwner &operator=(const WorkspaceOwner &) = delete;

    archive::FxArchiveDisk32NativeWorkspace *get() const noexcept
    {
        return workspace_;
    }

private:
    archive::ArchiveRestoreWorkspaceMemoryCallbacks callbacks_{};
    archive::FxArchiveDisk32NativeWorkspace *workspace_ = nullptr;
};

struct ResolverState final
{
    const FxEffectDef *result = reinterpret_cast<const FxEffectDef *>(
        static_cast<std::uintptr_t>(DEFINITION_KEY));
    std::array<std::uint32_t, 8> observedKeys{};
    std::size_t calls = 0;
    bool accept = true;
};

const FxEffectDef *ResolveDefinition(
    void *const context,
    const archive::EffectDefinitionKey32 key) noexcept
{
    auto *const state = static_cast<ResolverState *>(context);
    if (!state)
        return nullptr;
    if (state->calls < state->observedKeys.size())
        state->observedKeys[state->calls] = key.value;
    ++state->calls;
    return state->accept && key.value == DEFINITION_KEY
        ? state->result
        : nullptr;
}

archive::FxArchiveDisk32Resolver Resolver(
    ResolverState *const state) noexcept
{
    return {state, ResolveDefinition};
}

struct ReentrantResolverState final
{
    const archive::FxSystemDisk32 *system = nullptr;
    const archive::FxSystemBuffersDisk32 *buffers = nullptr;
    archive::FxArchiveDisk32NativeWorkspace *workspace = nullptr;
    const FxEffectDef *result = reinterpret_cast<const FxEffectDef *>(
        static_cast<std::uintptr_t>(DEFINITION_KEY));
    archive::FxArchiveDisk32StructuralStatus nestedStatus =
        archive::FxArchiveDisk32StructuralStatus::Success;
    std::size_t calls = 0;
};

const FxEffectDef *ResolveDefinitionReentrantly(
    void *const context,
    const archive::EffectDefinitionKey32 key) noexcept
{
    auto *const state = static_cast<ReentrantResolverState *>(context);
    if (!state || !state->system || !state->buffers || !state->workspace)
        return nullptr;

    ++state->calls;
    if (state->calls == 1)
    {
        const archive::FxArchiveDisk32Resolver nestedResolver{
            state, ResolveDefinitionReentrantly};
        state->nestedStatus =
            archive::TryBuildFxArchiveDisk32StructuralImage(
                *state->system,
                *state->buffers,
                nestedResolver,
                state->workspace);
    }
    return key.value == DEFINITION_KEY ? state->result : nullptr;
}

struct Fixture final
{
    std::unique_ptr<archive::FxSystemDisk32> system =
        std::make_unique<archive::FxSystemDisk32>();
    std::unique_ptr<archive::FxSystemBuffersDisk32> buffers =
        std::make_unique<archive::FxSystemBuffersDisk32>();

    Fixture()
    {
        CHECK(system != nullptr);
        CHECK(buffers != nullptr);
        if (!system || !buffers)
            return;

        system->effects = archive::ArchiveAddress32{BUFFER_BASE};
        system->elems =
            archive::ArchiveAddress32{BUFFER_BASE + ELEMS_OFFSET};
        system->trails =
            archive::ArchiveAddress32{BUFFER_BASE + TRAILS_OFFSET};
        system->trailElems =
            archive::ArchiveAddress32{BUFFER_BASE + TRAIL_ELEMS_OFFSET};
        system->deferredElems =
            archive::ArchiveAddress32{BUFFER_BASE + DEFERRED_ELEMS_OFFSET};
        system->visState =
            archive::ArchiveAddress32{BUFFER_BASE + VIS_STATE_OFFSET};
        system->visStateBufferRead = system->visState;
        system->visStateBufferWrite = archive::ArchiveAddress32{
            system->visState.value + VIS_STATE_STRIDE};

        system->firstActiveEffect = 0;
        system->firstNewEffect = 0;
        system->firstFreeEffect = 0;
        for (std::size_t index = 0; index < MAX_EFFECTS; ++index)
            system->allEffectHandles[index] = DiskEffectHandle(index);

        system->msecNow = 1000;
        system->msecDraw = -1;
        system->frameCount = 7;
        system->isInitialized = 1;
        system->isArchiving = 1;
        for (std::size_t index = 0; index < 32; ++index)
        {
            system->restartList[index] =
                UINT32_C(0xA5000000)
                + static_cast<std::uint32_t>(index);
        }

        PopulateVisibilityAndDeferred();
        ConfigurePools(0, 0, 0);
    }

    void ConfigureVisibilitySelectors(
        const std::uint8_t readSelector,
        const std::uint8_t writeSelector) noexcept
    {
        CHECK(readSelector < 2);
        CHECK(writeSelector < 2);
        CHECK(readSelector != writeSelector);
        if (readSelector >= 2 || writeSelector >= 2
            || readSelector == writeSelector)
        {
            return;
        }
        system->visStateBufferRead = archive::ArchiveAddress32{
            system->visState.value
            + static_cast<std::uint32_t>(readSelector) * VIS_STATE_STRIDE};
        system->visStateBufferWrite = archive::ArchiveAddress32{
            system->visState.value
            + static_cast<std::uint32_t>(writeSelector) * VIS_STATE_STRIDE};
    }

    void ConfigurePools(
        const std::size_t activeElems,
        const std::size_t activeTrails,
        const std::size_t activeTrailElems) noexcept
    {
        CHECK(activeElems <= MAX_ELEMS);
        CHECK(activeTrails <= MAX_TRAILS);
        CHECK(activeTrailElems <= MAX_TRAIL_ELEMS);
        if (activeElems > MAX_ELEMS || activeTrails > MAX_TRAILS
            || activeTrailElems > MAX_TRAIL_ELEMS)
        {
            return;
        }

        system->activeElemCount = static_cast<std::int32_t>(activeElems);
        system->activeTrailCount = static_cast<std::int32_t>(activeTrails);
        system->activeTrailElemCount =
            static_cast<std::int32_t>(activeTrailElems);
        system->firstFreeElem = activeElems == MAX_ELEMS
            ? -1
            : static_cast<std::int32_t>(activeElems);
        system->firstFreeTrail = activeTrails == MAX_TRAILS
            ? -1
            : static_cast<std::int32_t>(activeTrails);
        system->firstFreeTrailElem = activeTrailElems == MAX_TRAIL_ELEMS
            ? -1
            : static_cast<std::int32_t>(activeTrailElems);

        for (std::size_t index = activeElems; index < MAX_ELEMS; ++index)
        {
            StoreFreeLink(
                buffers->elems[index].bytes,
                index + 1u < MAX_ELEMS
                    ? static_cast<std::int32_t>(index + 1u)
                    : -1);
        }
        for (std::size_t index = activeTrails; index < MAX_TRAILS; ++index)
        {
            StoreFreeLink(
                buffers->trails[index].bytes,
                index + 1u < MAX_TRAILS
                    ? static_cast<std::int32_t>(index + 1u)
                    : -1);
        }
        for (std::size_t index = activeTrailElems;
             index < MAX_TRAIL_ELEMS;
             ++index)
        {
            StoreFreeLink(
                buffers->trailElems[index].bytes,
                index + 1u < MAX_TRAIL_ELEMS
                    ? static_cast<std::int32_t>(index + 1u)
                    : -1);
        }
    }

    void PopulateGraph(
        const std::size_t elemCount,
        const std::size_t trailCount,
        const std::size_t trailElemCount) noexcept
    {
        CHECK(elemCount <= MAX_ELEMS);
        CHECK(trailCount <= MAX_TRAILS);
        CHECK(trailElemCount <= MAX_TRAIL_ELEMS);
        CHECK(trailElemCount == 0 || trailCount != 0);
        if (elemCount > MAX_ELEMS || trailCount > MAX_TRAILS
            || trailElemCount > MAX_TRAIL_ELEMS
            || (trailElemCount != 0 && trailCount == 0))
        {
            return;
        }

        ConfigurePools(elemCount, trailCount, trailElemCount);
        system->firstActiveEffect = 0;
        system->firstNewEffect = 1;
        system->firstFreeEffect = 1;

        archive::FxEffectDisk32 effect{};
        effect.definitionKey = archive::EffectDefinitionKey32{DEFINITION_KEY};
        effect.status = static_cast<std::int32_t>(
            SELF_OWNED_STATUS_BIT
            | static_cast<std::uint32_t>(elemCount + trailElemCount));
        effect.firstElemHandle[0] =
            elemCount == 0 ? INVALID_HANDLE : ElemHandle(0);
        effect.firstElemHandle[1] = INVALID_HANDLE;
        effect.firstElemHandle[2] = INVALID_HANDLE;
        effect.firstSortedElemHandle = elemCount == 0
            ? INVALID_HANDLE
            : ElemHandle(elemCount / 2u);
        effect.firstTrailHandle =
            trailCount == 0 ? INVALID_HANDLE : TrailHandle(0);
        effect.randomSeed = UINT16_C(0x55AA);
        effect.owner = DiskEffectHandle(0);
        effect.packedLighting = UINT16_C(0x7788);
        effect.boltAndSortOrder = UINT32_C(0xD3B55ABC);
        effect.frameCount = 123;
        effect.msecBegin = 700;
        effect.msecLastUpdate = 900;
        effect.frameAtSpawn.quat[0] = 1.0f;
        effect.frameNow.quat[1] = -0.5f;
        effect.framePrev.origin[2] = 42.25f;
        effect.distanceTraveled = 99.5f;
        buffers->effects[0] = effect;

        for (std::size_t index = 0; index < elemCount; ++index)
        {
            archive::FxElemDisk32 elem{};
            elem.defIndex = static_cast<std::uint8_t>(index & 0xFFu);
            elem.sequence = static_cast<std::uint8_t>(
                (index * 3u + 1u) & 0xFFu);
            elem.atRestFraction = UINT8_C(0x7F);
            elem.emitResidual = UINT8_C(0x23);
            elem.nextElemHandleInEffect = index + 1u < elemCount
                ? ElemHandle(index + 1u)
                : INVALID_HANDLE;
            elem.prevElemHandleInEffect = index == 0
                ? INVALID_HANDLE
                : ElemHandle(index - 1u);
            elem.msecBegin = static_cast<std::int32_t>(500u + index);
            elem.baseVel[0] = static_cast<float>(index) + 0.25f;
            elem.baseVel[1] = -2.5f;
            elem.baseVel[2] = 3.75f;
            for (std::size_t byte = 0; byte < sizeof(elem.payload); ++byte)
            {
                elem.payload[byte] = static_cast<std::uint8_t>(
                    index + byte + 0x31u);
            }
            for (std::size_t byte = 0; byte < sizeof(elem.value); ++byte)
            {
                elem.value[byte] = static_cast<std::uint8_t>(
                    index + byte + 0x91u);
            }
            StoreRecord(elem, buffers->elems[index].bytes);
        }

        for (std::size_t index = 0; index < trailCount; ++index)
        {
            archive::FxTrailDisk32 trail{};
            trail.nextTrailHandle = index + 1u < trailCount
                ? TrailHandle(index + 1u)
                : INVALID_HANDLE;
            trail.firstElemHandle = index == 0 && trailElemCount != 0
                ? TrailElemHandle(0)
                : INVALID_HANDLE;
            trail.lastElemHandle = index == 0 && trailElemCount != 0
                ? TrailElemHandle(trailElemCount - 1u)
                : INVALID_HANDLE;
            trail.defIndex = static_cast<std::uint8_t>(index & 0xFFu);
            trail.sequence = static_cast<std::uint8_t>(
                (index * 5u + 7u) & 0xFFu);
            StoreRecord(trail, buffers->trails[index].bytes);
        }

        for (std::size_t index = 0; index < trailElemCount; ++index)
        {
            archive::FxTrailElemDisk32 elem{};
            elem.origin[0] = static_cast<float>(index) + 10.0f;
            elem.origin[1] = -20.0f;
            elem.origin[2] = 30.0f;
            elem.spawnDist = static_cast<float>(index) * 0.5f;
            elem.msecBegin = static_cast<std::int32_t>(600u + index);
            elem.nextTrailElemHandle = index + 1u < trailElemCount
                ? TrailElemHandle(index + 1u)
                : INVALID_HANDLE;
            elem.baseVelZ = static_cast<std::int16_t>(
                static_cast<std::int32_t>(index % 400u) - 200);
            elem.basis[0][0] = 1;
            elem.basis[1][1] = -1;
            elem.sequence = static_cast<std::uint8_t>(index & 0xFFu);
            elem.unused = UINT8_C(0xA5);
            StoreRecord(elem, buffers->trailElems[index].bytes);
        }
    }

private:
    void PopulateVisibilityAndDeferred() noexcept
    {
        for (std::size_t selector = 0; selector < 2; ++selector)
        {
            archive::FxVisStateDisk32 &state = buffers->visState[selector];
            state.blockerCount = static_cast<std::int32_t>(selector + 1u);
            for (std::size_t blocker = 0; blocker <= selector; ++blocker)
            {
                state.blocker[blocker].origin[0] =
                    static_cast<float>(selector * 10u + blocker) + 0.25f;
                state.blocker[blocker].origin[1] = -2.5f;
                state.blocker[blocker].origin[2] = 3.75f;
                state.blocker[blocker].radius = static_cast<std::uint16_t>(
                    100u + selector * 10u + blocker);
                state.blocker[blocker].visibility =
                    static_cast<std::uint16_t>(
                        200u + selector * 10u + blocker);
            }
            for (std::size_t index = 0; index < 3; ++index)
            {
                state.pad[index] = UINT32_C(0xA0B00000)
                    + static_cast<std::uint32_t>(selector * 16u + index);
            }
        }
        for (std::size_t index = 0; index < MAX_ELEMS; ++index)
        {
            buffers->deferredElems[index] = static_cast<std::uint16_t>(
                (index * 13u + 7u) & UINT16_C(0xFFFF));
        }
        for (std::size_t index = 0; index < sizeof(buffers->padBuffer); ++index)
        {
            buffers->padBuffer[index] = static_cast<std::uint8_t>(
                (index * 29u + 0x5Du) & 0xFFu);
        }
    }
};

struct alignas(FX_ELEM_DEF_RUNTIME_ALIGNMENT) SemanticElemDefinition final
{
    std::array<std::uint8_t, archive::layout::ELEM_DEF_STRIDE> bytes{};
};
static_assert(archive::layout::ELEM_DEF_STRIDE
              == FX_ELEM_DEF_RUNTIME_SIZE);
static_assert(sizeof(SemanticElemDefinition)
              == archive::layout::ELEM_DEF_STRIDE);
static_assert(alignof(SemanticElemDefinition)
              == FX_ELEM_DEF_RUNTIME_ALIGNMENT);

template <typename VALUE>
void StoreSemanticDefinitionField(
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
    std::array<SemanticElemDefinition, 3> elemDefs{};
    FxEffectDef effect{};

    void Configure(const std::size_t count) noexcept
    {
        CHECK(count <= elemDefs.size());
        if (count > elemDefs.size())
            return;
        effect = {};
        effect.msecLoopingLife = 0;
        effect.elemDefCountOneShot = static_cast<std::int32_t>(count);
        effect.elemDefs = reinterpret_cast<const FxElemDef *>(
            elemDefs.data());
        for (std::size_t index = 0; index < count; ++index)
        {
            elemDefs[index] = {};
            const std::int32_t oneShotCount = 1;
            const std::int32_t lifespan = 1000;
            StoreSemanticDefinitionField(
                &elemDefs[index],
                archive::layout::ELEM_DEF_SPAWN_OFFSET,
                oneShotCount);
            StoreSemanticDefinitionField(
                &elemDefs[index],
                archive::layout::ELEM_DEF_LIFE_SPAN_OFFSET,
                lifespan);
        }
    }

    void SetType(
        const std::size_t index,
        const std::uint8_t type) noexcept
    {
        CHECK(index < elemDefs.size());
        if (index < elemDefs.size())
        {
            StoreSemanticDefinitionField(
                &elemDefs[index],
                archive::layout::ELEM_DEF_ELEM_TYPE_OFFSET,
                type);
        }
    }

    void SetPhysicsModel(
        const std::size_t index,
        const std::uintptr_t model) noexcept
    {
        CHECK(index < elemDefs.size());
        if (index >= elemDefs.size())
            return;
        const std::int32_t flags = 0x08000000;
        const std::uint8_t visualCount = 1;
        StoreSemanticDefinitionField(
            &elemDefs[index],
            archive::layout::ELEM_DEF_FLAGS_OFFSET,
            flags);
        SetType(index, ELEM_TYPE_MODEL);
        StoreSemanticDefinitionField(
            &elemDefs[index],
            archive::layout::ELEM_DEF_VISUAL_COUNT_OFFSET,
            visualCount);
        StoreSemanticDefinitionField(
            &elemDefs[index],
            archive::layout::ELEM_DEF_VISUALS_OFFSET,
            model);
    }

    void SetPhysicsModels(
        const std::size_t index,
        const XModel *const *const models,
        const std::uint8_t modelCount) noexcept
    {
        CHECK(index < elemDefs.size());
        CHECK(models != nullptr);
        CHECK(modelCount > 1);
        if (index >= elemDefs.size() || !models || modelCount <= 1)
            return;
        const std::int32_t flags = 0x08000000;
        const void *const modelStorage = models;
        StoreSemanticDefinitionField(
            &elemDefs[index],
            archive::layout::ELEM_DEF_FLAGS_OFFSET,
            flags);
        SetType(index, ELEM_TYPE_MODEL);
        StoreSemanticDefinitionField(
            &elemDefs[index],
            archive::layout::ELEM_DEF_VISUAL_COUNT_OFFSET,
            modelCount);
        StoreSemanticDefinitionField(
            &elemDefs[index],
            archive::layout::ELEM_DEF_VISUALS_OFFSET,
            modelStorage);
    }

    void SetTrail(
        const std::size_t index,
        const std::uintptr_t trailDefinition) noexcept
    {
        CHECK(index < elemDefs.size());
        if (index >= elemDefs.size())
            return;
        SetType(index, ELEM_TYPE_TRAIL);
        StoreSemanticDefinitionField(
            &elemDefs[index],
            archive::layout::ELEM_DEF_TRAIL_DEF_OFFSET,
            trailDefinition);
    }
};

struct SemanticTrailDefinitionHeader final
{
    std::int32_t scrollTimeMsec = 0;
    std::int32_t repeatDist = 1;
    std::int32_t splitDist = 1;
};
static_assert(sizeof(SemanticTrailDefinitionHeader) == 12);

struct SemanticCallbackProbe final
{
    std::uint32_t expectedToken = 0;
    std::size_t prepareCalls = 0;
    std::size_t sinkCalls = 0;
    bool valid = true;
    bool mutatePayload = false;
};

bool PrepareSemanticPayloadProbe(
    void *const context,
    FxSystem *const system,
    FxEffect *const effect,
    FxElem *const elem,
    const FxElemDef *const elemDef,
    const archive::FxArchiveElemPayloadKind payloadKind) noexcept
{
    auto *const probe = static_cast<SemanticCallbackProbe *>(context);
    if (!probe)
        return false;
    ++probe->prepareCalls;
    probe->valid = probe->valid && system && effect && elem && elemDef
        && payloadKind
            == archive::FxArchiveElemPayloadKind::PhysicsLighting;
    if (probe->valid && probe->mutatePayload)
    {
        const std::uint32_t changedToken = probe->expectedToken ^ 1u;
        auto *const elemBytes = reinterpret_cast<std::uint8_t *>(elem);
        std::memcpy(
            elemBytes + offsetof(FxElem, physObjId),
            &changedToken,
            sizeof(changedToken));
    }
    return probe->valid;
}

bool AcceptSemanticPhysicsProbe(
    void *const context,
    const archive::FxArchiveSemanticPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    auto *const probe = static_cast<SemanticCallbackProbe *>(context);
    if (!probe)
        return false;
    ++probe->sinkCalls;
    probe->valid = probe->valid && physicsIndex == 0
        && descriptor.elem != nullptr && descriptor.model != nullptr
        && descriptor.ownerIndex == 0
        && descriptor.token == probe->expectedToken;
    return probe->valid;
}

struct SemanticPhysicsSequenceProbe final
{
    std::array<std::uint32_t, 2> observedTokens{};
    std::array<const XModel *, 2> observedModels{};
    std::array<std::size_t, 2> observedOwnerIndices{};
    std::size_t sinkCalls = 0;
    bool valid = true;
};

bool AcceptSemanticPhysicsSequenceProbe(
    void *const context,
    const archive::FxArchiveSemanticPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    auto *const probe = static_cast<SemanticPhysicsSequenceProbe *>(context);
    if (!probe || physicsIndex >= probe->observedTokens.size())
        return false;
    probe->valid = probe->valid
        && physicsIndex == probe->sinkCalls
        && descriptor.elem != nullptr;
    probe->observedTokens[physicsIndex] = descriptor.token;
    probe->observedModels[physicsIndex] = descriptor.model;
    probe->observedOwnerIndices[physicsIndex] = descriptor.ownerIndex;
    ++probe->sinkCalls;
    return probe->valid;
}

void MakeEffectRuntimeSemanticallyValid(Fixture *const fixture) noexcept
{
    CHECK(fixture != nullptr);
    if (!fixture)
        return;
    archive::FxEffectDisk32 &effect = fixture->buffers->effects[0];
    effect.randomSeed = 0;
    effect.boltAndSortOrder =
        DOBJ_HANDLE_NONE | (BONE_INDEX_NONE << 13u);
    effect.msecBegin = 700;
    effect.msecLastUpdate = 900;
    effect.frameAtSpawn = {};
    effect.frameNow = {};
    effect.framePrev = {};
    effect.frameAtSpawn.quat[0] = 1.0f;
    effect.frameNow.quat[0] = 1.0f;
    effect.framePrev.quat[0] = 1.0f;
    effect.distanceTraveled = 0.0f;
}

archive::FxElemDisk32 LoadElemDisk32(
    const Fixture &fixture,
    const std::size_t index) noexcept
{
    archive::FxElemDisk32 elem{};
    CHECK(index < MAX_ELEMS);
    if (index < MAX_ELEMS)
    {
        std::memcpy(
            &elem,
            fixture.buffers->elems[index].bytes,
            sizeof(elem));
    }
    return elem;
}

void StoreElemDisk32(
    Fixture *const fixture,
    const std::size_t index,
    const archive::FxElemDisk32 &elem) noexcept
{
    CHECK(fixture != nullptr);
    CHECK(index < MAX_ELEMS);
    if (fixture && index < MAX_ELEMS)
        StoreRecord(elem, fixture->buffers->elems[index].bytes);
}

template <typename SLOT, std::size_t LIMIT>
std::int32_t ConfigureSparseFreeChain(
    SLOT (&slots)[LIMIT],
    const std::size_t firstAllocated,
    const std::size_t secondAllocated) noexcept
{
    std::int32_t firstFree = -1;
    std::int32_t previousFree = -1;
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        if (index == firstAllocated || index == secondAllocated)
            continue;

        const auto freeIndex = static_cast<std::int32_t>(index);
        if (firstFree == -1)
            firstFree = freeIndex;
        if (previousFree != -1)
        {
            StoreFreeLink(
                slots[static_cast<std::size_t>(previousFree)].bytes,
                freeIndex);
        }
        previousFree = freeIndex;
    }
    CHECK(previousFree != -1);
    if (previousFree != -1)
    {
        StoreFreeLink(
            slots[static_cast<std::size_t>(previousFree)].bytes,
            -1);
    }
    return firstFree;
}

void PopulateSparseGraph(
    Fixture *const fixture,
    const std::size_t firstAllocated,
    const std::size_t secondAllocated) noexcept
{
    CHECK(fixture != nullptr);
    CHECK(firstAllocated < secondAllocated);
    CHECK(secondAllocated < MAX_TRAILS);
    if (!fixture || firstAllocated >= secondAllocated
        || secondAllocated >= MAX_TRAILS)
    {
        return;
    }

    fixture->system->activeElemCount = 2;
    fixture->system->activeTrailCount = 2;
    fixture->system->activeTrailElemCount = 2;
    fixture->system->firstFreeElem = ConfigureSparseFreeChain(
        fixture->buffers->elems, firstAllocated, secondAllocated);
    fixture->system->firstFreeTrail = ConfigureSparseFreeChain(
        fixture->buffers->trails, firstAllocated, secondAllocated);
    fixture->system->firstFreeTrailElem = ConfigureSparseFreeChain(
        fixture->buffers->trailElems,
        firstAllocated,
        secondAllocated);
    fixture->system->firstActiveEffect = 0;
    fixture->system->firstNewEffect = 1;
    fixture->system->firstFreeEffect = 1;

    archive::FxEffectDisk32 effect{};
    effect.definitionKey = archive::EffectDefinitionKey32{DEFINITION_KEY};
    effect.status = static_cast<std::int32_t>(
        SELF_OWNED_STATUS_BIT | UINT32_C(4));
    effect.firstElemHandle[0] = ElemHandle(firstAllocated);
    effect.firstElemHandle[1] = INVALID_HANDLE;
    effect.firstElemHandle[2] = INVALID_HANDLE;
    effect.firstSortedElemHandle = ElemHandle(secondAllocated);
    effect.firstTrailHandle = TrailHandle(firstAllocated);
    effect.randomSeed = UINT16_C(0xCAFE);
    effect.owner = DiskEffectHandle(0);
    fixture->buffers->effects[0] = effect;

    archive::FxElemDisk32 firstElem{};
    firstElem.defIndex = UINT8_C(0x21);
    firstElem.sequence = UINT8_C(0x31);
    firstElem.nextElemHandleInEffect = ElemHandle(secondAllocated);
    firstElem.prevElemHandleInEffect = INVALID_HANDLE;
    firstElem.msecBegin = 111;
    firstElem.baseVel[0] = 1.25f;
    firstElem.payload[0] = UINT8_C(0xA1);
    firstElem.value[0] = UINT8_C(0xB1);
    StoreRecord(
        firstElem, fixture->buffers->elems[firstAllocated].bytes);

    archive::FxElemDisk32 secondElem{};
    secondElem.defIndex = UINT8_C(0x22);
    secondElem.sequence = UINT8_C(0x32);
    secondElem.nextElemHandleInEffect = INVALID_HANDLE;
    secondElem.prevElemHandleInEffect = ElemHandle(firstAllocated);
    secondElem.msecBegin = 222;
    secondElem.baseVel[0] = 2.5f;
    secondElem.payload[0] = UINT8_C(0xA2);
    secondElem.value[0] = UINT8_C(0xB2);
    StoreRecord(
        secondElem, fixture->buffers->elems[secondAllocated].bytes);

    archive::FxTrailDisk32 firstTrail{};
    firstTrail.nextTrailHandle = TrailHandle(secondAllocated);
    firstTrail.firstElemHandle = TrailElemHandle(firstAllocated);
    firstTrail.lastElemHandle = TrailElemHandle(secondAllocated);
    firstTrail.defIndex = UINT8_C(0x41);
    firstTrail.sequence = UINT8_C(0x51);
    StoreRecord(
        firstTrail, fixture->buffers->trails[firstAllocated].bytes);

    archive::FxTrailDisk32 secondTrail{};
    secondTrail.nextTrailHandle = INVALID_HANDLE;
    secondTrail.firstElemHandle = INVALID_HANDLE;
    secondTrail.lastElemHandle = INVALID_HANDLE;
    secondTrail.defIndex = UINT8_C(0x42);
    secondTrail.sequence = UINT8_C(0x52);
    StoreRecord(
        secondTrail, fixture->buffers->trails[secondAllocated].bytes);

    archive::FxTrailElemDisk32 firstTrailElem{};
    firstTrailElem.origin[0] = 10.25f;
    firstTrailElem.spawnDist = 1.5f;
    firstTrailElem.msecBegin = 333;
    firstTrailElem.nextTrailElemHandle =
        TrailElemHandle(secondAllocated);
    firstTrailElem.baseVelZ = 17;
    firstTrailElem.sequence = UINT8_C(0x61);
    StoreRecord(
        firstTrailElem,
        fixture->buffers->trailElems[firstAllocated].bytes);

    archive::FxTrailElemDisk32 secondTrailElem{};
    secondTrailElem.origin[0] = 20.5f;
    secondTrailElem.spawnDist = 2.75f;
    secondTrailElem.msecBegin = 444;
    secondTrailElem.nextTrailElemHandle = INVALID_HANDLE;
    secondTrailElem.baseVelZ = -23;
    secondTrailElem.sequence = UINT8_C(0x62);
    StoreRecord(
        secondTrailElem,
        fixture->buffers->trailElems[secondAllocated].bytes);
}

archive::FxArchiveDisk32StructuralStatus Build(
    Fixture &fixture,
    ResolverState &resolver,
    archive::FxArchiveDisk32NativeWorkspace *const workspace) noexcept
{
    return archive::TryBuildFxArchiveDisk32StructuralImage(
        *fixture.system, *fixture.buffers, Resolver(&resolver), workspace);
}

archive::FxArchiveDisk32StructuralView GetView(
    const archive::FxArchiveDisk32NativeWorkspace *const workspace)
{
    archive::FxArchiveDisk32StructuralView view{};
    CHECK(archive::TryGetFxArchiveDisk32StructuralView(workspace, &view));
    CHECK(view.system != nullptr);
    CHECK(view.buffers != nullptr);
    CHECK(view.poolStates != nullptr);
    CHECK(view.metadata != nullptr);
    return view;
}

template <std::size_t LIMIT>
void CheckAllocatedPrefix(
    const FxPoolAllocationState<LIMIT> &state,
    const std::size_t allocatedCount)
{
    CHECK(state.initialized);
    CHECK(state.allocatedCount == allocatedCount);
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        CHECK(FxPoolAllocationStateIsAllocated(state, index)
              == (index < allocatedCount));
    }
}

template <std::size_t LIMIT>
void CheckAllocatedPair(
    const FxPoolAllocationState<LIMIT> &state,
    const std::size_t firstAllocated,
    const std::size_t secondAllocated)
{
    CHECK(state.initialized);
    CHECK(state.allocatedCount == 2);
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        CHECK(FxPoolAllocationStateIsAllocated(state, index)
              == (index == firstAllocated
                  || index == secondAllocated));
    }
}

void CheckLinksAndSelectors(
    const archive::FxArchiveDisk32StructuralView &view,
    const std::uint8_t readSelector,
    const std::uint8_t writeSelector)
{
    CHECK(view.system->effects == view.buffers->effects);
    CHECK(view.system->elems == view.buffers->elems);
    CHECK(view.system->trails == view.buffers->trails);
    CHECK(view.system->trailElems == view.buffers->trailElems);
    CHECK(view.system->deferredElems == view.buffers->deferredElems);
    CHECK(view.system->visState == view.buffers->visState);
    CHECK(view.system->visStateBufferRead
          == &view.buffers->visState[readSelector]);
    CHECK(view.system->visStateBufferWrite
          == &view.buffers->visState[writeSelector]);
    CHECK(view.metadata->readVisibilitySelector == readSelector);
    CHECK(view.metadata->writeVisibilitySelector == writeSelector);
}

void TestWorkspaceContractAndEmptyGate()
{
    static_assert(archive::IsSupportedArchiveRestoreWorkspace<
                  archive::FxArchiveDisk32NativeWorkspace>);
    static_assert(std::is_nothrow_default_constructible_v<
                  archive::FxArchiveDisk32NativeWorkspace>);
    static_assert(std::is_nothrow_destructible_v<
                  archive::FxArchiveDisk32NativeWorkspace>);
    static_assert(!std::is_copy_constructible_v<
                  archive::FxArchiveDisk32NativeWorkspace>);
    static_assert(!std::is_copy_assignable_v<
                  archive::FxArchiveDisk32NativeWorkspace>);
    static_assert(
        sizeof(archive::FxArchiveDisk32NativeWorkspace)
        == (KISAK_PTR_BITS == 32 ? 310672u : 327128u));
    static_assert(
        alignof(archive::FxArchiveDisk32NativeWorkspace)
        == 8u);
    static_assert(noexcept(std::declval<
                           const archive::FxArchiveDisk32NativeWorkspace &>()
                               .phase()));

    int expectedByteCount = -1;
    CHECK(archive::TryGetArchiveRestoreWorkspaceAllocationSize<
          archive::FxArchiveDisk32NativeWorkspace>(&expectedByteCount));
    CHECK(expectedByteCount
          == static_cast<int>(
              sizeof(archive::FxArchiveDisk32NativeWorkspace)));

    AllocationState allocation{};
    {
        WorkspaceOwner owner{&allocation};
        CHECK(owner.get() != nullptr);
        if (!owner.get())
            return;
        CHECK(allocation.allocateCalls == 1);
        CHECK(allocation.requestedByteCount == expectedByteCount);
        CHECK(owner.get()->phase()
              == archive::FxArchiveDisk32WorkspacePhase::Empty);

        const auto poison = reinterpret_cast<const void *>(
            static_cast<std::uintptr_t>(UINT32_C(0x13579)));
        archive::FxArchiveDisk32StructuralView view{
            static_cast<const FxSystem *>(poison),
            static_cast<const FxSystemBuffers *>(poison),
            static_cast<const archive::FxSystemBuffersDisk32PoolStates *>(
                poison),
            static_cast<const archive::FxSystemDisk32Metadata *>(poison)};
        const auto before = view;
        CHECK(!archive::TryGetFxArchiveDisk32StructuralView(
            owner.get(), &view));
        CHECK(std::memcmp(&view, &before, sizeof(view)) == 0);
        CHECK(!archive::TryGetFxArchiveDisk32StructuralView(nullptr, &view));
        CHECK(std::memcmp(&view, &before, sizeof(view)) == 0);
        CHECK(!archive::TryGetFxArchiveDisk32StructuralView(
            owner.get(), nullptr));
    }
    CHECK(allocation.freeCalls == 1);
}

void TestFreeLinkDecoderOverloads()
{
    CheckFreeLinkDecoderBoundaries<archive::FxElemPoolSlotDisk32>(
        MAX_ELEMS);
    CheckFreeLinkDecoderBoundaries<archive::FxTrailPoolSlotDisk32>(
        MAX_TRAILS);
    CheckFreeLinkDecoderBoundaries<archive::FxTrailElemPoolSlotDisk32>(
        MAX_TRAIL_ELEMS);
}

void TestEmptyImageAndExactRelinking()
{
    Fixture fixture{};
    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    const std::vector<std::uint8_t> sourceBefore(
        reinterpret_cast<const std::uint8_t *>(fixture.buffers.get()),
        reinterpret_cast<const std::uint8_t *>(fixture.buffers.get())
            + sizeof(*fixture.buffers));
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::StructurallyValid);
    CHECK(owner.get()->phase()
          != archive::FxArchiveDisk32WorkspacePhase::Ready);
    CHECK(resolver.calls == 0);
    CHECK(std::memcmp(
              fixture.buffers.get(),
              sourceBefore.data(),
              sourceBefore.size())
          == 0);

    const auto view = GetView(owner.get());
    CheckLinksAndSelectors(view, 0, 1);
    CHECK(view.system->firstFreeElem == 0);
    CHECK(view.system->firstFreeTrail == 0);
    CHECK(view.system->firstFreeTrailElem == 0);
    CHECK(view.system->firstActiveEffect == 0);
    CHECK(view.system->firstFreeEffect == 0);
    CheckAllocatedPrefix(view.poolStates->elems, 0);
    CheckAllocatedPrefix(view.poolStates->trails, 0);
    CheckAllocatedPrefix(view.poolStates->trailElems, 0);
    CHECK(view.buffers->elems[0].nextFree == 1);
    CHECK(view.buffers->elems[MAX_ELEMS - 1u].nextFree == -1);
    CHECK(view.buffers->trails[0].nextFree == 1);
    CHECK(view.buffers->trails[MAX_TRAILS - 1u].nextFree == -1);
    CHECK(view.buffers->trailElems[0].nextFree == 1);
    CHECK(view.buffers->trailElems[MAX_TRAIL_ELEMS - 1u].nextFree == -1);
    for (std::size_t index = 0; index < MAX_EFFECTS; ++index)
    {
        CHECK(view.buffers->effects[index].def == nullptr);
        CHECK(view.metadata->activeEffectSlots[index] == 0);
        CHECK(view.system->allEffectHandles[index]
              == NativeEffectHandle(index));
    }
}

void TestMixedImageOpaqueResolutionAndCopies()
{
    Fixture fixture{};
    fixture.ConfigureVisibilitySelectors(1, 0);
    fixture.PopulateGraph(3, 2, 4);
    for (std::size_t index = 1; index < MAX_EFFECTS; ++index)
    {
        fixture.buffers->effects[index].definitionKey =
            archive::EffectDefinitionKey32{
                UINT32_C(0x80000000)
                + static_cast<std::uint32_t>(index)};
    }
    for (std::size_t byte = sizeof(std::uint32_t);
         byte < sizeof(fixture.buffers->elems[3].bytes);
         ++byte)
    {
        fixture.buffers->elems[3].bytes[byte] =
            static_cast<std::uint8_t>((byte * 11u + 0x31u) & 0xFFu);
    }
    for (std::size_t byte = sizeof(std::uint32_t);
         byte < sizeof(fixture.buffers->trails[2].bytes);
         ++byte)
    {
        fixture.buffers->trails[2].bytes[byte] =
            static_cast<std::uint8_t>((byte * 13u + 0x53u) & 0xFFu);
    }
    for (std::size_t byte = sizeof(std::uint32_t);
         byte < sizeof(fixture.buffers->trailElems[4].bytes);
         ++byte)
    {
        fixture.buffers->trailElems[4].bytes[byte] =
            static_cast<std::uint8_t>((byte * 17u + 0x79u) & 0xFFu);
    }

    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(resolver.calls == 1);
    CHECK(resolver.observedKeys[0] == DEFINITION_KEY);

    const auto view = GetView(owner.get());
    CheckLinksAndSelectors(view, 1, 0);
    CheckAllocatedPrefix(view.poolStates->elems, 3);
    CheckAllocatedPrefix(view.poolStates->trails, 2);
    CheckAllocatedPrefix(view.poolStates->trailElems, 4);
    CHECK(view.metadata->activeEffectSlots[0] == 1);
    for (std::size_t index = 1; index < MAX_EFFECTS; ++index)
    {
        CHECK(view.metadata->activeEffectSlots[index] == 0);
        CHECK(view.buffers->effects[index].def == nullptr);
    }

    const FxEffect &effect = view.buffers->effects[0];
    CHECK(effect.def == resolver.result);
    CHECK(effect.status
          == static_cast<std::int32_t>(SELF_OWNED_STATUS_BIT | 7u));
    CHECK(effect.firstElemHandle[0] == ElemHandle(0));
    CHECK(effect.firstElemHandle[1] == INVALID_HANDLE);
    CHECK(effect.firstSortedElemHandle == ElemHandle(1));
    CHECK(effect.firstTrailHandle == TrailHandle(0));
    CHECK(effect.owner == NativeEffectHandle(0));
    CHECK(effect.randomSeed == UINT16_C(0x55AA));
    CHECK(effect.packedLighting == UINT16_C(0x7788));
    CHECK(effect.frameCount == 123);
    CHECK(effect.msecBegin == 700);
    CHECK(effect.msecLastUpdate == 900);
    CHECK(effect.frameAtSpawn.quat[0] == 1.0f);
    CHECK(effect.frameNow.quat[1] == -0.5f);
    CHECK(effect.framePrev.origin[2] == 42.25f);
    CHECK(effect.distanceTraveled == 99.5f);

    CHECK(view.buffers->elems[0].item.prevElemHandleInEffect
          == INVALID_HANDLE);
    CHECK(view.buffers->elems[0].item.nextElemHandleInEffect
          == ElemHandle(1));
    CHECK(view.buffers->elems[1].item.prevElemHandleInEffect
          == ElemHandle(0));
    CHECK(view.buffers->elems[2].item.nextElemHandleInEffect
          == INVALID_HANDLE);
    CHECK(view.buffers->elems[3].nextFree == 4);
    CHECK(std::memcmp(
              std::addressof(view.buffers->elems[3]),
              fixture.buffers->elems[3].bytes,
              sizeof(fixture.buffers->elems[3]))
          == 0);
    CHECK(view.buffers->trails[0].item.nextTrailHandle
          == TrailHandle(1));
    CHECK(view.buffers->trails[0].item.firstElemHandle
          == TrailElemHandle(0));
    CHECK(view.buffers->trails[0].item.lastElemHandle
          == TrailElemHandle(3));
    CHECK(view.buffers->trails[1].item.firstElemHandle
          == INVALID_HANDLE);
    CHECK(view.buffers->trails[2].nextFree == 3);
    CHECK(std::memcmp(
              std::addressof(view.buffers->trails[2]),
              fixture.buffers->trails[2].bytes,
              sizeof(fixture.buffers->trails[2]))
          == 0);
    CHECK(view.buffers->trailElems[0].item.nextTrailElemHandle
          == TrailElemHandle(1));
    CHECK(view.buffers->trailElems[3].item.nextTrailElemHandle
          == INVALID_HANDLE);
    CHECK(view.buffers->trailElems[4].nextFree == 5);
    CHECK(std::memcmp(
              std::addressof(view.buffers->trailElems[4]),
              fixture.buffers->trailElems[4].bytes,
              sizeof(fixture.buffers->trailElems[4]))
          == 0);

    CHECK(std::memcmp(
              view.buffers->visState,
              fixture.buffers->visState,
              sizeof(fixture.buffers->visState))
          == 0);
    CHECK(std::memcmp(
              view.buffers->deferredElems,
              fixture.buffers->deferredElems,
              sizeof(fixture.buffers->deferredElems))
          == 0);
    CHECK(std::memcmp(
              view.buffers->padBuffer,
              fixture.buffers->padBuffer,
              sizeof(fixture.buffers->padBuffer))
          == 0);
}

void TestMaximumCapacityAndConditionalX86Oracle()
{
    Fixture fixture{};
    fixture.PopulateGraph(MAX_ELEMS, MAX_TRAILS, MAX_TRAIL_ELEMS);
    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    const auto view = GetView(owner.get());
    CHECK(resolver.calls == 1);
    CHECK(view.system->firstFreeElem == -1);
    CHECK(view.system->firstFreeTrail == -1);
    CHECK(view.system->firstFreeTrailElem == -1);
    CheckAllocatedPrefix(view.poolStates->elems, MAX_ELEMS);
    CheckAllocatedPrefix(view.poolStates->trails, MAX_TRAILS);
    CheckAllocatedPrefix(view.poolStates->trailElems, MAX_TRAIL_ELEMS);
    CHECK(view.buffers->effects[0].status
          == static_cast<std::int32_t>(
              SELF_OWNED_STATUS_BIT
              | static_cast<std::uint32_t>(
                  MAX_ELEMS + MAX_TRAIL_ELEMS)));
    CHECK(view.buffers->elems[MAX_ELEMS - 1u]
              .item.nextElemHandleInEffect
          == INVALID_HANDLE);
    CHECK(view.buffers->trails[MAX_TRAILS - 1u].item.nextTrailHandle
          == INVALID_HANDLE);
    CHECK(view.buffers->trailElems[MAX_TRAIL_ELEMS - 1u]
              .item.nextTrailElemHandle
          == INVALID_HANDLE);

#if KISAK_PTR_BITS == 32
    static_assert(sizeof(FxSystemBuffers)
                  == sizeof(archive::FxSystemBuffersDisk32));
    CHECK(std::memcmp(
              view.buffers,
              fixture.buffers.get(),
              sizeof(*fixture.buffers))
          == 0);
#endif
}

void CheckViewIsGated(
    archive::FxArchiveDisk32NativeWorkspace *const workspace)
{
    CHECK(workspace != nullptr);
    if (!workspace)
        return;
    CHECK(workspace->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Empty);
    archive::FxArchiveDisk32StructuralView view{};
    CHECK(!archive::TryGetFxArchiveDisk32StructuralView(workspace, &view));
}

void TestFailureStatusAndTransactionalGating()
{
    Fixture fixture{};
    fixture.PopulateGraph(1, 1, 1);
    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::StructurallyValid);

    const archive::FxArchiveDisk32Resolver nullResolver{};
    CHECK(archive::TryBuildFxArchiveDisk32StructuralImage(
              *fixture.system,
              *fixture.buffers,
              nullResolver,
              owner.get())
          == archive::FxArchiveDisk32StructuralStatus::InvalidArgument);
    CheckViewIsGated(owner.get());
    CHECK(archive::TryBuildFxArchiveDisk32StructuralImage(
              *fixture.system,
              *fixture.buffers,
              Resolver(&resolver),
              nullptr)
          == archive::FxArchiveDisk32StructuralStatus::InvalidArgument);

    const std::uint8_t savedInitialized = fixture.system->isInitialized;
    fixture.system->isInitialized = 0;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::InvalidSystem);
    CheckViewIsGated(owner.get());
    fixture.system->isInitialized = savedInitialized;

    const auto savedReadVisibility =
        fixture.system->visStateBufferRead;
    fixture.system->visStateBufferRead = archive::ArchiveAddress32{
        fixture.system->visState.value + 2u * VIS_STATE_STRIDE};
    const std::size_t callsBeforeInvalidSelector = resolver.calls;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::InvalidSystem);
    CHECK(resolver.calls == callsBeforeInvalidSelector);
    CheckViewIsGated(owner.get());
    fixture.system->visStateBufferRead = savedReadVisibility;

    const std::int32_t savedFirstFree = fixture.system->firstFreeElem;
    const auto savedFreeSlot = fixture.buffers->elems[1];
    fixture.system->firstFreeElem = 1;
    StoreFreeLink(
        fixture.buffers->elems[1].bytes,
        static_cast<std::int32_t>(MAX_ELEMS));
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::InvalidFreeLists);
    CheckViewIsGated(owner.get());
    fixture.system->firstFreeElem = savedFirstFree;
    fixture.buffers->elems[1] = savedFreeSlot;

    resolver.accept = false;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::DefinitionNotFound);
    CheckViewIsGated(owner.get());
    resolver.accept = true;

    const std::uint16_t savedOwner = fixture.buffers->effects[0].owner;
    fixture.buffers->effects[0].owner = 1;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::InvalidEffect);
    CheckViewIsGated(owner.get());
    fixture.buffers->effects[0].owner = savedOwner;

    archive::FxElemDisk32 malformedElem{};
    std::memcpy(
        &malformedElem,
        fixture.buffers->elems[0].bytes,
        sizeof(malformedElem));
    const std::uint16_t savedPrevious =
        malformedElem.prevElemHandleInEffect;
    malformedElem.prevElemHandleInEffect = ElemHandle(0);
    StoreRecord(malformedElem, fixture.buffers->elems[0].bytes);
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::InvalidGraph);
    CheckViewIsGated(owner.get());
    malformedElem.prevElemHandleInEffect = savedPrevious;
    StoreRecord(malformedElem, fixture.buffers->elems[0].bytes);

    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::StructurallyValid);
    CHECK(archive::TryGetFxArchiveDisk32StructuralView(
        owner.get(), nullptr) == false);
}

void TestActiveKeyAndResolverFailures()
{
    Fixture fixture{};
    fixture.PopulateGraph(0, 0, 0);
    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    fixture.buffers->effects[0].definitionKey =
        archive::EffectDefinitionKey32{};
    const std::size_t callsBefore = resolver.calls;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::InvalidEffect);
    CHECK(resolver.calls == callsBefore);
    CheckViewIsGated(owner.get());

    fixture.buffers->effects[0].definitionKey =
        archive::EffectDefinitionKey32{DEFINITION_KEY};
    resolver.result = nullptr;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::DefinitionNotFound);
    CheckViewIsGated(owner.get());
}

void TestSameWorkspaceResolverReentrancyIsRejected()
{
    Fixture fixture{};
    fixture.PopulateGraph(0, 0, 0);
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    ReentrantResolverState state{
        fixture.system.get(),
        fixture.buffers.get(),
        owner.get()};
    const archive::FxArchiveDisk32Resolver resolver{
        &state, ResolveDefinitionReentrantly};
    CHECK(archive::TryBuildFxArchiveDisk32StructuralImage(
              *fixture.system,
              *fixture.buffers,
              resolver,
              owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(state.calls == 1);
    CHECK(state.nestedStatus
          == archive::FxArchiveDisk32StructuralStatus::InvalidArgument);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::StructurallyValid);
    const auto view = GetView(owner.get());
    CHECK(view.buffers->effects[0].def == state.result);

    ResolverState ordinaryResolver{};
    CHECK(Build(fixture, ordinaryResolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::StructurallyValid);
}

void TestSameWorkspaceSparsePoolMemberTransitions()
{
    Fixture fixture{};
    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    PopulateSparseGraph(&fixture, 0, 2);
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::StructurallyValid);
    auto view = GetView(owner.get());
    CheckAllocatedPair(view.poolStates->elems, 0, 2);
    CheckAllocatedPair(view.poolStates->trails, 0, 2);
    CheckAllocatedPair(view.poolStates->trailElems, 0, 2);
    CHECK(view.system->firstFreeElem == 1);
    CHECK(view.system->firstFreeTrail == 1);
    CHECK(view.system->firstFreeTrailElem == 1);
    CHECK(view.buffers->elems[0].item.nextElemHandleInEffect
          == ElemHandle(2));
    CHECK(view.buffers->elems[2].item.prevElemHandleInEffect
          == ElemHandle(0));
    CHECK(view.buffers->elems[1].nextFree == 3);
    CHECK(view.buffers->trails[0].item.nextTrailHandle
          == TrailHandle(2));
    CHECK(view.buffers->trails[1].nextFree == 3);
    CHECK(view.buffers->trailElems[0].item.nextTrailElemHandle
          == TrailElemHandle(2));
    CHECK(view.buffers->trailElems[1].nextFree == 3);

    // Reusing this exact workspace now changes slots 0/2 from item to free
    // and slots 1/3 from free to item in every pool.
    PopulateSparseGraph(&fixture, 1, 3);
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::StructurallyValid);
    view = GetView(owner.get());
    CheckAllocatedPair(view.poolStates->elems, 1, 3);
    CheckAllocatedPair(view.poolStates->trails, 1, 3);
    CheckAllocatedPair(view.poolStates->trailElems, 1, 3);
    CHECK(view.system->firstFreeElem == 0);
    CHECK(view.system->firstFreeTrail == 0);
    CHECK(view.system->firstFreeTrailElem == 0);
    CHECK(view.buffers->elems[0].nextFree == 2);
    CHECK(view.buffers->elems[2].nextFree == 4);
    CHECK(view.buffers->elems[1].item.nextElemHandleInEffect
          == ElemHandle(3));
    CHECK(view.buffers->elems[3].item.prevElemHandleInEffect
          == ElemHandle(1));
    CHECK(view.buffers->elems[3].item.msecBegin == 222);
    CHECK(view.buffers->trails[0].nextFree == 2);
    CHECK(view.buffers->trails[2].nextFree == 4);
    CHECK(view.buffers->trails[1].item.nextTrailHandle
          == TrailHandle(3));
    CHECK(view.buffers->trails[3].item.defIndex == UINT8_C(0x42));
    CHECK(view.buffers->trailElems[0].nextFree == 2);
    CHECK(view.buffers->trailElems[2].nextFree == 4);
    CHECK(view.buffers->trailElems[1].item.nextTrailElemHandle
          == TrailElemHandle(3));
    CHECK(view.buffers->trailElems[3].item.msecBegin == 444);
    CHECK(resolver.calls == 2);
}

void TestSourceImagesRemainReadOnlyAcrossSuccessAndFailure()
{
    Fixture fixture{};
    fixture.PopulateGraph(5, 3, 7);
    const std::vector<std::uint8_t> systemBefore(
        reinterpret_cast<const std::uint8_t *>(fixture.system.get()),
        reinterpret_cast<const std::uint8_t *>(fixture.system.get())
            + sizeof(*fixture.system));
    const std::vector<std::uint8_t> buffersBefore(
        reinterpret_cast<const std::uint8_t *>(fixture.buffers.get()),
        reinterpret_cast<const std::uint8_t *>(fixture.buffers.get())
            + sizeof(*fixture.buffers));

    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(std::memcmp(
              fixture.system.get(),
              systemBefore.data(),
              systemBefore.size())
          == 0);
    CHECK(std::memcmp(
              fixture.buffers.get(),
              buffersBefore.data(),
              buffersBefore.size())
          == 0);

    resolver.accept = false;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::DefinitionNotFound);
    CHECK(std::memcmp(
              fixture.system.get(),
              systemBefore.data(),
              systemBefore.size())
          == 0);
    CHECK(std::memcmp(
              fixture.buffers.get(),
              buffersBefore.data(),
              buffersBefore.size())
          == 0);
    CheckViewIsGated(owner.get());
}

archive::FxArchiveDisk32ReadyView GetReadyView(
    const archive::FxArchiveDisk32NativeWorkspace *const workspace)
{
    archive::FxArchiveDisk32ReadyView view{};
    CHECK(archive::TryGetFxArchiveDisk32ReadyView(workspace, &view));
    CHECK(view.system != nullptr);
    CHECK(view.buffers != nullptr);
    CHECK(view.poolStates != nullptr);
    CHECK(view.metadata != nullptr);
    return view;
}

void TestReadyEmptyImageAndPhaseGates()
{
    Fixture fixture{};
    fixture.system->frameCount = 0;
    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    const auto poison = reinterpret_cast<const void *>(
        static_cast<std::uintptr_t>(UINT32_C(0x2468A)));
    archive::FxArchiveDisk32ReadyView output{
        static_cast<const FxSystem *>(poison),
        static_cast<const FxSystemBuffers *>(poison),
        static_cast<const archive::FxSystemBuffersDisk32PoolStates *>(
            poison),
        static_cast<const archive::FxSystemDisk32Metadata *>(poison),
        UINT32_C(0xA5A55A5A)};
    const auto outputBefore = output;
    CHECK(!archive::TryGetFxArchiveDisk32ReadyView(
        owner.get(), &output));
    CHECK(std::memcmp(&output, &outputBefore, sizeof(output)) == 0);
    CHECK(!archive::TryGetFxArchiveDisk32ReadyView(nullptr, &output));
    CHECK(std::memcmp(&output, &outputBefore, sizeof(output)) == 0);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(nullptr)
          == archive::FxArchiveDisk32ReadyStatus::InvalidArgument);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::InvalidPhase);

    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(!archive::TryGetFxArchiveDisk32ReadyView(
        owner.get(), &output));
    CHECK(std::memcmp(&output, &outputBefore, sizeof(output)) == 0);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Ready);
    const auto ready = GetReadyView(owner.get());
    CHECK(ready.physicsBodyCount == 0);
    CHECK(ready.system->activeSpotLightBoltDobj == -1);
    CHECK(ready.system->frameCount == 1);
    archive::FxArchiveDisk32StructuralView structural{};
    CHECK(archive::TryGetFxArchiveDisk32StructuralView(
        owner.get(), &structural));
    CHECK(structural.system == ready.system);

    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::InvalidPhase);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Ready);
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::StructurallyValid);
    CHECK(!archive::TryGetFxArchiveDisk32ReadyView(
        owner.get(), &output));
}

void TestReadyOriginLightingAndCanonicalization()
{
    Fixture fixture{};
    fixture.PopulateGraph(1, 0, 0);
    MakeEffectRuntimeSemanticallyValid(&fixture);

    SemanticDefinition definition{};
    definition.Configure(1);
    definition.SetType(0, ELEM_TYPE_SPRITE_BILLBOARD);
    ResolverState resolver{};
    resolver.result = &definition.effect;

    archive::FxElemDisk32 elem = LoadElemDisk32(fixture, 0);
    elem.defIndex = 0;
    const float origin[3]{1.25f, -2.5f, 3.75f};
    std::memcpy(elem.payload, origin, sizeof(origin));
    const std::array<std::uint8_t, 4> value{
        UINT8_C(0xEF), UINT8_C(0xBE), UINT8_C(0xC3), UINT8_C(0x7A)};
    std::memcpy(elem.value, value.data(), value.size());
    StoreElemDisk32(&fixture, 0, elem);

    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    const auto structural = GetView(owner.get());
    std::vector<std::uint8_t> expectedSystem(
        reinterpret_cast<const std::uint8_t *>(structural.system),
        reinterpret_cast<const std::uint8_t *>(structural.system)
            + sizeof(*structural.system));
    std::vector<std::uint8_t> expectedBuffers(
        reinterpret_cast<const std::uint8_t *>(structural.buffers),
        reinterpret_cast<const std::uint8_t *>(structural.buffers)
            + sizeof(*structural.buffers));
    const std::int32_t zeroFrameCount = 0;
    std::memcpy(
        expectedBuffers.data()
            + offsetof(FxSystemBuffers, effects)
            + offsetof(FxEffect, frameCount),
        &zeroFrameCount,
        sizeof(zeroFrameCount));
    const std::int16_t noSpotLightBolt = -1;
    std::memcpy(
        expectedSystem.data()
            + offsetof(FxSystem, activeSpotLightBoltDobj),
        &noSpotLightBolt,
        sizeof(noSpotLightBolt));
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);

    const auto ready = GetReadyView(owner.get());
    CHECK(ready.physicsBodyCount == 0);
    CHECK(Sys_AtomicLoad(&ready.buffers->effects[0].frameCount) == 0);
    const FxElem &readyElem = ready.buffers->elems[0].item;
    CHECK(std::memcmp(readyElem.origin, origin, sizeof(origin)) == 0);
    CHECK(readyElem.u.lightingHandle == UINT16_C(0xBEEF));
    CHECK(std::memcmp(
              reinterpret_cast<const std::uint8_t *>(&readyElem)
                  + offsetof(FxElem, u),
              value.data(),
              value.size())
          == 0);
    CHECK(std::memcmp(
              ready.system,
              expectedSystem.data(),
              expectedSystem.size())
          == 0);
    CHECK(std::memcmp(
              ready.buffers,
              expectedBuffers.data(),
              expectedBuffers.size())
          == 0);
}

void ConfigurePhysicsModelFixture(
    Fixture *const fixture,
    SemanticDefinition *const definition,
    const std::uint32_t token) noexcept
{
    CHECK(fixture != nullptr);
    CHECK(definition != nullptr);
    if (!fixture || !definition)
        return;
    fixture->PopulateGraph(1, 0, 0);
    MakeEffectRuntimeSemanticallyValid(fixture);
    archive::FxEffectDisk32 &effect = fixture->buffers->effects[0];
    effect.firstElemHandle[0] = INVALID_HANDLE;
    effect.firstElemHandle[1] = ElemHandle(0);
    effect.firstSortedElemHandle = INVALID_HANDLE;

    definition->Configure(1);
    definition->SetPhysicsModel(
        0, static_cast<std::uintptr_t>(UINT32_C(0x135790)));

    archive::FxElemDisk32 elem = LoadElemDisk32(*fixture, 0);
    elem.defIndex = 0;
    std::memcpy(elem.payload, &token, sizeof(token));
    for (std::size_t index = sizeof(token);
         index < sizeof(elem.payload);
         ++index)
    {
        elem.payload[index] = static_cast<std::uint8_t>(0x80u + index);
    }
    StoreElemDisk32(fixture, 0, elem);
}

void TestReadyPhysicsSelectionAndFailureGating()
{
    constexpr std::uint32_t token = UINT32_C(0xF1234567);
    Fixture fixture{};
    SemanticDefinition definition{};
    ConfigurePhysicsModelFixture(&fixture, &definition, token);
    ResolverState resolver{};
    resolver.result = &definition.effect;

    const archive::FxElemDisk32 sourceElem =
        LoadElemDisk32(fixture, 0);
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    SemanticCallbackProbe successfulProbe{token};
    const archive::FxArchiveSemanticCallbacks probeCallbacks{
        &successfulProbe,
        PrepareSemanticPayloadProbe,
        AcceptSemanticPhysicsProbe};
    archive::FxArchiveSemanticResult probedResult{};
    const auto probedStructural = GetView(owner.get());
    CHECK(archive::TryValidateFxArchiveSemanticsNoReport(
        const_cast<FxSystem *>(probedStructural.system),
        probeCallbacks,
        &probedResult));
    CHECK(successfulProbe.valid);
    CHECK(successfulProbe.prepareCalls == 1);
    CHECK(successfulProbe.sinkCalls == 1);
    CHECK(probedResult.physicsBodyCount == 1);

    SemanticCallbackProbe mutatingProbe{token};
    mutatingProbe.mutatePayload = true;
    const archive::FxArchiveSemanticCallbacks mutatingCallbacks{
        &mutatingProbe,
        PrepareSemanticPayloadProbe,
        AcceptSemanticPhysicsProbe};
    archive::FxArchiveSemanticResult mutationResult{
        UINT32_C(0x55AAAA55), -321};
    const auto mutationResultBefore = mutationResult;
    CHECK(!archive::TryValidateFxArchiveSemanticsNoReport(
        const_cast<FxSystem *>(probedStructural.system),
        mutatingCallbacks,
        &mutationResult));
    CHECK(mutatingProbe.valid);
    CHECK(mutatingProbe.prepareCalls == 1);
    CHECK(mutatingProbe.sinkCalls == 0);
    CHECK(std::memcmp(
              &mutationResult,
              &mutationResultBefore,
              sizeof(mutationResult))
          == 0);

    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);
    const auto ready = GetReadyView(owner.get());
    CHECK(ready.physicsBodyCount == 1);
    std::uint32_t observedToken = 0;
    std::memcpy(
        &observedToken,
        &ready.buffers->elems[0].item.physObjId,
        sizeof(observedToken));
    CHECK(observedToken == token);
    CHECK(std::memcmp(
              reinterpret_cast<const std::uint8_t *>(
                  &ready.buffers->elems[0].item)
                  + offsetof(FxElem, physObjId),
              sourceElem.payload,
              sizeof(sourceElem.payload))
          == 0);

    ConfigurePhysicsModelFixture(&fixture, &definition, 0);
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    SemanticCallbackProbe failedProbe{0};
    const archive::FxArchiveSemanticCallbacks failedCallbacks{
        &failedProbe,
        PrepareSemanticPayloadProbe,
        AcceptSemanticPhysicsProbe};
    archive::FxArchiveSemanticResult failedResult{
        UINT32_C(0xA5A55A5A), -123};
    const auto failedResultBefore = failedResult;
    const auto failedStructural = GetView(owner.get());
    CHECK(!archive::TryValidateFxArchiveSemanticsNoReport(
        const_cast<FxSystem *>(failedStructural.system),
        failedCallbacks,
        &failedResult));
    CHECK(failedProbe.prepareCalls == 0);
    CHECK(failedProbe.sinkCalls == 0);
    CHECK(std::memcmp(
              &failedResult,
              &failedResultBefore,
              sizeof(failedResult))
          == 0);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::InvalidSemantics);
    CheckViewIsGated(owner.get());
    archive::FxArchiveDisk32ReadyView failedView{};
    CHECK(!archive::TryGetFxArchiveDisk32ReadyView(
        owner.get(), &failedView));
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::InvalidPhase);

    ConfigurePhysicsModelFixture(&fixture, &definition, token);
    definition.SetPhysicsModel(0, 0);
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::InvalidSemantics);
    CheckViewIsGated(owner.get());
}

void TestSemanticMultiVisualSelectionWithoutPrepare()
{
    constexpr std::array<std::uint32_t, 2> tokens{
        UINT32_C(0x81234567), UINT32_C(0xF2345678)};
    const std::array<const XModel *, 2> models{
        reinterpret_cast<const XModel *>(
            static_cast<std::uintptr_t>(UINT32_C(0x00135790))),
        reinterpret_cast<const XModel *>(
            static_cast<std::uintptr_t>(UINT32_C(0x002468A0)))};

    Fixture fixture{};
    fixture.PopulateGraph(2, 0, 0);
    MakeEffectRuntimeSemanticallyValid(&fixture);
    archive::FxEffectDisk32 &effect = fixture.buffers->effects[0];
    effect.firstElemHandle[0] = INVALID_HANDLE;
    effect.firstElemHandle[1] = ElemHandle(0);
    effect.firstElemHandle[2] = INVALID_HANDLE;
    effect.firstSortedElemHandle = INVALID_HANDLE;

    SemanticDefinition definition{};
    definition.Configure(1);
    definition.SetPhysicsModels(
        0, models.data(), static_cast<std::uint8_t>(models.size()));
    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        archive::FxElemDisk32 elem = LoadElemDisk32(fixture, index);
        elem.defIndex = 0;
        elem.sequence = 0;
        // 479 modulo the 479-entry random period selects seed zero. The
        // production-exact table prefix makes a two-visual lookup choose slot
        // one, covering the native pointer-stride array path deterministically.
        elem.msecBegin = 479;
        std::memcpy(elem.payload, &tokens[index], sizeof(tokens[index]));
        StoreElemDisk32(&fixture, index, elem);
    }

    ResolverState resolver{};
    resolver.result = &definition.effect;
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);

    SemanticPhysicsSequenceProbe probe{};
    const archive::FxArchiveSemanticCallbacks callbacks{
        &probe, nullptr, AcceptSemanticPhysicsSequenceProbe};
    archive::FxArchiveSemanticResult result{};
    const auto structural = GetView(owner.get());
    CHECK(archive::TryValidateFxArchiveSemanticsNoReport(
        const_cast<FxSystem *>(structural.system), callbacks, &result));
    CHECK(probe.valid);
    CHECK(probe.sinkCalls == tokens.size());
    CHECK(probe.observedTokens == tokens);
    CHECK(probe.observedModels[0] == models[1]);
    CHECK(probe.observedModels[1] == models[1]);
    CHECK(probe.observedOwnerIndices[0] == 0);
    CHECK(probe.observedOwnerIndices[1] == 1);
    CHECK(result.physicsBodyCount == tokens.size());
    CHECK(result.spotLightBoltDobj == -1);
}

void TestReadySpotlightSelectionAndDerivedBolt()
{
    Fixture fixture{};
    fixture.PopulateGraph(1, 0, 0);
    MakeEffectRuntimeSemanticallyValid(&fixture);
    archive::FxEffectDisk32 &effect = fixture.buffers->effects[0];
    effect.firstElemHandle[0] = INVALID_HANDLE;
    effect.firstSortedElemHandle = INVALID_HANDLE;
    constexpr std::uint32_t boltDobj = 123;
    constexpr std::uint32_t boltBone = 5;
    effect.boltAndSortOrder = boltDobj | (boltBone << 13u);
    fixture.system->activeSpotLightEffectCount = 1;
    fixture.system->activeSpotLightElemCount = 1;
    fixture.system->activeSpotLightEffectHandle = DiskEffectHandle(0);
    fixture.system->activeSpotLightElemHandle = ElemHandle(0);
    fixture.system->activeSpotLightBoltDobj = -77;

    SemanticDefinition definition{};
    definition.Configure(1);
    definition.SetType(0, ELEM_TYPE_SPOT_LIGHT);
    ResolverState resolver{};
    resolver.result = &definition.effect;

    archive::FxElemDisk32 elem = LoadElemDisk32(fixture, 0);
    elem.defIndex = 0;
    const float origin[3]{25.0f, -50.0f, 75.0f};
    std::memcpy(elem.payload, origin, sizeof(origin));
    StoreElemDisk32(&fixture, 0, elem);

    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);
    const auto ready = GetReadyView(owner.get());
    CHECK(ready.buffers != nullptr);
    if (!ready.buffers)
        return;
    CHECK(ready.physicsBodyCount == 0);
    CHECK(ready.system->activeSpotLightBoltDobj
          == static_cast<std::int16_t>(boltDobj));
    CHECK(std::memcmp(
              ready.buffers->elems[0].item.origin,
              origin,
              sizeof(origin))
          == 0);
}

void TestReadyAllOrdinaryClasses()
{
    Fixture fixture{};
    fixture.PopulateGraph(3, 0, 0);
    MakeEffectRuntimeSemanticallyValid(&fixture);
    archive::FxEffectDisk32 &effect = fixture.buffers->effects[0];
    effect.firstElemHandle[0] = ElemHandle(0);
    effect.firstElemHandle[1] = ElemHandle(1);
    effect.firstElemHandle[2] = ElemHandle(2);
    effect.firstSortedElemHandle = ElemHandle(0);

    SemanticDefinition definition{};
    definition.Configure(3);
    definition.SetType(0, ELEM_TYPE_SPRITE_BILLBOARD);
    definition.SetType(1, ELEM_TYPE_MODEL);
    definition.SetType(2, ELEM_TYPE_CLOUD);
    const std::array<std::array<float, 3>, 3> origins{{
        {{1.0f, 2.0f, 3.0f}},
        {{4.0f, 5.0f, 6.0f}},
        {{7.0f, 8.0f, 9.0f}}}};
    for (std::size_t index = 0; index < origins.size(); ++index)
    {
        archive::FxElemDisk32 elem = LoadElemDisk32(fixture, index);
        elem.defIndex = static_cast<std::uint8_t>(index);
        elem.nextElemHandleInEffect = INVALID_HANDLE;
        elem.prevElemHandleInEffect = INVALID_HANDLE;
        std::memcpy(
            elem.payload,
            origins[index].data(),
            sizeof(elem.payload));
        StoreElemDisk32(&fixture, index, elem);
    }

    ResolverState resolver{};
    resolver.result = &definition.effect;
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);
    const auto ready = GetReadyView(owner.get());
    CHECK(ready.buffers != nullptr);
    if (!ready.buffers)
        return;
    CHECK(ready.physicsBodyCount == 0);
    for (std::size_t index = 0; index < origins.size(); ++index)
    {
        CHECK(std::memcmp(
                  ready.buffers->elems[index].item.origin,
                  origins[index].data(),
                  sizeof(ready.buffers->elems[index].item.origin))
              == 0);
    }
}

void TestReadyTrailSemanticsAndFailureGating()
{
    Fixture fixture{};
    fixture.PopulateGraph(0, 1, 1);
    MakeEffectRuntimeSemanticallyValid(&fixture);
    SemanticTrailDefinitionHeader trailDefinition{};
    SemanticDefinition definition{};
    definition.Configure(1);
    definition.effect.elemDefCountOneShot = 0;
    definition.effect.elemDefCountEmission = 1;
    definition.SetTrail(
        0, reinterpret_cast<std::uintptr_t>(&trailDefinition));
    ResolverState resolver{};
    resolver.result = &definition.effect;

    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);
    const auto ready = GetReadyView(owner.get());
    CHECK(ready.physicsBodyCount == 0);
    CHECK(ready.buffers->trails[0].item.defIndex == 0);
    CHECK(ready.buffers->trailElems[0].item.spawnDist == 0.0f);

    trailDefinition.splitDist = 0;
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::InvalidSemantics);
    CheckViewIsGated(owner.get());
}

void ConfigurePhysicsCapacityFixture(
    Fixture *const fixture,
    SemanticDefinition *const definition,
    const std::size_t elemCount) noexcept
{
    CHECK(fixture != nullptr);
    CHECK(definition != nullptr);
    CHECK(elemCount <= MAX_ELEMS);
    if (!fixture || !definition || elemCount > MAX_ELEMS)
        return;
    fixture->PopulateGraph(elemCount, 0, 0);
    MakeEffectRuntimeSemanticallyValid(fixture);
    archive::FxEffectDisk32 &effect = fixture->buffers->effects[0];
    effect.firstElemHandle[0] = INVALID_HANDLE;
    effect.firstElemHandle[1] =
        elemCount == 0 ? INVALID_HANDLE : ElemHandle(0);
    effect.firstSortedElemHandle = INVALID_HANDLE;
    definition->Configure(1);
    definition->SetPhysicsModel(
        0, static_cast<std::uintptr_t>(UINT32_C(0x2468A0)));

    for (std::size_t index = 0; index < elemCount; ++index)
    {
        archive::FxElemDisk32 elem = LoadElemDisk32(*fixture, index);
        elem.defIndex = 0;
        const std::uint32_t token =
            static_cast<std::uint32_t>(index + 1u);
        std::memcpy(elem.payload, &token, sizeof(token));
        StoreElemDisk32(fixture, index, elem);
    }
}

struct ReadyPhysicsCountProbe final
{
    const FxSystemBuffers *buffers = nullptr;
    std::size_t calls = 0;
    bool valid = true;
};

bool AcceptReadyPhysicsCount(
    void *const context,
    const archive::FxArchiveDisk32ReadyPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    auto *const probe = static_cast<ReadyPhysicsCountProbe *>(context);
    if (!probe || !probe->buffers)
        return false;
    probe->valid = probe->valid
        && physicsIndex == probe->calls
        && descriptor.ownerIndex == physicsIndex
        && descriptor.ownerIndex < MAX_ELEMS
        && descriptor.elem
            == &probe->buffers->elems[descriptor.ownerIndex].item
        && descriptor.model != nullptr
        && descriptor.token
            == static_cast<std::uint32_t>(physicsIndex + 1u);
    ++probe->calls;
    return probe->valid;
}

struct ReadyPhysicsSequenceProbe final
{
    const FxSystemBuffers *buffers = nullptr;
    std::array<std::uint32_t, 2> expectedTokens{};
    const XModel *expectedModel = nullptr;
    std::array<archive::FxArchiveDisk32ReadyPhysicsDescriptor, 2>
        observed{};
    std::size_t rejectIndex = (std::numeric_limits<std::size_t>::max)();
    std::size_t calls = 0;
    bool valid = true;
};

bool AcceptReadyPhysicsSequence(
    void *const context,
    const archive::FxArchiveDisk32ReadyPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    auto *const probe = static_cast<ReadyPhysicsSequenceProbe *>(context);
    if (!probe || !probe->buffers
        || physicsIndex >= probe->observed.size())
    {
        return false;
    }

    probe->valid = probe->valid
        && physicsIndex == probe->calls
        && descriptor.ownerIndex == physicsIndex
        && descriptor.elem
            == &probe->buffers->elems[physicsIndex].item
        && descriptor.model == probe->expectedModel
        && descriptor.token == probe->expectedTokens[physicsIndex];
    probe->observed[physicsIndex] = descriptor;
    ++probe->calls;
    return probe->valid && physicsIndex != probe->rejectIndex;
}

void ConfigureReadyPhysicsEnumerationFixture(
    Fixture *const fixture,
    SemanticDefinition *const definition,
    const std::array<std::uint32_t, 2> &tokens,
    const std::array<const XModel *, 2> &models) noexcept
{
    CHECK(fixture != nullptr);
    CHECK(definition != nullptr);
    if (!fixture || !definition)
        return;

    fixture->PopulateGraph(tokens.size(), 0, 0);
    MakeEffectRuntimeSemanticallyValid(fixture);
    archive::FxEffectDisk32 &effect = fixture->buffers->effects[0];
    effect.firstElemHandle[0] = INVALID_HANDLE;
    effect.firstElemHandle[1] = ElemHandle(0);
    effect.firstElemHandle[2] = INVALID_HANDLE;
    effect.firstSortedElemHandle = INVALID_HANDLE;

    definition->Configure(1);
    definition->SetPhysicsModels(
        0, models.data(), static_cast<std::uint8_t>(models.size()));
    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        archive::FxElemDisk32 elem = LoadElemDisk32(*fixture, index);
        elem.defIndex = 0;
        elem.sequence = 0;
        elem.msecBegin = 479;
        std::memcpy(elem.payload, &tokens[index], sizeof(tokens[index]));
        StoreElemDisk32(fixture, index, elem);
    }
}

void TestReadyPhysicsEnumerationPhaseAndZeroGates()
{
    Fixture fixture{};
    fixture.system->frameCount = 0;
    ResolverState resolver{};
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    ReadyPhysicsCountProbe probe{};
    CHECK(!archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        nullptr, &probe, AcceptReadyPhysicsCount));
    CHECK(!archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), &probe, AcceptReadyPhysicsCount));
    CHECK(!archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), &probe, nullptr));
    CHECK(probe.calls == 0);

    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(!archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), &probe, AcceptReadyPhysicsCount));
    CHECK(probe.calls == 0);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);
    const auto ready = GetReadyView(owner.get());
    CHECK(ready.physicsBodyCount == 0);
    CHECK(!archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), nullptr, nullptr));
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Ready);

    // A callback is required to make the enumeration boundary explicit, but
    // a zero-body Ready image invokes it zero times and remains retryable.
    CHECK(archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), nullptr, AcceptReadyPhysicsCount));
    CHECK(probe.calls == 0);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Ready);
    CHECK(archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), nullptr, AcceptReadyPhysicsCount));
}

void TestReadyPhysicsEnumerationOrderRetryAndPreservation()
{
    constexpr std::array<std::uint32_t, 2> tokens{
        UINT32_C(0x81234567), UINT32_C(0xF2345678)};
    const std::array<const XModel *, 2> models{
        reinterpret_cast<const XModel *>(
            static_cast<std::uintptr_t>(UINT32_C(0x00135790))),
        reinterpret_cast<const XModel *>(
            static_cast<std::uintptr_t>(UINT32_C(0x002468A0)))};
    Fixture fixture{};
    SemanticDefinition definition{};
    ConfigureReadyPhysicsEnumerationFixture(
        &fixture, &definition, tokens, models);
    ResolverState resolver{};
    resolver.result = &definition.effect;
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);
    const auto ready = GetReadyView(owner.get());
    CHECK(ready.physicsBodyCount == tokens.size());

    const std::vector<std::uint8_t> systemBefore(
        reinterpret_cast<const std::uint8_t *>(ready.system),
        reinterpret_cast<const std::uint8_t *>(ready.system)
            + sizeof(*ready.system));
    const std::vector<std::uint8_t> buffersBefore(
        reinterpret_cast<const std::uint8_t *>(ready.buffers),
        reinterpret_cast<const std::uint8_t *>(ready.buffers)
            + sizeof(*ready.buffers));
    const std::vector<std::uint8_t> poolStatesBefore(
        reinterpret_cast<const std::uint8_t *>(ready.poolStates),
        reinterpret_cast<const std::uint8_t *>(ready.poolStates)
            + sizeof(*ready.poolStates));
    const std::vector<std::uint8_t> metadataBefore(
        reinterpret_cast<const std::uint8_t *>(ready.metadata),
        reinterpret_cast<const std::uint8_t *>(ready.metadata)
            + sizeof(*ready.metadata));

    definition.SetPhysicsModel(0, 0);
    ReadyPhysicsSequenceProbe invalidSemantics{
        ready.buffers, tokens, models[1]};
    CHECK(!archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(),
        &invalidSemantics,
        AcceptReadyPhysicsSequence));
    CHECK(invalidSemantics.calls == 0);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Ready);
    definition.SetPhysicsModels(
        0, models.data(), static_cast<std::uint8_t>(models.size()));

    ReadyPhysicsSequenceProbe rejected{
        ready.buffers, tokens, models[1]};
    rejected.rejectIndex = 1;
    CHECK(!archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), &rejected, AcceptReadyPhysicsSequence));
    CHECK(rejected.valid);
    CHECK(rejected.calls == 2);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Ready);
    CHECK(std::memcmp(
              ready.system, systemBefore.data(), systemBefore.size())
          == 0);
    CHECK(std::memcmp(
              ready.buffers, buffersBefore.data(), buffersBefore.size())
          == 0);
    CHECK(std::memcmp(
              ready.poolStates,
              poolStatesBefore.data(),
              poolStatesBefore.size())
          == 0);
    CHECK(std::memcmp(
              ready.metadata,
              metadataBefore.data(),
              metadataBefore.size())
          == 0);

    ReadyPhysicsSequenceProbe accepted{
        ready.buffers, tokens, models[1]};
    CHECK(archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), &accepted, AcceptReadyPhysicsSequence));
    CHECK(accepted.valid);
    CHECK(accepted.calls == tokens.size());
    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        CHECK(accepted.observed[index].elem
              == &ready.buffers->elems[index].item);
        CHECK(accepted.observed[index].model == models[1]);
        CHECK(accepted.observed[index].ownerIndex == index);
        CHECK(accepted.observed[index].token == tokens[index]);
    }

    CHECK(std::memcmp(
              ready.system, systemBefore.data(), systemBefore.size())
          == 0);
    CHECK(std::memcmp(
              ready.buffers, buffersBefore.data(), buffersBefore.size())
          == 0);
    CHECK(std::memcmp(
              ready.poolStates,
              poolStatesBefore.data(),
              poolStatesBefore.size())
          == 0);
    CHECK(std::memcmp(
              ready.metadata,
              metadataBefore.data(),
              metadataBefore.size())
          == 0);
}

struct ReadyPhysicsReentryProbe final
{
    Fixture *fixture = nullptr;
    ResolverState *resolver = nullptr;
    archive::FxArchiveDisk32NativeWorkspace *workspace = nullptr;
    bool nestedEnumerationResult = true;
    archive::FxArchiveDisk32StructuralStatus nestedBuildStatus =
        archive::FxArchiveDisk32StructuralStatus::Success;
    std::size_t calls = 0;
    bool valid = true;
};

bool AcceptReadyPhysicsReentrantly(
    void *const context,
    const archive::FxArchiveDisk32ReadyPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    auto *const probe = static_cast<ReadyPhysicsReentryProbe *>(context);
    if (!probe || !probe->fixture || !probe->resolver
        || !probe->workspace)
    {
        return false;
    }
    ++probe->calls;
    probe->valid = probe->valid && physicsIndex == 0
        && descriptor.ownerIndex == 0 && descriptor.elem != nullptr
        && descriptor.model != nullptr && descriptor.token != 0;
    if (probe->calls == 1)
    {
        probe->nestedEnumerationResult =
            archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
                probe->workspace,
                probe,
                AcceptReadyPhysicsReentrantly);
        probe->nestedBuildStatus =
            Build(*probe->fixture, *probe->resolver, probe->workspace);
        probe->valid = probe->valid
            && !probe->nestedEnumerationResult
            && probe->nestedBuildStatus
                == archive::FxArchiveDisk32StructuralStatus::InvalidArgument
            && probe->workspace->phase()
                == archive::FxArchiveDisk32WorkspacePhase::Ready;
    }
    return probe->valid;
}

void TestReadyPhysicsEnumerationReentryIsRejected()
{
    constexpr std::uint32_t token = UINT32_C(0xF1234567);
    Fixture fixture{};
    SemanticDefinition definition{};
    ConfigurePhysicsModelFixture(&fixture, &definition, token);
    ResolverState resolver{};
    resolver.result = &definition.effect;
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);

    ReadyPhysicsReentryProbe probe{
        &fixture, &resolver, owner.get()};
    CHECK(archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), &probe, AcceptReadyPhysicsReentrantly));
    CHECK(probe.valid);
    CHECK(probe.calls == 1);
    CHECK(!probe.nestedEnumerationResult);
    CHECK(probe.nestedBuildStatus
          == archive::FxArchiveDisk32StructuralStatus::InvalidArgument);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Ready);

    ReadyPhysicsSequenceProbe retry{};
    retry.buffers = GetReadyView(owner.get()).buffers;
    retry.expectedTokens[0] = token;
    retry.expectedModel = reinterpret_cast<const XModel *>(
        static_cast<std::uintptr_t>(UINT32_C(0x135790)));
    // This one-body image uses the first descriptor slot only.
    retry.observed[1].token = UINT32_C(0xA5A55A5A);
    CHECK(archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), &retry, AcceptReadyPhysicsSequence));
    CHECK(retry.valid);
    CHECK(retry.calls == 1);
    CHECK(retry.observed[1].token == UINT32_C(0xA5A55A5A));
}

void TestReadyPhysicsCapacityBoundary()
{
    Fixture fixture{};
    SemanticDefinition definition{};
    ResolverState resolver{};
    resolver.result = &definition.effect;
    AllocationState allocation{};
    WorkspaceOwner owner{&allocation};
    CHECK(owner.get() != nullptr);
    if (!owner.get())
        return;

    ConfigurePhysicsCapacityFixture(
        &fixture, &definition, archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT);
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::Success);
    const auto ready = GetReadyView(owner.get());
    CHECK(ready.physicsBodyCount
          == archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT);
    ReadyPhysicsCountProbe enumeration{ready.buffers};
    CHECK(archive::TryEnumerateFxArchiveDisk32ReadyPhysics(
        owner.get(), &enumeration, AcceptReadyPhysicsCount));
    CHECK(enumeration.valid);
    CHECK(enumeration.calls == archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT);
    CHECK(owner.get()->phase()
          == archive::FxArchiveDisk32WorkspacePhase::Ready);

    ConfigurePhysicsCapacityFixture(
        &fixture,
        &definition,
        archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT + 1u);
    CHECK(Build(fixture, resolver, owner.get())
          == archive::FxArchiveDisk32StructuralStatus::Success);
    CHECK(archive::TryFinalizeFxArchiveDisk32NativeImage(owner.get())
          == archive::FxArchiveDisk32ReadyStatus::InvalidSemantics);
    CheckViewIsGated(owner.get());
}
} // namespace

int main()
{
    TestWorkspaceContractAndEmptyGate();
    TestFreeLinkDecoderOverloads();
    TestEmptyImageAndExactRelinking();
    TestMixedImageOpaqueResolutionAndCopies();
    TestMaximumCapacityAndConditionalX86Oracle();
    TestFailureStatusAndTransactionalGating();
    TestActiveKeyAndResolverFailures();
    TestSameWorkspaceResolverReentrancyIsRejected();
    TestSameWorkspaceSparsePoolMemberTransitions();
    TestSourceImagesRemainReadOnlyAcrossSuccessAndFailure();
    TestReadyEmptyImageAndPhaseGates();
    TestReadyOriginLightingAndCanonicalization();
    TestReadyPhysicsSelectionAndFailureGating();
    TestSemanticMultiVisualSelectionWithoutPrepare();
    TestReadySpotlightSelectionAndDerivedBolt();
    TestReadyAllOrdinaryClasses();
    TestReadyTrailSemanticsAndFailureGating();
    TestReadyPhysicsEnumerationPhaseAndZeroGates();
    TestReadyPhysicsEnumerationOrderRetryAndPreservation();
    TestReadyPhysicsEnumerationReentryIsRejected();
    TestReadyPhysicsCapacityBoundary();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d native Disk32 test(s) failed\n", failures);
        return 1;
    }
    std::puts("fx archive native Disk32 tests passed");
    return 0;
}
