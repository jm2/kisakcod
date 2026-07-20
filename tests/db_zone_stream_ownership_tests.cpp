#include <database/db_stream.h>
#include <database/db_zone_stream_ownership.h>
#include <database/db_zone_stream_ownership_internal.h>
#include <qcommon/com_error.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <type_traits>

namespace
{
namespace ownership = db::zone_stream_ownership;
namespace lifecycle = db::zone_load;
namespace relocation = db::relocation;

int failures = 0;
int reportCount = 0;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void ExpectStatus(
    const ownership::ZoneStreamOwnershipStatus actual,
    const ownership::ZoneStreamOwnershipStatus expected,
    const char *const message)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << message << " (got "
                  << static_cast<unsigned>(actual) << ", expected "
                  << static_cast<unsigned>(expected) << ")\n";
        ++failures;
    }
}

void ExpectRelocationStatus(
    const relocation::Status actual,
    const relocation::Status expected,
    const char *const message)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << message << " (got "
                  << relocation::StatusName(actual) << ", expected "
                  << relocation::StatusName(expected) << ")\n";
        ++failures;
    }
}

struct ZoneFixture final
{
    alignas(16) std::array<std::array<std::uint8_t, 64>,
        relocation::kBlockCount> storage{};
    XZoneMemory zone{};
    relocation::BlockView blocks[relocation::kBlockCount]{};

    ZoneFixture() noexcept
    {
        for (std::size_t i = 0; i < relocation::kBlockCount; ++i)
        {
            zone.blocks[i].data = storage[i].data();
            zone.blocks[i].size =
                static_cast<std::uint32_t>(storage[i].size());
            blocks[i].base = reinterpret_cast<std::uintptr_t>(
                storage[i].data());
            blocks[i].size = zone.blocks[i].size;
        }
    }
};

struct StreamSnapshot final
{
    std::uint32_t delayIndex = 0;
    std::uint32_t posIndex = 0;
    std::uint32_t stackIndex = 0;
    XZoneMemory *zone = nullptr;
    std::uint8_t *pos = nullptr;
    std::array<std::uint8_t *, relocation::kBlockCount> cursors{};
    std::array<StreamDelayInfo, 4096> delays{};
    std::array<StreamPosInfo, 64> stack{};
};

StreamSnapshot SnapshotStreams() noexcept
{
    StreamSnapshot snapshot{};
    snapshot.delayIndex = g_streamDelayIndex;
    snapshot.posIndex = g_streamPosIndex;
    snapshot.stackIndex = g_streamPosStackIndex;
    snapshot.zone = g_streamZoneMem;
    snapshot.pos = g_streamPos;
    std::copy_n(
        g_streamPosArray,
        relocation::kBlockCount,
        snapshot.cursors.begin());
    std::copy_n(g_streamDelayArray, 4096, snapshot.delays.begin());
    std::copy_n(g_streamPosStack, 64, snapshot.stack.begin());
    return snapshot;
}

bool StreamsEqual(
    const StreamSnapshot &left,
    const StreamSnapshot &right) noexcept
{
    if (left.delayIndex != right.delayIndex
        || left.posIndex != right.posIndex
        || left.stackIndex != right.stackIndex
        || left.zone != right.zone || left.pos != right.pos
        || left.cursors != right.cursors)
    {
        return false;
    }
    for (std::size_t i = 0; i < left.delays.size(); ++i)
    {
        if (left.delays[i].ptr != right.delays[i].ptr
            || left.delays[i].size != right.delays[i].size)
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < left.stack.size(); ++i)
    {
        if (left.stack[i].pos != right.stack[i].pos
            || left.stack[i].index != right.stack[i].index)
        {
            return false;
        }
    }
    return true;
}

template <typename T>
std::array<std::byte, sizeof(T)> ObjectBytes(const T &object) noexcept
{
    std::array<std::byte, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &object, sizeof(object));
    return bytes;
}

void RawCopy(
    void *const destination,
    const void *const source,
    const std::size_t size) noexcept
{
    std::memcpy(destination, source, size);
}

lifecycle::ZoneLoadContextKey Claim(
    lifecycle::ZoneLoadContextSlot *const slot,
    const std::uint32_t slotIndex,
    const std::uint64_t initialGeneration = 0)
{
    lifecycle::ZoneLoadContextKey key{};
    Expect(
        lifecycle::TryInitializeZoneLoadContextSlot(
            slot, slotIndex, initialGeneration)
            == lifecycle::ZoneLoadContextStatus::Success,
        "initialize lifecycle slot");
    Expect(
        lifecycle::TryClaimZoneLoadContext(slot, &key)
            == lifecycle::ZoneLoadContextStatus::Success,
        "claim lifecycle generation");
    return key;
}

lifecycle::ZoneLoadCleanupCallbackStatus CompleteCleanup(
    void *, lifecycle::ZoneLoadCleanupOperation) noexcept
{
    return lifecycle::ZoneLoadCleanupCallbackStatus::Success;
}

void ReleaseLoadingGeneration(
    lifecycle::ZoneLoadContextSlot *const slot,
    const lifecycle::ZoneLoadContextKey &key)
{
    Expect(
        lifecycle::TryBeginZoneLoadContextAbandonment(slot, key)
            == lifecycle::ZoneLoadContextStatus::Success,
        "begin lifecycle abandonment");
    lifecycle::ZoneLoadCleanupCallbacks callbacks{};
    callbacks.context = slot;
    callbacks.perform = CompleteCleanup;
    Expect(
        lifecycle::TryFinishZoneLoadContextAbandonment(
            slot, key, callbacks)
            == lifecycle::ZoneLoadContextStatus::Success,
        "finish lifecycle abandonment");
}

void ReleaseLiveGeneration(
    lifecycle::ZoneLoadContextSlot *const slot,
    const lifecycle::ZoneLoadContextKey &key)
{
    lifecycle::ZoneLoadCleanupCallbacks callbacks{};
    callbacks.context = slot;
    callbacks.perform = CompleteCleanup;
    Expect(
        lifecycle::TryUnloadZoneLoadContext(slot, key, callbacks)
            == lifecycle::ZoneLoadContextStatus::Success,
        "finish lifecycle unload");
}

void ExpectAllStreamsScrubbed()
{
    Expect(g_streamDelayIndex == 0, "delay count scrubbed");
    Expect(g_streamPosIndex == 0, "stream index scrubbed");
    Expect(g_streamPosStackIndex == 0, "stream stack depth scrubbed");
    Expect(g_streamZoneMem == nullptr, "zone identity scrubbed");
    Expect(g_streamPos == nullptr, "active cursor scrubbed");
    for (const std::uint8_t *const cursor : g_streamPosArray)
        Expect(cursor == nullptr, "all nine block cursors scrubbed");
    for (const StreamDelayInfo &delay : g_streamDelayArray)
    {
        Expect(delay.ptr == nullptr, "delayed stream pointer scrubbed");
        Expect(delay.size == 0, "delayed stream size scrubbed");
    }
    for (const StreamPosInfo &saved : g_streamPosStack)
    {
        Expect(saved.pos == nullptr, "saved stream pointer scrubbed");
        Expect(saved.index == 0, "saved stream index scrubbed");
    }
}

