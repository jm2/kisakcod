#pragma once

// Content-addressed derived shader cache / sidecar (ki-y49k / issue #131).
//
// Roadmap A09 and docs/PORTING.md require an explicit shader policy:
//   * load original fast-files unchanged;
//   * identify bytecode by content hash;
//   * translate/cache without rewriting the user's retail archive;
//   * version the translator and the cache format;
//   * regenerate from the original inputs;
//   * reject unsupported programs clearly.
//
// This header is the platform-neutral core of that policy. It contains no
// D3D9/Win32 types and performs no file I/O, so it compiles and is tested on
// the Linux host while the Windows renderer enrolls it at the D3D9 shader
// creation boundary. The original retail bytecode is never rewritten and
// never copied into an archive: callers only read it, mint a content address
// from it, and store a derived artifact beside it under a versioned directory.
//
// The derived artifact itself (SPIR-V today) is produced by the translator at
// the platform boundary. This core owns identity, versioning, framing and
// verification; bumping kConverterVersion is what forces regeneration.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "database/db_graph_hash.h"
#include "database/db_validation.h"

namespace db::shader_cache
{
// Domain separation for every digest in this module. Bump whenever the hashed
// framing changes so digests from different definitions can never compare
// equal by accident.
inline constexpr char kHashDomain[] = "kisakcod/derived-shader-cache/v1";

// On-disk sidecar framing version. Bump when any serialized field changes
// width, order, or meaning; older sidecars then report NeedsRegeneration.
inline constexpr std::uint32_t kFormatVersion = 1;

// Translator identity. Bump whenever original-bytecode -> derived-artifact
// translation changes. Stale artifacts are regenerated from the untouched
// original inputs rather than silently reused.
inline constexpr std::uint32_t kConverterVersion = 1;

// Eight-byte magic so a truncated or foreign file is rejected before any
// offset is trusted. The trailing digit is part of the magic, not a version.
inline constexpr std::uint8_t kSidecarMagic[8] =
    {'K', 'S', 'D', 'S', 'H', 'D', 'R', '1'};

// Human-facing message for the UnsupportedSource result. Callers translate
// this into the engine's own drop path (e.g. Com_Error(ERR_DROP, ...)).
inline constexpr char kUnsupportedProgramMessage[] =
    "derived shader cache: unsupported D3D9 shader program (not SM2/SM3 bytecode)";

using Digest = db::graph_hash::Digest;
using Stage = db::validation::D3D9ShaderStage;

// The only derived artifact kind defined today. Kept explicit and versioned so
// a future SPIR-V-via-vkd3d path, a dxvk-native differential aid, or a
// fallback capture is a distinct kind rather than a silent reinterpretation of
// existing sidecars.
enum class DerivedArtifactKind : std::uint32_t
{
    SpirV = 1,
};

// Outcome of a lookup. Missing and NeedsRegeneration are deliberately
// distinct: Missing means "no usable sidecar on disk, translate and store";
// NeedsRegeneration means "a sidecar exists but is stale or corrupt, rebuild
// it from the original bytecode".
enum class LookupResult : std::uint32_t
{
    Hit = 0,
    Missing = 1,
    NeedsRegeneration = 2,
    UnsupportedSource = 3,
};

const char *LookupResultName(LookupResult result) noexcept;

// Identity of an original (retail fast-file) shader program. The raw bytes are
// never stored in the key; only canonical, width-explicit digests are.
struct SourceIdentity
{
    Stage stage = Stage::Vertex;
    std::uint32_t loadForRenderer = 0;
    std::uint32_t programDwordCount = 0;
    // Checksum of the original bytecode dwords, retained for regeneration and
    // for detecting a changed input. Stage- and renderer-neutral.
    Digest contentHash{};
    // Domain-separated digest of the full identity (stage + renderer + size +
    // contentHash). This is the sidecar file name.
    Digest cacheKey{};
};

// Fixed sidecar header. Serialized little-endian, field by field, so the
// layout is identical on any host. Payload (the derived artifact) follows the
// header immediately.
struct SidecarHeader
{
    std::uint32_t formatVersion = 0;
    std::uint32_t converterVersion = 0;
    Stage stage = Stage::Vertex;
    std::uint32_t loadForRenderer = 0;
    std::uint32_t sourceDwordCount = 0;
    Digest sourceHash{};
    DerivedArtifactKind artifactKind = DerivedArtifactKind::SpirV;
    std::uint32_t artifactSize = 0;
    Digest artifactHash{};
};

// 8 magic + 5 x u32 + 32 sourceHash + u32 kind + u32 size + 32 artifactHash.
inline constexpr std::size_t kSidecarHeaderBytes = 100;
static_assert(kSidecarHeaderBytes == 8 + 5 * 4
        + db::graph_hash::kDigestBytes + 4 + 4
        + db::graph_hash::kDigestBytes,
    "sidecar header size must match the serialized framing");

// Validates the original D3D9 bytecode for the given stage and renderer.
bool ValidateSourceProgram(const std::uint32_t *program,
    std::uint32_t dwordCount,
    Stage stage,
    std::uint32_t loadForRenderer) noexcept;

// SHA-256 content checksum of the original bytecode dwords, hashed
// little-endian and width-explicit so it is host independent.
Digest HashSourceProgram(const std::uint32_t *program,
    std::uint32_t dwordCount) noexcept;

// Domain-separated digest of a derived artifact's bytes.
Digest HashArtifact(const void *artifact, std::size_t artifactSize) noexcept;

// Validates the program and fills outIdentity (contentHash and cacheKey). A
// false return means the program must be rejected clearly, not cached.
bool MakeSourceIdentity(const std::uint32_t *program,
    std::uint32_t dwordCount,
    Stage stage,
    std::uint32_t loadForRenderer,
    SourceIdentity &outIdentity) noexcept;

// True when the header's format/converter/kind/identity still describe
// outIdentity. Does not inspect the payload.
bool SidecarHeaderMatchesSource(const SidecarHeader &header,
    const SourceIdentity &source) noexcept;

// Explicit little-endian serialization. Returns false on a null/undersized
// destination.
bool SerializeSidecarHeader(const SidecarHeader &header,
    std::uint8_t *outBytes,
    std::size_t outSize) noexcept;

// Parses a fixed header. Returns false only for truncation or bad magic;
// version/kind mismatches parse successfully and are classified by the lookup.
bool ParseSidecarHeader(const std::uint8_t *bytes,
    std::size_t size,
    SidecarHeader &outHeader) noexcept;

// Builds a complete sidecar (header + payload). Returns an empty vector when
// the artifact is empty, null, or too large for the u32 framing.
std::vector<std::uint8_t> BuildSidecar(const SourceIdentity &source,
    DerivedArtifactKind kind,
    const void *artifact,
    std::size_t artifactSize);

// Full lookup. On Hit, outArtifact receives the verified derived bytes. On
// UnsupportedSource the caller must reject the program; on Missing or
// NeedsRegeneration the caller translates from the original inputs and stores
// a fresh sidecar.
LookupResult LookupDerivedShader(const std::uint8_t *sidecarBytes,
    std::size_t sidecarSize,
    const std::uint32_t *sourceProgram,
    std::uint32_t sourceDwordCount,
    Stage stage,
    std::uint32_t loadForRenderer,
    std::vector<std::uint8_t> &outArtifact);

// Versioned cache directory, relative to the game root. A format or
// converter bump lands in a new directory, so a version change invalidates
// cleanly without touching any retail archive.
std::string CacheDirectoryName();

// Sidecar file name for an identity: 64 hex characters plus the .dsc suffix.
std::string SidecarFileName(const SourceIdentity &source);

} // namespace db::shader_cache
