#include <EffectsCore/fx_iterator_atomic.h>
#include <EffectsCore/fx_snapshot_publication.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>

namespace
{
constexpr std::uint32_t kStressRounds = 4000u;

int Fail(const char *const message)
{
    std::fprintf(stderr, "FX snapshot publication test failed: %s\n", message);
    return 1;
}

float CameraValue(
    const std::uint32_t sequence,
    const std::uint32_t ordinal) noexcept
{
    return static_cast<float>(sequence * 128u + ordinal);
}

void PopulateCamera(
    FxCamera *const camera,
    const std::uint32_t sequence,
    const std::int32_t validity) noexcept
{
    std::uint32_t ordinal = 1u;
    for (std::size_t component = 0; component < 3; ++component)
        camera->origin[component] = CameraValue(sequence, ordinal++);
    for (std::size_t plane = 0; plane < 6; ++plane)
    {
        for (std::size_t component = 0; component < 4; ++component)
            camera->frustum[plane][component] = CameraValue(sequence, ordinal++);
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t component = 0; component < 3; ++component)
            camera->axis[row][component] = CameraValue(sequence, ordinal++);
    }
    camera->frustumPlaneCount = 5u + (sequence & 1u);
    for (std::size_t component = 0; component < 3; ++component)
        camera->viewOffset[component] = CameraValue(sequence, ordinal++);
    camera->pad[0] = sequence;
    camera->pad[1] = sequence ^ UINT32_C(0x5A5A5A5A);
    camera->pad[2] = ~sequence;
    Sys_AtomicStore(&camera->isValid, validity);
}

bool CameraPayloadEquals(
    const FxCamera &actual,
    const FxCamera &expected) noexcept
{
    for (std::size_t component = 0; component < 3; ++component)
    {
        if (actual.origin[component] != expected.origin[component])
            return false;
    }
    for (std::size_t plane = 0; plane < 6; ++plane)
    {
        for (std::size_t component = 0; component < 4; ++component)
        {
            if (actual.frustum[plane][component]
                != expected.frustum[plane][component])
            {
                return false;
            }
        }
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t component = 0; component < 3; ++component)
        {
            if (actual.axis[row][component] != expected.axis[row][component])
                return false;
        }
    }
    if (actual.frustumPlaneCount != expected.frustumPlaneCount)
        return false;
    for (std::size_t component = 0; component < 3; ++component)
    {
        if (actual.viewOffset[component] != expected.viewOffset[component]
            || actual.pad[component] != expected.pad[component])
        {
            return false;
        }
    }
    return true;
}

bool TestCameraPublicationCoversEveryField()
{
    FxCamera source{};
    FxCamera destination{};
    PopulateCamera(&source, 17u, 0);
    PopulateCamera(&destination, 3u, 1);

    FX_PublishCamera(&destination, source, true);
    if (Sys_AtomicLoad(&destination.isValid) != 1
        || !CameraPayloadEquals(destination, source))
    {
        return false;
    }

    PopulateCamera(&source, 29u, 1);
    FX_PublishCamera(&destination, source, false);
    return Sys_AtomicLoad(&destination.isValid) == 0
        && CameraPayloadEquals(destination, source)
        && destination.frustumPlaneCount == source.frustumPlaneCount;
}

bool TestCameraPublicationInvalidation()
{
    FxCamera camera{};
    FxCamera expected{};
    PopulateCamera(&camera, 41u, 1);
    PopulateCamera(&expected, 41u, 0);
    expected.frustumPlaneCount = 0;

    FX_InvalidateCameraPublication(&camera);
    return Sys_AtomicLoad(&camera.isValid) == 0
        && camera.frustumPlaneCount == 0
        && CameraPayloadEquals(camera, expected);
}

