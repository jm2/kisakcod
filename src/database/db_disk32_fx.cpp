#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/20-fx.schema

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

// FX, a wave-3 family (docs/design/FASTFILE_LOADER.md). The body mirrors
// Load_FxEffectDef without its FX zone-adapter return: the 32-byte record
// streams into the temp block, then with block 4 pushed its name and, for any
// non-null token, its elements 4-aligned (Load_FxElemDefArray), converted
// into native storage. Each element's samples follow it 4-aligned and stay in
// block 4, since they hold no pointer; then its visuals: materials and models
// through their families' pointer steps, sound names, and effects named
// as Load_FxEffectDefRef names them; then its trail, whose vertices and
// indices also stay in block 4. The rules are the FX converter's
// (fx_fastfile_native_disk32.cpp), the oracle this loader replaces: counts,
// timing, atlas, samples, visual counts by element type, the looping life
// and the converted size, every visual token it resolves, and the trails.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
constexpr std::uint32_t kMaxElements = 256;
constexpr std::int64_t kElemPoolSize = 2048; // MAX_ELEMS, the runtime's element pool
constexpr std::uint32_t kMaxVisuals = 32;
constexpr std::uint32_t kMaxDecalVisuals = 16;
constexpr std::int32_t kMaxTrailVertices = 64;
constexpr std::int32_t kMaxTrailIndices = 128;
// Bounds the runtime's signed sums of delay, lifespan and last spawn time.
constexpr std::int64_t kDurationLimitMsec = std::int64_t{24} * 60 * 60 * 1000;
// The largest amplitude whose (amplitude + 1) * random16 fits 32 bits.
constexpr std::int32_t kRandomRangeAmplitudeMax = 32767;

// FxElemTypeDisk32's values, a fast-file fact.
enum ElemType : std::uint8_t
{
    kTrail = 3,
    kModel = 5,
    kOmniLight = 6,
    kSpotLight = 7,
    kSound = 8,
    kDecal = 9,
    kRunner = 10,
    kTypeCount = 11,
};

bool IsLight(std::uint8_t type)
{
    return type == kOmniLight || type == kSpotLight;
}

bool UsesMaterial(std::uint8_t type)
{
    return type <= kTrail + 1 || type == kDecal; // sprites, tails, trails, clouds, decals
}

bool TimeRangeValid(const disk32::FxIntRangeDisk32 &range, bool positive)
{
    const std::int64_t low = range.base;
    const std::int64_t high = low + range.amplitude;
    return range.amplitude >= 0 && range.amplitude <= kRandomRangeAmplitudeMax && (!positive || low > 0)
        && low >= -kDurationLimitMsec && high <= kDurationLimitMsec;
}

// A one-shot element reads spawn as a count range.
bool OneShotCountValid(const disk32::FxSpawnDefLoopingDisk32 &spawn)
{
    return spawn.intervalMsec >= 0 && spawn.count >= 0 && spawn.count <= kRandomRangeAmplitudeMax
        && std::int64_t{spawn.intervalMsec} + spawn.count <= kElemPoolSize;
}

// An inactive atlas is all zero; an active one has 2^bits entries and a
// time-based frame rate whose product with any lifespan fits 32 bits.
bool AtlasValid(const disk32::FxElemDefDisk32 &elem)
{
    if (!UsesMaterial(elem.elemType) || !elem.visualCount)
    {
        return !(elem.behavior | elem.index | elem.fps | elem.loopCount | elem.colIndexBits | elem.rowIndexBits
                 | elem.entryCount);
    }
    const std::uint32_t bits = std::uint32_t{elem.colIndexBits} + elem.rowIndexBits;
    if (bits > 8 || elem.entryCount != (1 << bits) || (elem.behavior & 3u) == 3u)
        return false;
    const std::int64_t life = std::int64_t{elem.lifeSpanMsec.base} + elem.lifeSpanMsec.amplitude;
    return (elem.behavior & 4u) || !elem.fps || life <= (std::numeric_limits<std::int32_t>::max)() / elem.fps;
}

