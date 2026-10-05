#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/20-fx.schema

#include <cstdint>
#include <cstring>
#include <utility>

// FX, a wave-3 family (docs/design/FASTFILE_LOADER.md). The body mirrors
// Load_FxEffectDef without its FX zone-adapter return: the 32-byte record
// streams into the temp block, then with block 4 pushed its name and, for any
// non-null token, its elements 4-aligned (Load_FxElemDefArray), converted
// into native storage. Each element's samples follow it 4-aligned and stay in
// block 4, since they hold no pointer. Visuals, effect references and trails
// do not load yet, so an element that names one fails closed.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
constexpr std::uint32_t kMaxElements = 256;

// The three counts' sum is at most 256, so none is negative, and its token
// agrees with it.
bool HeaderValid(const disk32::FxEffectDefDisk32 &effect, std::uint32_t *count)
{
    const std::uint64_t sum = std::uint64_t{static_cast<std::uint32_t>(effect.elemDefCountLooping)}
        + static_cast<std::uint32_t>(effect.elemDefCountOneShot)
        + static_cast<std::uint32_t>(effect.elemDefCountEmission);
    *count = static_cast<std::uint32_t>(sum);
    return sum <= kMaxElements && (sum == 0) == effect.elemDefs.token.isNull();
}

// Load_FxElemVelStateSampleArray or its visual twin: any non-null token means
// count samples follow 4-aligned; they keep their layout in block 4.
template <typename Sample>
bool LoadSamples(disk32::PointerToken token, std::uint32_t count, Sample **out)
{
    static_assert(alignof(Sample) <= 4);
    *out = nullptr;
    if (token.isNull())
        return true;
    std::uint8_t *const samples = DB_AllocStreamPos(3);
    if (!StreamBytes(samples, static_cast<std::int32_t>(count * sizeof(Sample))))
        return false;
    *out = reinterpret_cast<Sample *>(samples);
    return true;
}

void CopyElement(const disk32::FxElemDefDisk32 &disk, FxElemDef *out)
{
    CopyFxElemDefScalars(disk, out);
    CopyFxSpawnDefLoopingScalars(disk.spawn, &out->spawn.looping);
    CopyFxIntRangeScalars(disk.spawnDelayMsec, &out->spawnDelayMsec);
    CopyFxIntRangeScalars(disk.lifeSpanMsec, &out->lifeSpanMsec);
    const std::pair<const disk32::FxFloatRangeDisk32 &, FxFloatRange &> ranges[] = {
        {disk.spawnRange, out->spawnRange},
        {disk.fadeInRange, out->fadeInRange},
        {disk.fadeOutRange, out->fadeOutRange},
        {disk.spawnOffsetRadius, out->spawnOffsetRadius},
        {disk.spawnOffsetHeight, out->spawnOffsetHeight},
        {disk.initialRotation, out->initialRotation},
        {disk.gravity, out->gravity},
        {disk.reflectionFactor, out->reflectionFactor},
        {disk.emitDist, out->emitDist},
        {disk.emitDistVariance, out->emitDistVariance},
    };
    for (const auto &[from, to] : ranges)
        CopyFxFloatRangeScalars(from, &to);
    for (int axis = 0; axis < 3; ++axis)
    {
        CopyFxFloatRangeScalars(disk.spawnOrigin[axis], &out->spawnOrigin[axis]);
        CopyFxFloatRangeScalars(disk.spawnAngles[axis], &out->spawnAngles[axis]);
        CopyFxFloatRangeScalars(disk.angularVelocity[axis], &out->angularVelocity[axis]);
    }
}

// Load_FxElemDef: the scalars, then the samples.
bool LoadElement(const disk32::FxElemDefDisk32 &disk, FxElemDef *out)
{
    std::memset(out, 0, sizeof(*out)); // padding too: native storage starts as junk
    CopyElement(disk, out);
    if (!LoadSamples(disk.velSamples.token, disk.velIntervalCount + 1u, &out->velSamples)
        || !LoadSamples(disk.visSamples.token, disk.visStateIntervalCount + 1u, &out->visSamples))
    {
        return false;
    }
    if (!disk.visuals.token.isNull() || !disk.effectOnImpact.token.isNull() || !disk.effectOnDeath.token.isNull()
        || !disk.effectEmitted.token.isNull() || !disk.trailDef.token.isNull())
    {
        return Drop("Fast-file effect visuals, effect names and trails have no 64-bit loader yet");
    }
    return true;
}

// Load_FxElemDefArray: count records 4-aligned, then each element's parts.
bool LoadElements(std::uint32_t count, const FxElemDef **out)
{
    std::uint8_t *const records = DB_AllocStreamPos(3);
    if (!StreamBytes(records, static_cast<std::int32_t>(count * sizeof(disk32::FxElemDefDisk32))))
        return false;
    FxElemDef *const native = AllocNative<FxElemDef>(static_cast<std::int32_t>(count));
    if (!native)
        return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        disk32::FxElemDefDisk32 disk{};
        std::memcpy(&disk, records + index * sizeof(disk), sizeof(disk));
        if (!LoadElement(disk, &native[index]))
            return false;
    }
    *out = native;
    return true;
}
} // namespace

// The record at the temp block's position, then its name and its elements
// with block 4 pushed.
bool LoadFxEffectDef(FxEffectDef *out)
{
    disk32::FxEffectDefDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    std::uint32_t count = 0;
    if (!HeaderValid(disk, &count))
        return Drop("Invalid fast-file effect header");
    CopyFxEffectDefScalars(disk, out);
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file effect has no name"); // the asset pool hashes it
    out->elemDefs = nullptr;
    if (count && !LoadElements(count, &out->elemDefs))
        return false;
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadFxEffectDefHandleDisk32(bool atStreamStart, const FxEffectDef **slot)
{
    db::disk32_load::LoadFxEffectDefHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
