#include <EffectsCore/fx_missing_effect_alias.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

static_assert(sizeof(FxElemDef) == FX_ELEM_DEF_RUNTIME_SIZE);
static_assert(alignof(FxElemDef) == FX_ELEM_DEF_RUNTIME_ALIGNMENT);

namespace
{
constexpr std::size_t TEST_STORAGE_SIZE =
    sizeof(FxEffectDef) + FX_MISSING_EFFECT_ALIAS_NAME_CAPACITY;

bool PlanEquals(
    const FxMissingEffectAliasPlan &left,
    const FxMissingEffectAliasPlan &right)
{
    return left.totalSize == right.totalSize
        && left.nameOffset == right.nameOffset
        && left.nameLength == right.nameLength;
}

bool HeaderFieldsArePreserved(
    const FxEffectDef &alias,
    const FxEffectDef &source)
{
    return alias.flags == source.flags
        && alias.msecLoopingLife == source.msecLoopingLife
        && alias.elemDefCountLooping == source.elemDefCountLooping
        && alias.elemDefCountOneShot == source.elemDefCountOneShot
        && alias.elemDefCountEmission == source.elemDefCountEmission
        && alias.elemDefs == source.elemDefs;
}

template <std::size_t SIZE>
bool BytesEqual(
    const std::array<std::uint8_t, SIZE> &left,
    const std::array<std::uint8_t, SIZE> &right)
{
    return std::memcmp(left.data(), right.data(), SIZE) == 0;
}
}