bool TestArchiveCameraReadiness()
{
    FxCamera current{};
    FxCamera previous{};
    if (!FX_IsCanonicalInvalidCamera(current)
        || !FX_IsCanonicalInvalidCamera(previous)
        || !FX_AreArchiveCamerasReady(current, previous, -1)
        || FX_AreArchiveCamerasReady(current, previous, -2))
    {
        return false;
    }

    PopulateCamera(&previous, 9u, 1);
    if (!FX_AreArchiveCamerasReady(current, previous, -1)
        || FX_AreArchiveCamerasReady(current, previous, 0))
    {
        return false;
    }

    PopulateCamera(&current, 10u, 1);
    if (!FX_AreArchiveCamerasReady(current, previous, 0))
        return false;
    FX_InvalidateCameraPublication(&current);
    if (FX_IsCanonicalInvalidCamera(current)
        || FX_AreArchiveCamerasReady(current, previous, 0)
        || FX_AreArchiveCamerasReady(current, previous, -1))
    {
        return false;
    }

    FxCamera malformed{};
    malformed.pad[2] = 1u;
    if (FX_IsCanonicalInvalidCamera(malformed))
        return false;
    malformed.pad[2] = 0u;
    Sys_AtomicStore(&malformed.isValid, 2);
    return !FX_IsCanonicalInvalidCamera(malformed)
        && !FX_AreArchiveCamerasReady(malformed, previous, -1);
}

bool RejectsSelectorsWithoutChangingOutputs(
    const FxVisState *const slot0,
    const FxVisState *const slot1,
    const FxVisState *const readState,
    const FxVisState *const writeState) noexcept
{
    std::uint8_t readSelector = UINT8_C(0xA5);
    std::uint8_t writeSelector = UINT8_C(0x5A);
    return !FX_TryDeriveVisibilitySelectors(
               slot0,
               slot1,
               readState,
               writeState,
               &readSelector,
               &writeSelector)
        && readSelector == UINT8_C(0xA5)
        && writeSelector == UINT8_C(0x5A);
}

struct VisibilitySelectorFixture
{
    FxVisState live[2]{};
    FxVisState staged[2]{};
    FxVisState foreign{};
};

union VisibilitySelectorOutputAlias
{
    const FxVisState *read;
    FxVisState *write;
};

bool SelectorPairsEqual(
    const FxVisibilityBufferSelectors &left,
    const FxVisibilityBufferSelectors &right) noexcept
{
    return left.read == right.read && left.write == right.write;
}

bool RejectsSelectorPairWithoutChangingOutput(
    const FxVisState *const slot0,
    const FxVisState *const slot1,
    const FxVisState *const readState,
    const FxVisState *const writeState) noexcept
{
    const FxVisibilityBufferSelectors sentinel{
        UINT8_C(0xA5), UINT8_C(0x5A)};
    FxVisibilityBufferSelectors selectors = sentinel;
    return !FX_TryDeriveVisibilitySelectorPair(
               slot0,
               slot1,
               readState,
               writeState,
               &selectors)
        && SelectorPairsEqual(selectors, sentinel);
}

bool RejectsResolutionWithoutChangingOutputs(
    FxVisState *const slot0,
    FxVisState *const slot1,
    const FxVisibilityBufferSelectors &selectors,
    const FxVisState *const readSentinel,
    FxVisState *const writeSentinel) noexcept
{
    const FxVisState *readState = readSentinel;
    FxVisState *writeState = writeSentinel;
    return !FX_TryResolveVisibilitySelectors(
               slot0,
               slot1,
               selectors,
               &readState,
               &writeState)
        && readState == readSentinel
        && writeState == writeSentinel;
}

bool TestVisibilitySelectorDerivation()
{
    const auto fixture = std::make_unique<VisibilitySelectorFixture>();
    FxVisState *const slots = fixture->live;
    FxVisState *const foreign = &fixture->foreign;
    std::uint8_t readSelector = UINT8_C(0xFF);
    std::uint8_t writeSelector = UINT8_C(0xFF);

    if (!FX_TryDeriveVisibilitySelectors(
            &slots[0],
            &slots[1],
            &slots[0],
            &slots[1],
            &readSelector,
            &writeSelector)
        || readSelector != 0u || writeSelector != 1u)
    {
        return false;
    }
    readSelector = UINT8_C(0xFF);
    writeSelector = UINT8_C(0xFF);
    if (!FX_TryDeriveVisibilitySelectors(
            &slots[0],
            &slots[1],
            &slots[1],
            &slots[0],
            &readSelector,
            &writeSelector)
        || readSelector != 1u || writeSelector != 0u)
    {
        return false;
    }

    if (!RejectsSelectorsWithoutChangingOutputs(
            nullptr, &slots[1], &slots[0], &slots[1])
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], nullptr, &slots[0], &slots[1])
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[0], &slots[0], &slots[0])
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[1], nullptr, &slots[1])
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[1], &slots[0], nullptr)
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[1], foreign, &slots[1])
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[1], &slots[0], foreign)
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[1], &slots[2], &slots[1])
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[1], &slots[0], &slots[2])
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[1], &slots[0], &slots[0])
        || !RejectsSelectorsWithoutChangingOutputs(
            &slots[0], &slots[1], &slots[1], &slots[1]))
    {
        return false;
    }

    readSelector = UINT8_C(0xA5);
    writeSelector = UINT8_C(0x5A);
    if (FX_TryDeriveVisibilitySelectors(
            &slots[0],
            &slots[1],
            &slots[0],
            &slots[1],
            nullptr,
            &writeSelector)
        || readSelector != UINT8_C(0xA5)
        || writeSelector != UINT8_C(0x5A))
    {
        return false;
    }
    if (FX_TryDeriveVisibilitySelectors(
            &slots[0],
            &slots[1],
            &slots[0],
            &slots[1],
            &readSelector,
            nullptr)
        || readSelector != UINT8_C(0xA5)
        || writeSelector != UINT8_C(0x5A))
    {
        return false;
    }

    std::uint8_t aliasedOutput = UINT8_C(0xCC);
    return !FX_TryDeriveVisibilitySelectors(
               &slots[0],
               &slots[1],
               &slots[0],
               &slots[1],
               &aliasedOutput,
               &aliasedOutput)
        && aliasedOutput == UINT8_C(0xCC);
}

