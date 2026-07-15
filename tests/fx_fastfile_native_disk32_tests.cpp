#include <EffectsCore/fx_fastfile_disk32.h>
#include <EffectsCore/fx_fastfile_native_disk32.h>
#include <EffectsCore/fx_runtime.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// The production definition lives in the renderer-facing header.  This
// dependency-free test needs only its exact native storage and reads selected
// pointer fields through the exported ABI offsets below.
struct alignas(FX_ELEM_DEF_RUNTIME_ALIGNMENT) FxElemDef
{
    std::uint8_t bytes[FX_ELEM_DEF_RUNTIME_SIZE];
};
static_assert(sizeof(FxElemDef) == FX_ELEM_DEF_RUNTIME_SIZE);
static_assert(alignof(FxElemDef) == FX_ELEM_DEF_RUNTIME_ALIGNMENT);

namespace
{
namespace fastfile = fx::fastfile;

constexpr std::uint32_t kDataBlock = 4;
constexpr std::size_t kBlockCount = 9;
constexpr std::uint8_t kOutputGuard = UINT8_C(0xA5);

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

struct MaterializedInterval final
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::uint32_t provenance = 0;
};

struct DiskBlock final
{
    std::vector<std::uint8_t> bytes{};
    std::vector<MaterializedInterval> materialized{};
};

class DiskImage final
{
public:
    DiskImage()
    {
        for (DiskBlock &block : blocks_)
            block.bytes.reserve(64 * 1024);
    }

    template <typename VALUE>
    disk32::PointerToken Append(
        const std::uint32_t block,
        const VALUE &value,
        const std::uint32_t provenance = 1)
    {
        static_assert(std::is_trivially_copyable_v<VALUE>);
        return AppendArray(block, &value, 1, provenance);
    }

    template <typename VALUE>
    disk32::PointerToken AppendArray(
        const std::uint32_t block,
        const VALUE *const values,
        const std::size_t count,
        const std::uint32_t provenance = 1)
    {
        static_assert(std::is_trivially_copyable_v<VALUE>);
        CHECK(block < blocks_.size());
        CHECK(values != nullptr || count == 0);
        if (block >= blocks_.size() || (!values && count != 0))
            return {};

        const std::size_t byteCount = count * sizeof(VALUE);
        CHECK(count == 0 || byteCount / sizeof(VALUE) == count);
        DiskBlock &destination = blocks_[block];
        const std::size_t alignment = alignof(VALUE);
        const std::size_t aligned =
            (destination.bytes.size() + alignment - 1) & ~(alignment - 1);
        CHECK(aligned <= disk32::kOffsetMask);
        CHECK(byteCount <= disk32::kOffsetMask - aligned);
        if (aligned > disk32::kOffsetMask
            || byteCount > disk32::kOffsetMask - aligned)
        {
            return {};
        }

        destination.bytes.resize(aligned + byteCount, 0);
        if (byteCount)
        {
            std::memcpy(
                destination.bytes.data() + aligned,
                values,
                byteCount);
        }
        destination.materialized.push_back({
            static_cast<std::uint32_t>(aligned),
            static_cast<std::uint32_t>(aligned + byteCount),
            provenance,
        });
        return Encode(block, static_cast<std::uint32_t>(aligned));
    }

    disk32::PointerToken AppendString(
        const std::uint32_t block,
        const std::string_view value,
        const std::uint32_t provenance = 1)
    {
        std::vector<char> terminated(value.begin(), value.end());
        terminated.push_back('\0');
        return AppendArray(
            block, terminated.data(), terminated.size(), provenance);
    }

    template <typename VALUE>
    VALUE *Resolve(const disk32::PointerToken token)
    {
        const Decoded decoded = Decode(token);
        if (!decoded.valid || decoded.block >= blocks_.size())
            return nullptr;
        DiskBlock &block = blocks_[decoded.block];
        if (decoded.offset > block.bytes.size()
            || sizeof(VALUE) > block.bytes.size() - decoded.offset
            || decoded.offset % alignof(VALUE) != 0)
        {
            return nullptr;
        }
        return reinterpret_cast<VALUE *>(
            block.bytes.data() + decoded.offset);
    }

    template <typename VALUE>
    const VALUE *Resolve(const disk32::PointerToken token) const
    {
        return const_cast<DiskImage *>(this)->Resolve<VALUE>(token);
    }

    const DiskBlock &block(const std::size_t index) const
    {
        return blocks_[index];
    }

    DiskBlock &block(const std::size_t index)
    {
        return blocks_[index];
    }

    bool ValidateRange(
        const disk32::PointerToken token,
        const void *const address,
        const std::uint64_t byteCount,
        const std::size_t alignment,
        const std::uint32_t provenance = 1) const
    {
        if (!address || alignment == 0
            || (alignment & (alignment - 1u)) != 0
            || reinterpret_cast<std::uintptr_t>(address) % alignment != 0)
        {
            return false;
        }

        std::size_t blockIndex = blocks_.size();
        std::uint64_t offset = 0;
        for (std::size_t index = 0; index < blocks_.size(); ++index)
        {
            const DiskBlock &candidate = blocks_[index];
            const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(
                candidate.bytes.data());
            const std::uintptr_t pointer =
                reinterpret_cast<std::uintptr_t>(address);
            if (pointer >= begin
                && pointer - begin <= candidate.bytes.size())
            {
                blockIndex = index;
                offset = pointer - begin;
                break;
            }
        }
        if (blockIndex == blocks_.size()
            || byteCount > blocks_[blockIndex].bytes.size() - offset)
        {
            return false;
        }

        if (token.isOffset())
        {
            const Decoded decoded = Decode(token);
            if (!decoded.valid || decoded.block != blockIndex
                || decoded.offset != offset)
            {
                return false;
            }
        }
        else if (!token.isInline() && !token.isSharedInline()
                 && !token.isNull())
        {
            return false;
        }

        const std::uint64_t end = offset + byteCount;
        for (const MaterializedInterval &interval :
             blocks_[blockIndex].materialized)
        {
            if (interval.provenance == provenance
                && offset >= interval.begin && end <= interval.end)
            {
                return true;
            }
        }
        return false;
    }

    std::uint32_t CStringBytes(const disk32::PointerToken token) const
    {
        const char *const string = Resolve<char>(token);
        if (!string)
            return 0;
        const Decoded decoded = Decode(token);
        if (!decoded.valid)
            return 0;
        const std::size_t available =
            blocks_[decoded.block].bytes.size() - decoded.offset;
        for (std::size_t index = 0; index < available; ++index)
        {
            if (string[index] == '\0')
                return static_cast<std::uint32_t>(index + 1u);
        }
        return 0;
    }

    void SetIntervalProvenance(
        const disk32::PointerToken token,
        const std::uint32_t provenance)
    {
        const Decoded decoded = Decode(token);
        CHECK(decoded.valid);
        if (!decoded.valid)
            return;
        for (MaterializedInterval &interval :
             blocks_[decoded.block].materialized)
        {
            if (decoded.offset >= interval.begin
                && decoded.offset < interval.end)
            {
                interval.provenance = provenance;
                return;
            }
        }
        CHECK(false);
    }

    void RemoveMaterializedInterval(const disk32::PointerToken token)
    {
        const Decoded decoded = Decode(token);
        CHECK(decoded.valid);
        if (!decoded.valid)
            return;
        auto &intervals = blocks_[decoded.block].materialized;
        for (auto iterator = intervals.begin(); iterator != intervals.end();
             ++iterator)
        {
            if (decoded.offset >= iterator->begin
                && decoded.offset < iterator->end)
            {
                intervals.erase(iterator);
                return;
            }
        }
        CHECK(false);
    }

    static disk32::PointerToken Encode(
        const std::uint32_t block,
        const std::uint32_t offset)
    {
        CHECK(block < kBlockCount);
        CHECK(offset <= disk32::kOffsetMask);
        const std::uint32_t encoded =
            (block << 28u) | offset;
        CHECK(encoded < disk32::kSharedInline - 1u);
        return {encoded + 1u};
    }

private:
    struct Decoded final
    {
        std::uint32_t block = 0;
        std::uint32_t offset = 0;
        bool valid = false;
    };

    static Decoded Decode(const disk32::PointerToken token)
    {
        if (!token.isOffset())
            return {};
        const std::uint32_t adjusted = token.value - 1u;
        return {
            adjusted >> 28u,
            adjusted & disk32::kOffsetMask,
            true,
        };
    }

    std::array<DiskBlock, kBlockCount> blocks_{};
};

struct EffectFixture final
{
    DiskImage image{};
    disk32::PointerToken effectToken{};
    disk32::PointerToken nameToken{};
    disk32::PointerToken elemsToken{};
    std::vector<disk32::PointerToken> velSampleTokens{};
    std::vector<disk32::PointerToken> visSampleTokens{};
    std::vector<disk32::PointerToken> visualTokens{};
    std::vector<disk32::PointerToken> trailTokens{};
    std::vector<disk32::PointerToken> trailVertexTokens{};
    std::vector<disk32::PointerToken> trailIndexTokens{};
    std::size_t elemCount = 0;

    fastfile::FxEffectDefDisk32 *effect()
    {
        return image.Resolve<fastfile::FxEffectDefDisk32>(effectToken);
    }

    const fastfile::FxEffectDefDisk32 *effect() const
    {
        return image.Resolve<fastfile::FxEffectDefDisk32>(effectToken);
    }

    fastfile::FxElemDefDisk32 *elems()
    {
        return image.Resolve<fastfile::FxElemDefDisk32>(elemsToken);
    }

    const fastfile::FxElemDefDisk32 *elems() const
    {
        return image.Resolve<fastfile::FxElemDefDisk32>(elemsToken);
    }
};

fastfile::FxElemDefDisk32 MinimalElem(
    const fastfile::FxElemTypeDisk32 type =
        fastfile::FxElemTypeDisk32::SpriteBillboard)
{
    fastfile::FxElemDefDisk32 elem{};
    elem.elemType = type;
    elem.spawn.intervalMsecOrCountBase = 1;
    elem.lifeSpanMsec.base = 1000;
    return elem;
}

EffectFixture MakeEffect(
    std::vector<fastfile::FxElemDefDisk32> elems,
    const std::int32_t looping,
    const std::int32_t oneShot,
    const std::int32_t emission)
{
    EffectFixture fixture{};
    fixture.elemCount = elems.size();
    fixture.nameToken = fixture.image.AppendString(
        kDataBlock, "fx/fastfile_native_disk32_test");
    if (!elems.empty())
    {
        fixture.elemsToken = fixture.image.AppendArray(
            kDataBlock, elems.data(), elems.size());
    }

    fastfile::FxEffectDefDisk32 effect{};
    effect.name.token = fixture.nameToken;
    effect.flags = 0x12345678;
    // Builders finalize this after every owned payload is attached.
    effect.totalSize = 0;
    effect.msecLoopingLife = 2500;
    effect.elemDefCountLooping = looping;
    effect.elemDefCountOneShot = oneShot;
    effect.elemDefCountEmission = emission;
    effect.elemDefs.token = fixture.elemsToken;
    fixture.effectToken = fixture.image.Append(kDataBlock, effect);
    return fixture;
}

