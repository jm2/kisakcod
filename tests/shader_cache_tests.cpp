// Contract tests for the content-addressed derived shader cache (ki-y49k).
//
// These tests pin the policy from docs/ROADMAP_EXPANSION_PROPOSAL.md (A09)
// and docs/PORTING.md: original bytecode is identified by content hash, the
// sidecar is versioned by format and converter identity, stale or corrupt
// evidence regenerates from the original inputs, and unsupported programs are
// rejected rather than cached.

#include "database/shader_cache.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
int g_failures;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

using db::shader_cache::DerivedArtifactKind;
using db::shader_cache::Digest;
using db::shader_cache::LookupResult;
using db::shader_cache::SidecarHeader;
using db::shader_cache::SourceIdentity;
using db::shader_cache::Stage;

// Bounded SM2 vertex bytecode (shader model 2, renderer 0).
const std::uint32_t kVertexSm2[] = {
    UINT32_C(0xFFFE0200),
    UINT32_C(0x02000001),
    UINT32_C(0x800F0000),
    UINT32_C(0x90E40000),
    UINT32_C(0x0000FFFF)};

// A second, distinct valid SM2 vertex program.
const std::uint32_t kOtherVertexSm2[] = {
    UINT32_C(0xFFFE0200),
    UINT32_C(0x0000FFFF)};

// Bounded SM3 pixel bytecode (shader model 3, renderer 1) with a comment.
const std::uint32_t kPixelSm3[] = {
    UINT32_C(0xFFFF0300),
    UINT32_C(0x0001FFFE),
    UINT32_C(0x0000FFFF),
    UINT32_C(0x0000FFFF)};

// Serialized field offsets, pinned so a framing change is caught here as well
// as by the static size assertion.
constexpr std::size_t kOffsetFormatVersion = 8;
constexpr std::size_t kOffsetConverterVersion = 12;
constexpr std::size_t kOffsetLoadForRenderer = 20;
constexpr std::size_t kOffsetArtifactKind = 60;
constexpr std::size_t kOffsetArtifactSize = 64;
constexpr std::size_t kOffsetArtifactHash = 68;

void WriteU32At(std::vector<std::uint8_t> &bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

bool IdentityFor(const std::uint32_t *program,
    const std::uint32_t count,
    const Stage stage,
    const std::uint32_t loadForRenderer,
    SourceIdentity &out)
{
    return db::shader_cache::MakeSourceIdentity(
        program, count, stage, loadForRenderer, out);
}

LookupResult LookupWithSidecar(const std::vector<std::uint8_t> &sidecar,
    const std::uint32_t *program,
    const std::uint32_t count,
    const Stage stage,
    const std::uint32_t loadForRenderer,
    std::vector<std::uint8_t> &outArtifact)
{
    return db::shader_cache::LookupDerivedShader(sidecar.empty() ? nullptr : sidecar.data(),
        sidecar.size(),
        program,
        count,
        stage,
        loadForRenderer,
        outArtifact);
}

void TestSourceIdentity()
{
    SourceIdentity first;
    SourceIdentity again;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, first),
        "valid SM2 vertex program mints an identity");
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, again),
        "identity is idempotent");
    Expect(first.contentHash == again.contentHash
            && first.cacheKey == again.cacheKey,
        "identical inputs produce identical content hash and cache key");
    Expect(first.contentHash == db::shader_cache::HashSourceProgram(kVertexSm2, 5),
        "identity content hash matches the one-shot content checksum");
    Expect(first.programDwordCount == 5 && first.loadForRenderer == 0,
        "identity records the source size and renderer");

    SourceIdentity other;
    Expect(IdentityFor(kOtherVertexSm2, 2, Stage::Vertex, 0, other),
        "a second valid vertex program mints an identity");
    Expect(first.contentHash != other.contentHash,
        "different bytecode produces a different content hash");
    Expect(first.cacheKey != other.cacheKey,
        "different bytecode produces a different cache key");

    SourceIdentity pixel;
    Expect(IdentityFor(kPixelSm3, 4, Stage::Pixel, 1, pixel),
        "valid SM3 pixel program mints an identity");
    Expect(pixel.contentHash != first.contentHash,
        "a different stage/program produces a different content hash");
}