void TestConstructionAndKeyValidation()
{
    static_assert(std::is_standard_layout_v<
        ownership::ZoneStreamGenerationReceipt>);
    static_assert(std::is_standard_layout_v<
        ownership::ActiveZoneStreamBinding>);
    static_assert(std::is_trivially_destructible_v<
        ownership::ZoneStreamGenerationReceipt>);
    static_assert(std::is_trivially_destructible_v<
        ownership::ActiveZoneStreamBinding>);
    static_assert(!std::is_copy_constructible_v<
        ownership::ZoneStreamGenerationReceipt>);
    static_assert(!std::is_move_constructible_v<
        ownership::ZoneStreamGenerationReceipt>);
    static_assert(!std::is_copy_constructible_v<
        ownership::ActiveZoneStreamBinding>);
    static_assert(!std::is_move_constructible_v<
        ownership::ActiveZoneStreamBinding>);

    ownership::ZoneStreamGenerationReceipt receipt;
    ownership::ActiveZoneStreamBinding active;
    Expect(receipt.canonical(), "default receipt is canonical");
    Expect(
        receipt.phase() == ownership::ZoneStreamGenerationPhase::NeverBound,
        "default receipt is pristine NeverBound");
    Expect(active.canonical(), "default active controller is canonical");
    Expect(
        active.phase() == ownership::ActiveZoneStreamPhase::Idle,
        "default active controller is idle");
    static_assert(noexcept(
        ownership::AuthenticatePassiveZoneStreamSingleton(active)));
    Expect(
        ownership::AuthenticatePassiveZoneStreamSingleton(active),
        "default active controller authenticates the passive singleton");

    lifecycle::ZoneLoadContextSlot lifecycleSlot;
    const lifecycle::ZoneLoadContextKey key = Claim(&lifecycleSlot, 1);

    lifecycle::ZoneLoadContextKey malformed{};
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, malformed),
        ownership::ZoneStreamOwnershipStatus::InvalidKey,
        "null key rejected");
    malformed = key;
    malformed.slot = 0;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, malformed),
        ownership::ZoneStreamOwnershipStatus::InvalidKey,
        "reserved physical slot zero rejected");
    malformed = key;
    malformed.slot = 33;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, malformed),
        ownership::ZoneStreamOwnershipStatus::InvalidKey,
        "out-of-range physical slot rejected");
    malformed = key;
    malformed.reserved = 1;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, malformed),
        ownership::ZoneStreamOwnershipStatus::InvalidKey,
        "reserved key bits rejected");
    malformed = key;
    malformed.slot = 2;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, malformed),
        ownership::ZoneStreamOwnershipStatus::InvalidPhase,
        "cross-slot lifecycle key rejected");

    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "valid generation receipt begins");
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "exact begin retry is idempotent");
    malformed = key;
    ++malformed.generation;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, malformed),
        ownership::ZoneStreamOwnershipStatus::StaleKey,
        "stale begin retry rejected");

    ownership::ZoneStreamGenerationReceipt copiedReceipt;
    RawCopy(&copiedReceipt, &receipt, sizeof(receipt));
    Expect(!copiedReceipt.canonical(), "raw-copied receipt fails self-auth");

    struct ReceiptMirror final
    {
        lifecycle::ZoneLoadContextKey key;
        lifecycle::ZoneLoadContextSlot *lifecycle;
        const ownership::ZoneStreamGenerationReceipt *self;
        std::uint32_t phaseWord;
    };
    static_assert(sizeof(ReceiptMirror) == sizeof(receipt));
    auto *const mirror = reinterpret_cast<ReceiptMirror *>(
        static_cast<void *>(&copiedReceipt));
    RawCopy(&copiedReceipt, &receipt, sizeof(receipt));
    mirror->self = &copiedReceipt;
    mirror->phaseWord |= UINT32_C(0x100);
    Expect(!copiedReceipt.canonical(), "reserved receipt bits fail closed");
    RawCopy(&copiedReceipt, &receipt, sizeof(receipt));
    mirror->self = &copiedReceipt;
    mirror->phaseWord = UINT32_C(0xFF);
    Expect(!copiedReceipt.canonical(), "unknown receipt phase fails closed");

    ownership::ActiveZoneStreamBinding copiedActive;
    RawCopy(&copiedActive, &active, sizeof(active));
    Expect(copiedActive.canonical(), "pristine active representation relocates");
    struct ActiveMirror final
    {
        lifecycle::ZoneLoadContextKey key;
        ownership::ZoneStreamGenerationReceipt *receipt;
        const XZoneMemory *zoneIdentity;
        relocation::BlockView blocks[relocation::kBlockCount];
        const ownership::ActiveZoneStreamBinding *self;
        std::uint32_t phaseWord;
    };
    static_assert(sizeof(ActiveMirror) == sizeof(active));
    auto *const activeMirror = reinterpret_cast<ActiveMirror *>(
        static_cast<void *>(&copiedActive));
    activeMirror->phaseWord = UINT32_C(0x100);
    Expect(!copiedActive.canonical(), "reserved active bits fail closed");
    RawCopy(&copiedActive, &active, sizeof(active));
    activeMirror->phaseWord = UINT32_C(0xFF);
    Expect(!copiedActive.canonical(), "unknown active phase fails closed");

    ReleaseLoadingGeneration(&lifecycleSlot, key);

    lifecycle::ZoneLoadContextSlot highLifecycle;
    const lifecycle::ZoneLoadContextKey highKey = Claim(
        &highLifecycle,
        32,
        (std::numeric_limits<std::uint64_t>::max)() - 1);
    Expect(
        highKey.generation
            == (std::numeric_limits<std::uint64_t>::max)(),
        "highest lifecycle generation claimed without wrap");
    ownership::ZoneStreamGenerationReceipt highReceipt;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &highReceipt, &highLifecycle, highKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "highest exact lifecycle generation accepted");
    ownership::ActiveZoneStreamBinding unusedActive;
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(
            &unusedActive, &highReceipt, highKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "highest NeverBound generation abandons safely");
    ReleaseLoadingGeneration(&highLifecycle, highKey);
}