// What the record states of its elements: the latest last-spawn time of the
// looping ones (or infinite), and the size FX_Convert gave the whole effect.
struct Totals
{
    std::int64_t msec = 0;
    bool infinite = false;
    std::uint64_t bytes = 0;
};

bool SpawnValid(bool looping, const disk32::FxSpawnDefLoopingDisk32 &spawn, Totals *totals)
{
    if (!looping)
        return OneShotCountValid(spawn);
    if (spawn.intervalMsec <= 0 || spawn.intervalMsec > kDurationLimitMsec || spawn.count <= 0)
        return false;
    if (spawn.count == (std::numeric_limits<std::int32_t>::max)())
    {
        totals->infinite = true;
        return true;
    }
    const std::int64_t last = std::int64_t{spawn.intervalMsec} * (spawn.count - 1);
    totals->msec = (std::max)(totals->msec, last);
    return last <= kDurationLimitMsec;
}

// Every element but a runner samples its visual state; a runner has none.
bool SamplesValid(const disk32::FxElemDefDisk32 &elem)
{
    const bool visible = elem.elemType != kRunner;
    return !elem.velSamples.token.isNull() && elem.velIntervalCount
        && visible == !elem.visSamples.token.isNull() && visible == (elem.visStateIntervalCount != 0);
}

// A light has one implicit visual; a decal names 1..16 mark pairs; the rest
// name up to 32 visuals, a runner at least one, inline past one.
bool VisualCountValid(const disk32::FxElemDefDisk32 &elem)
{
    const bool named = !elem.visuals.token.isNull();
    if (IsLight(elem.elemType))
        return elem.visualCount == 1 && !named;
    if (elem.elemType == kDecal)
        return elem.visualCount && elem.visualCount <= kMaxDecalVisuals && named;
    return elem.visualCount <= kMaxVisuals && (elem.elemType != kRunner || elem.visualCount)
        && named == (elem.visualCount != 0);
}

// Only a looping trail element has a trail.
bool TrailPresenceValid(bool looping, const disk32::FxElemDefDisk32 &elem)
{
    const bool named = !elem.trailDef.token.isNull();
    return elem.elemType == kTrail ? looping && named : !named;
}

bool ElementValid(const disk32::FxEffectDefDisk32 &effect, std::uint32_t index, const disk32::FxElemDefDisk32 &elem,
                  Totals *totals)
{
    const bool looping = index < static_cast<std::uint32_t>(effect.elemDefCountLooping);
    return elem.elemType < kTypeCount && TimeRangeValid(elem.spawnDelayMsec, false)
        && TimeRangeValid(elem.lifeSpanMsec, true) && SpawnValid(looping, elem.spawn, totals) && AtlasValid(elem)
        && SamplesValid(elem) && VisualCountValid(elem) && TrailPresenceValid(looping, elem);
}

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