void TestSourceValidation()
{
    Expect(db::shader_cache::ValidateSourceProgram(kVertexSm2, 5, Stage::Vertex, 0),
        "SM2 vertex bytecode validates for the vertex stage");
    Expect(!db::shader_cache::ValidateSourceProgram(kVertexSm2, 5, Stage::Pixel, 0),
        "vertex bytecode is rejected for the pixel stage");
    Expect(!db::shader_cache::ValidateSourceProgram(kVertexSm2, 5, Stage::Vertex, 1),
        "SM2 bytecode is rejected for renderer 1");
    Expect(!db::shader_cache::ValidateSourceProgram(nullptr, 0, Stage::Vertex, 0),
        "a null program is rejected");

    const std::uint32_t missingEnd[] = {UINT32_C(0xFFFE0200), UINT32_C(0x00000000)};
    Expect(!db::shader_cache::ValidateSourceProgram(missingEnd, 2, Stage::Vertex, 0),
        "a malformed program is rejected");

    SourceIdentity identity;
    Expect(!IdentityFor(missingEnd, 2, Stage::Vertex, 0, identity),
        "an unsupported program cannot mint an identity");
}

void TestUnsupportedProgramLookup()
{
    SourceIdentity identity;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, identity),
        "supported identity for the unsupported-source case");
    const std::vector<std::uint8_t> sidecar =
        db::shader_cache::BuildSidecar(identity, DerivedArtifactKind::SpirV, "abc", 3);
    Expect(!sidecar.empty(), "sidecar builds for the supported identity");

    const std::uint32_t badProgram[] = {UINT32_C(0xFFFE0400), UINT32_C(0x0000FFFF)};
    std::vector<std::uint8_t> artifact;
    Expect(LookupWithSidecar(sidecar, badProgram, 2, Stage::Vertex, 0, artifact)
            == LookupResult::UnsupportedSource,
        "unsupported source is rejected before any cache evidence is trusted");
    Expect(artifact.empty(), "unsupported source yields no artifact");
}

