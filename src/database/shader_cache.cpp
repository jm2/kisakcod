#include "database/shader_cache.h"

#include <cstdio>
#include <cstring>

namespace db::shader_cache
{
namespace
{
// Canonical record/field tags for the domain-separated digests. The tags are
// local to this domain, so their numeric values are an implementation detail;
// changing the framing bumps kHashDomain.
constexpr std::uint32_t kRecordProgram = 1;
constexpr std::uint32_t kRecordCacheKey = 2;
constexpr std::uint32_t kRecordArtifact = 3;

constexpr std::uint32_t kTagDomain = 1;
constexpr std::uint32_t kTagProgramDword = 2;
constexpr std::uint32_t kTagStage = 3;
constexpr std::uint32_t kTagLoadForRenderer = 4;
constexpr std::uint32_t kTagProgramDwordCount = 5;
constexpr std::uint32_t kTagContentHash = 6;
constexpr std::uint32_t kTagArtifactBytes = 7;
constexpr std::uint32_t kTagFormatVersion = 8;
constexpr std::uint32_t kTagConverterVersion = 9;

void WriteU32(std::uint8_t *out, const std::uint32_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value & 0xFFu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

std::uint32_t ReadU32(const std::uint8_t *in) noexcept
{
    return static_cast<std::uint32_t>(in[0])
        | (static_cast<std::uint32_t>(in[1]) << 8)
        | (static_cast<std::uint32_t>(in[2]) << 16)
        | (static_cast<std::uint32_t>(in[3]) << 24);
}

void WriteDigest(std::uint8_t *out, const Digest &digest) noexcept
{
    std::memcpy(out, digest.data(), digest.size());
}

Digest ReadDigest(const std::uint8_t *in) noexcept
{
    Digest digest{};
    std::memcpy(digest.data(), in, digest.size());
    return digest;
}

bool IsKnownStage(const std::uint32_t value) noexcept
{
    return value == 0 || value == 1;
}

} // namespace

const char *LookupResultName(const LookupResult result) noexcept
{
    switch (result)
    {
        case LookupResult::Hit:
            return "hit";
        case LookupResult::Missing:
            return "missing";
        case LookupResult::NeedsRegeneration:
            return "needs-regeneration";
        case LookupResult::UnsupportedSource:
            return "unsupported-source";
    }
    return "unknown";
}

bool ValidateSourceProgram(const std::uint32_t *const program,
    const std::uint32_t dwordCount,
    const Stage stage,
    const std::uint32_t loadForRenderer) noexcept
{
    if (!db::validation::MaterialShaderLoadDefValid(
            program != nullptr, dwordCount, loadForRenderer))
    {
        return false;
    }
    return db::validation::D3D9ShaderBytecodeValid(
        program, dwordCount, stage, loadForRenderer);
}

Digest HashSourceProgram(const std::uint32_t *const program,
    const std::uint32_t dwordCount) noexcept
{
    // Each dword enters as an explicit u64 field carrying a u32 value. The
    // builder's little-endian framing makes the digest independent of host
    // byte order and struct layout, matching the repo's parity-hash contract.
    db::graph_hash::GraphHashBuilder builder;
    builder.BeginRecord(kRecordProgram);
    builder.FieldString(kTagDomain, kHashDomain);
    builder.FieldU64(kTagProgramDwordCount, dwordCount);
    for (std::uint32_t index = 0; index < dwordCount; ++index)
    {
        builder.FieldU64(kTagProgramDword, program[index]);
    }
    builder.EndRecord();
    return builder.Finish();
}

Digest HashArtifact(const void *const artifact, const std::size_t artifactSize) noexcept
{
    db::graph_hash::GraphHashBuilder builder;
    builder.BeginRecord(kRecordArtifact);
    builder.FieldString(kTagDomain, kHashDomain);
    builder.FieldBytes(kTagArtifactBytes, artifact, artifactSize);
    builder.EndRecord();
    return builder.Finish();
}

bool MakeSourceIdentity(const std::uint32_t *const program,
    const std::uint32_t dwordCount,
    const Stage stage,
    const std::uint32_t loadForRenderer,
    SourceIdentity &outIdentity) noexcept
{
    if (!ValidateSourceProgram(program, dwordCount, stage, loadForRenderer))
    {
        return false;
    }

    outIdentity.stage = stage;
    outIdentity.loadForRenderer = loadForRenderer;
    outIdentity.programDwordCount = dwordCount;
    outIdentity.contentHash = HashSourceProgram(program, dwordCount);

    db::graph_hash::GraphHashBuilder builder;
    builder.BeginRecord(kRecordCacheKey);
    builder.FieldString(kTagDomain, kHashDomain);
    builder.FieldU64(kTagFormatVersion, kFormatVersion);
    builder.FieldU64(kTagConverterVersion, kConverterVersion);
    builder.FieldU64(kTagStage, static_cast<std::uint64_t>(stage));
    builder.FieldU64(kTagLoadForRenderer, loadForRenderer);
    builder.FieldU64(kTagProgramDwordCount, dwordCount);
    builder.FieldBytes(
        kTagContentHash, outIdentity.contentHash.data(), outIdentity.contentHash.size());
    builder.EndRecord();
    outIdentity.cacheKey = builder.Finish();
    return true;
}

bool SidecarHeaderMatchesSource(const SidecarHeader &header,
    const SourceIdentity &source) noexcept
{
    return header.stage == source.stage
        && header.loadForRenderer == source.loadForRenderer
        && header.sourceDwordCount == source.programDwordCount
        && header.sourceHash == source.contentHash;
}

bool SerializeSidecarHeader(const SidecarHeader &header,
    std::uint8_t *const outBytes,
    const std::size_t outSize) noexcept
{
    if (outBytes == nullptr || outSize < kSidecarHeaderBytes)
    {
        return false;
    }

    std::size_t offset = 0;
    std::memcpy(outBytes + offset, kSidecarMagic, sizeof(kSidecarMagic));
    offset += sizeof(kSidecarMagic);
    WriteU32(outBytes + offset, header.formatVersion);
    offset += 4;
    WriteU32(outBytes + offset, header.converterVersion);
    offset += 4;
    WriteU32(outBytes + offset, static_cast<std::uint32_t>(header.stage));
    offset += 4;
    WriteU32(outBytes + offset, header.loadForRenderer);
    offset += 4;
    WriteU32(outBytes + offset, header.sourceDwordCount);
    offset += 4;
    WriteDigest(outBytes + offset, header.sourceHash);
    offset += header.sourceHash.size();
    WriteU32(outBytes + offset, static_cast<std::uint32_t>(header.artifactKind));
    offset += 4;
    WriteU32(outBytes + offset, header.artifactSize);
    offset += 4;
    WriteDigest(outBytes + offset, header.artifactHash);
    offset += header.artifactHash.size();

    return offset == kSidecarHeaderBytes;
}

bool ParseSidecarHeader(const std::uint8_t *const bytes,
    const std::size_t size,
    SidecarHeader &outHeader) noexcept
{
    if (bytes == nullptr || size < kSidecarHeaderBytes
        || std::memcmp(bytes, kSidecarMagic, sizeof(kSidecarMagic)) != 0)
    {
        return false;
    }

    std::size_t offset = sizeof(kSidecarMagic);
    const std::uint32_t formatVersion = ReadU32(bytes + offset);
    offset += 4;
    const std::uint32_t converterVersion = ReadU32(bytes + offset);
    offset += 4;
    const std::uint32_t stage = ReadU32(bytes + offset);
    offset += 4;
    const std::uint32_t loadForRenderer = ReadU32(bytes + offset);
    offset += 4;
    const std::uint32_t sourceDwordCount = ReadU32(bytes + offset);
    offset += 4;
    const Digest sourceHash = ReadDigest(bytes + offset);
    offset += sourceHash.size();
    const std::uint32_t artifactKind = ReadU32(bytes + offset);
    offset += 4;
    const std::uint32_t artifactSize = ReadU32(bytes + offset);
    offset += 4;
    const Digest artifactHash = ReadDigest(bytes + offset);
    offset += artifactHash.size();

    // A malformed stage or an unset kind is garbage, not a future version.
    // Unknown *higher* kind values parse and let the lookup classify them as
    // NeedsRegeneration.
    if (offset != kSidecarHeaderBytes || !IsKnownStage(stage) || artifactKind == 0)
    {
        return false;
    }

    outHeader.formatVersion = formatVersion;
    outHeader.converterVersion = converterVersion;
    outHeader.stage = static_cast<Stage>(stage);
    outHeader.loadForRenderer = loadForRenderer;
    outHeader.sourceDwordCount = sourceDwordCount;
    outHeader.sourceHash = sourceHash;
    outHeader.artifactKind = static_cast<DerivedArtifactKind>(artifactKind);
    outHeader.artifactSize = artifactSize;
    outHeader.artifactHash = artifactHash;
    return true;
}

std::vector<std::uint8_t> BuildSidecar(const SourceIdentity &source,
    const DerivedArtifactKind kind,
    const void *const artifact,
    const std::size_t artifactSize)
{
    std::vector<std::uint8_t> bytes;
    // Reject an unrepresentable total before sizing a buffer or reading the
    // payload. The u32 framing limit alone is not enough: on ILP32,
    // kSidecarHeaderBytes + artifactSize can wrap even when artifactSize still
    // fits the framing field, which would under-allocate the buffer and then
    // overrun it in the header write and payload memcpy below.
    if (artifact == nullptr || artifactSize == 0
        || artifactSize > static_cast<std::size_t>(UINT32_MAX)
        || !SidecarTotalSizeRepresentable(artifactSize))
    {
        return bytes;
    }

    const std::size_t totalSize = kSidecarHeaderBytes + artifactSize;

    SidecarHeader header;
    header.formatVersion = kFormatVersion;
    header.converterVersion = kConverterVersion;
    header.stage = source.stage;
    header.loadForRenderer = source.loadForRenderer;
    header.sourceDwordCount = source.programDwordCount;
    header.sourceHash = source.contentHash;
    header.artifactKind = kind;
    header.artifactSize = static_cast<std::uint32_t>(artifactSize);
    header.artifactHash = HashArtifact(artifact, artifactSize);

    bytes.resize(totalSize);
    if (!SerializeSidecarHeader(header, bytes.data(), bytes.size()))
    {
        bytes.clear();
        return bytes;
    }
    std::memcpy(bytes.data() + kSidecarHeaderBytes, artifact, artifactSize);
    return bytes;
}

LookupResult LookupDerivedShader(const std::uint8_t *const sidecarBytes,
    const std::size_t sidecarSize,
    const std::uint32_t *const sourceProgram,
    const std::uint32_t sourceDwordCount,
    const Stage stage,
    const std::uint32_t loadForRenderer,
    std::vector<std::uint8_t> &outArtifact)
{
    outArtifact.clear();

    SourceIdentity source;
    if (!MakeSourceIdentity(
            sourceProgram, sourceDwordCount, stage, loadForRenderer, source))
    {
        return LookupResult::UnsupportedSource;
    }

    SidecarHeader header;
    if (!ParseSidecarHeader(sidecarBytes, sidecarSize, header))
    {
        return LookupResult::Missing;
    }

    if (header.formatVersion != kFormatVersion
        || header.converterVersion != kConverterVersion)
    {
        return LookupResult::NeedsRegeneration;
    }

    if (header.artifactKind != DerivedArtifactKind::SpirV
        || header.artifactSize == 0
        || !SidecarTotalSizeRepresentable(header.artifactSize)
        || !SidecarHeaderMatchesSource(header, source))
    {
        return LookupResult::NeedsRegeneration;
    }

    const std::size_t payloadOffset = kSidecarHeaderBytes;
    // SidecarTotalSizeRepresentable guarantees this addition cannot wrap.
    const std::size_t expectedSize =
        payloadOffset + static_cast<std::size_t>(header.artifactSize);
    // A short file or trailing bytes is corruption; the size is checked here
    // rather than trusted from the header.
    if (expectedSize != sidecarSize)
    {
        return LookupResult::NeedsRegeneration;
    }

    const std::uint8_t *const payload = sidecarBytes + payloadOffset;
    if (HashArtifact(payload, header.artifactSize) != header.artifactHash)
    {
        return LookupResult::NeedsRegeneration;
    }

    outArtifact.assign(payload, payload + header.artifactSize);
    return LookupResult::Hit;
}

std::string CacheDirectoryName()
{
    char buffer[96];
    const int written = std::snprintf(buffer,
        sizeof(buffer),
        "derived-shader-cache/format%u-converter%u",
        static_cast<unsigned>(kFormatVersion),
        static_cast<unsigned>(kConverterVersion));
    if (written <= 0 || written >= static_cast<int>(sizeof(buffer)))
    {
        return std::string("derived-shader-cache");
    }
    return std::string(buffer, static_cast<std::size_t>(written));
}

std::string SidecarFileName(const SourceIdentity &source)
{
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(source.cacheKey, hex);
    return std::string(hex, db::graph_hash::kHexDigestBytes - 1) + ".dsc";
}

} // namespace db::shader_cache