void TestPassiveSingletonAuthentication()
{
    ownership::ActiveZoneStreamBinding expected;
    ZoneFixture fixture;
    const auto expectRejected = [&](const char *const message)
    {
        Expect(
            !ownership::AuthenticatePassiveZoneStreamSingleton(expected),
            message);
    };

    Expect(
        ownership::AuthenticatePassiveZoneStreamSingleton(expected),
        "scrubbed process singleton authenticates as passive");

    g_streamDelayIndex = 1;
    expectRejected("nonzero delay count rejects passive authentication");
    g_streamDelayIndex = 0;
    g_streamPosIndex = 1;
    expectRejected("nonzero stream index rejects passive authentication");
    g_streamPosIndex = 0;
    g_streamPosStackIndex = 1;
    expectRejected("nonzero stack depth rejects passive authentication");
    g_streamPosStackIndex = 0;
    g_streamZoneMem = &fixture.zone;
    expectRejected("foreign zone identity rejects passive authentication");
    g_streamZoneMem = nullptr;
    g_streamPos = fixture.storage[0].data();
    expectRejected("foreign active cursor rejects passive authentication");
    g_streamPos = nullptr;
    g_streamPosArray[3] = fixture.storage[3].data();
    expectRejected("foreign block cursor rejects passive authentication");
    g_streamPosArray[3] = nullptr;
    g_streamDelayArray[7].ptr = fixture.storage[7].data();
    expectRejected("retained delay pointer rejects passive authentication");
    g_streamDelayArray[7].ptr = nullptr;
    g_streamDelayArray[7].size = 4;
    expectRejected("retained delay size rejects passive authentication");
    g_streamDelayArray[7].size = 0;
    g_streamPosStack[5].pos = fixture.storage[5].data();
    expectRejected("retained stack pointer rejects passive authentication");
    g_streamPosStack[5].pos = nullptr;
    g_streamPosStack[5].index = 5;
    expectRejected("retained stack index rejects passive authentication");
    g_streamPosStack[5].index = 0;

    lifecycle::ZoneLoadContextSlot lifecycleSlot;
    const lifecycle::ZoneLoadContextKey key = Claim(&lifecycleSlot, 4);
    ownership::ZoneStreamGenerationReceipt receipt;
    ownership::ActiveZoneStreamBinding foreignOwner;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "foreign-owner receipt begins");
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &foreignOwner,
            &receipt,
            key,
            &fixture.zone,
            fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "foreign active binding acquires the hidden singleton");
    Expect(expected.canonical(), "expected passive binding remains pristine");
    expectRejected(
        "pristine binding rejects a hidden singleton owned by another object");
    Expect(
        !ownership::AuthenticatePassiveZoneStreamSingleton(foreignOwner),
        "bound owner is not accepted by the passive authenticator");
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&foreignOwner, &receipt, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "foreign owner invalidates exactly");
    Expect(
        ownership::AuthenticatePassiveZoneStreamSingleton(expected),
        "exact invalidation restores passive authentication");
    ReleaseLoadingGeneration(&lifecycleSlot, key);

    // The legacy wrapper can populate relocation contexts without publishing
    // a receipt owner. The passive authenticator must still reject that actual
    // process state, then accept it only after the legacy scrub completes.
    DB_InitStreams(&fixture.zone);
    expectRejected(
        "legacy relocation contexts reject passive authentication without an owner");
    const int reportsBeforeScrub = reportCount;
    DB_InitStreams(nullptr);
    Expect(
        reportCount == reportsBeforeScrub + 1,
        "legacy null-zone scrub reports after clearing state");
    Expect(
        ownership::AuthenticatePassiveZoneStreamSingleton(expected),
        "legacy scrub restores complete passive authentication");
}