void TestMissingSidecar()
{
    std::vector<std::uint8_t> artifact;
    Expect(LookupWithSidecar({}, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::Missing,
        "no sidecar bytes reports Missing");
    Expect(artifact.empty(), "a missing sidecar yields no artifact");
}

void TestSidecarRoundTrip()
{
    SourceIdentity identity;
    Expect(IdentityFor(kPixelSm3, 4, Stage::Pixel, 1, identity),
        "SM3 pixel identity for round trip");

    const std::vector<std::uint8_t> derived = {0x03, 0x02, 0x23, 0x07, 0xAA};
    const std::vector<std::uint8_t> sidecar = db::shader_cache::BuildSidecar(
        identity, DerivedArtifactKind::SpirV, derived.data(), derived.size());
    Expect(sidecar.size() == db::shader_cache::kSidecarHeaderBytes + derived.size(),
        "sidecar is header plus payload");

    SidecarHeader header;
    Expect(db::shader_cache::ParseSidecarHeader(sidecar.data(), sidecar.size(), header),
        "a freshly built sidecar parses");
    Expect(header.formatVersion == db::shader_cache::kFormatVersion
            && header.converterVersion == db::shader_cache::kConverterVersion,
        "sidecar records the current format and converter versions");
    Expect(header.stage == Stage::Pixel && header.loadForRenderer == 1,
        "sidecar records stage and renderer");
    Expect(header.sourceDwordCount == 4 && header.sourceHash == identity.contentHash,
        "sidecar records the original size and content checksum");
    Expect(header.artifactKind == DerivedArtifactKind::SpirV
            && header.artifactSize == derived.size(),
        "sidecar records the derived kind and size");
    Expect(header.artifactHash == db::shader_cache::HashArtifact(derived.data(), derived.size()),
        "sidecar records the derived artifact hash");

    std::vector<std::uint8_t> artifact;
    Expect(LookupWithSidecar(sidecar, kPixelSm3, 4, Stage::Pixel, 1, artifact)
            == LookupResult::Hit,
        "a matching sidecar is a cache hit");
    Expect(artifact == derived, "a cache hit returns the exact derived bytes");

    // Explicit header serialization must round-trip every field.
    std::uint8_t serialized[db::shader_cache::kSidecarHeaderBytes];
    Expect(db::shader_cache::SerializeSidecarHeader(header, serialized, sizeof(serialized)),
        "header serializes into an exact-size buffer");
    SidecarHeader reparsed;
    Expect(db::shader_cache::ParseSidecarHeader(serialized, sizeof(serialized), reparsed),
        "serialized header reparses");
    Expect(reparsed.formatVersion == header.formatVersion
            && reparsed.converterVersion == header.converterVersion
            && reparsed.stage == header.stage
            && reparsed.loadForRenderer == header.loadForRenderer
            && reparsed.sourceDwordCount == header.sourceDwordCount
            && reparsed.sourceHash == header.sourceHash
            && reparsed.artifactKind == header.artifactKind
            && reparsed.artifactSize == header.artifactSize
            && reparsed.artifactHash == header.artifactHash,
        "header round trip preserves every field");
}

void TestVersionInvalidation()
{
    SourceIdentity identity;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, identity),
        "identity for version invalidation");

    const std::vector<std::uint8_t> derived = {0x11, 0x22};
    std::vector<std::uint8_t> sidecar = db::shader_cache::BuildSidecar(
        identity, DerivedArtifactKind::SpirV, derived.data(), derived.size());

    std::vector<std::uint8_t> formatBumped = sidecar;
    WriteU32At(formatBumped, kOffsetFormatVersion,
        db::shader_cache::kFormatVersion + 1);
    std::vector<std::uint8_t> artifact;
    Expect(LookupWithSidecar(formatBumped, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "a format version bump forces regeneration");

    std::vector<std::uint8_t> converterBumped = sidecar;
    WriteU32At(converterBumped, kOffsetConverterVersion,
        db::shader_cache::kConverterVersion + 1);
    Expect(LookupWithSidecar(converterBumped, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "a converter version bump forces regeneration");
}

void TestIdentityMismatchRegeneration()
{
    SourceIdentity identity;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, identity),
        "identity for mismatch regeneration");

    const std::vector<std::uint8_t> derived = {0x0A, 0x0B, 0x0C};
    const std::vector<std::uint8_t> sidecar = db::shader_cache::BuildSidecar(
        identity, DerivedArtifactKind::SpirV, derived.data(), derived.size());

    std::vector<std::uint8_t> artifact;
    Expect(LookupWithSidecar(sidecar, kOtherVertexSm2, 2, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "changed source bytecode forces regeneration");

    // Same payload, but the header claims renderer 0 while the source is a
    // renderer-1 program: the identity no longer matches.
    std::vector<std::uint8_t> rendererMismatch = sidecar;
    WriteU32At(rendererMismatch, kOffsetLoadForRenderer, 0);
    SourceIdentity pixel;
    Expect(IdentityFor(kPixelSm3, 4, Stage::Pixel, 1, pixel),
        "renderer-1 identity for mismatch case");
    std::vector<std::uint8_t> pixelSidecar = db::shader_cache::BuildSidecar(
        pixel, DerivedArtifactKind::SpirV, derived.data(), derived.size());
    WriteU32At(pixelSidecar, kOffsetLoadForRenderer, 0);
    Expect(LookupWithSidecar(pixelSidecar, kPixelSm3, 4, Stage::Pixel, 1, artifact)
            == LookupResult::NeedsRegeneration,
        "a renderer mismatch forces regeneration");
}