EffectFixture MakeEmptyEffect();
EffectFixture MakeMinimalEffect();
disk32::PointerToken AddOpaqueReference(
    EffectFixture *fixture,
    std::uint32_t identity);
void AttachVisuals(
    EffectFixture *fixture,
    std::size_t elemIndex,
    fastfile::FxElemTypeDisk32 type,
    const std::vector<disk32::PointerToken> &references);
void AttachSamples(
    EffectFixture *fixture,
    std::size_t elemIndex,
    std::uint8_t velIntervals,
    std::uint8_t visIntervals);
void AttachTrail(
    EffectFixture *fixture,
    std::size_t elemIndex,
    std::int32_t vertCount,
    std::int32_t indCount);
EffectFixture MakeAllVisualKindsEffect();

std::uint32_t AlignSize(
    const std::uint32_t size,
    const std::uint32_t alignment)
{
    CHECK(alignment != 0 && (alignment & (alignment - 1u)) == 0);
    CHECK(size <= (std::numeric_limits<std::uint32_t>::max)()
                      - (alignment - 1u));
    return (size + alignment - 1u) & ~(alignment - 1u);
}

std::uint32_t ComputeDisk32CompactBytes(const EffectFixture &fixture)
{
    std::uint32_t bytes = sizeof(fastfile::FxEffectDefDisk32);
    bytes = AlignSize(bytes, alignof(fastfile::FxElemDefDisk32));
    CHECK(fixture.elemCount
          <= (std::numeric_limits<std::uint32_t>::max)()
                 / sizeof(fastfile::FxElemDefDisk32));
    bytes += static_cast<std::uint32_t>(
        fixture.elemCount * sizeof(fastfile::FxElemDefDisk32));
    const fastfile::FxElemDefDisk32 *const elems = fixture.elems();
    for (std::size_t index = 0; index < fixture.elemCount; ++index)
    {
        const fastfile::FxElemDefDisk32 &elem = elems[index];
        if (!elem.velSamples.token.isNull())
        {
            bytes = AlignSize(
                bytes, alignof(fastfile::FxElemVelStateSampleDisk32));
            bytes += (static_cast<std::uint32_t>(elem.velIntervalCount) + 1u)
                * sizeof(fastfile::FxElemVelStateSampleDisk32);
        }
        if (!elem.visSamples.token.isNull())
        {
            bytes = AlignSize(
                bytes, alignof(fastfile::FxElemVisStateSampleDisk32));
            bytes +=
                (static_cast<std::uint32_t>(elem.visStateIntervalCount) + 1u)
                * sizeof(fastfile::FxElemVisStateSampleDisk32);
        }
        if (elem.elemType == fastfile::FxElemTypeDisk32::Decal
            && !elem.visuals.token.isNull())
        {
            bytes = AlignSize(
                bytes, alignof(fastfile::FxElemMarkVisualsDisk32));
            bytes += static_cast<std::uint32_t>(elem.visualCount)
                * sizeof(fastfile::FxElemMarkVisualsDisk32);
        }
        else if (elem.visualCount > 1u
                 && !elem.visuals.token.isNull())
        {
            bytes = AlignSize(bytes, alignof(fastfile::FxElemVisualsDisk32));
            bytes += static_cast<std::uint32_t>(elem.visualCount)
                * sizeof(fastfile::FxElemVisualsDisk32);
        }
        if (!elem.trailDef.token.isNull())
        {
            const auto *const trail = fixture.image.Resolve<
                fastfile::FxTrailDefDisk32>(elem.trailDef.token);
            CHECK(trail != nullptr);
            if (!trail || trail->indCount < 0)
                continue;
            bytes = AlignSize(bytes, alignof(fastfile::FxTrailDefDisk32));
            bytes += sizeof(fastfile::FxTrailDefDisk32);
            bytes = AlignSize(bytes, alignof(fastfile::FxTrailVertexDisk32));
            bytes += static_cast<std::uint32_t>(trail->indCount)
                * sizeof(fastfile::FxTrailVertexDisk32);
            bytes = AlignSize(bytes, alignof(std::uint16_t));
            bytes += static_cast<std::uint32_t>(trail->indCount)
                * sizeof(std::uint16_t);
        }
    }
    bytes = AlignSize(bytes, alignof(char));
    bytes += fixture.image.CStringBytes(fixture.effect()->name.token);
    return bytes;
}

void FinalizeEffectTotalSize(EffectFixture *const fixture)
{
    CHECK(fixture != nullptr && fixture->effect() != nullptr);
    if (!fixture || !fixture->effect())
        return;
    const std::uint32_t bytes = ComputeDisk32CompactBytes(*fixture);
    CHECK(bytes <= static_cast<std::uint32_t>(
        (std::numeric_limits<std::int32_t>::max)()));
    fixture->effect()->totalSize = static_cast<std::int32_t>(bytes);
}

struct ProvenanceState final
{
    DiskImage *image = nullptr;
    std::size_t calls = 0;
    fastfile::FxFastFileDisk32SourceSpanKind failKind =
        fastfile::FxFastFileDisk32SourceSpanKind::EffectHeader;
    bool failEnabled = false;
    const void *externalString = nullptr;
    std::uint32_t externalStringBytes = 0;
};

bool ValidateSourceSpan(
    void *const context,
    const fastfile::FxFastFileDisk32SourceSpanKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    const void *const address,
    const std::uint64_t byteCount,
    const std::size_t alignment) noexcept
{
    auto *const state = static_cast<ProvenanceState *>(context);
    if (!state || !state->image)
        return false;
    ++state->calls;
    if (state->failEnabled && state->failKind == kind)
        return false;
    if (sourceField && sourceField->value != token.value)
        return false;
    if (kind == fastfile::FxFastFileDisk32SourceSpanKind::String
        && address == state->externalString
        && byteCount == state->externalStringBytes
        && alignment == alignof(char))
    {
        return true;
    }

    // The top-level header has no owning Ptr32 field.  Locate it by address;
    // every other span must bind the exact serialized token slot.
    if (kind == fastfile::FxFastFileDisk32SourceSpanKind::EffectHeader)
    {
        for (std::size_t block = 0; block < kBlockCount; ++block)
        {
            const DiskBlock &candidate = state->image->block(block);
            const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(
                candidate.bytes.data());
            const std::uintptr_t pointer =
                reinterpret_cast<std::uintptr_t>(address);
            if (pointer < begin || pointer - begin > candidate.bytes.size())
                continue;
            return state->image->ValidateRange(
                DiskImage::Encode(
                    static_cast<std::uint32_t>(block),
                    static_cast<std::uint32_t>(pointer - begin)),
                address,
                byteCount,
                alignment);
        }
        return false;
    }
    return sourceField
        && state->image->ValidateRange(
            token, address, byteCount, alignment);
}

class EffectViewOwner final
{
public:
    explicit EffectViewOwner(EffectFixture *const fixture)
        : fixture_(fixture)
    {
        Rebuild();
    }

    void Rebuild()
    {
        CHECK(fixture_ != nullptr);
        if (!fixture_)
            return;
        elementViews_.assign(
            fixture_->elemCount,
            fastfile::FxFastFileElemDefDisk32View{});
        fastfile::FxElemDefDisk32 *const elements = fixture_->elems();
        for (std::size_t index = 0; index < elementViews_.size(); ++index)
        {
            fastfile::FxElemDefDisk32 &elem = elements[index];
            fastfile::FxFastFileElemDefDisk32View &elemView =
                elementViews_[index];
            if (!elem.velSamples.token.isNull())
            {
                elemView.velocitySamples = {
                    fixture_->image.Resolve<
                        fastfile::FxElemVelStateSampleDisk32>(
                            elem.velSamples.token),
                    static_cast<std::uint32_t>(elem.velIntervalCount) + 1u,
                };
            }
            if (!elem.visSamples.token.isNull())
            {
                elemView.visibilitySamples = {
                    fixture_->image.Resolve<
                        fastfile::FxElemVisStateSampleDisk32>(
                            elem.visSamples.token),
                    static_cast<std::uint32_t>(
                        elem.visStateIntervalCount) + 1u,
                };
            }
            if (elem.elemType == fastfile::FxElemTypeDisk32::Decal
                && !elem.visuals.token.isNull())
            {
                elemView.markVisuals = {
                    fixture_->image.Resolve<
                        fastfile::FxElemMarkVisualsDisk32>(
                            elem.visuals.token),
                    elem.visualCount,
                };
            }
            else if (elem.visualCount > 1u
                     && !elem.visuals.token.isNull())
            {
                elemView.visuals = {
                    fixture_->image.Resolve<fastfile::FxElemVisualsDisk32>(
                        elem.visuals.token),
                    elem.visualCount,
                };
            }
            if (!elem.trailDef.token.isNull())
            {
                elemView.trail =
                    fixture_->image.Resolve<fastfile::FxTrailDefDisk32>(
                        elem.trailDef.token);
                if (elemView.trail && elemView.trail->vertCount >= 0)
                {
                    elemView.trailVertices = {
                        fixture_->image.Resolve<
                            fastfile::FxTrailVertexDisk32>(
                                elemView.trail->verts.token),
                        static_cast<std::uint32_t>(
                            elemView.trail->vertCount),
                    };
                }
                if (elemView.trail && elemView.trail->indCount >= 0)
                {
                    elemView.trailIndices = {
                        fixture_->image.Resolve<std::uint16_t>(
                            elemView.trail->inds.token),
                        static_cast<std::uint32_t>(
                            elemView.trail->indCount),
                    };
                }
            }
        }

        provenance_.image = &fixture_->image;
        view_ = {
            fixture_->effect(),
            {fixture_->elems(), static_cast<std::uint32_t>(fixture_->elemCount)},
            {elementViews_.data(),
             static_cast<std::uint32_t>(elementViews_.size())},
            {&provenance_, ValidateSourceSpan},
        };
    }

    fastfile::FxFastFileEffectDefDisk32View &view()
    {
        return view_;
    }

    ProvenanceState &provenance()
    {
        return provenance_;
    }

    std::vector<fastfile::FxFastFileElemDefDisk32View> &elementViews()
    {
        return elementViews_;
    }

private:
    EffectFixture *fixture_ = nullptr;
    ProvenanceState provenance_{};
    std::vector<fastfile::FxFastFileElemDefDisk32View> elementViews_{};
    fastfile::FxFastFileEffectDefDisk32View view_{};
};

struct ResolverObservation final
{
    fastfile::FxFastFileDisk32ReferenceKind kind =
        fastfile::FxFastFileDisk32ReferenceKind::EffectName;
    const disk32::PointerToken *sourceField = nullptr;
    std::uint32_t token = 0;
    const void *result = nullptr;
};