void TestCompositionAuthentication()
{
    using Mode = ownership::ZoneStreamCompositionMode;

    ZoneFixture fixture;
    ownership::ZoneStreamGenerationReceipt receipt;
    ownership::ActiveZoneStreamBinding active;
    const lifecycle::ZoneLoadContextKey nullKey{};
    static_assert(noexcept(ownership::AuthenticateZoneStreamComposition(
        active, receipt, nullptr, nullKey, Mode::Pristine)));
    Expect(
        ownership::AuthenticateZoneStreamComposition(
            active, receipt, nullptr, nullKey, Mode::Pristine),
        "exact pristine stream composition authenticates");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, nullptr, nullKey, Mode::NeverBound),
        "pristine receipt is distinct from enrolled NeverBound");

    lifecycle::ZoneLoadContextSlot lifecycleSlot;
    const lifecycle::ZoneLoadContextKey key = Claim(&lifecycleSlot, 5);
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::Pristine),
        "pristine mode rejects an enrolled lifecycle and key");
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "composition receipt begins");
    Expect(
        ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::NeverBound),
        "exact NeverBound stream composition authenticates");

    ownership::ZoneStreamGenerationReceipt wrongReceipt;
    lifecycle::ZoneLoadContextSlot wrongLifecycle;
    lifecycle::ZoneLoadContextKey staleKey = key;
    ++staleKey.generation;
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, wrongReceipt, &lifecycleSlot, key, Mode::NeverBound),
        "NeverBound authentication rejects another receipt");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &wrongLifecycle, key, Mode::NeverBound),
        "NeverBound authentication rejects another lifecycle address");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, staleKey, Mode::NeverBound),
        "NeverBound authentication rejects a stale key");
    g_streamPos = fixture.storage[0].data();
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::NeverBound),
        "NeverBound authentication requires the fully scrubbed singleton");
    g_streamPos = nullptr;

    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active,
            &receipt,
            key,
            &fixture.zone,
            fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "composition stream singleton binds");
    const auto authenticatesBound = [&]() noexcept
    {
        return ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::Bound);
    };
    const auto expectBoundRejected = [&](const char *const message)
    {
        Expect(!authenticatesBound(), message);
    };
    Expect(authenticatesBound(), "exact Bound stream composition authenticates");
    Expect(
        ownership::AuthenticateZoneStreamComposition(
            active, wrongReceipt, nullptr, nullKey, Mode::Pristine),
        "pristine receipt coexists with another exact Bound owner");

    lifecycle::ZoneLoadContextSlot queuedLifecycle;
    const lifecycle::ZoneLoadContextKey queuedKey =
        Claim(&queuedLifecycle, 6);
    ownership::ZoneStreamGenerationReceipt queuedReceipt;
    ownership::ActiveZoneStreamBinding queuedBinding;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &queuedReceipt, &queuedLifecycle, queuedKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "queued composition receipt begins while another generation owns streams");
    Expect(
        ownership::AuthenticateZoneStreamComposition(
            active,
            queuedReceipt,
            &queuedLifecycle,
            queuedKey,
            Mode::NeverBound),
        "NeverBound receipt coexists with another exact Bound owner");
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(
            &queuedBinding, &queuedReceipt, queuedKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "queued NeverBound receipt invalidates without disturbing its peer");
    ReleaseLoadingGeneration(&queuedLifecycle, queuedKey);
    Expect(
        ownership::AuthenticateZoneStreamComposition(
            active,
            queuedReceipt,
            &queuedLifecycle,
            queuedKey,
            Mode::Invalidated),
        "prior terminal Invalidated receipt coexists with a Bound owner");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::NeverBound),
        "Bound composition rejects NeverBound mode");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::Invalidated),
        "Bound composition rejects Invalidated mode");
    ownership::ActiveZoneStreamBinding wrongBinding;
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            wrongBinding, receipt, &lifecycleSlot, key, Mode::Bound),
        "Bound authentication rejects another singleton binding");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, wrongReceipt, &lifecycleSlot, key, Mode::Bound),
        "Bound authentication rejects another exact receipt");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &wrongLifecycle, key, Mode::Bound),
        "Bound authentication rejects another lifecycle address");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, staleKey, Mode::Bound),
        "Bound authentication rejects a stale key");

    g_streamPosIndex = relocation::kBlockCount;
    expectBoundRejected("out-of-range active stream index is rejected");
    g_streamPosIndex = 0;
    g_streamPosStackIndex = 65;
    expectBoundRejected("overflowed stream stack depth is rejected");
    g_streamPosStackIndex = 0;
    g_streamDelayIndex = 4097;
    expectBoundRejected("overflowed delayed-stream count is rejected");
    g_streamDelayIndex = 0;

    XZoneMemory wrongZone{};
    g_streamZoneMem = &wrongZone;
    expectBoundRejected("foreign legacy zone identity is rejected");
    g_streamZoneMem = &fixture.zone;
    std::uint8_t outsideByte = 0;
    g_streamPos = &outsideByte;
    expectBoundRejected("active cursor outside all retained blocks is rejected");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, wrongReceipt, nullptr, nullKey, Mode::Pristine),
        "non-Bound receipt scan still authenticates the shared Bound topology");
    g_streamPos = fixture.storage[0].data();
    g_streamPosArray[3] = fixture.storage[4].data() + 1;
    expectBoundRejected("block cursor with wrong-block provenance is rejected");
    g_streamPosArray[3] = fixture.storage[3].data();

    g_streamPosStackIndex = 1;
    g_streamPosStack[0].pos = fixture.storage[2].data();
    g_streamPosStack[0].index = relocation::kBlockCount;
    expectBoundRejected("used stack entry with invalid index is rejected");
    g_streamPosStack[0].index = 0;
    g_streamPosStack[0].pos = &outsideByte;
    expectBoundRejected("used stack cursor outside retained blocks is rejected");
    g_streamPosStackIndex = 0;
    g_streamPosStack[0] = {};
    g_streamPosStack[63].pos = &outsideByte;
    g_streamPosStack[63].index = UINT32_MAX;
    Expect(
        authenticatesBound(),
        "unused stack tail does not claim live cursor provenance");
    g_streamPosStack[63] = {};

    g_streamPosIndex = 2;
    g_streamPos = fixture.storage[2].data();
    g_streamPosStackIndex = 1;
    g_streamPosStack[0].pos = fixture.storage[8].data();
    g_streamPosStack[0].index = 0;
    Expect(
        authenticatesBound(),
        "cross-block saved cursor is valid when pop does not consume it");
    g_streamPosStackIndex = 2;
    g_streamPosStack[1].pos = fixture.storage[7].data();
    g_streamPosStack[1].index = 0;
    g_streamPosStack[0].pos = fixture.storage[8].data();
    g_streamPosStack[0].index = 3;
    expectBoundRejected(
        "effective block-zero pop rejects a foreign saved cursor");
    g_streamPosStack[0].pos = fixture.storage[0].data();
    Expect(
        authenticatesBound(),
        "effective block-zero pop accepts a block-zero saved cursor");
    g_streamPosIndex = 0;
    g_streamPos = fixture.storage[0].data();
    g_streamPosStackIndex = 0;
    g_streamPosStack[0] = {};
    g_streamPosStack[1] = {};

    g_streamDelayIndex = 1;
    expectBoundRejected("used empty delayed-stream entry is rejected");
    g_streamDelayArray[0].ptr = fixture.storage[2].data();
    g_streamDelayArray[0].size = 0;
    expectBoundRejected("delayed-stream pointer without size is rejected");
    g_streamDelayArray[0].size = -1;
    expectBoundRejected("negative delayed-stream span is rejected");
    g_streamDelayArray[0].size = 1;
    g_streamDelayArray[0].ptr = &outsideByte;
    expectBoundRejected("delayed-stream span outside retained blocks is rejected");
    g_streamDelayArray[0].ptr = fixture.storage[2].data() + 63;
    g_streamDelayArray[0].size = 2;
    expectBoundRejected("delayed-stream span crossing a block end is rejected");
    g_streamDelayIndex = 0;
    g_streamDelayArray[0] = {};
    g_streamDelayArray[7].ptr = &outsideByte;
    g_streamDelayArray[7].size = 1;
    expectBoundRejected("malformed retained delayed-stream entry is rejected");
    g_streamDelayArray[7] = {};

    g_streamPosIndex = 4;
    g_streamPos = fixture.storage[4].data() + fixture.storage[4].size();
    for (std::size_t i = 0; i < relocation::kBlockCount; ++i)
        g_streamPosArray[i] = fixture.storage[i].data() + i;
    g_streamPosStackIndex = 2;
    g_streamPosStack[0].pos = fixture.storage[8].data()
        + fixture.storage[8].size();
    g_streamPosStack[0].index = 1;
    g_streamPosStack[1].pos = fixture.storage[0].data() + 1;
    g_streamPosStack[1].index = 8;
    g_streamDelayIndex = 2;
    g_streamDelayArray[0].ptr = fixture.storage[2].data() + 4;
    g_streamDelayArray[0].size = 8;
    g_streamDelayArray[1].ptr = fixture.storage[8].data() + 63;
    g_streamDelayArray[1].size = 1;
    g_streamDelayArray[7].ptr = fixture.storage[3].data() + 16;
    g_streamDelayArray[7].size = 4;
    Expect(
        authenticatesBound(),
        "bounded cursors, used stack, and retained delayed spans authenticate");

    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&active, &receipt, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "composition stream singleton invalidates");
    Expect(
        ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::Invalidated),
        "exact Invalidated stream composition authenticates");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::NeverBound),
        "Invalidated composition rejects NeverBound mode");
    g_streamPosStack[7].index = 1;
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::Invalidated),
        "Invalidated authentication requires every idle field scrubbed");
    g_streamPosStack[7].index = 0;

    ReleaseLoadingGeneration(&lifecycleSlot, key);
    Expect(
        ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::Invalidated),
        "Invalidated authentication accepts the exact terminal Empty receipt");
    lifecycle::ZoneLoadContextKey newerKey{};
    Expect(
        lifecycle::TryClaimZoneLoadContext(&lifecycleSlot, &newerKey)
            == lifecycle::ZoneLoadContextStatus::Success,
        "composition test advances the same lifecycle slot");
    Expect(
        !ownership::AuthenticateZoneStreamComposition(
            active, receipt, &lifecycleSlot, key, Mode::Invalidated),
        "old Invalidated receipt rejects same-slot generation reuse");
    ReleaseLoadingGeneration(&lifecycleSlot, newerKey);
}

