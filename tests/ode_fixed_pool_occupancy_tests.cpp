#include "EffectsCore/fx_archive_capacity.h"
#include "EffectsCore/fx_archive_physics_batch_control.h"
#include "EffectsCore/fx_physics_sidecar.h"
#include "physics/phys_resource_pair.h"
#include "universal/pool_allocator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

struct dxBody
{
    std::uint64_t identity = 0;
    std::uint32_t owner = 0;
    std::uint32_t marker = 0;
};

namespace
{
namespace allocation = physics::allocation;
namespace archive = fx::archive;

using BatchOperation = archive::ArchivePhysicsBatchOperation;
using BatchStatus = archive::RestoreControlOperationStatus;

constexpr std::size_t kBodyCapacity = fx::physics::BODY_LIMIT;
constexpr std::size_t kUserDataCapacity = fx::physics::BODY_LIMIT;
constexpr std::size_t kGeomCapacity = 2048;
constexpr std::size_t kMaxFxEntries = 12;
constexpr std::size_t kMaxEntryGeoms = 2;
constexpr std::uint32_t kBodyMarker = 0x424F4459u;
constexpr std::uint32_t kUserDataMarker = 0x55534552u;
constexpr std::uint32_t kGeomMarker = 0x47454F4Du;
constexpr std::uint64_t kNonFxBodyBase = 0x1000000000000000ull;
constexpr std::uint64_t kNonFxGeomBase = 0x2000000000000000ull;
constexpr std::uint64_t kFxBodyBase = 0x3000000000000000ull;

static_assert(kBodyCapacity == 512);
static_assert(kUserDataCapacity == 512);
static_assert(kGeomCapacity == 2048);
static_assert(sizeof(dxBody) >= sizeof(void *));
static_assert(std::is_trivially_copyable_v<dxBody>);

struct OpaqueUserData
{
    dxBody *body = nullptr;
    std::uint64_t identity = 0;
    std::uint32_t marker = 0;
    std::uint32_t reserved = 0;
};

struct OpaqueGeom
{
    dxBody *body = nullptr;
    std::uint64_t identity = 0;
    std::uint32_t marker = 0;
    std::uint32_t ordinal = 0;
};

static_assert(sizeof(OpaqueUserData) >= sizeof(void *));
static_assert(sizeof(OpaqueGeom) >= sizeof(void *));
static_assert(std::is_trivially_copyable_v<OpaqueUserData>);
static_assert(std::is_trivially_copyable_v<OpaqueGeom>);

std::size_t assertionCount = 0;
int failures = 0;

void Check(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::uint64_t HashBytes(
    std::uint64_t hash,
    const void *const bytes,
    const std::size_t byteCount) noexcept
{
    constexpr std::uint64_t prime = 1099511628211ull;
    const auto *const data = static_cast<const unsigned char *>(bytes);
    for (std::size_t index = 0; index < byteCount; ++index)
    {
        hash ^= data[index];
        hash *= prime;
    }
    return hash;
}

template <typename T>
std::uint64_t HashValue(std::uint64_t hash, const T &value) noexcept
{
    return HashBytes(hash, &value, sizeof(value));
}

template <typename T, std::size_t Capacity>
struct FixedPool
{
    T items[Capacity]{};
    poolslotstate_t slotState[Capacity]{};
    poolcontrol_t control;
    poolstorage_t storage;
    pooldata_t data{};
    bool initialized = false;

    FixedPool() noexcept
        : control(Pool_ControlFor(slotState)),
          storage(Pool_StorageFor(items, control))
    {
    }

    FixedPool(const FixedPool &) = delete;
    FixedPool &operator=(const FixedPool &) = delete;

    bool Initialize() noexcept
    {
        initialized = Pool_Init(storage, &data);
        return initialized;
    }

    T *Allocate() noexcept
    {
        return static_cast<T *>(Pool_Alloc(storage, &data));
    }

    bool Free(T *const item) noexcept
    {
        return Pool_Free(storage, &data, item);
    }

    std::size_t ActiveCount() const noexcept
    {
        return data.activeCount < 0
            ? Capacity + 1u
            : static_cast<std::size_t>(data.activeCount);
    }

    std::size_t FreeCount() const noexcept
    {
        const poolcountresult_t result = Pool_GetFreeCount(storage, &data);
        return result.valid ? result.count : Capacity + 1u;
    }

    bool IsAllocated(const T *const item) const noexcept
    {
        const poolindexresult_t index = Pool_GetSlotIndex(storage, item);
        return index.valid && index.index < Capacity
            && control.slotState[index.index] == POOL_SLOT_ALLOCATED;
    }

    bool IsConserved() const noexcept
    {
        const poolcountresult_t free = Pool_GetFreeCount(storage, &data);
        return initialized && free.valid
            && ActiveCount() <= Capacity
            && free.count + ActiveCount() == Capacity
            && Pool_ValidateFull(storage, &data);
    }

    std::uint64_t Digest() const noexcept
    {
        std::uint64_t hash = 1469598103934665603ull;
        hash = HashValue(hash, data.firstFree);
        hash = HashValue(hash, data.activeCount);
        hash = HashValue(hash, control.headIndex);
        hash = HashValue(hash, control.activeCount);
        hash = HashValue(hash, control.initMagic);
        hash = HashBytes(hash, slotState, sizeof(slotState));
        return HashBytes(hash, items, sizeof(items));
    }
};

struct NonFxBodyRecord
{
    dxBody *body = nullptr;
    OpaqueUserData *userData = nullptr;
    std::uint64_t identity = 0;
};

struct FxEntry
{
    bool configured = false;
    bool active = false;
    std::size_t ownerIndex = MAX_ELEMS;
    std::size_t geomCount = 0;
    std::uint64_t identity = 0;
    dxBody *body = nullptr;
    OpaqueUserData *userData = nullptr;
    std::array<OpaqueGeom *, kMaxEntryGeoms> geoms{};
    fx::physics::BodyToken token = fx::physics::INVALID_BODY_TOKEN;
    dxBody *pendingBody = nullptr;
    OpaqueUserData *pendingUserData = nullptr;
    OpaqueGeom *pendingGeom = nullptr;
};

enum class EntryCreateStatus : std::uint8_t
{
    Success,
    RecoverableFailure,
    UnsafeFailure,
};

struct Fixture;

struct BodyPairContext
{
    Fixture *fixture = nullptr;
};

struct GeomPairContext
{
    Fixture *fixture = nullptr;
};

struct StateDigest
{
    std::uint64_t bodyPool = 0;
    std::uint64_t userDataPool = 0;
    std::uint64_t geomPool = 0;
    std::uint64_t ownership = 0;
    std::uint64_t fixture = 0;

    bool operator==(const StateDigest &) const noexcept = default;
};

struct Fixture
{
    FixedPool<dxBody, kBodyCapacity> bodyPool{};
    FixedPool<OpaqueUserData, kUserDataCapacity> userDataPool{};
    FixedPool<OpaqueGeom, kGeomCapacity> geomPool{};
    fx::physics::BodySidecar sidecar{};
    std::array<NonFxBodyRecord, kBodyCapacity> nonFxBodies{};
    std::array<OpaqueGeom *, kGeomCapacity> nonFxGeoms{};
    std::array<FxEntry, kMaxFxEntries> entries{};
    std::array<OpaqueGeom *, 8> geomBlockers{};
    OpaqueUserData *userDataBlocker = nullptr;
    std::size_t nonFxBodyCount = 0;
    std::size_t nonFxGeomCount = 0;
    std::size_t entryCount = 0;
    std::size_t geomBlockerCount = 0;
    std::size_t callbackCount = 0;
    bool refuseBodyCleanup = false;
    BodyPairContext bodyPairContext{this};
    GeomPairContext geomPairContext{this};

    Fixture() = default;
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;

    ~Fixture() noexcept
    {
        Cleanup();
    }

    static allocation::ResourceHandle AllocateBodyPrimary(
        void *const opaque) noexcept
    {
        auto *const context = static_cast<BodyPairContext *>(opaque);
        return context && context->fixture
            ? context->fixture->bodyPool.Allocate()
            : nullptr;
    }

    static allocation::ResourceHandle AllocateUserDataCompanion(
        void *const opaque,
        const allocation::ResourceHandle primary) noexcept
    {
        auto *const context = static_cast<BodyPairContext *>(opaque);
        if (!context || !context->fixture || !primary)
            return nullptr;
        return context->fixture->userDataPool.Allocate();
    }

    static bool FreeBodyPrimary(
        void *const opaque,
        const allocation::ResourceHandle primary) noexcept
    {
        auto *const context = static_cast<BodyPairContext *>(opaque);
        if (!context || !context->fixture || !primary
            || context->fixture->refuseBodyCleanup)
        {
            return false;
        }
        return context->fixture->bodyPool.Free(
            static_cast<dxBody *>(primary));
    }

    static allocation::ResourceHandle AllocateGeomPrimary(
        void *const opaque) noexcept
    {
        auto *const context = static_cast<GeomPairContext *>(opaque);
        return context && context->fixture
            ? context->fixture->geomPool.Allocate()
            : nullptr;
    }

    static allocation::ResourceHandle AllocateGeomCompanion(
        void *const opaque,
        const allocation::ResourceHandle primary) noexcept
    {
        auto *const context = static_cast<GeomPairContext *>(opaque);
        if (!context || !context->fixture || !primary)
            return nullptr;
        return context->fixture->geomPool.Allocate();
    }

    static bool FreeGeomPrimary(
        void *const opaque,
        const allocation::ResourceHandle primary) noexcept
    {
        auto *const context = static_cast<GeomPairContext *>(opaque);
        return context && context->fixture && primary
            && context->fixture->geomPool.Free(
                static_cast<OpaqueGeom *>(primary));
    }

    allocation::ResourcePairCallbacks BodyPairCallbacks() noexcept
    {
        return {
            &bodyPairContext,
            AllocateBodyPrimary,
            AllocateUserDataCompanion,
            FreeBodyPrimary,
        };
    }

    allocation::ResourcePairCallbacks GeomPairCallbacks() noexcept
    {
        return {
            &geomPairContext,
            AllocateGeomPrimary,
            AllocateGeomCompanion,
            FreeGeomPrimary,
        };
    }

    bool InitializePools() noexcept
    {
        return bodyPool.Initialize()
            && userDataPool.Initialize()
            && geomPool.Initialize()
            && fx::physics::ResetEmpty(&sidecar)
                == fx::physics::SidecarStatus::Success;
    }

    bool AllocateNonFxBodies(const std::size_t count) noexcept
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            const allocation::ResourcePairResult pair =
                allocation::TryCreateResourcePair(
                    BodyPairCallbacks(), true);
            if (!pair)
                return false;

            auto *const body = static_cast<dxBody *>(pair.primary);
            auto *const userData =
                static_cast<OpaqueUserData *>(pair.companion);
            const std::uint64_t identity = kNonFxBodyBase + index;
            *body = {identity, static_cast<std::uint32_t>(index),
                     kBodyMarker};
            *userData = {body, identity, kUserDataMarker, 0};
            nonFxBodies[nonFxBodyCount++] = {
                body, userData, identity};
        }
        return true;
    }