struct ResolverState final
{
    DiskImage *image = nullptr;
    std::array<ResolverObservation, 4096> observations{};
    std::size_t calls = 0;
    std::size_t failAt = (std::numeric_limits<std::size_t>::max)();
    std::size_t nullResultAt = (std::numeric_limits<std::size_t>::max)();
    std::size_t zeroStringBytesAt =
        (std::numeric_limits<std::size_t>::max)();
    std::size_t assetStringBytesAt =
        (std::numeric_limits<std::size_t>::max)();
    std::size_t assetResultAt =
        (std::numeric_limits<std::size_t>::max)();
    const void *assetResult = nullptr;
    const char *effectNameResult = nullptr;
    std::uint32_t effectNameResultBytes = 0;
    bool reenter = false;
    bool reentered = false;
    bool mutateSourceDuringResolve = false;
    bool sourceMutated = false;
    fastfile::FxFastFileNativeDisk32Workspace *workspace = nullptr;
    const fastfile::FxFastFileEffectDefDisk32View *source = nullptr;
    const fastfile::FxFastFileDisk32Resolvers *resolvers = nullptr;
    fastfile::FxFastFileNativeDisk32Status nestedStatus =
        fastfile::FxFastFileNativeDisk32Status::Success;
    fastfile::FxFastFileNativeDisk32Plan nestedPlan{};
};

const void *HighIdentity(
    const fastfile::FxFastFileDisk32ReferenceKind kind,
    const std::size_t index) noexcept
{
    std::uintptr_t identity = UINT32_C(0x01000000)
        + static_cast<std::uintptr_t>(kind) * UINT32_C(0x00100000)
        + index * UINT32_C(0x100);
    if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t))
        identity += UINT64_C(0x100000000);
    return reinterpret_cast<const void *>(identity);
}

const void *FindResolved(
    const ResolverState &state,
    const disk32::PointerToken *const sourceField,
    const fastfile::FxFastFileDisk32ReferenceKind kind)
{
    for (std::size_t index = 0;
         index < state.calls && index < state.observations.size();
         ++index)
    {
        const ResolverObservation &observation = state.observations[index];
        if (observation.sourceField == sourceField
            && observation.kind == kind)
        {
            return observation.result;
        }
    }
    return nullptr;
}

bool ResolveReference(
    void *const context,
    const fastfile::FxFastFileDisk32ReferenceKind kind,
    const disk32::PointerToken *const sourceField,
    const disk32::PointerToken token,
    fastfile::FxFastFileDisk32ResolvedReference *const outReference) noexcept
{
    auto *const state = static_cast<ResolverState *>(context);
    if (!state || !state->image || !sourceField || !outReference
        || sourceField->value != token.value)
    {
        return false;
    }

    const std::size_t callIndex = state->calls++;
    if (state->mutateSourceDuringResolve && !state->sourceMutated
        && state->source && state->source->elements.data
        && state->source->elements.count != 0)
    {
        state->sourceMutated = true;
        auto *const elements = const_cast<fastfile::FxElemDefDisk32 *>(
            state->source->elements.data);
        elements[0].flags ^= 1u;
    }
    if (state->reenter && !state->reentered)
    {
        state->reentered = true;
        if (!state->workspace || !state->source || !state->resolvers)
            return false;
        state->nestedStatus = fastfile::TryPlanFxEffectDefDisk32(
            state->workspace,
            *state->source,
            *state->resolvers,
            &state->nestedPlan);
    }
    if (callIndex == state->failAt)
        return false;

    fastfile::FxFastFileDisk32ResolvedReference resolved{};
    if (kind == fastfile::FxFastFileDisk32ReferenceKind::EffectName
        || kind == fastfile::FxFastFileDisk32ReferenceKind::SoundName)
    {
        if (kind == fastfile::FxFastFileDisk32ReferenceKind::EffectName
            && state->effectNameResult)
        {
            resolved.pointer = state->effectNameResult;
            resolved.stringByteCount = state->effectNameResultBytes;
        }
        else
        {
            resolved.pointer = state->image->Resolve<char>(token);
            resolved.stringByteCount = state->image->CStringBytes(token);
        }
        if (!resolved.pointer || !resolved.stringByteCount)
            return false;
    }
    else
    {
        resolved.pointer = HighIdentity(kind, callIndex);
    }
    if (callIndex == state->assetResultAt)
        resolved.pointer = state->assetResult;
    if (callIndex == state->nullResultAt)
        resolved.pointer = nullptr;
    if (callIndex == state->zeroStringBytesAt)
        resolved.stringByteCount = 0;
    if (callIndex == state->assetStringBytesAt)
        resolved.stringByteCount = 4;

    if (callIndex < state->observations.size())
    {
        state->observations[callIndex] = {
            kind,
            sourceField,
            token.value,
            resolved.pointer,
        };
    }
    *outReference = resolved;
    return true;
}

struct ResolverOwner final
{
    explicit ResolverOwner(DiskImage *const image)
        : state{image}, callbacks{&state, ResolveReference}
    {
        state.resolvers = &callbacks;
    }

    ResolverState state{};
    fastfile::FxFastFileDisk32Resolvers callbacks{};
};

class WorkspaceOwner final
{
public:
    WorkspaceOwner()
        : workspace_(
              std::make_unique<
                  fastfile::FxFastFileNativeDisk32Workspace>())
    {
    }

    fastfile::FxFastFileNativeDisk32Workspace *get() const noexcept
    {
        return workspace_.get();
    }

private:
    std::unique_ptr<fastfile::FxFastFileNativeDisk32Workspace> workspace_;
};

class OutputStorage final
{
public:
    OutputStorage(
        const std::size_t bytes,
        const std::size_t alignment,
        const std::uint8_t fill = kOutputGuard)
        : bytes_(bytes + alignment + 1u, fill)
    {
        CHECK(alignment != 0 && (alignment & (alignment - 1u)) == 0);
        const std::uintptr_t begin =
            reinterpret_cast<std::uintptr_t>(bytes_.data());
        const std::uintptr_t aligned =
            (begin + alignment - 1u) & ~(alignment - 1u);
        storage_ = reinterpret_cast<std::uint8_t *>(aligned);
        capacity_ = bytes;
    }

    void *data() noexcept
    {
        return storage_;
    }

    const void *data() const noexcept
    {
        return storage_;
    }