void TestLiveTerminalCompositionCoexistence()
{
    using Mode = ownership::ZoneStreamCompositionMode;

    ZoneFixture priorFixture;
    lifecycle::ZoneLoadContextSlot priorLifecycle;
    const lifecycle::ZoneLoadContextKey priorKey =
        Claim(&priorLifecycle, 7);
    ownership::ZoneStreamGenerationReceipt priorReceipt;
    ownership::ActiveZoneStreamBinding sharedBinding;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &priorReceipt, &priorLifecycle, priorKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "prior Live stream generation begins");
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &sharedBinding,
            &priorReceipt,
            priorKey,
            &priorFixture.zone,
            priorFixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "prior Live stream generation binds");
    Expect(
        lifecycle::TryCommitZoneLoadContext(&priorLifecycle, priorKey)
            == lifecycle::ZoneLoadContextStatus::Success,
        "prior stream generation publishes Live");
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(
            &sharedBinding, &priorReceipt, priorKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "prior Live stream generation invalidates");
    ReleaseLiveGeneration(&priorLifecycle, priorKey);

    ZoneFixture currentFixture;
    lifecycle::ZoneLoadContextSlot currentLifecycle;
    const lifecycle::ZoneLoadContextKey currentKey =
        Claim(&currentLifecycle, 8);
    ownership::ZoneStreamGenerationReceipt currentReceipt;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &currentReceipt, &currentLifecycle, currentKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "current stream generation begins");
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &sharedBinding,
            &currentReceipt,
            currentKey,
            &currentFixture.zone,
            currentFixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "current stream generation owns the shared singleton");
    Expect(
        ownership::AuthenticateZoneStreamComposition(
            sharedBinding,
            priorReceipt,
            &priorLifecycle,
            priorKey,
            Mode::Invalidated),
        "prior Live terminal receipt coexists with a different Bound owner");
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(
            &sharedBinding, &currentReceipt, currentKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "current coexistence generation invalidates");
    ReleaseLoadingGeneration(&currentLifecycle, currentKey);
}

void TestBindValidationAndFailureAtomicity()
{
    ZoneFixture fixture;
    lifecycle::ZoneLoadContextSlot lifecycleSlot;
    const lifecycle::ZoneLoadContextKey key = Claim(&lifecycleSlot, 1);
    ownership::ZoneStreamGenerationReceipt receipt;
    ownership::ActiveZoneStreamBinding active;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "begin receipt for bind validation");

    const auto activeBefore = ObjectBytes(active);
    const auto receiptBefore = ObjectBytes(receipt);
    const StreamSnapshot streamsBefore = SnapshotStreams();
    const auto expectUnchanged = [&]()
    {
        Expect(
            ObjectBytes(active) == activeBefore,
            "rejected bind preserves active controller bytes");
        Expect(
            ObjectBytes(receipt) == receiptBefore,
            "rejected bind preserves receipt bytes");
        Expect(
            StreamsEqual(SnapshotStreams(), streamsBefore),
            "rejected bind preserves singleton stream state");
    };

    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active,
            &receipt,
            key,
            &fixture.zone,
            fixture.blocks,
            relocation::kBlockCount - 1),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "pointer/count mismatch rejected");
    expectUnchanged();

    alignas(XZoneMemory)
        std::array<std::byte, sizeof(XZoneMemory) + 1> zoneStorage{};
    const auto *const misalignedZone =
        reinterpret_cast<const XZoneMemory *>(zoneStorage.data() + 1);
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active,
            &receipt,
            key,
            misalignedZone,
            fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidArgument,
        "misaligned typed zone identity rejected before dereference");
    expectUnchanged();

    relocation::BlockView malformed[relocation::kBlockCount];
    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[8].base = 0;
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "size without pointer rejected");
    expectUnchanged();

    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[8].size = 0;
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "pointer without size rejected");
    expectUnchanged();

    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[0] = {};
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "unusable block zero rejected");
    expectUnchanged();

    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[8].base = fixture.blocks[7].base + 4;
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "overlapping nonempty blocks rejected");
    expectUnchanged();

    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[8].base += 1;
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "misaligned block base rejected");
    expectUnchanged();

    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[8].base =
        (std::numeric_limits<std::uintptr_t>::max)() - 3;
    malformed[8].size = 8;
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "native block end overflow rejected");
    expectUnchanged();

    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[0].base = reinterpret_cast<std::uintptr_t>(&active);
    malformed[0].size = static_cast<std::uint32_t>(sizeof(active));
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "block overlapping active controller rejected");
    expectUnchanged();

    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[0].base = reinterpret_cast<std::uintptr_t>(&receipt);
    malformed[0].size = static_cast<std::uint32_t>(sizeof(receipt));
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "block overlapping generation receipt rejected");
    expectUnchanged();

    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active,
            &receipt,
            key,
            reinterpret_cast<const XZoneMemory *>(
                static_cast<const void *>(&active)),
            fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "zone identity overlapping controller rejected");
    expectUnchanged();

    lifecycle::ZoneLoadContextKey stale = key;
    ++stale.generation;
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, stale, &fixture.zone, fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::StaleKey,
        "stale bind key rejected");
    expectUnchanged();

    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "validated layout binds");
    Expect(active.canonical(), "bound active controller authenticates layout");
    Expect(receipt.canonical(), "bound receipt remains canonical");
    static_assert(std::is_same_v<
        decltype(active.zoneIdentity()), const XZoneMemory *>);
    Expect(
        active.zoneIdentity() == &fixture.zone,
        "active controller retains typed zone identity");
    Expect(g_streamZoneMem == &fixture.zone, "zone identity published");
    Expect(g_streamPos == fixture.zone.blocks[0].data, "block zero cursor published");

    ownership::ActiveZoneStreamBinding copiedBoundActive;
    RawCopy(&copiedBoundActive, &active, sizeof(active));
    Expect(
        !copiedBoundActive.canonical(),
        "raw-copied bound controller fails self-auth");

    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::AlreadyComplete,
        "exact bind retry is idempotent");
    std::copy_n(fixture.blocks, relocation::kBlockCount, malformed);
    malformed[8].size -= 4;
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &fixture.zone, malformed,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "bound retry with different layout rejected");
    XZoneMemory otherIdentity{};
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &otherIdentity, fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::InvalidLayout,
        "bound retry with different zone identity rejected");

    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&active, &receipt, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "validated binding invalidates");
    ExpectAllStreamsScrubbed();
    ReleaseLoadingGeneration(&lifecycleSlot, key);
}