bool TestVisibilitySelectorPairDerivation()
{
    const auto fixture = std::make_unique<VisibilitySelectorFixture>();
    FxVisState *const slots = fixture->live;
    FxVisState *const foreign = &fixture->foreign;
    FxVisibilityBufferSelectors selectors{
        UINT8_C(0xFF), UINT8_C(0xFF)};

    if (!FX_TryDeriveVisibilitySelectorPair(
            &slots[0],
            &slots[1],
            &slots[0],
            &slots[1],
            &selectors)
        || !SelectorPairsEqual(selectors, {0u, 1u}))
    {
        return false;
    }
    selectors = {UINT8_C(0xFF), UINT8_C(0xFF)};
    if (!FX_TryDeriveVisibilitySelectorPair(
            &slots[0],
            &slots[1],
            &slots[1],
            &slots[0],
            &selectors)
        || !SelectorPairsEqual(selectors, {1u, 0u}))
    {
        return false;
    }

    if (!RejectsSelectorPairWithoutChangingOutput(
            nullptr, &slots[1], &slots[0], &slots[1])
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], nullptr, &slots[0], &slots[1])
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[0], &slots[0], &slots[0])
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[1], nullptr, &slots[1])
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[1], &slots[0], nullptr)
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[1], foreign, &slots[1])
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[1], &slots[0], foreign)
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[1], &slots[2], &slots[1])
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[1], &slots[0], &slots[2])
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[1], &slots[0], &slots[0])
        || !RejectsSelectorPairWithoutChangingOutput(
            &slots[0], &slots[1], &slots[1], &slots[1]))
    {
        return false;
    }

    const FxVisibilityBufferSelectors sentinel{
        UINT8_C(0xA5), UINT8_C(0x5A)};
    selectors = sentinel;
    return !FX_TryDeriveVisibilitySelectorPair(
               &slots[0],
               &slots[1],
               &slots[0],
               &slots[1],
               nullptr)
        && SelectorPairsEqual(selectors, sentinel);
}