    std::uint8_t *misalignedData() noexcept
    {
        return storage_ + 1u;
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    bool IsFilled(const std::uint8_t value = kOutputGuard) const noexcept
    {
        for (const std::uint8_t byte : bytes_)
        {
            if (byte != value)
                return false;
        }
        return true;
    }

    bool TailGuardIsIntact() const noexcept
    {
        return storage_[capacity_] == kOutputGuard;
    }

    bool Contains(
        const void *const pointer,
        const std::size_t bytes = 1) const noexcept
    {
        if (!pointer)
            return false;
        const std::uintptr_t begin =
            reinterpret_cast<std::uintptr_t>(storage_);
        const std::uintptr_t value =
            reinterpret_cast<std::uintptr_t>(pointer);
        return value >= begin && value - begin <= capacity_
            && bytes <= capacity_ - (value - begin);
    }

private:
    std::vector<std::uint8_t> bytes_{};
    std::uint8_t *storage_ = nullptr;
    std::size_t capacity_ = 0;
};

template <typename POINTER>
POINTER LoadNativePointer(
    const FxElemDef &elem,
    const std::size_t offset) noexcept
{
    static_assert(std::is_pointer_v<POINTER>);
    POINTER result = nullptr;
    CHECK(offset <= sizeof(elem.bytes));
    CHECK(sizeof(result) <= sizeof(elem.bytes) - offset);
    if (offset <= sizeof(elem.bytes)
        && sizeof(result) <= sizeof(elem.bytes) - offset)
    {
        std::memcpy(&result, elem.bytes + offset, sizeof(result));
    }
    return result;
}

constexpr std::size_t kNativeElemTypeOffset = 0xB0;
constexpr std::size_t kNativeVisualCountOffset = 0xB1;
constexpr std::size_t kNativeVelocitySamplesOffset =
    KISAK_ARCH_64BIT ? 0xB8 : 0xB4;
constexpr std::size_t kNativeVisibilitySamplesOffset =
    KISAK_ARCH_64BIT ? 0xC0 : 0xB8;
constexpr std::size_t kNativeVisualsOffset =
    KISAK_ARCH_64BIT ? 0xC8 : 0xBC;
constexpr std::size_t kNativeEffectOnImpactOffset =
    KISAK_ARCH_64BIT ? 0xE8 : 0xD8;
constexpr std::size_t kNativeEffectOnDeathOffset =
    KISAK_ARCH_64BIT ? 0xF0 : 0xDC;
constexpr std::size_t kNativeEffectEmittedOffset =
    KISAK_ARCH_64BIT ? 0xF8 : 0xE0;
constexpr std::size_t kNativeTrailOffset =
    KISAK_ARCH_64BIT ? 0x110 : 0xF4;

struct NativeTrailVertex final
{
    float pos[2];
    float normal[2];
    float texCoord;
};
static_assert(sizeof(NativeTrailVertex) == 0x14);

struct NativeTrailDef final
{
    std::int32_t scrollTimeMsec;
    std::int32_t repeatDist;
    std::int32_t splitDist;
    std::int32_t vertCount;
    NativeTrailVertex *verts;
    std::int32_t indCount;
    std::uint16_t *inds;
};
static_assert(sizeof(NativeTrailDef) == (KISAK_ARCH_64BIT ? 0x28 : 0x1C));

fastfile::FxFastFileNativeDisk32Status PlanEffect(
    WorkspaceOwner *const workspace,
    EffectViewOwner *const view,
    ResolverOwner *const resolver,
    fastfile::FxFastFileNativeDisk32Plan *const outPlan)
{
    CHECK(workspace != nullptr);
    CHECK(view != nullptr);
    CHECK(resolver != nullptr);
    if (!workspace || !view || !resolver)
        return fastfile::FxFastFileNativeDisk32Status::InvalidArgument;
    resolver->state.workspace = workspace->get();
    resolver->state.source = &view->view();
    return fastfile::TryPlanFxEffectDefDisk32(
        workspace->get(),
        view->view(),
        resolver->callbacks,
        outPlan);
}

bool PlansEqual(
    const fastfile::FxFastFileNativeDisk32Plan &left,
    const fastfile::FxFastFileNativeDisk32Plan &right) noexcept
{
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

void CheckNativeVisualReferences(
    const EffectFixture &fixture,
    const FxEffectDef &effect,
    const ResolverState &resolver,
    const OutputStorage &storage)
{
    CHECK(effect.elemDefs != nullptr);
    if (!effect.elemDefs)
        return;

    for (std::size_t elemIndex = 0;
         elemIndex < fixture.elemCount;
         ++elemIndex)
    {
        const fastfile::FxElemDefDisk32 &source = fixture.elems()[elemIndex];
        const FxElemDef &output = effect.elemDefs[elemIndex];
        CHECK(output.bytes[kNativeElemTypeOffset]
              == static_cast<std::uint8_t>(source.elemType));
        CHECK(output.bytes[kNativeVisualCountOffset] == source.visualCount);

        if (source.elemType == fastfile::FxElemTypeDisk32::OmniLight
            || source.elemType == fastfile::FxElemTypeDisk32::SpotLight)
        {
            CHECK(LoadNativePointer<const void *>(
                      output, kNativeVisualsOffset)
                  == nullptr);
            continue;
        }

        if (source.elemType == fastfile::FxElemTypeDisk32::Decal)
        {
            const void *const rawArray = LoadNativePointer<const void *>(
                output, kNativeVisualsOffset);
            CHECK(storage.Contains(
                rawArray,
                static_cast<std::size_t>(source.visualCount)
                    * 2u * sizeof(void *)));
            if (!rawArray)
                continue;
            const auto *const pointers =
                static_cast<const void *const *>(rawArray);
            const auto *const sourceVisuals = fixture.image.Resolve<
                fastfile::FxElemMarkVisualsDisk32>(source.visuals.token);
            for (std::size_t visual = 0;
                 visual < source.visualCount;
                 ++visual)
            {
                for (std::size_t material = 0; material < 2; ++material)
                {
                    const disk32::PointerToken *const field =
                        &sourceVisuals[visual].materials[material].token;
                    CHECK(pointers[visual * 2u + material]
                          == FindResolved(
                              resolver,
                              field,
                              fastfile::FxFastFileDisk32ReferenceKind::Material));
                }
            }
            continue;
        }

        const auto kind =
            source.elemType == fastfile::FxElemTypeDisk32::Model
            ? fastfile::FxFastFileDisk32ReferenceKind::Model
            : source.elemType == fastfile::FxElemTypeDisk32::Sound
            ? fastfile::FxFastFileDisk32ReferenceKind::SoundName
            : source.elemType == fastfile::FxElemTypeDisk32::Runner
            ? fastfile::FxFastFileDisk32ReferenceKind::EffectNameReference
            : fastfile::FxFastFileDisk32ReferenceKind::Material;
        if (source.visualCount == 1u)
        {
            CHECK(LoadNativePointer<const void *>(
                      output, kNativeVisualsOffset)
                  == FindResolved(resolver, &source.visuals.token, kind));
            continue;
        }

        const void *const rawArray = LoadNativePointer<const void *>(
            output, kNativeVisualsOffset);
        CHECK(storage.Contains(
            rawArray,
            static_cast<std::size_t>(source.visualCount) * sizeof(void *)));
        if (!rawArray)
            continue;
        const auto *const pointers = static_cast<const void *const *>(rawArray);
        const auto *const sourceVisuals = fixture.image.Resolve<
            fastfile::FxElemVisualsDisk32>(source.visuals.token);
        for (std::size_t visual = 0;
             visual < source.visualCount;
             ++visual)
        {
            CHECK(pointers[visual]
                  == FindResolved(
                      resolver,
                      &sourceVisuals[visual].token,
                      kind));
        }
    }
}

void CheckValidEffect(
    EffectFixture *const fixture,
    const std::size_t expectedResolverCalls)
{
    CHECK(fixture != nullptr);
    if (!fixture)
        return;
    FinalizeEffectTotalSize(fixture);
    const std::uint32_t disk32CompactBytes =
        ComputeDisk32CompactBytes(*fixture);
    CHECK(fixture->effect()->totalSize
          == static_cast<std::int32_t>(disk32CompactBytes));
    EffectViewOwner view(fixture);
    ResolverOwner resolver(&fixture->image);
    WorkspaceOwner workspace;
    fastfile::FxFastFileNativeDisk32Plan plan{};
    const auto planStatus = PlanEffect(
        &workspace, &view, &resolver, &plan);
    CHECK(planStatus == fastfile::FxFastFileNativeDisk32Status::Success);
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Planned);
    CHECK(static_cast<bool>(plan));
    CHECK(plan.outputBytes() >= sizeof(FxEffectDef));
    CHECK(plan.outputAlignment() == alignof(FxEffectDef));
    CHECK(plan.elementCount() == fixture->elemCount);
    CHECK(plan.resolvedReferenceCount() == resolver.state.calls);
    CHECK(resolver.state.calls == expectedResolverCalls);
    if constexpr (KISAK_ARCH_64BIT)
        CHECK(plan.outputBytes() != disk32CompactBytes);
    if (planStatus != fastfile::FxFastFileNativeDisk32Status::Success)
        return;

    OutputStorage storage(plan.outputBytes(), plan.outputAlignment());
    FxEffectDef *const pointerSentinel = reinterpret_cast<FxEffectDef *>(
        static_cast<std::uintptr_t>(1));
    FxEffectDef *output = pointerSentinel;
    const std::size_t resolverCallsBeforeMaterialize = resolver.state.calls;
    const auto materializeStatus =
        fastfile::TryMaterializeFxEffectDefDisk32(
            workspace.get(),
            plan,
            storage.data(),
            storage.capacity(),
            &output);
    CHECK(materializeStatus
          == fastfile::FxFastFileNativeDisk32Status::Success);
    CHECK(output == storage.data());
    CHECK(output != pointerSentinel);
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Empty);
    CHECK(resolver.state.calls == resolverCallsBeforeMaterialize);
    CHECK(storage.TailGuardIsIntact());
    if (materializeStatus
            != fastfile::FxFastFileNativeDisk32Status::Success
        || !output)
    {
        return;
    }

    CHECK(storage.Contains(output, sizeof(*output)));
    CHECK(output->name != nullptr);
    CHECK(storage.Contains(
        output->name,
        fixture->image.CStringBytes(fixture->effect()->name.token)));
    CHECK(output->name
          != fixture->image.Resolve<char>(fixture->effect()->name.token));
    CHECK(std::strcmp(output->name, "fx/fastfile_native_disk32_test") == 0);
    CHECK(output->flags == fixture->effect()->flags);
    CHECK(output->totalSize == static_cast<std::int32_t>(plan.outputBytes()));
    CHECK(output->msecLoopingLife == fixture->effect()->msecLoopingLife);
    CHECK(output->elemDefCountLooping
          == fixture->effect()->elemDefCountLooping);
    CHECK(output->elemDefCountOneShot
          == fixture->effect()->elemDefCountOneShot);
    CHECK(output->elemDefCountEmission
          == fixture->effect()->elemDefCountEmission);
    if (fixture->elemCount == 0)
    {
        CHECK(output->elemDefs == nullptr);
        return;
    }

    CHECK(storage.Contains(
        output->elemDefs,
        fixture->elemCount * sizeof(FxElemDef)));
    CheckNativeVisualReferences(*fixture, *output, resolver.state, storage);
    for (std::size_t index = 0; index < fixture->elemCount; ++index)
    {
        const fastfile::FxElemDefDisk32 &source = fixture->elems()[index];
        const FxElemDef &native = output->elemDefs[index];
        if (!source.velSamples.token.isNull())
        {
            CHECK(storage.Contains(LoadNativePointer<const void *>(
                native, kNativeVelocitySamplesOffset)));
        }
        if (!source.visSamples.token.isNull())
        {
            CHECK(storage.Contains(LoadNativePointer<const void *>(
                native, kNativeVisibilitySamplesOffset)));
        }
        const auto checkEffectReference = [&resolver, &native](
                                              const std::size_t offset,
                                              const disk32::PointerToken *const
                                                  sourceField) {
            const void *const expected = sourceField->isNull()
                ? nullptr
                : FindResolved(
                      resolver.state,
                      sourceField,
                      fastfile::FxFastFileDisk32ReferenceKind::
                          EffectNameReference);
            CHECK(LoadNativePointer<const void *>(native, offset)
                  == expected);
        };
        checkEffectReference(
            kNativeEffectOnImpactOffset, &source.effectOnImpact.token);
        checkEffectReference(
            kNativeEffectOnDeathOffset, &source.effectOnDeath.token);
        checkEffectReference(
            kNativeEffectEmittedOffset, &source.effectEmitted.token);
        if (!source.trailDef.token.isNull())
        {
            const auto *const trail =
                LoadNativePointer<const NativeTrailDef *>(
                    native, kNativeTrailOffset);
            const auto *const sourceTrail = fixture->image.Resolve<
                fastfile::FxTrailDefDisk32>(source.trailDef.token);
            CHECK(storage.Contains(trail, sizeof(*trail)));
            if (trail && sourceTrail)
            {
                CHECK(trail->vertCount == sourceTrail->vertCount);
                CHECK(trail->indCount == sourceTrail->indCount);
                CHECK(storage.Contains(
                    trail->verts,
                    static_cast<std::size_t>(sourceTrail->indCount)
                        * sizeof(*trail->verts)));
                CHECK(storage.Contains(
                    trail->inds,
                    static_cast<std::size_t>(sourceTrail->indCount)
                        * sizeof(*trail->inds)));
                for (std::int32_t tail = sourceTrail->vertCount;
                     tail < sourceTrail->indCount;
                     ++tail)
                {
                    const NativeTrailVertex zero{};
                    CHECK(std::memcmp(
                              &trail->verts[tail],
                              &zero,
                              sizeof(zero))
                          == 0);
                }
            }
        }
    }
}

void CheckPlanFailure(
    EffectFixture *const fixture,
    const fastfile::FxFastFileNativeDisk32Status expected)
{
    CHECK(fixture != nullptr);
    if (!fixture)
        return;
    FinalizeEffectTotalSize(fixture);
    EffectViewOwner view(fixture);
    ResolverOwner resolver(&fixture->image);
    WorkspaceOwner workspace;
    fastfile::FxFastFileNativeDisk32Plan plan{};
    CHECK(PlanEffect(&workspace, &view, &resolver, &plan) == expected);
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Empty);
    CHECK(!static_cast<bool>(plan));
}

EffectFixture MakeTrailEffect(
    const std::int32_t vertCount = 3,
    const std::int32_t indCount = 4)
{
    EffectFixture fixture = MakeEffect(
        {MinimalElem(fastfile::FxElemTypeDisk32::Trail)}, 0, 1, 0);
    AttachSamples(&fixture, 0, 1, 0);
    AttachVisuals(
        &fixture,
        0,
        fastfile::FxElemTypeDisk32::Trail,
        {AddOpaqueReference(&fixture, UINT32_C(0xC101))});
    AttachTrail(&fixture, 0, vertCount, indCount);
    return fixture;
}