void TestFullInvalidationAndStaleRetry()
{
    ZoneFixture fixtureA;
    lifecycle::ZoneLoadContextSlot lifecycleA;
    lifecycle::ZoneLoadContextKey keyA = Claim(&lifecycleA, 1);
    ownership::ZoneStreamGenerationReceipt receiptA;
    ownership::ActiveZoneStreamBinding active;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receiptA, &lifecycleA, keyA),
        ownership::ZoneStreamOwnershipStatus::Success,
        "begin first exact stream generation");
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receiptA, keyA, &fixtureA.zone, fixtureA.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "bind first exact stream generation");

    const DBAliasHandle staleHandle = DB_RegisterPointerSlot(
        fixtureA.storage[4].data(), DBAliasKind::XAnimParts);
    Expect(static_cast<bool>(staleHandle), "alias handle issued before teardown");
    DB_SetInsertedPointer(
        staleHandle,
        DBAliasKind::XAnimParts,
        fixtureA.storage[3].data(),
        7);
    Expect(reportCount == 0, "valid alias publication does not report");
    ExpectRelocationStatus(
        DB_MarkStreamRangeMaterialized(
            fixtureA.storage[4].data(), 16),
        relocation::Status::Ok,
        "direct provenance recorded before teardown");

    ownership::ActiveZoneStreamBinding wrongActive;
    const StreamSnapshot beforeWrongActive = SnapshotStreams();
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(
            &wrongActive, &receiptA, keyA),
        ownership::ZoneStreamOwnershipStatus::UnsafeFailure,
        "bound receipt mismatch fails closed");
    Expect(
        StreamsEqual(SnapshotStreams(), beforeWrongActive),
        "bound mismatch preserves owned singleton");

    g_streamDelayIndex = 4096;
    for (std::size_t i = 0; i < std::size(g_streamDelayArray); ++i)
    {
        g_streamDelayArray[i].ptr = fixtureA.storage[i % 9].data();
        g_streamDelayArray[i].size = static_cast<std::int32_t>(i + 1);
    }
    g_streamPosStackIndex = 64;
    for (std::size_t i = 0; i < std::size(g_streamPosStack); ++i)
    {
        g_streamPosStack[i].pos = fixtureA.storage[i % 9].data() + 1;
        g_streamPosStack[i].index = static_cast<std::uint32_t>(i % 9);
    }
    g_streamPosIndex = 8;
    g_streamPos = fixtureA.storage[8].data() + 4;
    for (std::size_t i = 0; i < std::size(g_streamPosArray); ++i)
        g_streamPosArray[i] = fixtureA.storage[i].data() + 2;

    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&active, &receiptA, keyA),
        ownership::ZoneStreamOwnershipStatus::Success,
        "bound stream generation invalidates exactly");
    ExpectAllStreamsScrubbed();
    Expect(active.canonical(), "active controller resets to canonical idle");
    Expect(
        receiptA.phase()
            == ownership::ZoneStreamGenerationPhase::Invalidated,
        "terminal receipt published after scrub");

    std::uintptr_t resolved = 1;
    ExpectRelocationStatus(
        DB_ResolveInsertedPointer(
            {UINT32_C(0x40000001)},
            DBAliasKind::XAnimParts,
            7,
            &resolved),
        relocation::Status::InvalidContext,
        "alias resolution rejects after invalidation");
    Expect(resolved == 0, "failed alias resolution clears output");
    const int reportsBeforeStalePublish = reportCount;
    DB_SetInsertedPointer(
        staleHandle,
        DBAliasKind::XAnimParts,
        fixtureA.storage[3].data(),
        7);
    Expect(
        reportCount == reportsBeforeStalePublish + 1,
        "stale alias handle cannot publish after invalidation");
    ExpectRelocationStatus(
        DB_ValidateStreamAddress(
            fixtureA.storage[4].data(),
            1,
            1,
            relocation::BlockBit(4)),
        relocation::Status::InvalidContext,
        "direct resolver rejects after invalidation");

    ReleaseLoadingGeneration(&lifecycleA, keyA);
    lifecycle::ZoneLoadContextKey keyB{};
    Expect(
        lifecycle::TryClaimZoneLoadContext(&lifecycleA, &keyB)
            == lifecycle::ZoneLoadContextStatus::Success,
        "same physical slot advances to newer generation");
    Expect(
        keyB.slot == keyA.slot && keyB.generation == keyA.generation + 1,
        "same-slot generation changes exactly");

    ZoneFixture fixtureB;
    ownership::ZoneStreamGenerationReceipt receiptB;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receiptB, &lifecycleA, keyB),
        ownership::ZoneStreamOwnershipStatus::Success,
        "begin newer same-slot generation");
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receiptB, keyB, &fixtureB.zone, fixtureB.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "bind newer same-slot generation");

    const auto activeB = ObjectBytes(active);
    const auto receiptBBytes = ObjectBytes(receiptB);
    const StreamSnapshot streamsB = SnapshotStreams();
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(nullptr, &receiptA, keyA),
        ownership::ZoneStreamOwnershipStatus::AlreadyComplete,
        "terminal retry returns before active inspection");
    Expect(ObjectBytes(active) == activeB, "old retry preserves newer active bytes");
    Expect(
        ObjectBytes(receiptB) == receiptBBytes,
        "old retry preserves newer receipt bytes");
    Expect(
        StreamsEqual(SnapshotStreams(), streamsB),
        "old retry preserves newer singleton state");

    lifecycle::ZoneLoadContextKey wrongSlot = keyA;
    wrongSlot.slot = 2;
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&active, &receiptA, wrongSlot),
        ownership::ZoneStreamOwnershipStatus::StaleKey,
        "same generation under another slot is stale");
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&active, &receiptB, keyA),
        ownership::ZoneStreamOwnershipStatus::StaleKey,
        "old same-slot generation cannot invalidate newer receipt");
    Expect(
        StreamsEqual(SnapshotStreams(), streamsB),
        "all stale-key attempts preserve newer singleton");

    lifecycle::ZoneLoadContextSlot lifecycleC;
    const lifecycle::ZoneLoadContextKey keyC = Claim(&lifecycleC, 2);
    ZoneFixture fixtureC;
    ownership::ZoneStreamGenerationReceipt receiptC;
    ownership::ActiveZoneStreamBinding activeC;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receiptC, &lifecycleC, keyC),
        ownership::ZoneStreamOwnershipStatus::Success,
        "begin competing stream generation");
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &activeC, &receiptC, keyC, &fixtureC.zone, fixtureC.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Busy,
        "different active key cannot replace singleton owner");
    Expect(
        StreamsEqual(SnapshotStreams(), streamsB),
        "active-key collision preserves current singleton");
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&activeC, &receiptC, keyC),
        ownership::ZoneStreamOwnershipStatus::Success,
        "NeverBound competing generation abandons without singleton access");
    ReleaseLoadingGeneration(&lifecycleC, keyC);

    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&active, &receiptB, keyB),
        ownership::ZoneStreamOwnershipStatus::Success,
        "newer generation invalidates through exact receipt");
    ReleaseLoadingGeneration(&lifecycleA, keyB);
}

void TestAliasEpochExhaustion()
{
    ZoneFixture fixture;
    relocation::AliasRegistry exhausted(
        16,
        (std::numeric_limits<std::uint64_t>::max)());
    Expect(!exhausted.CanReset(), "UINT64_MAX alias epoch is exhausted");
    ExpectRelocationStatus(
        exhausted.Reset(fixture.blocks, relocation::kBlockCount),
        relocation::Status::GenerationExhausted,
        "alias epoch never wraps to a reusable value");
    Expect(
        exhausted.generation()
            == (std::numeric_limits<std::uint64_t>::max)(),
        "failed reset preserves terminal alias epoch");
    Expect(!exhausted.contextValid(), "exhausted alias registry stays invalid");
}