bool TestVisibilitySelectorResolution()
{
    const auto fixture = std::make_unique<VisibilitySelectorFixture>();
    FxVisState *const live = fixture->live;
    FxVisState *const staged = fixture->staged;
    FxVisState *const foreign = &fixture->foreign;
    const FxVisibilityBufferSelectors canonical{0u, 1u};
    const FxVisibilityBufferSelectors swapped{1u, 0u};
    const FxVisibilityBufferSelectors zeroInitialized{};
    if (zeroInitialized.read != 0u || zeroInitialized.write != 0u
        || !RejectsResolutionWithoutChangingOutputs(
            &live[0],
            &live[1],
            zeroInitialized,
            foreign,
            &staged[0]))
    {
        return false;
    }

    const FxVisState *readState = foreign;
    FxVisState *writeState = &staged[0];
    if (!FX_TryResolveVisibilitySelectors(
            &live[0],
            &live[1],
            canonical,
            &readState,
            &writeState)
        || readState != &live[0] || writeState != &live[1]
        || readState == &staged[0] || readState == &staged[1]
        || writeState == &staged[0] || writeState == &staged[1])
    {
        return false;
    }

    readState = foreign;
    writeState = &staged[0];
    if (!FX_TryResolveVisibilitySelectors(
            &live[0],
            &live[1],
            swapped,
            &readState,
            &writeState)
        || readState != &live[1] || writeState != &live[0]
        || readState == &staged[0] || readState == &staged[1]
        || writeState == &staged[0] || writeState == &staged[1])
    {
        return false;
    }

    const FxVisibilityBufferSelectors invalidPairs[] = {
        {0u, 0u},
        {1u, 1u},
        {2u, 0u},
        {0u, 2u},
        {UINT8_C(0xFF), 1u},
        {1u, UINT8_C(0xFF)},
    };
    for (const FxVisibilityBufferSelectors &invalid : invalidPairs)
    {
        if (!RejectsResolutionWithoutChangingOutputs(
                &live[0],
                &live[1],
                invalid,
                foreign,
                &staged[0]))
        {
            return false;
        }
    }

    if (!RejectsResolutionWithoutChangingOutputs(
            nullptr,
            &live[1],
            canonical,
            foreign,
            &staged[0])
        || !RejectsResolutionWithoutChangingOutputs(
            &live[0],
            nullptr,
            canonical,
            foreign,
            &staged[0])
        || !RejectsResolutionWithoutChangingOutputs(
            &live[0],
            &live[0],
            canonical,
            foreign,
            &staged[0]))
    {
        return false;
    }

    readState = foreign;
    writeState = &staged[0];
    if (FX_TryResolveVisibilitySelectors(
            &live[0],
            &live[1],
            canonical,
            nullptr,
            &writeState)
        || writeState != &staged[0])
    {
        return false;
    }
    if (FX_TryResolveVisibilitySelectors(
            &live[0],
            &live[1],
            canonical,
            &readState,
            nullptr)
        || readState != foreign)
    {
        return false;
    }

    VisibilitySelectorOutputAlias aliasedOutput{};
    aliasedOutput.read = foreign;
    if (FX_TryResolveVisibilitySelectors(
            &live[0],
            &live[1],
            canonical,
            &aliasedOutput.read,
            &aliasedOutput.write)
        || aliasedOutput.read != foreign)
    {
        return false;
    }

    return FX_VisibilitySelectorsRoundTrip(
        &live[0],
        &live[1],
        &live[0],
        &live[1],
        canonical);
}

bool TestVisibilitySelectorRoundTripRejection()
{
    const auto fixture = std::make_unique<VisibilitySelectorFixture>();
    FxVisState *const live = fixture->live;
    FxVisState *const staged = fixture->staged;
    FxVisState *const foreign = &fixture->foreign;
    const FxVisibilityBufferSelectors canonical{0u, 1u};
    const FxVisibilityBufferSelectors swapped{1u, 0u};

    if (!FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[0], &live[1], canonical)
        || !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[1], &live[0], swapped)
        || FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[0], &live[1], swapped)
        || FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[1], &live[0], canonical))
    {
        return false;
    }

    const FxVisibilityBufferSelectors invalidPairs[] = {
        {0u, 0u},
        {1u, 1u},
        {2u, 0u},
        {0u, 2u},
        {UINT8_C(0xFF), 1u},
        {1u, UINT8_C(0xFF)},
    };
    for (const FxVisibilityBufferSelectors &invalid : invalidPairs)
    {
        if (FX_VisibilitySelectorsRoundTrip(
                &live[0], &live[1], &live[0], &live[1], invalid))
        {
            return false;
        }
    }

    return !FX_VisibilitySelectorsRoundTrip(
               nullptr, &live[1], &live[0], &live[1], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], nullptr, &live[0], &live[1], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[0], &live[0], &live[0], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], nullptr, &live[1], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[0], nullptr, canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &staged[0], &live[1], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[0], &staged[1], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], foreign, &live[1], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[0], foreign, canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[2], &live[1], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[0], &live[2], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[0], &live[0], canonical)
        && !FX_VisibilitySelectorsRoundTrip(
            &live[0], &live[1], &live[1], &live[1], swapped);
}

struct PublicationFixture
{
    alignas(4) volatile std::int32_t iteratorCount{};
    alignas(4) volatile std::int32_t cameraIteratorCount{};
    FxCamera camera{};
    FxVisState visibility[2]{};
    const FxVisState *readState{};
    const FxVisState *writeState{};
};