void TestValidDefinitions()
{
    EffectFixture empty = MakeEmptyEffect();
    CheckValidEffect(&empty, 1);

    EffectFixture minimal = MakeMinimalEffect();
    CheckValidEffect(&minimal, 2);

    EffectFixture allVisuals = MakeAllVisualKindsEffect();
    CheckValidEffect(&allVisuals, 24);

    // The compact native blob reserves indCount vertices and copies only the
    // serialized vertCount prefix.  A zero-filled capacity tail is required.
    EffectFixture trailCapacity = MakeEffect(
        {MinimalElem(fastfile::FxElemTypeDisk32::Trail)}, 0, 1, 0);
    AttachSamples(&trailCapacity, 0, 1, 0);
    AttachVisuals(
        &trailCapacity,
        0,
        fastfile::FxElemTypeDisk32::Trail,
        {AddOpaqueReference(&trailCapacity, UINT32_C(0xC001))});
    AttachTrail(&trailCapacity, 0, 2, 8);
    CheckValidEffect(&trailCapacity, 2);
}

void TestArgumentsCountsAndOutputPlanPreservation()
{
    EffectFixture fixture = MakeMinimalEffect();
    FinalizeEffectTotalSize(&fixture);
    EffectViewOwner view(&fixture);
    ResolverOwner resolver(&fixture.image);
    WorkspaceOwner validWorkspace;
    fastfile::FxFastFileNativeDisk32Plan preserved{};
    CHECK(PlanEffect(
              &validWorkspace, &view, &resolver, &preserved)
          == fastfile::FxFastFileNativeDisk32Status::Success);
    const fastfile::FxFastFileNativeDisk32Plan preservedSnapshot = preserved;

    WorkspaceOwner workspace;
    CHECK(fastfile::TryPlanFxEffectDefDisk32(
              nullptr, view.view(), resolver.callbacks, &preserved)
          == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
    CHECK(PlansEqual(preserved, preservedSnapshot));
    CHECK(fastfile::TryPlanFxEffectDefDisk32(
              workspace.get(), view.view(), resolver.callbacks, nullptr)
          == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
    CHECK(PlansEqual(preserved, preservedSnapshot));

    auto invalidView = view.view();
    invalidView.effect = nullptr;
    CHECK(fastfile::TryPlanFxEffectDefDisk32(
              workspace.get(), invalidView, resolver.callbacks, &preserved)
          == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
    CHECK(PlansEqual(preserved, preservedSnapshot));

    auto missingProvenance = view.view();
    missingProvenance.provenance.validateSpan = nullptr;
    CHECK(fastfile::TryPlanFxEffectDefDisk32(
              workspace.get(),
              missingProvenance,
              resolver.callbacks,
              &preserved)
          == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
    CHECK(PlansEqual(preserved, preservedSnapshot));

    fastfile::FxFastFileDisk32Resolvers missingResolver{};
    CHECK(fastfile::TryPlanFxEffectDefDisk32(
              workspace.get(), view.view(), missingResolver, &preserved)
          == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
    CHECK(PlansEqual(preserved, preservedSnapshot));

    for (const std::int32_t invalidCount : {
             -1,
             static_cast<std::int32_t>(
                 fastfile::kFxFastFileDisk32MaxEffectElements + 1u)})
    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        invalid.effect()->elemDefCountLooping = invalidCount;
        EffectViewOwner invalidOwner(&invalid);
        ResolverOwner invalidResolver(&invalid.image);
        WorkspaceOwner invalidWorkspace;
        fastfile::FxFastFileNativeDisk32Plan out = preservedSnapshot;
        CHECK(PlanEffect(
                  &invalidWorkspace,
                  &invalidOwner,
                  &invalidResolver,
                  &out)
              == fastfile::FxFastFileNativeDisk32Status::InvalidCount);
        CHECK(PlansEqual(out, preservedSnapshot));
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        invalid.effect()->elemDefCountLooping =
            static_cast<std::int32_t>(
                fastfile::kFxFastFileDisk32MaxEffectElements);
        invalid.effect()->elemDefCountOneShot = 1;
        EffectViewOwner invalidOwner(&invalid);
        ResolverOwner invalidResolver(&invalid.image);
        WorkspaceOwner invalidWorkspace;
        fastfile::FxFastFileNativeDisk32Plan out = preservedSnapshot;
        CHECK(PlanEffect(
                  &invalidWorkspace,
                  &invalidOwner,
                  &invalidResolver,
                  &out)
              == fastfile::FxFastFileNativeDisk32Status::InvalidCount);
        CHECK(PlansEqual(out, preservedSnapshot));
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner invalidOwner(&invalid);
        invalidOwner.view().elements.count = 0;
        ResolverOwner invalidResolver(&invalid.image);
        WorkspaceOwner invalidWorkspace;
        fastfile::FxFastFileNativeDisk32Plan out = preservedSnapshot;
        CHECK(PlanEffect(
                  &invalidWorkspace,
                  &invalidOwner,
                  &invalidResolver,
                  &out)
              == fastfile::FxFastFileNativeDisk32Status::InvalidCount);
        CHECK(PlansEqual(out, preservedSnapshot));
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner invalidOwner(&invalid);
        invalidOwner.view().elementViews.count = 0;
        ResolverOwner invalidResolver(&invalid.image);
        WorkspaceOwner invalidWorkspace;
        fastfile::FxFastFileNativeDisk32Plan out = preservedSnapshot;
        CHECK(PlanEffect(
                  &invalidWorkspace,
                  &invalidOwner,
                  &invalidResolver,
                  &out)
              == fastfile::FxFastFileNativeDisk32Status::InvalidCount);
        CHECK(PlansEqual(out, preservedSnapshot));
    }
}

void TestPointerSpanAndProvenanceFailures()
{
    {
        EffectFixture invalid = MakeEffect({MinimalElem()}, 0, 1, 0);
        AttachVisuals(
            &invalid,
            0,
            fastfile::FxElemTypeDisk32::SpriteBillboard,
            {AddOpaqueReference(&invalid, 0xB101u)});
        CheckPlanFailure(
            &invalid,
            fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    {
        EffectFixture invalid = MakeEffect({MinimalElem()}, 0, 1, 0);
        AttachSamples(&invalid, 0, 0, 0);
        AttachVisuals(
            &invalid,
            0,
            fastfile::FxElemTypeDisk32::SpriteBillboard,
            {AddOpaqueReference(&invalid, 0xB102u)});
        CheckPlanFailure(
            &invalid,
            fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        invalid.effect()->elemDefs.token = {};
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        view.view().elements.data = nullptr;
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidSourceLayout);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        view.elementViews()[0].velocitySamples.count = 1;
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        invalid.elems()[0].velSamples.token = {};
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        invalid.elems()[0].visStateIntervalCount = 1;
        invalid.elems()[0].visSamples.token = {};
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    for (const auto failKind : {
             fastfile::FxFastFileDisk32SourceSpanKind::EffectHeader,
             fastfile::FxFastFileDisk32SourceSpanKind::ElementDefinitions,
             fastfile::FxFastFileDisk32SourceSpanKind::VelocitySamples,
             fastfile::FxFastFileDisk32SourceSpanKind::String})
    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        view.provenance().failEnabled = true;
        view.provenance().failKind = failKind;
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidProvenance);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        invalid.image.RemoveMaterializedInterval(
            invalid.elems()[0].velSamples.token);
        EffectViewOwner view(&invalid);
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidProvenance);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        invalid.image.SetIntervalProvenance(invalid.elemsToken, 9);
        EffectViewOwner view(&invalid);
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidProvenance);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        ++invalid.effect()->totalSize;
        EffectViewOwner view(&invalid);
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidSourceLayout);
    }
}

void TestVisualValidation()
{
    {
        EffectFixture invalid = MakeMinimalEffect();
        invalid.elems()[0].elemType = fastfile::FxElemTypeDisk32::Count;
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidVisual);
    }

    {
        EffectFixture invalid = MakeEffect({MinimalElem()}, 0, 1, 0);
        AttachSamples(&invalid, 0, 1, 0);
        std::vector<disk32::PointerToken> references;
        for (std::uint32_t index = 0;
             index < fastfile::kFxFastFileDisk32MaxVisuals + 1u;
             ++index)
        {
            references.push_back(AddOpaqueReference(&invalid, 0xD100u + index));
        }
        AttachVisuals(
            &invalid,
            0,
            fastfile::FxElemTypeDisk32::SpriteBillboard,
            references);
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidVisual);
    }

    {
        EffectFixture invalid = MakeEffect(
            {MinimalElem(fastfile::FxElemTypeDisk32::Decal)}, 0, 1, 0);
        AttachSamples(&invalid, 0, 1, 0);
        std::vector<disk32::PointerToken> references;
        for (std::uint32_t index = 0;
             index < (fastfile::kFxFastFileDisk32MaxDecalVisuals + 1u) * 2u;
             ++index)
        {
            references.push_back(AddOpaqueReference(&invalid, 0xD200u + index));
        }
        AttachVisuals(
            &invalid, 0, fastfile::FxElemTypeDisk32::Decal, references);
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidVisual);
    }

    for (const auto light : {
             fastfile::FxElemTypeDisk32::OmniLight,
             fastfile::FxElemTypeDisk32::SpotLight})
    {
        EffectFixture zero = MakeEffect({MinimalElem(light)}, 0, 1, 0);
        AttachSamples(&zero, 0, 1, 0);
        CheckPlanFailure(
            &zero, fastfile::FxFastFileNativeDisk32Status::InvalidVisual);

        EffectFixture referenced = MakeMinimalEffect();
        referenced.elems()[0].elemType = light;
        CheckPlanFailure(
            &referenced,
            fastfile::FxFastFileNativeDisk32Status::InvalidVisual);
    }

    {
        EffectFixture invalid = MakeEffect(
            {MinimalElem(fastfile::FxElemTypeDisk32::Runner)}, 0, 1, 0);
        AttachSamples(&invalid, 0, 1, 0);
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidVisual);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        invalid.elems()[0].visualCount = 0;
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidVisual);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        view.elementViews()[0].visuals = {
            reinterpret_cast<const fastfile::FxElemVisualsDisk32 *>(
                invalid.elems()),
            1,
        };
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidVisual);
    }

    {
        EffectFixture invalid = MakeEffect({MinimalElem()}, 0, 1, 0);
        AttachSamples(&invalid, 0, 1, 0);
        AttachVisuals(
            &invalid,
            0,
            fastfile::FxElemTypeDisk32::SpriteBillboard,
            {AddOpaqueReference(&invalid, 0xD301u),
             AddOpaqueReference(&invalid, 0xD302u)});
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        --view.elementViews()[0].visuals.count;
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    {
        EffectFixture invalid = MakeEffect(
            {MinimalElem(fastfile::FxElemTypeDisk32::Decal)}, 0, 1, 0);
        AttachSamples(&invalid, 0, 1, 0);
        AttachVisuals(
            &invalid,
            0,
            fastfile::FxElemTypeDisk32::Decal,
            {AddOpaqueReference(&invalid, 0xD401u),
             AddOpaqueReference(&invalid, 0xD402u)});
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        view.elementViews()[0].markVisuals.count = 0;
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    {
        EffectFixture invalid = MakeEffect({MinimalElem()}, 0, 1, 0);
        AttachSamples(&invalid, 0, 1, 0);
        AttachVisuals(
            &invalid,
            0,
            fastfile::FxElemTypeDisk32::SpriteBillboard,
            {disk32::PointerToken{}, AddOpaqueReference(&invalid, 0xD501u)});
        CheckPlanFailure(
            &invalid,
            fastfile::FxFastFileNativeDisk32Status::InvalidPointerCount);
    }

    for (const std::uint32_t sentinel : {
             disk32::kInline,
             disk32::kSharedInline})
    {
        EffectFixture valid = MakeMinimalEffect();
        valid.elems()[0].visuals.token = {sentinel};
        CheckValidEffect(&valid, 2);
    }

    {
        EffectFixture valid = MakeMinimalEffect();
        valid.effect()->elemDefs.token = {disk32::kInline};
        CheckValidEffect(&valid, 2);
    }
}

void TestTrailValidation()
{
    {
        EffectFixture invalid = MakeEffect(
            {MinimalElem(fastfile::FxElemTypeDisk32::Trail)}, 0, 1, 0);
        AttachSamples(&invalid, 0, 1, 0);
        AttachVisuals(
            &invalid,
            0,
            fastfile::FxElemTypeDisk32::Trail,
            {AddOpaqueReference(&invalid, 0xE001u)});
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    {
        EffectFixture invalid = MakeMinimalEffect();
        AttachTrail(&invalid, 0, 3, 4);
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    for (const auto mutation : {
             0,
             1,
             2,
             3,
             4,
             5,
             6,
             7})
    {
        EffectFixture invalid = MakeTrailEffect();
        auto *const trail = invalid.image.Resolve<fastfile::FxTrailDefDisk32>(
            invalid.elems()[0].trailDef.token);
        CHECK(trail != nullptr);
        if (!trail)
            continue;
        switch (mutation)
        {
        case 0:
            trail->repeatDist = 0;
            break;
        case 1:
            trail->splitDist = 0;
            break;
        case 2:
            trail->vertCount = 0;
            break;
        case 3:
            trail->vertCount = -1;
            break;
        case 4:
            trail->indCount = 0;
            break;
        case 5:
            trail->indCount = -1;
            break;
        case 6:
            trail->indCount = 3;
            break;
        case 7:
            trail->vertCount = 5;
            break;
        }
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    {
        EffectFixture invalid = MakeTrailEffect(65, 66);
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    {
        EffectFixture invalid = MakeTrailEffect(64, 130);
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    // The native compact representation reserves indCount vertices.  The
    // serialized prefix must still fit that capacity.
    {
        EffectFixture invalid = MakeTrailEffect(4, 2);
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    {
        EffectFixture invalid = MakeTrailEffect();
        auto *const trail = invalid.image.Resolve<fastfile::FxTrailDefDisk32>(
            invalid.elems()[0].trailDef.token);
        auto *const indices = invalid.image.Resolve<std::uint16_t>(
            trail->inds.token);
        CHECK(indices != nullptr);
        if (indices)
            indices[0] = static_cast<std::uint16_t>(trail->vertCount);
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    {
        EffectFixture invalid = MakeTrailEffect();
        auto *const trail = invalid.image.Resolve<fastfile::FxTrailDefDisk32>(
            invalid.elems()[0].trailDef.token);
        trail->verts.token = {};
        CheckPlanFailure(
            &invalid, fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    {
        EffectFixture invalid = MakeTrailEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        view.elementViews()[0].trail = nullptr;
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    {
        EffectFixture invalid = MakeTrailEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        --view.elementViews()[0].trailVertices.count;
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidTrail);
    }

    {
        EffectFixture invalid = MakeTrailEffect();
        FinalizeEffectTotalSize(&invalid);
        EffectViewOwner view(&invalid);
        view.provenance().failEnabled = true;
        view.provenance().failKind =
            fastfile::FxFastFileDisk32SourceSpanKind::TrailIndices;
        ResolverOwner resolver(&invalid.image);
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidProvenance);
    }

    EffectFixture maximum = MakeTrailEffect(64, 128);
    CheckValidEffect(&maximum, 2);
}

void TestResolverFailuresAndReentry()
{
    for (const auto mode : {0, 1, 2, 3})
    {
        EffectFixture fixture = MakeMinimalEffect();
        FinalizeEffectTotalSize(&fixture);
        EffectViewOwner view(&fixture);
        ResolverOwner resolver(&fixture.image);
        WorkspaceOwner workspace;
        switch (mode)
        {
        case 0:
            resolver.state.failAt = 1;
            break;
        case 1:
            resolver.state.nullResultAt = 0;
            break;
        case 2:
            resolver.state.zeroStringBytesAt = 0;
            break;
        case 3:
            resolver.state.assetStringBytesAt = 1;
            break;
        }

        fastfile::FxFastFileNativeDisk32Plan plan{};
        const auto expected = mode < 2
            ? fastfile::FxFastFileNativeDisk32Status::UnresolvedReference
            : fastfile::FxFastFileNativeDisk32Status::InvalidString;
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan) == expected);
        CHECK(!static_cast<bool>(plan));
        CHECK(workspace.get()->phase()
              == fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    // A failed resolver may leave a partial private journal, but it must not
    // poison the workspace or publish a partial plan.
    {
        EffectFixture fixture = MakeMinimalEffect();
        FinalizeEffectTotalSize(&fixture);
        EffectViewOwner view(&fixture);
        ResolverOwner resolver(&fixture.image);
        resolver.state.failAt = 1;
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::UnresolvedReference);
        resolver.state.failAt = (std::numeric_limits<std::size_t>::max)();
        resolver.state.calls = 0;
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::Success);
        CHECK(plan.resolvedReferenceCount() == 2);
    }

    {
        EffectFixture fixture = MakeMinimalEffect();
        FinalizeEffectTotalSize(&fixture);
        EffectViewOwner view(&fixture);
        ResolverOwner resolver(&fixture.image);
        WorkspaceOwner workspace;
        resolver.state.reenter = true;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::Success);
        CHECK(resolver.state.reentered);
        CHECK(resolver.state.nestedStatus
              == fastfile::FxFastFileNativeDisk32Status::Busy);
        CHECK(!static_cast<bool>(resolver.state.nestedPlan));

        const std::size_t calls = resolver.state.calls;
        fastfile::FxFastFileNativeDisk32Plan secondPlan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &secondPlan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidPhase);
        CHECK(!static_cast<bool>(secondPlan));
        CHECK(resolver.state.calls == calls);
    }

    {
        EffectFixture fixture = MakeMinimalEffect();
        FinalizeEffectTotalSize(&fixture);
        EffectViewOwner view(&fixture);
        ResolverOwner resolver(&fixture.image);
        resolver.state.mutateSourceDuringResolve = true;
        WorkspaceOwner workspace;
        fastfile::FxFastFileNativeDisk32Plan plan{};
        CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
              == fastfile::FxFastFileNativeDisk32Status::SourceChanged);
        CHECK(resolver.state.sourceMutated);
        CHECK(!static_cast<bool>(plan));
        CHECK(workspace.get()->phase()
              == fastfile::FxFastFileNativeDisk32Phase::Empty);
    }
}

void TestPlanningOutputAliases()
{
    using Plan = fastfile::FxFastFileNativeDisk32Plan;
    {
        EffectFixture fixture = MakeMinimalEffect();
        FinalizeEffectTotalSize(&fixture);
        const std::uint32_t originalNameBytes =
            fixture.image.CStringBytes(fixture.nameToken);
        CHECK(originalNameBytes >= 2);
        fixture.effect()->totalSize -=
            static_cast<std::int32_t>(originalNameBytes - 2u);
        EffectViewOwner view(&fixture);
        ResolverOwner resolver(&fixture.image);
        WorkspaceOwner workspace;

        alignas(Plan) std::array<std::uint8_t, sizeof(Plan) + 1u> backing{};
        Plan *const aliasedPlan =
            std::construct_at(reinterpret_cast<Plan *>(backing.data()));
        char *const aliasedName = reinterpret_cast<char *>(
            backing.data() + sizeof(Plan) - 1u);
        aliasedName[0] = 'x';
        aliasedName[1] = '\0';
        const Plan snapshot = *aliasedPlan;
        resolver.state.effectNameResult = aliasedName;
        resolver.state.effectNameResultBytes = 2;
        view.provenance().externalString = aliasedName;
        view.provenance().externalStringBytes = 2;

        CHECK(PlanEffect(&workspace, &view, &resolver, aliasedPlan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
        CHECK(PlansEqual(*aliasedPlan, snapshot));
        CHECK(workspace.get()->phase()
              == fastfile::FxFastFileNativeDisk32Phase::Empty);
        std::destroy_at(aliasedPlan);
    }

    {
        EffectFixture fixture = MakeMinimalEffect();
        FinalizeEffectTotalSize(&fixture);
        EffectViewOwner view(&fixture);
        ResolverOwner resolver(&fixture.image);
        WorkspaceOwner workspace;
        Plan aliasedPlan{};
        const Plan snapshot = aliasedPlan;
        resolver.state.assetResultAt = 1;
        resolver.state.assetResult = &aliasedPlan;

        CHECK(PlanEffect(&workspace, &view, &resolver, &aliasedPlan)
              == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
        CHECK(PlansEqual(aliasedPlan, snapshot));
        CHECK(workspace.get()->phase()
              == fastfile::FxFastFileNativeDisk32Phase::Empty);
    }
}

void TestMaterializationGuardsAndRetry()
{
    EffectFixture fixture = MakeMinimalEffect();
    FinalizeEffectTotalSize(&fixture);
    EffectViewOwner view(&fixture);
    ResolverOwner resolver(&fixture.image);
    WorkspaceOwner workspace;
    fastfile::FxFastFileNativeDisk32Plan plan{};
    CHECK(PlanEffect(&workspace, &view, &resolver, &plan)
          == fastfile::FxFastFileNativeDisk32Status::Success);
    const std::size_t resolverCalls = resolver.state.calls;
    OutputStorage storage(plan.outputBytes(), plan.outputAlignment());
    FxEffectDef *const sentinel = reinterpret_cast<FxEffectDef *>(
        static_cast<std::uintptr_t>(1));
    FxEffectDef *output = sentinel;

    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              nullptr,
              plan,
              storage.data(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(), plan, nullptr, storage.capacity(), &output)
          == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              storage.data(),
              storage.capacity(),
              nullptr)
          == fastfile::FxFastFileNativeDisk32Status::InvalidArgument);
    CHECK(output == sentinel);
    CHECK(storage.IsFilled());
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Planned);

    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              storage.misalignedData(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::MisalignedStorage);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              storage.data(),
              storage.capacity() - 1u,
              &output)
          == fastfile::FxFastFileNativeDisk32Status::InsufficientCapacity);

    const std::uintptr_t overflowAddress =
        (std::numeric_limits<std::uintptr_t>::max)()
        & ~(static_cast<std::uintptr_t>(plan.outputAlignment()) - 1u);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              reinterpret_cast<void *>(overflowAddress),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::SizeOverflow);

    CHECK(reinterpret_cast<std::uintptr_t>(view.elementViews().data())
              % plan.outputAlignment()
          == 0);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              view.elementViews().data(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::OverlappingStorage);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              workspace.get(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::OverlappingStorage);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              const_cast<fastfile::FxFastFileNativeDisk32Plan *>(&plan),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::OverlappingStorage);

    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              storage.data(),
              storage.capacity(),
              reinterpret_cast<FxEffectDef **>(storage.data()))
          == fastfile::FxFastFileNativeDisk32Status::OverlappingStorage);
    const auto firstViewSnapshot = view.elementViews()[0];
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              storage.data(),
              storage.capacity(),
              reinterpret_cast<FxEffectDef **>(
                  view.elementViews().data()))
          == fastfile::FxFastFileNativeDisk32Status::OverlappingStorage);
    CHECK(std::memcmp(
              &firstViewSnapshot,
              &view.elementViews()[0],
              sizeof(firstViewSnapshot))
          == 0);

    CHECK(output == sentinel);
    CHECK(storage.IsFilled());
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Planned);
    CHECK(resolver.state.calls == resolverCalls);

    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              plan,
              storage.data(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::Success);
    CHECK(output == storage.data());
    CHECK(storage.TailGuardIsIntact());
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Empty);
    CHECK(resolver.state.calls == resolverCalls);

    // Retained non-string identities are an overlap hazard even though their
    // byte extent is opaque.  Treat the identity itself as one retained byte.
    EffectFixture assetFixture = MakeMinimalEffect();
    FinalizeEffectTotalSize(&assetFixture);
    EffectViewOwner probeView(&assetFixture);
    ResolverOwner probeResolver(&assetFixture.image);
    WorkspaceOwner probeWorkspace;
    fastfile::FxFastFileNativeDisk32Plan probePlan{};
    CHECK(PlanEffect(
              &probeWorkspace, &probeView, &probeResolver, &probePlan)
          == fastfile::FxFastFileNativeDisk32Status::Success);
    OutputStorage assetStorage(
        probePlan.outputBytes(), probePlan.outputAlignment());

    EffectViewOwner assetView(&assetFixture);
    ResolverOwner assetResolver(&assetFixture.image);
    assetResolver.state.assetResultAt = 1;
    assetResolver.state.assetResult = assetStorage.data();
    WorkspaceOwner assetWorkspace;
    fastfile::FxFastFileNativeDisk32Plan assetPlan{};
    CHECK(PlanEffect(
              &assetWorkspace, &assetView, &assetResolver, &assetPlan)
          == fastfile::FxFastFileNativeDisk32Status::Success);
    CHECK(assetPlan.outputBytes() == assetStorage.capacity());
    FxEffectDef *assetOutput = sentinel;
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              assetWorkspace.get(),
              assetPlan,
              assetStorage.data(),
              assetStorage.capacity(),
              &assetOutput)
          == fastfile::FxFastFileNativeDisk32Status::OverlappingStorage);
    CHECK(assetOutput == sentinel);
    CHECK(assetStorage.IsFilled());
    CHECK(assetWorkspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Planned);
}