int main()
{
    struct SourceBlob
    {
        FxEffectDef effect;
        FxElemDef elemDefs[2];
        char name[sizeof("misc/missing_fx")];
    };
    SourceBlob sourceBlob{};
    sourceBlob.elemDefs[0].flags = 0x40;
    sourceBlob.elemDefs[1].flags = 0x80;
    std::memcpy(
        sourceBlob.name, "misc/missing_fx", sizeof(sourceBlob.name));
    sourceBlob.effect = {
        sourceBlob.name,
        0x12345678,
        static_cast<std::int32_t>(sizeof(sourceBlob)),
        9876,
        1,
        1,
        0,
        sourceBlob.elemDefs,
    };
    FxEffectDef &source = sourceBlob.effect;
    const FxEffectDef sourceSnapshot = source;
    // Snapshot the complete object representation.  Copy construction is not
    // required to preserve padding bytes, so a typed snapshot makes the
    // subsequent immutability check compiler-dependent for the canonical
    // native FxElemDef.
    std::array<std::uint8_t, sizeof(sourceBlob.elemDefs)> elemDefSnapshot{};
    std::memcpy(
        elemDefSnapshot.data(),
        sourceBlob.elemDefs,
        sizeof(sourceBlob.elemDefs));

    char requestedName[] = "weapons/rifle/muzzle_flash";
    FxMissingEffectAliasPlan plan{};
    if (!FX_TryPlanMissingEffectAlias(&source, requestedName, &plan)
        || plan.nameLength != std::strlen(requestedName)
        || plan.nameOffset != sizeof(FxEffectDef)
        || plan.totalSize != plan.nameOffset + plan.nameLength + 1)
    {
        return 1;
    }

    alignas(FxEffectDef)
        std::array<std::uint8_t, TEST_STORAGE_SIZE> exactStorage{};
    exactStorage.fill(0xA5);
    FxEffectDef *alias = reinterpret_cast<FxEffectDef *>(
        static_cast<std::uintptr_t>(1));
    if (!FX_TryBuildMissingEffectAlias(
            &source, requestedName, exactStorage.data(), plan.totalSize, &alias)
        || alias != reinterpret_cast<FxEffectDef *>(exactStorage.data())
        || alias->name != reinterpret_cast<const char *>(
            exactStorage.data() + plan.nameOffset)
        || std::strcmp(alias->name, requestedName) != 0
        || alias->totalSize != static_cast<std::int32_t>(plan.totalSize)
        || !HeaderFieldsArePreserved(*alias, source)
        || alias->elemDefs != sourceBlob.elemDefs
        || std::memcmp(&source, &sourceSnapshot, sizeof(source)) != 0
        || std::memcmp(
            sourceBlob.elemDefs,
            elemDefSnapshot.data(),
            sizeof(sourceBlob.elemDefs)) != 0
        || exactStorage[plan.totalSize] != 0xA5)
    {
        return 2;
    }

    // The requested name is owned by the alias rather than borrowed.
    requestedName[0] = 'X';
    if (std::strcmp(alias->name, "weapons/rifle/muzzle_flash") != 0)
        return 3;

    alignas(FxEffectDef)
        std::array<std::uint8_t, TEST_STORAGE_SIZE> truncatedStorage{};
    truncatedStorage.fill(0x6B);
    const auto truncatedSnapshot = truncatedStorage;
    FxEffectDef *const pointerSentinel = reinterpret_cast<FxEffectDef *>(
        static_cast<std::uintptr_t>(1));
    FxEffectDef *truncatedAlias = pointerSentinel;
    if (FX_TryBuildMissingEffectAlias(
            &source,
            "weapons/rifle/muzzle_flash",
            truncatedStorage.data(),
            plan.totalSize - 1,
            &truncatedAlias)
        || truncatedAlias != pointerSentinel
        || !BytesEqual(truncatedStorage, truncatedSnapshot)
        || std::memcmp(&source, &sourceSnapshot, sizeof(source)) != 0)
    {
        return 4;
    }

    std::array<char, FX_MISSING_EFFECT_ALIAS_NAME_CAPACITY> maxName{};
    maxName.fill('m');
    maxName.back() = '\0';
    FxMissingEffectAliasPlan maxPlan{};
    alignas(FxEffectDef)
        std::array<std::uint8_t, TEST_STORAGE_SIZE> maxStorage{};
    FxEffectDef *maxAlias = nullptr;
    if (!FX_TryPlanMissingEffectAlias(&source, maxName.data(), &maxPlan)
        || maxPlan.nameLength != FX_MISSING_EFFECT_ALIAS_NAME_CAPACITY - 1
        || maxPlan.totalSize != TEST_STORAGE_SIZE
        || !FX_TryBuildMissingEffectAlias(
            &source, maxName.data(), maxStorage.data(), maxStorage.size(), &maxAlias)
        || std::memcmp(maxAlias->name, maxName.data(), maxName.size()) != 0)
    {
        return 5;
    }

    std::array<char, FX_MISSING_EFFECT_ALIAS_NAME_CAPACITY + 1> tooLongName{};
    tooLongName.fill('l');
    tooLongName.back() = '\0';
    std::array<char, FX_MISSING_EFFECT_ALIAS_NAME_CAPACITY> unterminatedName{};
    unterminatedName.fill('u');
    const FxMissingEffectAliasPlan planSentinel{11, 22, 33};
    FxMissingEffectAliasPlan failedPlan = planSentinel;
    if (FX_TryPlanMissingEffectAlias(&source, tooLongName.data(), &failedPlan)
        || !PlanEquals(failedPlan, planSentinel))
    {
        return 6;
    }
    if (FX_TryPlanMissingEffectAlias(
            &source, unterminatedName.data(), &failedPlan)
        || FX_TryPlanMissingEffectAlias(&source, "", &failedPlan)
        || FX_TryPlanMissingEffectAlias(&source, nullptr, &failedPlan)
        || FX_TryPlanMissingEffectAlias(nullptr, "valid", &failedPlan)
        || !PlanEquals(failedPlan, planSentinel))
    {
        return 7;
    }

    FxEffectDef invalidSource = source;
    invalidSource.elemDefCountLooping = -1;
    if (FX_TryPlanMissingEffectAlias(
            &invalidSource, "valid", &failedPlan))
    {
        return 8;
    }
    invalidSource = source;
    invalidSource.elemDefCountLooping =
        static_cast<std::int32_t>(FX_MISSING_EFFECT_ALIAS_MAX_ELEM_DEFS);
    invalidSource.elemDefCountOneShot = 1;
    invalidSource.elemDefCountEmission = 0;
    if (FX_TryPlanMissingEffectAlias(
            &invalidSource, "valid", &failedPlan))
    {
        return 9;
    }
    invalidSource = source;
    invalidSource.elemDefs = nullptr;
    if (FX_TryPlanMissingEffectAlias(
            &invalidSource, "valid", &failedPlan))
    {
        return 10;
    }
    invalidSource = source;
    invalidSource.elemDefs =
        reinterpret_cast<const FxElemDef *>(exactStorage.data());
    if (FX_TryPlanMissingEffectAlias(
            &invalidSource, "valid", &failedPlan))
    {
        return 11;
    }
    invalidSource = source;
    invalidSource.elemDefs = reinterpret_cast<const FxElemDef *>(
        reinterpret_cast<const std::uint8_t *>(source.elemDefs) + 1);
    if (FX_TryPlanMissingEffectAlias(
            &invalidSource, "valid", &failedPlan))
    {
        return 12;
    }
    invalidSource = source;
    const std::uintptr_t truncatedElemAddress =
        (reinterpret_cast<std::uintptr_t>(&sourceBlob)
            + sizeof(sourceBlob) - 1)
        & ~(static_cast<std::uintptr_t>(FX_ELEM_DEF_RUNTIME_ALIGNMENT) - 1);
    invalidSource.elemDefs =
        reinterpret_cast<const FxElemDef *>(truncatedElemAddress);
    if (FX_TryPlanMissingEffectAlias(
            &invalidSource, "valid", &failedPlan))
    {
        return 13;
    }
    invalidSource = source;
    invalidSource.elemDefCountLooping = 0;
    invalidSource.elemDefCountOneShot = 0;
    invalidSource.elemDefCountEmission = 0;
    invalidSource.elemDefs = nullptr;
    if (!FX_TryPlanMissingEffectAlias(
            &invalidSource, "valid", &failedPlan))
    {
        return 14;
    }

    // The reusable helper must not overwrite any part of the immutable source
    // blob, including the shared element-definition storage.
    FxEffectDef *overlapAlias = pointerSentinel;
    if (FX_TryBuildMissingEffectAlias(
            &source,
            "valid",
            sourceBlob.elemDefs,
            plan.totalSize,
            &overlapAlias)
        || overlapAlias != pointerSentinel
        || std::memcmp(&source, &sourceSnapshot, sizeof(source)) != 0
        || std::memcmp(
            sourceBlob.elemDefs,
            elemDefSnapshot.data(),
            sizeof(sourceBlob.elemDefs)) != 0)
    {
        return 15;
    }

    // Publishing through a result slot inside an input range would mutate
    // that input even if the output blob itself were disjoint.
    overlapAlias = pointerSentinel;
    if (FX_TryBuildMissingEffectAlias(
            &source,
            "valid",
            truncatedStorage.data(),
            truncatedStorage.size(),
            reinterpret_cast<FxEffectDef **>(&source.flags))
        || std::memcmp(&source, &sourceSnapshot, sizeof(source)) != 0)
    {
        return 16;
    }

    alignas(FxEffectDef)
        std::array<std::uint8_t, TEST_STORAGE_SIZE> failedStorage{};
    failedStorage.fill(0xD3);
    const auto failedStorageSnapshot = failedStorage;
    FxEffectDef *failedAlias = pointerSentinel;
    if (FX_TryBuildMissingEffectAlias(
            &source,
            tooLongName.data(),
            failedStorage.data(),
            failedStorage.size(),
            &failedAlias)
        || failedAlias != pointerSentinel
        || !BytesEqual(failedStorage, failedStorageSnapshot))
    {
        return 17;
    }

    // A huge caller-reported capacity must not truncate when adapted to the
    // cursor's signed runtime-size boundary.
    failedAlias = nullptr;
    if (!FX_TryBuildMissingEffectAlias(
            &source,
            "valid",
            failedStorage.data(),
            (std::numeric_limits<std::size_t>::max)(),
            &failedAlias)
        || !failedAlias || std::strcmp(failedAlias->name, "valid") != 0)
    {
        return 18;
    }

    // Reject an otherwise aligned address whose write range would wrap before
    // touching it, and leave the result pointer transactional.
    const std::uintptr_t overflowingAddress =
        (std::numeric_limits<std::uintptr_t>::max)()
        & ~(static_cast<std::uintptr_t>(alignof(FxEffectDef)) - 1);
    failedAlias = pointerSentinel;
    if (FX_TryBuildMissingEffectAlias(
            &source,
            "valid",
            reinterpret_cast<void *>(overflowingAddress),
            (std::numeric_limits<std::size_t>::max)(),
            &failedAlias)
        || failedAlias != pointerSentinel)
    {
        return 19;
    }

    // Misalignment and in-place source writes fail before modifying storage.
    alignas(FxEffectDef)
        std::array<std::uint8_t, TEST_STORAGE_SIZE + alignof(FxEffectDef)> raw{};
    raw.fill(0x91);
    const auto rawSnapshot = raw;
    failedAlias = pointerSentinel;
    if (FX_TryBuildMissingEffectAlias(
            &source,
            "valid",
            raw.data() + 1,
            raw.size() - 1,
            &failedAlias)
        || failedAlias != pointerSentinel || !BytesEqual(raw, rawSnapshot))
    {
        return 20;
    }

    failedAlias = pointerSentinel;
    if (FX_TryBuildMissingEffectAlias(
            &source,
            "valid",
            &source,
            sizeof(source),
            &failedAlias)
        || failedAlias != pointerSentinel
        || std::memcmp(&source, &sourceSnapshot, sizeof(source)) != 0)
    {
        return 21;
    }

    return 0;
}
