#pragma once

#include <EffectsCore/fx_runtime.h>

#include <universal/sys_atomic.h>

#include <cstddef>
#include <cstdint>

// These helpers order the legacy camera-validity marker around its payload,
// but the marker is not a substitute for synchronization. Cooperative
// admission protects camera access from archive/restore/lifecycle-exclusive
// owners. Runtime readers and publishers additionally hold the external
// shared/exclusive camera-publication gate declared in fx_system.h; worker
// completion boundaries alone are not the payload-synchronization contract.
inline void FX_InvalidateCameraPublication(FxCamera *const camera) noexcept
{
    Sys_AtomicStore(&camera->isValid, 0);
    camera->frustumPlaneCount = 0;
}

inline void FX_PublishCamera(
    FxCamera *const destination,
    const FxCamera &source,
    const bool desiredValidity) noexcept
{
    // Withdraw validity before touching any payload field. The final atomic
    // store is the publication marker after every payload field is copied.
    Sys_AtomicStore(&destination->isValid, 0);

    for (std::size_t component = 0; component < 3; ++component)
        destination->origin[component] = source.origin[component];
    for (std::size_t plane = 0; plane < 6; ++plane)
    {
        for (std::size_t component = 0; component < 4; ++component)
        {
            destination->frustum[plane][component] =
                source.frustum[plane][component];
        }
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t component = 0; component < 3; ++component)
            destination->axis[row][component] = source.axis[row][component];
    }
    destination->frustumPlaneCount = source.frustumPlaneCount;
    for (std::size_t component = 0; component < 3; ++component)
    {
        destination->viewOffset[component] = source.viewOffset[component];
        destination->pad[component] = source.pad[component];
    }

    Sys_AtomicStore(&destination->isValid, desiredValidity ? 1 : 0);
}

inline bool FX_IsCanonicalInvalidCamera(
    const FxCamera &camera) noexcept
{
    if (Sys_AtomicLoad(&camera.isValid) != 0
        || camera.frustumPlaneCount != 0)
    {
        return false;
    }
    for (const float value : camera.origin)
    {
        if (value != 0.0f)
            return false;
    }
    for (const auto &plane : camera.frustum)
    {
        for (const float value : plane)
        {
            if (value != 0.0f)
                return false;
        }
    }
    for (const auto &row : camera.axis)
    {
        for (const float value : row)
        {
            if (value != 0.0f)
                return false;
        }
    }
    for (const float value : camera.viewOffset)
    {
        if (value != 0.0f)
            return false;
    }
    for (const std::uint32_t value : camera.pad)
    {
        if (value != 0)
            return false;
    }
    return true;
}

// An active frame must never serialize the intentional time-to-camera
// publication gap. Before the first view, the reset image may instead carry
// an exact all-zero invalid camera.
inline bool FX_AreArchiveCamerasReady(
    const FxCamera &camera,
    const FxCamera &previousCamera,
    const std::int32_t msecDraw) noexcept
{
    const std::int32_t cameraIsValid = Sys_AtomicLoad(&camera.isValid);
    const std::int32_t previousCameraIsValid =
        Sys_AtomicLoad(&previousCamera.isValid);
    if (msecDraw >= 0)
        return cameraIsValid == 1 && previousCameraIsValid == 1;
    if (msecDraw != -1)
        return false;
    return (cameraIsValid == 1 || FX_IsCanonicalInvalidCamera(camera))
        && (previousCameraIsValid == 1
            || FX_IsCanonicalInvalidCamera(previousCamera));
}

// Derive the read/write state selectors as one checked transaction. Exact
// pointer equality is intentional: neither foreign interior pointers nor an
// archived process address may be interpreted as a visibility-buffer index.
// Outputs are committed only after both selectors are known to be distinct.
inline bool FX_TryDeriveVisibilitySelectors(
    const FxVisState *const slot0,
    const FxVisState *const slot1,
    const FxVisState *const readState,
    const FxVisState *const writeState,
    std::uint8_t *const outReadSelector,
    std::uint8_t *const outWriteSelector) noexcept
{
    if (!slot0 || !slot1 || slot0 == slot1 || !readState || !writeState
        || readState == writeState || !outReadSelector || !outWriteSelector
        || outReadSelector == outWriteSelector)
    {
        return false;
    }

    std::uint8_t readSelector = 0;
    if (readState == slot1)
        readSelector = 1;
    else if (readState != slot0)
        return false;

    std::uint8_t writeSelector = 0;
    if (writeState == slot1)
        writeSelector = 1;
    else if (writeState != slot0)
        return false;

    if (readSelector == writeSelector)
        return false;

    *outReadSelector = readSelector;
    *outWriteSelector = writeSelector;
    return true;
}