void TestPlanBindingAndSourceChanges()
{
    EffectFixture fixture = MakeMinimalEffect();
    FinalizeEffectTotalSize(&fixture);
    EffectViewOwner view(&fixture);
    ResolverOwner resolver(&fixture.image);
    WorkspaceOwner workspace;
    fastfile::FxFastFileNativeDisk32Plan original{};
    CHECK(PlanEffect(&workspace, &view, &resolver, &original)
          == fastfile::FxFastFileNativeDisk32Status::Success);

    OutputStorage storage(
        original.outputBytes(), original.outputAlignment());
    FxEffectDef *const sentinel = reinterpret_cast<FxEffectDef *>(
        static_cast<std::uintptr_t>(1));
    FxEffectDef *output = sentinel;
    const fastfile::FxFastFileNativeDisk32Plan emptyPlan{};
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              emptyPlan,
              storage.data(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::InvalidPlan);

    WorkspaceOwner foreignWorkspace;
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              foreignWorkspace.get(),
              original,
              storage.data(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::InvalidPlan);

    fastfile::FxFastFileNativeDisk32Plan mutated = original;
    bool changedOutputBytes = false;
    for (std::size_t byte = 0; byte < sizeof(mutated); ++byte)
    {
        mutated = original;
        auto *const representation =
            reinterpret_cast<std::uint8_t *>(&mutated);
        representation[byte] ^= UINT8_C(1);
        if (mutated.outputBytes() != original.outputBytes())
        {
            changedOutputBytes = true;
            break;
        }
    }
    CHECK(changedOutputBytes);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              mutated,
              storage.data(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::InvalidPlan);
    CHECK(output == sentinel);
    CHECK(storage.IsFilled());
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Planned);

    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              original,
              storage.data(),
              storage.capacity(),
              &output)
          == fastfile::FxFastFileNativeDisk32Status::Success);
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Empty);

    OutputStorage consumedStorage(
        original.outputBytes(), original.outputAlignment());
    FxEffectDef *consumedOutput = sentinel;
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              original,
              consumedStorage.data(),
              consumedStorage.capacity(),
              &consumedOutput)
          == fastfile::FxFastFileNativeDisk32Status::InvalidPhase);
    CHECK(consumedOutput == sentinel);
    CHECK(consumedStorage.IsFilled());

    resolver.state.calls = 0;
    fastfile::FxFastFileNativeDisk32Plan replacement{};
    CHECK(PlanEffect(&workspace, &view, &resolver, &replacement)
          == fastfile::FxFastFileNativeDisk32Status::Success);
    CHECK(!PlansEqual(original, replacement));
    OutputStorage replacementStorage(
        replacement.outputBytes(), replacement.outputAlignment());
    FxEffectDef *replacementOutput = sentinel;
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              original,
              replacementStorage.data(),
              replacementStorage.capacity(),
              &replacementOutput)
          == fastfile::FxFastFileNativeDisk32Status::InvalidPlan);
    CHECK(replacementOutput == sentinel);
    CHECK(replacementStorage.IsFilled());
    CHECK(workspace.get()->phase()
          == fastfile::FxFastFileNativeDisk32Phase::Planned);
    CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
              workspace.get(),
              replacement,
              replacementStorage.data(),
              replacementStorage.capacity(),
              &replacementOutput)
          == fastfile::FxFastFileNativeDisk32Status::Success);

    {
        EffectFixture changed = MakeMinimalEffect();
        FinalizeEffectTotalSize(&changed);
        EffectViewOwner changedView(&changed);
        ResolverOwner changedResolver(&changed.image);
        WorkspaceOwner changedWorkspace;
        fastfile::FxFastFileNativeDisk32Plan changedPlan{};
        CHECK(PlanEffect(
                  &changedWorkspace,
                  &changedView,
                  &changedResolver,
                  &changedPlan)
              == fastfile::FxFastFileNativeDisk32Status::Success);
        changed.elems()[0].flags ^= 1u;
        OutputStorage changedStorage(
            changedPlan.outputBytes(), changedPlan.outputAlignment());
        FxEffectDef *changedOutput = sentinel;
        CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
                  changedWorkspace.get(),
                  changedPlan,
                  changedStorage.data(),
                  changedStorage.capacity(),
                  &changedOutput)
              == fastfile::FxFastFileNativeDisk32Status::SourceChanged);
        CHECK(changedOutput == sentinel);
        CHECK(changedStorage.IsFilled());
        CHECK(changedWorkspace.get()->phase()
              == fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        EffectFixture changed = MakeMinimalEffect();
        FinalizeEffectTotalSize(&changed);
        EffectViewOwner changedView(&changed);
        ResolverOwner changedResolver(&changed.image);
        WorkspaceOwner changedWorkspace;
        fastfile::FxFastFileNativeDisk32Plan changedPlan{};
        CHECK(PlanEffect(
                  &changedWorkspace,
                  &changedView,
                  &changedResolver,
                  &changedPlan)
              == fastfile::FxFastFileNativeDisk32Status::Success);
        char *const name = changed.image.Resolve<char>(changed.nameToken);
        CHECK(name != nullptr);
        if (name)
            name[0] = name[0] == 'f' ? 'g' : 'f';
        OutputStorage changedStorage(
            changedPlan.outputBytes(), changedPlan.outputAlignment());
        FxEffectDef *changedOutput = sentinel;
        CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
                  changedWorkspace.get(),
                  changedPlan,
                  changedStorage.data(),
                  changedStorage.capacity(),
                  &changedOutput)
              == fastfile::FxFastFileNativeDisk32Status::SourceChanged);
        CHECK(changedOutput == sentinel);
        CHECK(changedStorage.IsFilled());
        CHECK(changedWorkspace.get()->phase()
              == fastfile::FxFastFileNativeDisk32Phase::Empty);
    }

    {
        EffectFixture changed = MakeMinimalEffect();
        FinalizeEffectTotalSize(&changed);
        EffectViewOwner changedView(&changed);
        ResolverOwner changedResolver(&changed.image);
        WorkspaceOwner changedWorkspace;
        fastfile::FxFastFileNativeDisk32Plan changedPlan{};
        CHECK(PlanEffect(
                  &changedWorkspace,
                  &changedView,
                  &changedResolver,
                  &changedPlan)
              == fastfile::FxFastFileNativeDisk32Status::Success);
        --changedView.elementViews()[0].velocitySamples.count;
        OutputStorage changedStorage(
            changedPlan.outputBytes(), changedPlan.outputAlignment());
        FxEffectDef *changedOutput = sentinel;
        CHECK(fastfile::TryMaterializeFxEffectDefDisk32(
                  changedWorkspace.get(),
                  changedPlan,
                  changedStorage.data(),
                  changedStorage.capacity(),
                  &changedOutput)
              == fastfile::FxFastFileNativeDisk32Status::SourceChanged);
        CHECK(changedOutput == sentinel);
        CHECK(changedStorage.IsFilled());
        CHECK(changedWorkspace.get()->phase()
              == fastfile::FxFastFileNativeDisk32Phase::Empty);
    }
}