void TestCorruptSidecarRegeneration()
{
    SourceIdentity identity;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, identity),
        "identity for corruption cases");

    const std::vector<std::uint8_t> derived = {0x51, 0x52, 0x53, 0x54};
    const std::vector<std::uint8_t> sidecar = db::shader_cache::BuildSidecar(
        identity, DerivedArtifactKind::SpirV, derived.data(), derived.size());

    std::vector<std::uint8_t> flipped = sidecar;
    flipped.back() ^= 0x5A;
    std::vector<std::uint8_t> artifact;
    Expect(LookupWithSidecar(flipped, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "a flipped payload byte forces regeneration");

    std::vector<std::uint8_t> zeroedHash = sidecar;
    std::uint8_t zeroDigest[db::graph_hash::kDigestBytes] = {};
    std::memcpy(zeroedHash.data() + kOffsetArtifactHash, zeroDigest, sizeof(zeroDigest));
    Expect(LookupWithSidecar(zeroedHash, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "a wrong artifact hash forces regeneration");

    std::vector<std::uint8_t> truncated = sidecar;
    truncated.pop_back();
    Expect(LookupWithSidecar(truncated, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "a truncated payload forces regeneration");

    std::vector<std::uint8_t> extended = sidecar;
    extended.push_back(0xFF);
    Expect(LookupWithSidecar(extended, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "trailing bytes force regeneration");

    std::vector<std::uint8_t> unknownKind = sidecar;
    WriteU32At(unknownKind, kOffsetArtifactKind, 99);
    Expect(LookupWithSidecar(unknownKind, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "an unknown artifact kind forces regeneration");
}

void TestParseRejectsGarbage()
{
    SidecarHeader header;
    Expect(!db::shader_cache::ParseSidecarHeader(nullptr, 0, header),
        "a null buffer is rejected");
    std::vector<std::uint8_t> zeros(db::shader_cache::kSidecarHeaderBytes, 0);
    Expect(!db::shader_cache::ParseSidecarHeader(
               zeros.data(), zeros.size() - 1, header),
        "a truncated header is rejected");
    Expect(!db::shader_cache::ParseSidecarHeader(zeros.data(), zeros.size(), header),
        "a bad magic is rejected");

    SourceIdentity identity;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, identity),
        "identity for garbage parsing");
    std::vector<std::uint8_t> sidecar = db::shader_cache::BuildSidecar(
        identity, DerivedArtifactKind::SpirV, "xy", 2);
    WriteU32At(sidecar, 16, 7); // stage
    Expect(!db::shader_cache::ParseSidecarHeader(sidecar.data(), sidecar.size(), header),
        "an unknown stage is rejected");

    std::vector<std::uint8_t> unsetKind = db::shader_cache::BuildSidecar(
        identity, DerivedArtifactKind::SpirV, "xy", 2);
    WriteU32At(unsetKind, kOffsetArtifactKind, 0);
    Expect(!db::shader_cache::ParseSidecarHeader(unsetKind.data(), unsetKind.size(), header),
        "an unset artifact kind is rejected");
}

void TestNaming()
{
    const std::string directory = db::shader_cache::CacheDirectoryName();
    Expect(directory.find("format1") != std::string::npos,
        "cache directory embeds the format version");
    Expect(directory.find("converter1") != std::string::npos,
        "cache directory embeds the converter version");

    SourceIdentity first;
    SourceIdentity second;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, first),
        "first identity for naming");
    Expect(IdentityFor(kOtherVertexSm2, 2, Stage::Vertex, 0, second),
        "second identity for naming");

    const std::string name = db::shader_cache::SidecarFileName(first);
    Expect(name.size() == db::graph_hash::kHexDigestBytes - 1 + 4,
        "sidecar name is 64 hex characters plus the extension");
    Expect(name.compare(name.size() - 4, 4, ".dsc") == 0,
        "sidecar name ends in .dsc");
    bool allHex = true;
    for (std::size_t i = 0; i + 4 < name.size(); ++i)
    {
        const char c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            allHex = false;
    }
    Expect(allHex, "sidecar name body is lowercase hex");
    Expect(db::shader_cache::SidecarFileName(first) == name,
        "sidecar name is deterministic");
    Expect(db::shader_cache::SidecarFileName(second) != name,
        "distinct identities name distinct sidecars");
}

void TestBuildGuards()
{
    SourceIdentity identity;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, identity),
        "identity for build guards");
    Expect(db::shader_cache::BuildSidecar(identity, DerivedArtifactKind::SpirV, nullptr, 3).empty(),
        "a null artifact is rejected");
    Expect(db::shader_cache::BuildSidecar(identity, DerivedArtifactKind::SpirV, "a", 0).empty(),
        "an empty artifact is rejected");
}