    bool AllocateNonFxGeoms(const std::size_t count) noexcept
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            const allocation::ResourcePairResult geom =
                allocation::TryCreateResourcePair(
                    GeomPairCallbacks(), false);
            if (!geom)
                return false;
            auto *const item = static_cast<OpaqueGeom *>(geom.primary);
            *item = {nullptr, kNonFxGeomBase + index, kGeomMarker, 0};
            nonFxGeoms[nonFxGeomCount++] = item;
        }
        return true;
    }

    EntryCreateStatus CreateEntry(FxEntry *const entry) noexcept
    {
        if (!entry || !entry->configured || entry->active
            || entry->pendingBody || entry->pendingUserData
            || entry->pendingGeom || entry->geomCount == 0
            || entry->geomCount > kMaxEntryGeoms)
        {
            return EntryCreateStatus::UnsafeFailure;
        }

        const allocation::ResourcePairResult bodyPair =
            allocation::TryCreateResourcePair(
                BodyPairCallbacks(), true);
        if (!bodyPair)
        {
            if (bodyPair.status
                == allocation::ResourcePairStatus::PrimaryCleanupFailed)
            {
                entry->pendingBody =
                    static_cast<dxBody *>(bodyPair.primary);
                return EntryCreateStatus::UnsafeFailure;
            }
            return EntryCreateStatus::RecoverableFailure;
        }

        auto *const body = static_cast<dxBody *>(bodyPair.primary);
        auto *const userData =
            static_cast<OpaqueUserData *>(bodyPair.companion);
        const allocation::ResourcePairResult geomPair =
            allocation::TryCreateResourcePair(
                GeomPairCallbacks(), entry->geomCount == 2);
        if (!geomPair)
        {
            if (geomPair.status
                == allocation::ResourcePairStatus::PrimaryCleanupFailed)
            {
                entry->pendingGeom =
                    static_cast<OpaqueGeom *>(geomPair.primary);
            }
            const bool userDataFreed = userDataPool.Free(userData);
            if (!userDataFreed)
                entry->pendingUserData = userData;
            const bool bodyFreed = FreeBodyPrimary(
                &bodyPairContext, body);
            if (!bodyFreed)
                entry->pendingBody = body;
            return entry->pendingGeom || !userDataFreed || !bodyFreed
                ? EntryCreateStatus::UnsafeFailure
                : EntryCreateStatus::RecoverableFailure;
        }

        std::array<OpaqueGeom *, kMaxEntryGeoms> geoms{};
        geoms[0] = static_cast<OpaqueGeom *>(geomPair.primary);
        if (entry->geomCount == 2)
            geoms[1] = static_cast<OpaqueGeom *>(geomPair.companion);

        *body = {
            entry->identity,
            static_cast<std::uint32_t>(entry->ownerIndex),
            kBodyMarker,
        };
        *userData = {
            body, entry->identity, kUserDataMarker, 0};
        for (std::size_t index = 0; index < entry->geomCount; ++index)
        {
            *geoms[index] = {
                body,
                entry->identity + index + 1u,
                kGeomMarker,
                static_cast<std::uint32_t>(index),
            };
        }

        const fx::physics::TokenResult binding = fx::physics::Bind(
            &sidecar, entry->ownerIndex, body);
        if (!binding)
        {
            bool cleanupSucceeded = true;
            for (std::size_t index = entry->geomCount;
                 index != 0;
                 --index)
            {
                cleanupSucceeded = geomPool.Free(geoms[index - 1])
                    && cleanupSucceeded;
            }
            const bool userDataFreed = userDataPool.Free(userData);
            cleanupSucceeded = userDataFreed && cleanupSucceeded;
            if (!userDataFreed)
                entry->pendingUserData = userData;
            const bool bodyFreed = FreeBodyPrimary(
                &bodyPairContext, body);
            if (!bodyFreed)
                entry->pendingBody = body;
            (void)cleanupSucceeded;
            return EntryCreateStatus::UnsafeFailure;
        }

        entry->active = true;
        entry->body = body;
        entry->userData = userData;
        entry->geoms = geoms;
        entry->token = binding.token;
        return EntryCreateStatus::Success;
    }

    bool ConfigureAndCreateEntries(
        const std::size_t count,
        const std::size_t *const geomCounts,
        const std::size_t *const owners) noexcept
    {
        if (count > entries.size() || (count != 0 && !geomCounts))
            return false;
        entryCount = count;
        for (std::size_t index = 0; index < count; ++index)
        {
            FxEntry &entry = entries[index];
            entry.configured = true;
            entry.ownerIndex = owners ? owners[index] : 100u + index;
            entry.geomCount = geomCounts[index];
            entry.identity = kFxBodyBase + index;
            if (CreateEntry(&entry) != EntryCreateStatus::Success)
                return false;
        }
        return true;
    }

    bool Setup(
        const std::size_t requestedNonFxBodies,
        const std::size_t requestedNonFxGeoms,
        const std::size_t requestedEntries,
        const std::size_t *const geomCounts,
        const std::size_t *const owners = nullptr) noexcept
    {
        return InitializePools()
            && AllocateNonFxBodies(requestedNonFxBodies)
            && AllocateNonFxGeoms(requestedNonFxGeoms)
            && ConfigureAndCreateEntries(
                requestedEntries, geomCounts, owners);
    }

    bool ValidateEntryForRetirement(const FxEntry &entry) const noexcept
    {
        if (!entry.configured || !entry.active || !entry.body
            || !entry.userData || entry.pendingBody
            || entry.pendingUserData || entry.pendingGeom
            || entry.geomCount == 0
            || entry.geomCount > entry.geoms.size()
            || !bodyPool.IsAllocated(entry.body)
            || !userDataPool.IsAllocated(entry.userData))
        {
            return false;
        }
        const fx::physics::BodyResult resolved = fx::physics::Resolve(
            &sidecar, entry.ownerIndex, entry.token);
        if (!resolved || resolved.body != entry.body)
            return false;
        for (std::size_t index = 0; index < entry.geomCount; ++index)
        {
            if (!entry.geoms[index]
                || !geomPool.IsAllocated(entry.geoms[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool ValidateEntryForReconstruction(
        const FxEntry &entry) const noexcept
    {
        if (!entry.configured || entry.active || entry.body
            || entry.userData || entry.pendingBody
            || entry.pendingUserData || entry.pendingGeom
            || entry.token != fx::physics::INVALID_BODY_TOKEN
            || entry.geomCount == 0
            || entry.geomCount > entry.geoms.size())
        {
            return false;
        }
        for (OpaqueGeom *const geom : entry.geoms)
        {
            if (geom)
                return false;
        }
        return fx::physics::ValidateVacantOwner(
                   &sidecar, entry.ownerIndex)
            == fx::physics::SidecarStatus::Success;
    }

    BatchStatus RetireEntry(FxEntry *const entry) noexcept
    {
        if (!entry || !ValidateEntryForRetirement(*entry))
            return BatchStatus::UnsafeFailure;
        const fx::physics::BodyResult taken = fx::physics::Take(
            &sidecar, entry->ownerIndex, entry->token);
        if (!taken || taken.body != entry->body)
            return BatchStatus::UnsafeFailure;

        bool freed = true;
        for (std::size_t index = entry->geomCount; index != 0; --index)
        {
            freed = geomPool.Free(entry->geoms[index - 1]) && freed;
            entry->geoms[index - 1] = nullptr;
        }
        freed = userDataPool.Free(entry->userData) && freed;
        freed = bodyPool.Free(entry->body) && freed;
        entry->active = false;
        entry->body = nullptr;
        entry->userData = nullptr;
        entry->token = fx::physics::INVALID_BODY_TOKEN;
        return freed ? BatchStatus::Success : BatchStatus::UnsafeFailure;
    }

    static BatchStatus PerformBatch(
        void *const opaque,
        const BatchOperation operation,
        const std::size_t entryIndex) noexcept
    {
        auto *const fixture = static_cast<Fixture *>(opaque);
        if (!fixture || entryIndex >= fixture->entryCount)
            return BatchStatus::UnsafeFailure;
        ++fixture->callbackCount;
        FxEntry &entry = fixture->entries[entryIndex];
        switch (operation)
        {
        case BatchOperation::ValidateRetirement:
            return fixture->ValidateEntryForRetirement(entry)
                ? BatchStatus::Success
                : BatchStatus::RecoverableFailure;
        case BatchOperation::Retire:
            return fixture->RetireEntry(&entry);
        case BatchOperation::ValidateReconstruction:
            return fixture->ValidateEntryForReconstruction(entry)
                ? BatchStatus::Success
                : BatchStatus::RecoverableFailure;
        case BatchOperation::Reconstruct:
            switch (fixture->CreateEntry(&entry))
            {
            case EntryCreateStatus::Success:
                return BatchStatus::Success;
            case EntryCreateStatus::RecoverableFailure:
                return BatchStatus::RecoverableFailure;
            case EntryCreateStatus::UnsafeFailure:
                return BatchStatus::UnsafeFailure;
            }
        }
        return BatchStatus::UnsafeFailure;
    }

    archive::ArchivePhysicsBatchCallbacks BatchCallbacks() noexcept
    {
        return {this, PerformBatch};
    }

    archive::PhysicsResourceCount FreeCapacity() const noexcept
    {
        return {
            bodyPool.FreeCount(),
            userDataPool.FreeCount(),
            geomPool.FreeCount(),
        };
    }

    void MakeCandidates(
        archive::PhysicsRetirementCandidate *const candidates) const noexcept
    {
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            candidates[index] = {
                index,
                entries[index].ownerIndex,
                entries[index].geomCount,
            };
        }
    }

    bool ReserveGeoms(const std::size_t count) noexcept
    {
        if (geomBlockerCount != 0 || count > geomBlockers.size())
            return false;
        for (std::size_t index = 0; index < count; ++index)
        {
            OpaqueGeom *const geom = geomPool.Allocate();
            if (!geom)
                return false;
            *geom = {nullptr, 0xB1000000u + index, kGeomMarker,
                     static_cast<std::uint32_t>(index)};
            geomBlockers[geomBlockerCount++] = geom;
        }
        return true;
    }

    bool ReleaseGeomBlockers() noexcept
    {
        bool success = true;
        while (geomBlockerCount != 0)
        {
            --geomBlockerCount;
            success = geomPool.Free(geomBlockers[geomBlockerCount])
                && success;
            geomBlockers[geomBlockerCount] = nullptr;
        }
        return success;
    }

    bool ReserveUserDataBlocker() noexcept
    {
        if (userDataBlocker)
            return false;
        userDataBlocker = userDataPool.Allocate();
        if (!userDataBlocker)
            return false;
        *userDataBlocker = {
            nullptr, 0xB2000000u, kUserDataMarker, 0};
        return true;
    }

    bool ReleaseUserDataBlocker() noexcept
    {
        if (!userDataBlocker)
            return true;
        OpaqueUserData *const blocker = userDataBlocker;
        userDataBlocker = nullptr;
        return userDataPool.Free(blocker);
    }

    bool RecoverPendingBody(FxEntry *const entry) noexcept
    {
        if (!entry || !entry->pendingBody)
            return false;
        dxBody *const pending = entry->pendingBody;
        const bool savedRefusal = refuseBodyCleanup;
        refuseBodyCleanup = false;
        const bool success = bodyPool.Free(pending);
        refuseBodyCleanup = savedRefusal;
        if (success)
            entry->pendingBody = nullptr;
        return success;
    }

    bool RecoverOtherPendingResources(FxEntry *const entry) noexcept
    {
        if (!entry)
            return false;
        bool success = true;
        if (entry->pendingGeom)
        {
            OpaqueGeom *const pending = entry->pendingGeom;
            if (geomPool.Free(pending))
                entry->pendingGeom = nullptr;
            else
                success = false;
        }
        if (entry->pendingUserData)
        {
            OpaqueUserData *const pending = entry->pendingUserData;
            if (userDataPool.Free(pending))
                entry->pendingUserData = nullptr;
            else
                success = false;
        }
        return success;
    }

    bool NonFxIdentitiesAreExact() const noexcept
    {
        for (std::size_t index = 0; index < nonFxBodyCount; ++index)
        {
            const NonFxBodyRecord &record = nonFxBodies[index];
            if (!record.body || !record.userData
                || !bodyPool.IsAllocated(record.body)
                || !userDataPool.IsAllocated(record.userData)
                || record.identity != kNonFxBodyBase + index
                || record.body->identity != record.identity
                || record.body->marker != kBodyMarker
                || record.userData->body != record.body
                || record.userData->identity != record.identity
                || record.userData->marker != kUserDataMarker)
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < nonFxGeomCount; ++index)
        {
            const OpaqueGeom *const geom = nonFxGeoms[index];
            if (!geom || !geomPool.IsAllocated(geom)
                || geom->identity != kNonFxGeomBase + index
                || geom->marker != kGeomMarker)
            {
                return false;
            }
        }
        return true;
    }

    bool ActiveEntriesAreValid() const noexcept
    {
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            const FxEntry &entry = entries[index];
            if (!entry.active)
                continue;
            if (!ValidateEntryForRetirement(entry)
                || entry.body->identity != entry.identity
                || entry.body->owner != entry.ownerIndex
                || entry.body->marker != kBodyMarker
                || entry.userData->body != entry.body
                || entry.userData->identity != entry.identity
                || entry.userData->marker != kUserDataMarker)
            {
                return false;
            }
            for (std::size_t geomIndex = 0;
                 geomIndex < entry.geomCount;
                 ++geomIndex)
            {
                const OpaqueGeom *const geom = entry.geoms[geomIndex];
                if (geom->body != entry.body
                    || geom->identity
                        != entry.identity + geomIndex + 1u
                    || geom->marker != kGeomMarker
                    || geom->ordinal != geomIndex)
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool IsConserved() const noexcept
    {
        std::size_t activeEntries = 0;
        std::size_t activeEntryGeoms = 0;
        std::size_t pendingBodies = 0;
        std::size_t pendingUserData = 0;
        std::size_t pendingGeoms = 0;
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            const FxEntry &entry = entries[index];
            if (entry.active)
            {
                ++activeEntries;
                activeEntryGeoms += entry.geomCount;
            }
            if (entry.pendingBody)
            {
                if (!bodyPool.IsAllocated(entry.pendingBody))
                    return false;
                ++pendingBodies;
            }
            if (entry.pendingUserData)
            {
                if (!userDataPool.IsAllocated(entry.pendingUserData))
                    return false;
                ++pendingUserData;
            }
            if (entry.pendingGeom)
            {
                if (!geomPool.IsAllocated(entry.pendingGeom))
                    return false;
                ++pendingGeoms;
            }
        }

        const std::size_t expectedBodies =
            nonFxBodyCount + activeEntries + pendingBodies;
        const std::size_t expectedUserData = nonFxBodyCount
            + activeEntries + pendingUserData
            + (userDataBlocker ? 1u : 0u);
        const std::size_t expectedGeoms = nonFxGeomCount
            + activeEntryGeoms + pendingGeoms + geomBlockerCount;
        if (!bodyPool.IsConserved() || !userDataPool.IsConserved()
            || !geomPool.IsConserved()
            || bodyPool.ActiveCount() != expectedBodies
            || userDataPool.ActiveCount() != expectedUserData
            || geomPool.ActiveCount() != expectedGeoms
            || sidecar.ActiveCount() != activeEntries
            || fx::physics::Validate(&sidecar)
                != fx::physics::SidecarStatus::Success
            || !NonFxIdentitiesAreExact()
            || !ActiveEntriesAreValid())
        {
            return false;
        }
        for (std::size_t index = 0; index < geomBlockerCount; ++index)
        {
            if (!geomPool.IsAllocated(geomBlockers[index]))
                return false;
        }
        return !userDataBlocker
            || userDataPool.IsAllocated(userDataBlocker);
    }

    bool IsLogicalBaseline() const noexcept
    {
        if (geomBlockerCount != 0 || userDataBlocker)
            return false;
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            if (!entries[index].active || entries[index].pendingBody
                || entries[index].pendingUserData
                || entries[index].pendingGeom)
                return false;
        }
        return IsConserved();
    }

    StateDigest CaptureDigest() const noexcept
    {
        StateDigest digest{
            bodyPool.Digest(),
            userDataPool.Digest(),
            geomPool.Digest(),
            1469598103934665603ull,
            1469598103934665603ull,
        };
        fx::physics::OwnershipSnapshot ownership{};
        const fx::physics::SidecarStatus snapshotStatus =
            fx::physics::SnapshotOwnership(&sidecar, &ownership);
        digest.ownership = HashValue(digest.ownership, snapshotStatus);
        digest.ownership = HashValue(digest.ownership, ownership.count);
        digest.ownership = HashBytes(
            digest.ownership,
            ownership.records.data(),
            ownership.records.size() * sizeof(ownership.records[0]));

        digest.fixture = HashValue(digest.fixture, nonFxBodyCount);
        digest.fixture = HashValue(digest.fixture, nonFxGeomCount);
        digest.fixture = HashValue(digest.fixture, entryCount);
        digest.fixture = HashValue(digest.fixture, geomBlockerCount);
        digest.fixture = HashValue(digest.fixture, userDataBlocker);
        digest.fixture = HashValue(digest.fixture, callbackCount);
        digest.fixture = HashValue(digest.fixture, refuseBodyCleanup);
        digest.fixture = HashBytes(
            digest.fixture, entries.data(),
            entries.size() * sizeof(entries[0]));
        return digest;
    }

    void Cleanup() noexcept
    {
        refuseBodyCleanup = false;
        ReleaseGeomBlockers();
        ReleaseUserDataBlocker();
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            if (entries[index].pendingBody)
                RecoverPendingBody(&entries[index]);
            RecoverOtherPendingResources(&entries[index]);
            if (entries[index].active)
                RetireEntry(&entries[index]);
        }
        while (nonFxGeomCount != 0)
        {
            --nonFxGeomCount;
            geomPool.Free(nonFxGeoms[nonFxGeomCount]);
            nonFxGeoms[nonFxGeomCount] = nullptr;
        }
        while (nonFxBodyCount != 0)
        {
            --nonFxBodyCount;
            NonFxBodyRecord &record = nonFxBodies[nonFxBodyCount];
            userDataPool.Free(record.userData);
            bodyPool.Free(record.body);
            record = {};
        }
    }
};

BatchStatus RunRetirement(
    Fixture *const fixture,
    const std::size_t *const indices,
    const std::size_t count,
    std::size_t *const completed) noexcept
{
    return archive::RunArchivePhysicsRetirementBatch(
        fixture->BatchCallbacks(), indices, count,
        fixture->entryCount, completed);
}

BatchStatus RunReconstruction(
    Fixture *const fixture,
    const std::size_t *const indices,
    const std::size_t count,
    std::size_t *const completed) noexcept
{
    return archive::RunArchivePhysicsReconstructionBatch(
        fixture->BatchCallbacks(), indices, count,
        fixture->entryCount, completed);
}

archive::PhysicsRetirementPlan MakeSentinelPlan() noexcept
{
    archive::PhysicsRetirementPlan plan{};
    plan.entryIndices.fill(
        (std::numeric_limits<std::size_t>::max)());
    plan.count = 71;
    plan.released = {91, 92, 93};
    return plan;
}

bool PlansEqual(
    const archive::PhysicsRetirementPlan &left,
    const archive::PhysicsRetirementPlan &right) noexcept
{
    return left.entryIndices == right.entryIndices
        && left.count == right.count
        && left.released.bodies == right.released.bodies
        && left.released.userData == right.released.userData
        && left.released.geoms == right.released.geoms;
}

void TestFullPoolRetirementAndReconstruction()
{
    constexpr std::array<std::size_t, kMaxFxEntries> geomCounts{
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    Fixture fixture{};
    Check(fixture.Setup(500, 2024, geomCounts.size(),
                        geomCounts.data()),
          "exact 512/512/2048 baseline initializes");
    Check(fixture.IsLogicalBaseline(),
          "full baseline conserves all exact-size pools");

    std::array<dxBody *, kMaxFxEntries> originalBodies{};
    std::array<std::array<OpaqueGeom *, kMaxEntryGeoms>,
               kMaxFxEntries>
        originalGeoms{};
    for (std::size_t index = 0; index < fixture.entryCount; ++index)
    {
        originalBodies[index] = fixture.entries[index].body;
        originalGeoms[index] = fixture.entries[index].geoms;
    }

    std::array<archive::PhysicsRetirementCandidate, kMaxFxEntries>
        candidates{};
    fixture.MakeCandidates(candidates.data());
    archive::PhysicsRetirementPlan plan{};
    const archive::PhysicsRetirementPlanStatus planStatus =
        archive::BuildPhysicsRetirementPlan(
            fixture.FreeCapacity(), {12, 12, 24},
            candidates.data(), fixture.entryCount, &plan);
    Check(planStatus == archive::PhysicsRetirementPlanStatus::Success
              && plan.count == 12
              && plan.released.bodies == 12
              && plan.released.userData == 12
              && plan.released.geoms == 24,
          "full occupancy planner retires all twelve FX entries");

    std::size_t completed = 0;
    Check(RunRetirement(&fixture, plan.entryIndices.data(), plan.count,
                        &completed)
              == BatchStatus::Success
              && completed == plan.count,
          "controller retires the complete full-pool plan");
    Check(fixture.bodyPool.ActiveCount() == 500
              && fixture.userDataPool.ActiveCount() == 500
              && fixture.geomPool.ActiveCount() == 2024
              && fixture.sidecar.ActiveCount() == 0
              && fixture.IsConserved(),
          "retirement releases exactly 12/12/24 slots");
    Check(fixture.NonFxIdentitiesAreExact(),
          "retirement preserves every non-FX address and payload identity");

    std::array<std::size_t, kMaxFxEntries> reversePlan{};
    for (std::size_t index = 0; index < plan.count; ++index)
        reversePlan[index] = plan.entryIndices[plan.count - 1u - index];
    completed = 0;
    Check(RunReconstruction(&fixture, reversePlan.data(), plan.count,
                            &completed)
              == BatchStatus::Success
              && completed == plan.count,
          "controller reconstructs all twelve retired entries");
    Check(fixture.IsLogicalBaseline()
              && fixture.bodyPool.ActiveCount() == kBodyCapacity
              && fixture.userDataPool.ActiveCount() == kUserDataCapacity
              && fixture.geomPool.ActiveCount() == kGeomCapacity,
          "reconstruction returns every exact-size pool to full baseline");
    Check(fixture.NonFxIdentitiesAreExact(),
          "reconstruction preserves exact non-FX identities");

    bool fxSlotsRestored = true;
    for (std::size_t index = 0; index < fixture.entryCount; ++index)
    {
        fxSlotsRestored = fxSlotsRestored
            && fixture.entries[index].body == originalBodies[index]
            && fixture.entries[index].geoms == originalGeoms[index];
    }
    Check(fxSlotsRestored,
          "reverse reconstruction demonstrates lossless LIFO slot ownership");
}

void TestMixedGeometryMinimumRetirement()
{
    constexpr std::array<std::size_t, 4> geomCounts{1, 2, 1, 2};
    constexpr std::array<std::size_t, 4> owners{30, 20, 40, 10};
    Fixture fixture{};
    Check(fixture.Setup(508, 2042, geomCounts.size(),
                        geomCounts.data(), owners.data()),
          "mixed one/two-geom full baseline initializes");

    std::array<archive::PhysicsRetirementCandidate, kMaxFxEntries>
        candidates{};
    fixture.MakeCandidates(candidates.data());
    archive::PhysicsRetirementPlan plan{};
    const auto status = archive::BuildPhysicsRetirementPlan(
        fixture.FreeCapacity(), {2, 2, 3}, candidates.data(),
        fixture.entryCount, &plan);
    Check(status == archive::PhysicsRetirementPlanStatus::Success
              && plan.count == 2
              && plan.released.geoms == 4
              && plan.entryIndices[0] == 3
              && plan.entryIndices[1] == 1,
          "planner deterministically chooses the minimum two high-geom entries");

    std::size_t completed = 0;
    Check(RunRetirement(&fixture, plan.entryIndices.data(), plan.count,
                        &completed)
              == BatchStatus::Success
              && completed == 2
              && fixture.bodyPool.FreeCount() == 2
              && fixture.userDataPool.FreeCount() == 2
              && fixture.geomPool.FreeCount() == 4
              && fixture.IsConserved(),
          "mixed plan releases its exact resource demand");

    const std::array<std::size_t, 2> reverse{
        plan.entryIndices[1], plan.entryIndices[0]};
    completed = 0;
    Check(RunReconstruction(&fixture, reverse.data(), reverse.size(),
                            &completed)
              == BatchStatus::Success
              && completed == reverse.size()
              && fixture.IsLogicalBaseline(),
          "mixed retirement reconstructs to its full baseline");
}

void TestRecoverableReconstructionAfterPrefix()
{
    constexpr std::array<std::size_t, kMaxFxEntries> geomCounts{
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    Fixture fixture{};
    Check(fixture.Setup(500, 2024, geomCounts.size(),
                        geomCounts.data()),
          "prefix-failure baseline initializes");

    std::array<archive::PhysicsRetirementCandidate, kMaxFxEntries>
        candidates{};
    fixture.MakeCandidates(candidates.data());
    archive::PhysicsRetirementPlan plan{};
    Check(archive::BuildPhysicsRetirementPlan(
              fixture.FreeCapacity(), {3, 3, 6}, candidates.data(),
              fixture.entryCount, &plan)
              == archive::PhysicsRetirementPlanStatus::Success
              && plan.count == 3,
          "planner selects the three-entry recovery fixture");

    std::size_t completed = 0;
    Check(RunRetirement(&fixture, plan.entryIndices.data(), plan.count,
                        &completed)
              == BatchStatus::Success
              && completed == 3,
          "recovery fixture retires three entries");
    Check(fixture.ReserveGeoms(3),
          "real geom allocations constrain desired reconstruction");

    completed = 0;
    const BatchStatus failed = RunReconstruction(
        &fixture, plan.entryIndices.data(), plan.count, &completed);
    Check(failed == BatchStatus::RecoverableFailure && completed == 1,
          "second reconstruction fails recoverably after one committed prefix");
    Check(fixture.entries[plan.entryIndices[0]].active
              && !fixture.entries[plan.entryIndices[1]].active
              && !fixture.entries[plan.entryIndices[2]].active
              && !fixture.entries[plan.entryIndices[1]].pendingBody
              && fixture.IsConserved(),
          "failed current entry rolls body/userdata/primary geom back exactly");

    Check(fixture.ReleaseGeomBlockers(),
          "recovery releases externally held geometry capacity");
    const std::array<std::size_t, 2> remaining{
        plan.entryIndices[1], plan.entryIndices[2]};
    completed = 0;
    Check(RunReconstruction(&fixture, remaining.data(), remaining.size(),
                            &completed)
              == BatchStatus::Success
              && completed == remaining.size()
              && fixture.IsLogicalBaseline()
              && fixture.NonFxIdentitiesAreExact(),
          "remaining reconstruction recovers the exact logical baseline");
}

void TestCapacityAndSelectionRejectionsAreTransactional()
{
    constexpr std::array<std::size_t, 2> geomCounts{2, 2};
    Fixture fixture{};
    Check(fixture.Setup(510, 2044, geomCounts.size(),
                        geomCounts.data()),
          "transactional rejection baseline initializes");

    std::array<archive::PhysicsRetirementCandidate, kMaxFxEntries>
        candidates{};
    fixture.MakeCandidates(candidates.data());
    const StateDigest beforeCapacity = fixture.CaptureDigest();
    const archive::PhysicsRetirementPlan sentinel = MakeSentinelPlan();
    archive::PhysicsRetirementPlan plan = sentinel;
    Check(archive::BuildPhysicsRetirementPlan(
              fixture.FreeCapacity(), {3, 3, 6}, candidates.data(),
              fixture.entryCount, &plan)
              == archive::PhysicsRetirementPlanStatus::InsufficientCapacity
              && PlansEqual(plan, sentinel)
              && fixture.CaptureDigest() == beforeCapacity,
          "insufficient capacity preserves planner output and all ownership");

    constexpr std::array<std::size_t, 2> outOfRange{0, 2};
    StateDigest beforeBatch = fixture.CaptureDigest();
    std::size_t completed = 99;
    Check(RunRetirement(&fixture, outOfRange.data(), outOfRange.size(),
                        &completed)
              == BatchStatus::UnsafeFailure
              && completed == 0
              && fixture.CaptureDigest() == beforeBatch,
          "out-of-range retirement selection invokes no callback or mutation");

    constexpr std::array<std::size_t, 2> duplicate{0, 0};
    beforeBatch = fixture.CaptureDigest();
    completed = 99;
    Check(RunReconstruction(&fixture, duplicate.data(), duplicate.size(),
                            &completed)
              == BatchStatus::UnsafeFailure
              && completed == 0
              && fixture.CaptureDigest() == beforeBatch,
          "duplicate reconstruction selection invokes no callback or mutation");
    Check(fixture.IsLogicalBaseline(),
          "transactional rejection cases retain the original full baseline");
}

void TestCleanupRefusalRetainsOwnership()
{
    constexpr std::array<std::size_t, 1> geomCounts{2};
    Fixture fixture{};
    Check(fixture.Setup(511, 2046, geomCounts.size(),
                        geomCounts.data()),
          "cleanup-refusal baseline initializes");
    constexpr std::array<std::size_t, 1> plan{0};
    std::size_t completed = 0;
    Check(RunRetirement(&fixture, plan.data(), plan.size(), &completed)
              == BatchStatus::Success
              && completed == 1
              && fixture.ReserveUserDataBlocker(),
          "cleanup-refusal fixture retires one pair and consumes userdata slot");

    fixture.refuseBodyCleanup = true;
    completed = 77;
    const BatchStatus status = RunReconstruction(
        &fixture, plan.data(), plan.size(), &completed);
    FxEntry &entry = fixture.entries[0];
    Check(status == BatchStatus::UnsafeFailure && completed == 0,
          "refused primary cleanup propagates UnsafeFailure");
    Check(!entry.active && entry.pendingBody
              && fixture.bodyPool.IsAllocated(entry.pendingBody)
              && fixture.sidecar.ActiveCount() == 0
              && fixture.IsConserved(),
          "UnsafeFailure retains the allocated body in explicit ownership");

    fixture.refuseBodyCleanup = false;
    Check(fixture.ReleaseUserDataBlocker()
              && fixture.RecoverPendingBody(&entry),
          "caller can release blocker and recover retained primary ownership");
    completed = 0;
    Check(RunReconstruction(&fixture, plan.data(), plan.size(), &completed)
              == BatchStatus::Success
              && completed == 1
              && fixture.IsLogicalBaseline()
              && fixture.NonFxIdentitiesAreExact(),
          "cleanup-refusal recovery reconstructs the full exact baseline");
}
} // namespace

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++assertionCount;
}

int main()
{
    TestFullPoolRetirementAndReconstruction();
    TestMixedGeometryMinimumRetirement();
    TestRecoverableReconstructionAfterPrefix();
    TestCapacityAndSelectionRejectionsAreTransactional();
    TestCleanupRefusalRetainsOwnership();

    Check(assertionCount == 0,
          "valid occupancy transactions must not trigger engine assertions");
    if (failures != 0)
    {
        std::fprintf(stderr, "%d ODE occupancy test(s) failed\n", failures);
        return 1;
    }
    std::puts("exact-size ODE fixed-pool occupancy tests passed");
    return 0;
}
