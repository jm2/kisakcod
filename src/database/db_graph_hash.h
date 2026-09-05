#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace db::graph_hash
{
inline constexpr std::size_t kDigestBytes = 32;
inline constexpr std::size_t kHexDigestBytes = kDigestBytes * 2 + 1;

using Digest = std::array<std::uint8_t, kDigestBytes>;

namespace detail
{
// Byte-wise SHA-256 core. All loads/stores go through explicit byte math so
// digests are identical on any host byte order and struct layout. Exposed in
// detail so tests and simple one-shot helpers can share the block core.
struct Sha256Core
{
    std::uint32_t state[8];
    std::uint64_t bitLength;
    std::uint8_t block[64];
    std::size_t blockUsed;

    void Init() noexcept;
    void Update(const std::uint8_t *bytes, std::size_t size) noexcept;
    // Finalizes into digest and returns to the initialized state.
    void Finish(Digest &digest) noexcept;
};
} // namespace detail

// M5 widened-runtime-graph parity hash (ki-msb).
//
// The M5 exit loads an unmodified retail fast-file on native64 and hash-matches
// the widened runtime graph against the Windows x86 reference. The two hosts
// must therefore derive one identical digest from two different in-memory
// representations (32-bit reference graph, widened 64-bit graph).
//
// The capture contract makes that possible by construction:
//   * The digest is a domain-separated SHA-256 over a canonical, framed,
//     typed, little-endian byte stream.
//   * Callers never feed raw pointers, padding, or host-order integers.
//     Graph topology enters through canonical record ordering and named
//     references; pointer-bearing values are excluded from the stream.
//   * Every element carries an explicit tag and explicit width, so a 32-bit
//     reference walker and a 64-bit widened walker that agree on logical
//     values produce byte-identical streams.
//
// Version the domain tag whenever the framing or field semantics change; the
// parity driver refuses to compare digests from different domains.
// v2: envelope captures cover the FULL fast-file content (the v1 envelope
// minted only a bounded 1 MiB prefix probe, which let same-size assets
// collide on graph_sha256). v1 references cannot match v2 captures: the
// domain tag differs and capture_kind moved from envelope-v1 to envelope-v2;
// the byte-level framing itself is unchanged.
inline constexpr char kHashDomain[] = "kisakcod/m5-widened-graph-hash/v2";

// Maximum canonical record nesting depth. The loader graph walk nests per
// asset/per sub-structure; 64 leaves ample headroom while bounding the
// in-builder stack.
inline constexpr std::size_t kMaxRecordDepth = 64;

// Streaming canonical builder over SHA-256. All methods are noexcept; misuse
// (unbalanced records, depth overflow) flips the builder into an invalid
// state that callers must check, but never crashes and stays deterministic.
class GraphHashBuilder
{
public:
    GraphHashBuilder() noexcept;

    // Re-initializes to the fresh state (domain absorbed, records closed).
    void Reset() noexcept;

    // Opens a typed record. Records nest; every BeginRecord must be matched
    // by exactly one EndRecord before Finish.
    void BeginRecord(std::uint32_t typeTag) noexcept;
    void EndRecord() noexcept;

    // Canonical fixed-width fields. Tag namespaces belong to the walker.
    void FieldU64(std::uint32_t tag, std::uint64_t value) noexcept;
    void FieldI64(std::uint32_t tag, std::int64_t value) noexcept;
    // Bit-exact IEEE-754 binary32. NaN payloads are canonicalized to the
    // single quiet NaN 0x7fc00000 so x87/SSE NaN generation differences
    // cannot split otherwise-identical graphs. Signed zero is preserved:
    // +/-0 are logically distinct for parity purposes.
    void FieldF32(std::uint32_t tag, float value) noexcept;
    // Length-prefixed raw bytes (explicit size prefix disambiguates
    // concatenations).
    void FieldBytes(std::uint32_t tag, const void *data, std::size_t size) noexcept;
    // Length-prefixed UTF-8 text without the terminator. A null pointer is
    // canonicalized to the empty string.
    void FieldString(std::uint32_t tag, const char *text) noexcept;

    // Finalizes and returns the digest, then re-initializes the stream so
    // the builder is immediately reusable (no explicit Reset required). A
    // misused stream still finalizes deterministically. The misuse verdict
    // REMAINS OBSERVABLE after Finish: a stream that finished with records
    // still open — or that saw any earlier misuse — keeps Valid() == false
    // across Finish; only an explicit Reset() restores the fresh-stream
    // valid state. Check Valid() after Finish to detect misuse.
    Digest Finish() noexcept;

    // False when the current or most recently finished stream saw misuse:
    // EndRecord without BeginRecord, record depth above kMaxRecordDepth, or
    // records still open at Finish time. Reset() restores true; Finish()
    // preserves the verdict of the stream it finished so misuse can never
    // be silently laundered by the implicit post-Finish re-initialization.
    bool Valid() const noexcept { return m_valid; }

private:
    // Re-initializes the hash core to the fresh stream state: empty record
    // stack and the domain preamble absorbed. Does not touch m_valid.
    void InitStream() noexcept;

    // Charges bytes to both the hash core and the innermost open record's
    // framing length.
    void Absorb(const std::uint8_t *bytes, std::size_t size) noexcept;
    void AbsorbTagged(std::uint8_t marker, std::uint32_t tag) noexcept;
    void AbsorbU64(std::uint64_t value) noexcept;

    detail::Sha256Core m_core;
    // Per-open-record payload byte counters (framing lengths).
    std::uint64_t m_recordLengths[kMaxRecordDepth];
    std::size_t m_recordDepth;
    bool m_valid;
};

// Hex-encodes a digest into out (kHexDigestBytes capacity incl. NUL).
void FormatDigestHex(const Digest &digest, char *out) noexcept;

// One-shot SHA-256 over a byte span (used by tests and simple captures).
Digest HashBytes(const void *data, std::size_t size) noexcept;

} // namespace db::graph_hash