EffectFixture MakeEmptyEffect()
{
    return MakeEffect({}, 0, 0, 0);
}

EffectFixture MakeMinimalEffect()
{
    EffectFixture fixture = MakeEffect({MinimalElem()}, 0, 1, 0);
    AttachSamples(&fixture, 0, 1, 0);
    AttachVisuals(
        &fixture,
        0,
        fastfile::FxElemTypeDisk32::SpriteBillboard,
        {AddOpaqueReference(&fixture, UINT32_C(0xB001))});
    return fixture;
}

void AttachSamples(
    EffectFixture *const fixture,
    const std::size_t elemIndex,
    const std::uint8_t velIntervals,
    const std::uint8_t visIntervals)
{
    CHECK(fixture != nullptr);
    CHECK(fixture && fixture->elems() != nullptr);
    if (!fixture || !fixture->elems())
        return;

    fastfile::FxElemDefDisk32 &elem = fixture->elems()[elemIndex];
    elem.velIntervalCount = velIntervals;
    elem.visStateIntervalCount = visIntervals;
    std::vector<fastfile::FxElemVelStateSampleDisk32> velocity(
        static_cast<std::size_t>(velIntervals) + 1u);
    std::vector<fastfile::FxElemVisStateSampleDisk32> visibility(
        static_cast<std::size_t>(visIntervals) + 1u);
    for (std::size_t index = 0; index < velocity.size(); ++index)
        velocity[index].local.velocity.base[0] = static_cast<float>(index + 1u);
    for (std::size_t index = 0; index < visibility.size(); ++index)
        visibility[index].base.scale = static_cast<float>(index + 2u);
    const disk32::PointerToken velocityToken = fixture->image.AppendArray(
        kDataBlock, velocity.data(), velocity.size());
    const disk32::PointerToken visibilityToken = fixture->image.AppendArray(
        kDataBlock, visibility.data(), visibility.size());
    elem.velSamples.token = velocityToken;
    elem.visSamples.token = visibilityToken;
    fixture->velSampleTokens.push_back(velocityToken);
    fixture->visSampleTokens.push_back(visibilityToken);
}