bool CameraMatchesSequence(
    const FxCamera &camera,
    const std::uint32_t sequence) noexcept
{
    FxCamera expected{};
    PopulateCamera(&expected, sequence, 1);
    return Sys_AtomicLoad(&camera.isValid) == 1
        && CameraPayloadEquals(camera, expected);
}

bool TestCooperativeWriterVersusExclusiveSnapshot()
{
    PublicationFixture fixture{};
    FxCamera initial{};
    PopulateCamera(&initial, 0u, 1);
    FX_PublishCamera(&fixture.camera, initial, true);
    fixture.readState = &fixture.visibility[0];
    fixture.writeState = &fixture.visibility[1];

    std::atomic<bool> start{false};
    std::atomic<bool> writerDone{false};
    std::atomic<bool> failed{false};

    std::thread writer([&]() {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (std::uint32_t sequence = 1u;
             sequence <= kStressRounds;
             ++sequence)
        {
            if (failed.load(std::memory_order_relaxed))
                break;
            FxIteratorBeginCooperative(&fixture.iteratorCount);
            FxIteratorWaitBeginExclusive(&fixture.cameraIteratorCount);
            FxCamera next{};
            PopulateCamera(&next, sequence, 1);
            FX_PublishCamera(&fixture.camera, next, true);
            const std::uint8_t readSelector =
                static_cast<std::uint8_t>(sequence & 1u);
            fixture.readState = &fixture.visibility[readSelector];
            fixture.writeState = &fixture.visibility[readSelector ^ 1u];
            const bool cameraReleased =
                FxIteratorEndExclusive(&fixture.cameraIteratorCount);
            std::int32_t remaining = -1;
            if (!FxIteratorEndCooperative(
                    &fixture.iteratorCount, &remaining)
                || remaining != 0 || !cameraReleased)
            {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            if ((sequence & 15u) == 0u)
                std::this_thread::yield();
        }
        writerDone.store(true, std::memory_order_release);
    });

    bool sawWriterActive = false;
    std::uint32_t snapshotCount = 0;
    std::uint32_t lastSequence = 0;
    std::thread snapshot([&]() {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (;;)
        {
            while (!FxIteratorTryBeginExclusive(&fixture.iteratorCount))
            {
                if (writerDone.load(std::memory_order_acquire)
                    && failed.load(std::memory_order_relaxed))
                {
                    return;
                }
                std::this_thread::yield();
            }
            const std::uint32_t sequence = fixture.camera.pad[0];
            std::uint8_t readSelector = UINT8_C(0xFF);
            std::uint8_t writeSelector = UINT8_C(0xFF);
            const bool coherent =
                sequence <= kStressRounds
                && CameraMatchesSequence(fixture.camera, sequence)
                && FX_TryDeriveVisibilitySelectors(
                    &fixture.visibility[0],
                    &fixture.visibility[1],
                    fixture.readState,
                    fixture.writeState,
                    &readSelector,
                    &writeSelector)
                && readSelector
                    == static_cast<std::uint8_t>(sequence & 1u)
                && writeSelector == static_cast<std::uint8_t>(readSelector ^ 1u);
            if (!FxIteratorEndExclusive(&fixture.iteratorCount))
            {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            if (!coherent)
            {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            ++snapshotCount;
            lastSequence = sequence;
            if (!writerDone.load(std::memory_order_relaxed))
                sawWriterActive = true;
            if (writerDone.load(std::memory_order_acquire))
            {
                if (failed.load(std::memory_order_relaxed)
                    || sequence != kStressRounds)
                {
                    failed.store(true, std::memory_order_relaxed);
                }
                return;
            }
            std::this_thread::yield();
        }
    });

    start.store(true, std::memory_order_release);
    writer.join();
    snapshot.join();
    return !failed.load(std::memory_order_relaxed)
        && sawWriterActive && snapshotCount != 0
        && lastSequence == kStressRounds
        && Sys_AtomicLoad(&fixture.iteratorCount) == 0
        && Sys_AtomicLoad(&fixture.cameraIteratorCount) == 0;
}

bool TestSharedCameraReadersExcludeWriter()
{
    PublicationFixture fixture{};
    FxCamera initial{};
    PopulateCamera(&initial, 101u, 1);
    FX_PublishCamera(&fixture.camera, initial, true);

    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> readersReady{0};
    std::atomic<std::uint32_t> readersChecked{0};
    std::atomic<bool> firstWriterAttemptComplete{false};
    std::atomic<bool> releaseReaders{false};
    std::atomic<bool> failed{false};

    const auto readerBody = [&]() {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();

        FxIteratorBeginCooperative(&fixture.iteratorCount);
        FxIteratorBeginCooperative(&fixture.cameraIteratorCount);
        readersReady.fetch_add(1u, std::memory_order_release);
        while (!firstWriterAttemptComplete.load(std::memory_order_acquire))
            std::this_thread::yield();

        if (!CameraMatchesSequence(fixture.camera, 101u))
            failed.store(true, std::memory_order_relaxed);
        readersChecked.fetch_add(1u, std::memory_order_release);
        while (!releaseReaders.load(std::memory_order_acquire))
            std::this_thread::yield();

        if (!CameraMatchesSequence(fixture.camera, 101u))
            failed.store(true, std::memory_order_relaxed);
        std::int32_t cameraReadersRemaining = -1;
        if (!FxIteratorEndCooperative(
                &fixture.cameraIteratorCount, &cameraReadersRemaining)
            || cameraReadersRemaining < 0)
        {
            failed.store(true, std::memory_order_relaxed);
        }
        std::int32_t systemReadersRemaining = -1;
        if (!FxIteratorEndCooperative(
                &fixture.iteratorCount, &systemReadersRemaining)
            || systemReadersRemaining < 0)
        {
            failed.store(true, std::memory_order_relaxed);
        }
    };

    std::thread reader1(readerBody);
    std::thread reader2(readerBody);
    std::thread writer([&]() {
        while (readersReady.load(std::memory_order_acquire) != 2u)
            std::this_thread::yield();

        FxIteratorBeginCooperative(&fixture.iteratorCount);
        const bool acquiredWhileReadersOwned =
            FxIteratorTryBeginExclusive(&fixture.cameraIteratorCount);
        if (acquiredWhileReadersOwned)
        {
            failed.store(true, std::memory_order_relaxed);
            if (!FxIteratorEndExclusive(&fixture.cameraIteratorCount))
                failed.store(true, std::memory_order_relaxed);
        }
        firstWriterAttemptComplete.store(true, std::memory_order_release);

        while (!releaseReaders.load(std::memory_order_acquire))
            std::this_thread::yield();
        FxIteratorWaitBeginExclusive(&fixture.cameraIteratorCount);
        FxCamera next{};
        PopulateCamera(&next, 202u, 1);
        FX_PublishCamera(&fixture.camera, next, true);
        if (!FxIteratorEndExclusive(&fixture.cameraIteratorCount))
            failed.store(true, std::memory_order_relaxed);

        std::int32_t remaining = -1;
        if (!FxIteratorEndCooperative(&fixture.iteratorCount, &remaining)
            || remaining < 0)
        {
            failed.store(true, std::memory_order_relaxed);
        }
    });

    start.store(true, std::memory_order_release);
    while (readersChecked.load(std::memory_order_acquire) != 2u)
        std::this_thread::yield();
    if (!firstWriterAttemptComplete.load(std::memory_order_acquire))
        failed.store(true, std::memory_order_relaxed);
    releaseReaders.store(true, std::memory_order_release);

    reader1.join();
    reader2.join();
    writer.join();
    return !failed.load(std::memory_order_relaxed)
        && CameraMatchesSequence(fixture.camera, 202u)
        && Sys_AtomicLoad(&fixture.iteratorCount) == 0
        && Sys_AtomicLoad(&fixture.cameraIteratorCount) == 0;
}
} // namespace

int main()
{
    if (!TestCameraPublicationCoversEveryField())
        return Fail("camera payload field coverage");
    if (!TestCameraPublicationInvalidation())
        return Fail("camera publication invalidation");
    if (!TestArchiveCameraReadiness())
        return Fail("archive camera readiness");
    if (!TestVisibilitySelectorDerivation())
        return Fail("visibility selector derivation and rejection");
    if (!TestVisibilitySelectorPairDerivation())
        return Fail("visibility selector-pair derivation and rejection");
    if (!TestVisibilitySelectorResolution())
        return Fail("visibility selector resolution and failure atomicity");
    if (!TestVisibilitySelectorRoundTripRejection())
        return Fail("visibility selector roundtrip and rejection");
    if (!TestCooperativeWriterVersusExclusiveSnapshot())
        return Fail("cooperative writer versus exclusive snapshot coherence");
    if (!TestSharedCameraReadersExcludeWriter())
        return Fail("shared camera readers versus exclusive writer coherence");
    return 0;
}