// An element's share of FX_Convert's effect size: its samples and visual arrays.
std::uint64_t ElementBytes(const disk32::FxElemDefDisk32 &elem)
{
    std::uint64_t bytes = (elem.velIntervalCount + 1u) * sizeof(FxElemVelStateSample);
    if (!elem.visSamples.token.isNull())
        bytes += (elem.visStateIntervalCount + 1u) * sizeof(FxElemVisStateSample);
    if (elem.elemType == kDecal)
        return bytes + elem.visualCount * 2 * sizeof(disk32::PointerToken);
    return bytes + (IsLight(elem.elemType) || elem.visualCount < 2 ? 0 : elem.visualCount * sizeof(disk32::PointerToken));
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

// Load_FxEffectDefRef: a name, inline or by offset, that
// Load_FxEffectDefFromName turns into the effect it names.
bool LoadEffectRef(disk32::PointerToken token, FxEffectDefRef *out)
{
    if (!LoadXString(disk32::Ptr32<const char>{token}, &out->name))
        return false;
    Load_FxEffectDefFromName(&out->name);
    return true;
}

// Load_MaterialHandle, for a token the converter requires.
bool LoadMaterial(disk32::PointerToken token, Material **out)
{
    *out = nullptr;
    if (token.isNull())
        return Drop("Invalid fast-file effect visual");
    LoadMaterialPtr(token, out);
    return true;
}

// Load_FxElemVisuals: one visual, read as its element's type names it.
bool LoadVisual(std::uint8_t type, disk32::PointerToken token, FxElemVisuals *out)
{
    out->anonymous = nullptr;
    if (token.isNull())
        return Drop("Invalid fast-file effect visual"); // the converter resolves every one
    if (type == kModel)
        LoadXModelPtr(token, &out->model);
    else if (type == kRunner)
        return LoadEffectRef(token, &out->effectDef);
    else if (type == kSound)
        return LoadXString(disk32::Ptr32<const char>{token}, &out->soundName);
    else
        return LoadMaterial(token, &out->material);
    return true;
}

disk32::PointerToken TokenAt(const std::uint8_t *tokens, std::uint32_t index)
{
    disk32::PointerToken token{};
    std::memcpy(&token, tokens + index * sizeof(token), sizeof(token));
    return token;
}

// A decal's mark pairs: two material tokens each.
bool LoadMarks(const std::uint8_t *tokens, std::uint32_t count, FxElemMarkVisuals **out)
{
    FxElemMarkVisuals *const marks = AllocNative<FxElemMarkVisuals>(static_cast<std::int32_t>(count));
    if (!marks)
        return false;
    *out = marks;
    for (std::uint32_t index = 0; index < count * 2; ++index)
    {
        if (!LoadMaterial(TokenAt(tokens, index), &marks[index / 2].materials[index % 2]))
            return false;
    }
    return true;
}

bool LoadVisualArray(std::uint8_t type, const std::uint8_t *tokens, std::uint32_t count, FxElemVisuals **out)
{
    FxElemVisuals *const visuals = AllocNative<FxElemVisuals>(static_cast<std::int32_t>(count));
    if (!visuals)
        return false;
    *out = visuals;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (!LoadVisual(type, TokenAt(tokens, index), &visuals[index]))
            return false;
    }
    return true;
}

// Load_FxElemDefVisuals: past one visual, or for a decal's mark pairs, the
// tokens follow 4-aligned and convert into native storage; one visual sits
// in the record. A light has none.
bool LoadVisuals(const disk32::FxElemDefDisk32 &disk, FxElemDef *out)
{
    const bool decal = disk.elemType == kDecal;
    if (IsLight(disk.elemType) || !disk.visualCount)
        return true;
    if (!decal && disk.visualCount == 1)
        return LoadVisual(disk.elemType, disk.visuals.token, &out->visuals.instance);
    const std::uint32_t count = disk.visualCount * (decal ? 2u : 1u);
    std::uint8_t *const tokens = DB_AllocStreamPos(3);
    if (!StreamBytes(tokens, static_cast<std::int32_t>(count * sizeof(disk32::PointerToken))))
        return false;
    return decal ? LoadMarks(tokens, disk.visualCount, &out->visuals.markArray)
                 : LoadVisualArray(disk.elemType, tokens, disk.visualCount, &out->visuals.array);
}

// The converter's trail rules past its counts: positive distances, an even
// index count of at least one per vertex, each index naming a vertex.
bool TrailValid(const FxTrailDef &trail)
{
    if (trail.repeatDist <= 0 || trail.splitDist <= 0 || trail.vertCount > trail.indCount || (trail.indCount & 1))
        return false;
    for (std::int32_t index = 0; index < trail.indCount; ++index)
    {
        if (trail.inds[index] >= trail.vertCount)
            return false;
    }
    return true;
}