void AttachTrail(
    EffectFixture *const fixture,
    const std::size_t elemIndex,
    const std::int32_t vertCount = 3,
    const std::int32_t indCount = 4)
{
    CHECK(fixture != nullptr);
    CHECK(fixture && fixture->elems() != nullptr);
    if (!fixture || !fixture->elems())
        return;

    std::vector<fastfile::FxTrailVertexDisk32> vertices(
        static_cast<std::size_t>(vertCount));
    for (std::size_t index = 0; index < vertices.size(); ++index)
        vertices[index].pos[0] = static_cast<float>(index);
    std::vector<std::uint16_t> indices(
        static_cast<std::size_t>(indCount), 0);
    for (std::size_t index = 0; index < indices.size(); ++index)
    {
        indices[index] = static_cast<std::uint16_t>(
            index % vertices.size());
    }

    const disk32::PointerToken vertexToken = fixture->image.AppendArray(
        kDataBlock, vertices.data(), vertices.size());
    const disk32::PointerToken indexToken = fixture->image.AppendArray(
        kDataBlock, indices.data(), indices.size());
    fastfile::FxTrailDefDisk32 trail{};
    trail.scrollTimeMsec = 100;
    trail.repeatDist = 4;
    trail.splitDist = 8;
    trail.vertCount = vertCount;
    trail.verts.token = vertexToken;
    trail.indCount = indCount;
    trail.inds.token = indexToken;
    const disk32::PointerToken trailToken = fixture->image.Append(
        kDataBlock, trail);

    fixture->elems()[elemIndex].trailDef.token = trailToken;
    fixture->trailTokens.push_back(trailToken);
    fixture->trailVertexTokens.push_back(vertexToken);
    fixture->trailIndexTokens.push_back(indexToken);
}

disk32::PointerToken AddOpaqueReference(
    EffectFixture *const fixture,
    const std::uint32_t identity)
{
    CHECK(fixture != nullptr);
    if (!fixture)
        return {};
    return fixture->image.Append(kDataBlock, identity, identity);
}

void AttachVisuals(
    EffectFixture *const fixture,
    const std::size_t elemIndex,
    const fastfile::FxElemTypeDisk32 type,
    const std::vector<disk32::PointerToken> &references)
{
    CHECK(fixture != nullptr);
    CHECK(fixture && fixture->elems() != nullptr);
    CHECK(!references.empty());
    if (!fixture || !fixture->elems() || references.empty())
        return;

    fastfile::FxElemDefDisk32 &elem = fixture->elems()[elemIndex];
    elem.elemType = type;
    if (type == fastfile::FxElemTypeDisk32::Decal)
    {
        CHECK(references.size() % 2u == 0);
        const std::size_t visualCount = references.size() / 2u;
        CHECK(visualCount <= (std::numeric_limits<std::uint8_t>::max)());
        std::vector<fastfile::FxElemMarkVisualsDisk32> visuals(visualCount);
        for (std::size_t index = 0; index < visualCount; ++index)
        {
            visuals[index].materials[0].token = references[index * 2u];
            visuals[index].materials[1].token = references[index * 2u + 1u];
        }
        const disk32::PointerToken visualsToken = fixture->image.AppendArray(
            kDataBlock, visuals.data(), visuals.size());
        elem.visualCount = static_cast<std::uint8_t>(visualCount);
        elem.visuals.token = visualsToken;
        fixture->visualTokens.push_back(visualsToken);
        return;
    }

    CHECK(references.size()
          <= (std::numeric_limits<std::uint8_t>::max)());
    elem.visualCount = static_cast<std::uint8_t>(references.size());
    if (references.size() == 1u)
    {
        elem.visuals.token = references.front();
        return;
    }

    std::vector<fastfile::FxElemVisualsDisk32> visuals(references.size());
    for (std::size_t index = 0; index < references.size(); ++index)
        visuals[index].token = references[index];
    const disk32::PointerToken visualsToken = fixture->image.AppendArray(
        kDataBlock, visuals.data(), visuals.size());
    elem.visuals.token = visualsToken;
    fixture->visualTokens.push_back(visualsToken);
}

EffectFixture MakeAllVisualKindsEffect()
{
    constexpr std::array<fastfile::FxElemTypeDisk32, 11> kTypes{
        fastfile::FxElemTypeDisk32::SpriteBillboard,
        fastfile::FxElemTypeDisk32::SpriteOriented,
        fastfile::FxElemTypeDisk32::Tail,
        fastfile::FxElemTypeDisk32::Trail,
        fastfile::FxElemTypeDisk32::Cloud,
        fastfile::FxElemTypeDisk32::Model,
        fastfile::FxElemTypeDisk32::OmniLight,
        fastfile::FxElemTypeDisk32::SpotLight,
        fastfile::FxElemTypeDisk32::Sound,
        fastfile::FxElemTypeDisk32::Decal,
        fastfile::FxElemTypeDisk32::Runner,
    };
    std::vector<fastfile::FxElemDefDisk32> elems;
    elems.reserve(kTypes.size());
    for (const fastfile::FxElemTypeDisk32 type : kTypes)
        elems.push_back(MinimalElem(type));
    EffectFixture fixture = MakeEffect(
        std::move(elems),
        3,
        4,
        static_cast<std::int32_t>(kTypes.size() - 7u));

    for (std::size_t index = 0; index < kTypes.size(); ++index)
        AttachSamples(&fixture, index, 1, 0);

    for (std::size_t index = 0; index < kTypes.size(); ++index)
    {
        const fastfile::FxElemTypeDisk32 type = kTypes[index];
        if (type == fastfile::FxElemTypeDisk32::OmniLight
            || type == fastfile::FxElemTypeDisk32::SpotLight)
        {
            fixture.elems()[index].visualCount = 1;
            fixture.elems()[index].visuals.token = {};
            continue;
        }
        if (type == fastfile::FxElemTypeDisk32::Decal)
        {
            AttachVisuals(
                &fixture,
                index,
                type,
                {AddOpaqueReference(&fixture, 0xD001u),
                 AddOpaqueReference(&fixture, 0xD002u),
                 AddOpaqueReference(&fixture, 0xD003u),
                 AddOpaqueReference(&fixture, 0xD004u)});
            continue;
        }

        if (type == fastfile::FxElemTypeDisk32::Sound)
        {
            AttachVisuals(
                &fixture,
                index,
                type,
                {fixture.image.AppendString(kDataBlock, "sound/first"),
                 fixture.image.AppendString(kDataBlock, "sound/second")});
            continue;
        }

        if (type == fastfile::FxElemTypeDisk32::Runner)
        {
            AttachVisuals(
                &fixture,
                index,
                type,
                {fixture.image.AppendString(kDataBlock, "fx/runner_first"),
                 fixture.image.AppendString(kDataBlock, "fx/runner_second")});
            continue;
        }

        AttachVisuals(
            &fixture,
            index,
            type,
            {AddOpaqueReference(
                 &fixture,
                 static_cast<std::uint32_t>(0xA000u + index * 2u)),
             AddOpaqueReference(
                 &fixture,
                 static_cast<std::uint32_t>(0xA001u + index * 2u))});
    }

    // These name-backed references are independent of the active visual
    // grammar and exercise all three FxEffectDefRef fields.
    fixture.elems()[0].effectOnImpact.token =
        fixture.image.AppendString(kDataBlock, "fx/on_impact");
    fixture.elems()[0].effectOnDeath.token =
        fixture.image.AppendString(kDataBlock, "fx/on_death");
    fixture.elems()[0].effectEmitted.token =
        fixture.image.AppendString(kDataBlock, "fx/emitted");
    AttachTrail(&fixture, 3);
    return fixture;
}

void FixtureSelfTest()
{
    EffectFixture empty = MakeEmptyEffect();
    CHECK(empty.effect() != nullptr);
    CHECK(empty.effect()->elemDefs.token.isNull());

    EffectFixture minimal = MakeMinimalEffect();
    CHECK(minimal.effect() != nullptr);
    CHECK(minimal.elems() != nullptr);
    AttachSamples(&minimal, 0, 2, 3);
    AttachTrail(&minimal, 0);
    CHECK(minimal.elems()[0].velSamples.token.isOffset());
    CHECK(minimal.elems()[0].visSamples.token.isOffset());
    CHECK(minimal.elems()[0].trailDef.token.isOffset());

    EffectFixture allVisuals = MakeAllVisualKindsEffect();
    CHECK(allVisuals.effect() != nullptr);
    CHECK(allVisuals.elems() != nullptr);
    CHECK(allVisuals.effect()->elemDefCountLooping == 3);
    CHECK(allVisuals.effect()->elemDefCountOneShot == 4);
    CHECK(allVisuals.effect()->elemDefCountEmission == 4);
    CHECK(allVisuals.elems()[5].elemType
          == fastfile::FxElemTypeDisk32::Model);
    CHECK(allVisuals.elems()[9].elemType
          == fastfile::FxElemTypeDisk32::Decal);
}
} // namespace

int main()
{
    FixtureSelfTest();
    TestValidDefinitions();
    TestArgumentsCountsAndOutputPlanPreservation();
    TestPointerSpanAndProvenanceFailures();
    TestVisualValidation();
    TestTrailValidation();
    TestResolverFailuresAndReentry();
    TestPlanningOutputAliases();
    TestMaterializationGuardsAndRetry();
    TestPlanBindingAndSourceChanges();
    return failures == 0 ? 0 : 1;
}