void TestNativeWidthBlockAddresses()
{
    lifecycle::ZoneLoadContextSlot lifecycleSlot;
    const lifecycle::ZoneLoadContextKey key = Claim(&lifecycleSlot, 32);
    ownership::ZoneStreamGenerationReceipt receipt;
    ownership::ActiveZoneStreamBinding active;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "begin native-width stream layout");

    relocation::BlockView blocks[relocation::kBlockCount]{};
    std::uintptr_t base = UINT32_C(0x01000000);
    if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t))
        base = static_cast<std::uintptr_t>(UINT64_C(0x0000001200000000));
    for (std::size_t i = 0; i < relocation::kBlockCount; ++i)
    {
        blocks[i].base = base + i * UINT32_C(0x1000);
        blocks[i].size = UINT32_C(0x100);
    }
    XZoneMemory zoneIdentity{};
    for (std::size_t i = 0; i < relocation::kBlockCount; ++i)
    {
        zoneIdentity.blocks[i].data =
            reinterpret_cast<std::uint8_t *>(blocks[i].base);
        zoneIdentity.blocks[i].size = blocks[i].size;
    }
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active, &receipt, key, &zoneIdentity, blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "native-width high block addresses bind without narrowing");
    relocation::BlockView retained{};
    Expect(active.block(8, &retained), "retained block descriptor is readable");
    Expect(
        retained.base == blocks[8].base && retained.size == blocks[8].size,
        "retained block descriptor preserves native-width address");
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&active, &receipt, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "native-width high block addresses invalidate");
    ReleaseLoadingGeneration(&lifecycleSlot, key);
}