// The converter's trail counts, before they stream: 1..64 vertices and
// 1..128 indices, both present.
bool TrailCountsValid(const disk32::FxTrailDefDisk32 &disk)
{
    return disk.vertCount > 0 && disk.vertCount <= kMaxTrailVertices && disk.indCount > 0
        && disk.indCount <= kMaxTrailIndices && !disk.verts.token.isNull() && !disk.inds.token.isNull();
}

// Load_FxTrailDef: the record 4-aligned, converted into native storage, then
// its vertices (4-aligned) and indices (2-aligned), which keep their layout
// in block 4. The converter requires both.
bool LoadTrail(FxTrailDef **out)
{
    disk32::FxTrailDefDisk32 disk{};
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    FxTrailDef *const trail = AllocNative<FxTrailDef>(1);
    if (!trail)
        return false;
    *trail = {};
    CopyFxTrailDefScalars(disk, trail);
    if (!TrailCountsValid(disk))
        return Drop("Invalid fast-file effect trail");
    if (!LoadSamples(disk.verts.token, static_cast<std::uint32_t>(disk.vertCount), &trail->verts))
        return false;
    trail->inds = reinterpret_cast<std::uint16_t *>(DB_AllocStreamPos(1));
    if (!StreamBytes(reinterpret_cast<std::uint8_t *>(trail->inds), disk.indCount * 2))
        return false;
    if (!TrailValid(*trail))
        return Drop("Invalid fast-file effect trail");
    *out = trail;
    return true;
}

// Load_FxElemDef: the scalars, the samples, the visuals, the effects it
// names, then its trail.
bool LoadElement(const disk32::FxElemDefDisk32 &disk, FxElemDef *out)
{
    std::memset(out, 0, sizeof(*out)); // padding too: native storage starts as junk
    CopyElement(disk, out);
    if (!LoadSamples(disk.velSamples.token, disk.velIntervalCount + 1u, &out->velSamples)
        || !LoadSamples(disk.visSamples.token, disk.visStateIntervalCount + 1u, &out->visSamples))
    {
        return false;
    }
    if (!LoadVisuals(disk, out) || !LoadEffectRef(disk.effectOnImpact.token, &out->effectOnImpact)
        || !LoadEffectRef(disk.effectOnDeath.token, &out->effectOnDeath)
        || !LoadEffectRef(disk.effectEmitted.token, &out->effectEmitted))
    {
        return false;
    }
    return disk.trailDef.token.isNull() || LoadTrail(&out->trailDef);
}

// Load_FxElemDefArray: count records 4-aligned, each checked, then loaded.
bool LoadElements(const disk32::FxEffectDefDisk32 &effect, std::uint32_t count, Totals *totals,
                  const FxElemDef **out)
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
        if (!ElementValid(effect, index, disk, totals))
            return Drop("Invalid fast-file effect element");
        totals->bytes += ElementBytes(disk);
        if (!LoadElement(disk, &native[index]))
            return false;
        // FX_Convert sizes a trail's vertices by its index count.
        if (const FxTrailDef *const trail = native[index].trailDef)
            totals->bytes += sizeof(disk32::FxTrailDefDisk32) + trail->indCount * (sizeof(FxTrailVertex) + 2u);
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
    // LoadXString found the name's terminator inside block 4.
    const std::size_t nameBytes = std::strlen(out->name) + 1; // Flawfinder: ignore
    Totals totals{0, false, sizeof(disk) + count * sizeof(disk32::FxElemDefDisk32) + nameBytes};
    if (count && !LoadElements(disk, count, &totals, &out->elemDefs))
        return false;
    // Its longest looping spawn and its size, as FX_Convert computes them.
    if (disk.msecLoopingLife != (totals.infinite ? (std::numeric_limits<std::int32_t>::max)() : totals.msec))
        return Drop("Invalid fast-file effect looping life");
    if (static_cast<std::uint64_t>(disk.totalSize) != totals.bytes)
        return Drop("Invalid fast-file effect size");
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadFxEffectDefHandleDisk32(bool atStreamStart, const FxEffectDef **slot)
{
    db::disk32_load::LoadFxEffectDefHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