void TestDigestWidthIndependence()
{
    // The content checksum hashes dwords field-by-field rather than raw
    // memory, so it is defined the same way regardless of host layout.
    const std::uint32_t program[] = {UINT32_C(0xFFFE0200), UINT32_C(0x0000FFFF)};
    const Digest first = db::shader_cache::HashSourceProgram(program, 2);
    const Digest second = db::shader_cache::HashSourceProgram(program, 2);
    Expect(first == second, "content checksum is deterministic");
    Expect(db::shader_cache::HashArtifact("abc", 3) == db::shader_cache::HashArtifact("abc", 3),
        "artifact hash is deterministic");
    Expect(db::shader_cache::HashArtifact("abc", 3) != db::shader_cache::HashArtifact("abd", 3),
        "artifact hash is sensitive to content");
}

void TestSidecarSizeBoundary()
{
    // The full sidecar (header + payload) must stay representable in
    // std::size_t. On ILP32, kSidecarHeaderBytes + a u32-sized payload wraps,
    // so the guard has to fire before any buffer is sized or the payload is
    // read. These cases are allocation-free: a one-dword dummy stands in for
    // the payload, and a guard failure would be observable as a non-empty
    // result (or a read past the dummy) rather than as a huge allocation.
    const std::size_t maxSize = (std::numeric_limits<std::size_t>::max)();
    Expect(db::shader_cache::SidecarTotalSizeRepresentable(0),
        "an empty payload is representable");
    Expect(db::shader_cache::SidecarTotalSizeRepresentable(
               maxSize - db::shader_cache::kSidecarHeaderBytes),
        "the largest representable payload is accepted");
    Expect(!db::shader_cache::SidecarTotalSizeRepresentable(
               maxSize - db::shader_cache::kSidecarHeaderBytes + 1),
        "one byte past the representable payload is rejected");
    Expect(!db::shader_cache::SidecarTotalSizeRepresentable(maxSize),
        "an unrepresentable payload is rejected");

    SourceIdentity identity;
    Expect(IdentityFor(kVertexSm2, 5, Stage::Vertex, 0, identity),
        "identity for the size-boundary build");
    const std::uint32_t dummy = 0;
    Expect(db::shader_cache::BuildSidecar(identity, DerivedArtifactKind::SpirV,
               &dummy, maxSize).empty(),
        "a SIZE_MAX payload is rejected without allocation");
#if SIZE_MAX > UINT32_MAX
    Expect(db::shader_cache::BuildSidecar(identity, DerivedArtifactKind::SpirV,
               &dummy, static_cast<std::size_t>(UINT32_MAX) + 1).empty(),
        "a payload above the u32 framing limit is rejected");
#endif

    // A recorded payload size that cannot be represented must classify as
    // regeneration instead of being trusted into the size computation.
    const std::vector<std::uint8_t> derived = {0x01};
    std::vector<std::uint8_t> oversized = db::shader_cache::BuildSidecar(
        identity, DerivedArtifactKind::SpirV, derived.data(), derived.size());
    WriteU32At(oversized, kOffsetArtifactSize,
        (std::numeric_limits<std::uint32_t>::max)());
    std::vector<std::uint8_t> artifact;
    Expect(LookupWithSidecar(oversized, kVertexSm2, 5, Stage::Vertex, 0, artifact)
            == LookupResult::NeedsRegeneration,
        "an unrepresentable recorded size forces regeneration");
    Expect(artifact.empty(), "an unrepresentable recorded size yields no artifact");
}

} // namespace

int main()
{
    TestSourceIdentity();
    TestSourceValidation();
    TestUnsupportedProgramLookup();
    TestMissingSidecar();
    TestSidecarRoundTrip();
    TestVersionInvalidation();
    TestIdentityMismatchRegeneration();
    TestCorruptSidecarRegeneration();
    TestParseRejectsGarbage();
    TestNaming();
    TestBuildGuards();
    TestDigestWidthIndependence();
    TestSidecarSizeBoundary();

    if (g_failures > 0)
    {
        std::fprintf(stderr, "shader_cache: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("shader_cache: all checks passed\n");
    return 0;
}