void TestBlockOutputHardening()
{
    using OutputSpanAuthenticator = bool (*)(
        const ownership::ActiveZoneStreamBinding &,
        const void *,
        std::size_t,
        std::size_t) noexcept;
    static_assert(std::is_same_v<
        decltype(&ownership::AuthenticateZoneStreamOutputSpan),
        OutputSpanAuthenticator>);
    static_assert(
        alignof(ownership::ActiveZoneStreamBinding)
        >= alignof(relocation::BlockView));
    static_assert(
        alignof(ownership::ZoneStreamGenerationReceipt)
        >= alignof(relocation::BlockView));
    static_assert(
        alignof(lifecycle::ZoneLoadContextSlot)
        >= alignof(relocation::BlockView));
    static_assert(
        alignof(XZoneMemory) >= alignof(relocation::BlockView));

    ZoneFixture fixture;
    lifecycle::ZoneLoadContextSlot lifecycleSlot;
    const lifecycle::ZoneLoadContextKey key = Claim(&lifecycleSlot, 31);
    ownership::ZoneStreamGenerationReceipt receipt;
    ownership::ActiveZoneStreamBinding active;
    relocation::BlockView output{UINT32_C(0x12345678), UINT32_C(0x87654321)};
    Expect(
        ownership::AuthenticateZoneStreamOutputSpan(
            active,
            &output,
            sizeof(output),
            alignof(decltype(output))),
        "ordinary output authenticates against an idle stream binding");
    Expect(
        !ownership::AuthenticateZoneStreamOutputSpan(
            active,
            &g_streamPos,
            sizeof(g_streamPos),
            alignof(decltype(g_streamPos))),
        "idle binding still protects legacy singleton output ranges");

    lifecycle::ZoneLoadContextSlot foreignLifecycle;
    const lifecycle::ZoneLoadContextKey foreignKey =
        Claim(&foreignLifecycle, 30);
    ownership::ZoneStreamGenerationReceipt foreignReceipt;
    ownership::ActiveZoneStreamBinding foreignActive;
    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &foreignReceipt, &foreignLifecycle, foreignKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "begin foreign owner for idle output hardening");
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &foreignActive,
            &foreignReceipt,
            foreignKey,
            &fixture.zone,
            fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "bind foreign owner for idle output hardening");
    const auto idleOutputBefore = ObjectBytes(output);
    Expect(
        !ownership::AuthenticateZoneStreamOutputSpan(
            active,
            &output,
            sizeof(output),
            alignof(decltype(output))),
        "idle binding rejects a different hidden owner");
    Expect(
        ObjectBytes(output) == idleOutputBefore,
        "foreign-owner rejection preserves output bytes");
    ExpectStatus(
        ownership::TryInvalidateZoneStreams(
            &foreignActive, &foreignReceipt, foreignKey),
        ownership::ZoneStreamOwnershipStatus::Success,
        "invalidate foreign output-hardening owner");
    ReleaseLoadingGeneration(&foreignLifecycle, foreignKey);

    ExpectStatus(
        ownership::TryBeginZoneStreamGeneration(
            &receipt, &lifecycleSlot, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "begin stream generation for block output hardening");
    ExpectStatus(
        ownership::TryBindZoneStreams(
            &active,
            &receipt,
            key,
            &fixture.zone,
            fixture.blocks,
            relocation::kBlockCount),
        ownership::ZoneStreamOwnershipStatus::Success,
        "bind stream generation for block output hardening");

    const auto outputBefore = ObjectBytes(output);
    Expect(
        !active.block(relocation::kBlockCount, &output),
        "invalid block index is rejected");
    Expect(
        ObjectBytes(output) == outputBefore,
        "invalid block index preserves output bytes");

    alignas(relocation::BlockView)
        std::array<std::uint8_t, sizeof(relocation::BlockView) + 1>
            misalignedStorage{};
    misalignedStorage.fill(UINT8_C(0xA5));
    const auto misalignedBefore = misalignedStorage;
    Expect(
        !active.block(
            0,
            reinterpret_cast<relocation::BlockView *>(
                misalignedStorage.data() + 1)),
        "misaligned block output is rejected before dereference");
    Expect(
        misalignedStorage == misalignedBefore,
        "misaligned block output storage remains unchanged");

    const std::uintptr_t nonRepresentableAddress =
        (std::numeric_limits<std::uintptr_t>::max)()
        & ~(static_cast<std::uintptr_t>(
                alignof(relocation::BlockView))
            - 1);
    Expect(
        !active.block(
            0,
            reinterpret_cast<relocation::BlockView *>(
                nonRepresentableAddress)),
        "non-representable block output span is rejected");

    const auto activeBefore = ObjectBytes(active);
    const auto receiptBefore = ObjectBytes(receipt);
    const auto lifecycleBefore = ObjectBytes(lifecycleSlot);
    const auto zoneBefore = ObjectBytes(fixture.zone);
    const StreamSnapshot streamsBefore = SnapshotStreams();
    const auto aliasRegistryBefore = ObjectBytes(
        ownership::detail::AliasRegistryForLegacyStream());
    const auto directResolverBefore = ObjectBytes(
        ownership::detail::DirectResolverForLegacyStream());
    const auto expectControlsUnchanged = [&]()
    {
        Expect(
            ObjectBytes(active) == activeBefore,
            "rejected output preserves active binding bytes");
        Expect(
            ObjectBytes(receipt) == receiptBefore,
            "rejected output preserves retained receipt bytes");
        Expect(
            ObjectBytes(lifecycleSlot) == lifecycleBefore,
            "rejected output preserves retained lifecycle bytes");
        Expect(
            ObjectBytes(fixture.zone) == zoneBefore,
            "rejected output preserves retained zone identity bytes");
        Expect(
            StreamsEqual(SnapshotStreams(), streamsBefore),
            "rejected output preserves every legacy stream global");
        Expect(
            ObjectBytes(ownership::detail::AliasRegistryForLegacyStream())
                == aliasRegistryBefore,
            "rejected output preserves alias registry bytes");
        Expect(
            ObjectBytes(ownership::detail::DirectResolverForLegacyStream())
                == directResolverBefore,
            "rejected output preserves direct resolver bytes");
    };

    Expect(
        !active.block(
            0,
            reinterpret_cast<relocation::BlockView *>(&active)),
        "block output overlapping active binding is rejected");
    expectControlsUnchanged();
    Expect(
        !active.block(
            0,
            reinterpret_cast<relocation::BlockView *>(&receipt)),
        "block output overlapping retained receipt is rejected");
    expectControlsUnchanged();
    Expect(
        !active.block(
            0,
            reinterpret_cast<relocation::BlockView *>(&lifecycleSlot)),
        "block output overlapping retained lifecycle is rejected");
    expectControlsUnchanged();
    Expect(
        !active.block(
            0,
            reinterpret_cast<relocation::BlockView *>(&fixture.zone)),
        "block output overlapping retained zone identity is rejected");
    expectControlsUnchanged();

    struct SingletonProbe final
    {
        const void *object;
        std::size_t size;
        std::size_t alignment;
        const char *message;
    };
    const SingletonProbe singletonProbes[] = {
        {&g_streamDelayIndex,
         sizeof(g_streamDelayIndex),
         alignof(decltype(g_streamDelayIndex)),
         "delay index output overlap is rejected"},
        {&g_streamPosArray,
         sizeof(g_streamPosArray),
         alignof(decltype(g_streamPosArray)),
         "cursor array output overlap is rejected"},
        {&g_streamDelayArray,
         sizeof(g_streamDelayArray),
         alignof(decltype(g_streamDelayArray)),
         "delay array output overlap is rejected"},
        {&g_streamPosIndex,
         sizeof(g_streamPosIndex),
         alignof(decltype(g_streamPosIndex)),
         "stream index output overlap is rejected"},
        {&g_streamPosStack,
         sizeof(g_streamPosStack),
         alignof(decltype(g_streamPosStack)),
         "cursor stack output overlap is rejected"},
        {&g_streamZoneMem,
         sizeof(g_streamZoneMem),
         alignof(decltype(g_streamZoneMem)),
         "zone pointer output overlap is rejected"},
        {&g_streamPos,
         sizeof(g_streamPos),
         alignof(decltype(g_streamPos)),
         "active cursor output overlap is rejected"},
        {&g_streamPosStackIndex,
         sizeof(g_streamPosStackIndex),
         alignof(decltype(g_streamPosStackIndex)),
         "stack index output overlap is rejected"},
        {&ownership::detail::AliasRegistryForLegacyStream(),
         sizeof(relocation::AliasRegistry),
         alignof(relocation::AliasRegistry),
         "alias registry output overlap is rejected"},
        {&ownership::detail::DirectResolverForLegacyStream(),
         sizeof(relocation::DirectResolver),
         alignof(relocation::DirectResolver),
         "direct resolver output overlap is rejected"},
    };
    for (const SingletonProbe &probe : singletonProbes)
    {
        Expect(
            !ownership::AuthenticateZoneStreamOutputSpan(
                active,
                probe.object,
                probe.size,
                probe.alignment),
            probe.message);
        expectControlsUnchanged();
    }
    Expect(
        !active.block(
            0,
            reinterpret_cast<relocation::BlockView *>(&g_streamPos)),
        "block output overlapping active cursor global is rejected");
    expectControlsUnchanged();

    output = {UINT32_C(0x12345678), UINT32_C(0x87654321)};
    const auto staleOutputBefore = ObjectBytes(output);
    Expect(
        lifecycle::TryBeginZoneLoadContextAbandonment(
            &lifecycleSlot, key)
            == lifecycle::ZoneLoadContextStatus::Success,
        "begin lifecycle abandonment for stale block output");
    lifecycle::ZoneLoadCleanupCallbacks cleanup{};
    cleanup.context = &lifecycleSlot;
    cleanup.perform = CompleteCleanup;
    Expect(
        lifecycle::TryFinishZoneLoadContextAbandonment(
            &lifecycleSlot, key, cleanup)
            == lifecycle::ZoneLoadContextStatus::Success,
        "finish lifecycle abandonment for stale block output");
    Expect(active.canonical(), "terminal lifecycle leaves binding structural");
    Expect(
        !active.block(0, &output),
        "terminal lifecycle blocks retained descriptor output");
    Expect(
        ObjectBytes(output) == staleOutputBefore,
        "terminal lifecycle rejection preserves output bytes");

    lifecycle::ZoneLoadContextKey newerKey{};
    Expect(
        lifecycle::TryClaimZoneLoadContext(&lifecycleSlot, &newerKey)
            == lifecycle::ZoneLoadContextStatus::Success,
        "reclaim lifecycle for stale block output");
    Expect(newerKey != key, "reclaimed lifecycle advances exact key");
    Expect(
        !active.block(0, &output),
        "reused lifecycle generation blocks retained descriptor output");
    Expect(
        ObjectBytes(output) == staleOutputBefore,
        "reused lifecycle rejection preserves output bytes");

    RawCopy(
        &lifecycleSlot,
        lifecycleBefore.data(),
        lifecycleBefore.size());
    Expect(
        lifecycle::ZoneLoadContextKeyMatches(&lifecycleSlot, key),
        "test restores exact lifecycle authority before invalidation");
    Expect(
        active.block(0, &output),
        "restored exact lifecycle permits retained descriptor output");

    ExpectStatus(
        ownership::TryInvalidateZoneStreams(&active, &receipt, key),
        ownership::ZoneStreamOwnershipStatus::Success,
        "output-hardened stream generation invalidates");
    ReleaseLoadingGeneration(&lifecycleSlot, key);
}
} // namespace

void __cdecl Com_Error(errorParm_t, const char *, ...)
{
    ++reportCount;
}

int main()
{
    TestConstructionAndKeyValidation();
    TestBindValidationAndFailureAtomicity();
    TestFullInvalidationAndStaleRetry();
    TestAliasEpochExhaustion();
    TestNativeWidthBlockAddresses();
    TestBlockOutputHardening();
    TestPassiveSingletonAuthentication();
    TestCompositionAuthentication();
    TestLiveTerminalCompositionCoexistence();
    return failures == 0 ? 0 : 1;
}
