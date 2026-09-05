// M5 retail fast-file parity harness (ki-msb).
//
// Loads an unmodified retail fast-file from disk on the running host
// (native64 linux-amd64 for the host leg; the same executable built by the
// Windows x86 reference tree mints the reference leg) and emits the
// domain-separated widened-graph capture digest through the canonical
// db::graph_hash stream.
//
// Output protocol (stable; consumed by scripts/ci/run-retail-fastfile-parity.sh):
//   capture_kind=envelope-v2
//   hash_domain=kisakcod/m5-widened-graph-hash/v2
//   fastfile_bytes=<decimal size>
//   fastfile_zlib_stream=<0|1>
//   graph_sha256=<64 hex characters>
//
// capture_kind envelope-v2 covers the fast-file envelope identity: size,
// zlib stream header bits, and the FULL file content. (The v1 envelope
// minted only a bounded 1 MiB prefix probe, which let same-size assets
// whose tails differ collide on graph_sha256; v2 hashes every byte and
// bumps both the capture kind and the hash domain so v1 references can
// never silently match v2 captures.) The full widened runtime-graph walk
// (capture_kind graph-v1) enrolls through this identical harness contract
// and output protocol as the native64 loader path lands; the parity driver
// refuses to compare different capture kinds, so the contract cannot
// silently drift.
//
// Self-test mode synthesizes fixture fast-files in-process and verifies the
// capture pipeline end-to-end (determinism, pointer-independence across
// separately allocated reads, size sensitivity, zlib detection, and output
// formatting) without requiring any retail asset, so CI exercises the same
// instrument the retail gate runs.

#include "database/db_graph_hash.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{
constexpr std::uint32_t kEnvelopeRecordTag = 0x454E5632u; // "2VNE"

int g_failures;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

bool DetectZlibStream(const std::uint8_t *data, const std::size_t size)
{
    // zlib stream header: CMF/FLG. CM must be 8 (deflate) and the two-byte
    // header must be a multiple of 31. Retail fast-files begin with a raw
    // zlib stream (DB_AuthLoad inflate path).
    if (size < 2)
        return false;
    const std::uint32_t cmf = data[0];
    const std::uint32_t flg = data[1];
    return (cmf & 0x0Fu) == 0x8u && ((cmf << 8) | flg) % 31u == 0u;
}

// File I/O goes through C++ file streams: RAII closes the descriptor on
// every path, and no C stdio handle is held across the capture.
bool ReadFileBytes(const char *path, std::vector<std::uint8_t> &out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    out.clear();
    char chunk[64 * 1024];
    for (;;)
    {
        file.read(chunk, static_cast<std::streamsize>(sizeof(chunk)));
        const std::streamsize got = file.gcount();
        if (got > 0)
            out.insert(out.end(), chunk, chunk + got);
        if (got < static_cast<std::streamsize>(sizeof(chunk)))
            return !file.bad();
    }
}

// The envelope-v2 capture: canonical graph-hash stream over the fast-file
// envelope AND the full file content. `probe` may be a re-allocated copy of
// `bytes`; the digest must not change (pointer-independence of the capture
// contract).
db::graph_hash::Digest CaptureEnvelope(
    const std::uint8_t *bytes,
    const std::size_t size)
{
    db::graph_hash::GraphHashBuilder builder;
    builder.BeginRecord(kEnvelopeRecordTag);
    builder.FieldU64(1, static_cast<std::uint64_t>(size));
    builder.FieldU64(2, DetectZlibStream(bytes, size) ? 1u : 0u);
    // Every byte of the asset participates in the digest: a bounded prefix
    // probe would let same-size assets whose tails differ share a digest.
    builder.FieldBytes(3, bytes, size);
    builder.EndRecord();
    if (!builder.Valid())
    {
        std::fprintf(stderr,
            "error: envelope capture stream misuse (internal contract bug)\n");
        std::exit(2);
    }
    return builder.Finish();
}

void EmitCapture(
    const char *path,
    const std::vector<std::uint8_t> &bytes,
    const db::graph_hash::Digest &digest)
{
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(digest, hex);
    std::printf("capture_kind=envelope-v2\n");
    std::printf("hash_domain=%s\n", db::graph_hash::kHashDomain);
    std::printf("fastfile_path=%s\n", path);
    std::printf("fastfile_bytes=%zu\n", bytes.size());
    std::printf("fastfile_zlib_stream=%d\n", DetectZlibStream(bytes.data(), bytes.size()) ? 1 : 0);
    std::printf("graph_sha256=%s\n", hex);
}

int RunCapture(const char *path)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadFileBytes(path, bytes))
    {
        std::fprintf(stderr, "error: cannot read fast-file '%s'\n", path);
        return 2;
    }
    if (bytes.empty())
    {
        std::fprintf(stderr, "error: fast-file '%s' is empty\n", path);
        return 2;
    }

    // Independent allocations on purpose: the digest must be a function of
    // the bytes, never of their address.
    const db::graph_hash::Digest digest = CaptureEnvelope(bytes.data(), bytes.size());
    const std::vector<std::uint8_t> copy = bytes;
    const db::graph_hash::Digest digestFromCopy = CaptureEnvelope(copy.data(), copy.size());
    if (digest != digestFromCopy)
    {
        std::fprintf(stderr,
            "error: envelope capture is allocation-dependent (internal contract bug)\n");
        return 2;
    }

    EmitCapture(path, bytes, digest);
    return 0;
}

struct Lcg
{
    std::uint32_t state = 0x12345678u;
    std::uint8_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<std::uint8_t>(state >> 24);
    }
};

bool WriteFileBytes(const char *path, const std::uint8_t *data, const std::size_t size)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char *>(data),
        static_cast<std::streamsize>(size));
    const bool ok = file.good();
    file.close();
    return ok;
}

// Synthesizes a fixture fast-file: zlib header + deterministic payload. The
// payload spans well past the old v1 1 MiB probe window so the full-content
// contract is exercised where v1 was blind.
std::vector<std::uint8_t> SynthesizeZlibFixture()
{
    const std::size_t payloadSize = (1536u * 1024u) + 64u;
    std::vector<std::uint8_t> fixture(payloadSize);
    fixture[0] = 0x78; // CM=8 deflate, 32K window
    fixture[1] = 0x9C; // FLG making (0x789C % 31) == 0
    Lcg lcg;
    for (std::size_t i = 2; i < payloadSize; ++i)
        fixture[i] = lcg.Next();
    return fixture;
}

// Size and full-content sensitivity: one appended byte moves the digest, and
// so does a change INSIDE the former probe window ... and BEYOND it (the v1
// envelope was blind there; same-size assets could share a digest).
void ExpectDigestSensitivity(
    const std::vector<std::uint8_t> &fixture,
    const db::graph_hash::Digest &base)
{
    std::vector<std::uint8_t> grown = fixture;
    grown.push_back(0xAB);
    Expect(base != CaptureEnvelope(grown.data(), grown.size()),
        "appended bytes move the envelope digest");

    std::vector<std::uint8_t> headFlipped = fixture;
    headFlipped[4096] ^= 0xFF;
    Expect(base != CaptureEnvelope(headFlipped.data(), headFlipped.size()),
        "head changes move the digest");

    std::vector<std::uint8_t> tailFlipped = fixture;
    tailFlipped[fixture.size() - 32] ^= 0xFF;
    Expect(base != CaptureEnvelope(tailFlipped.data(), tailFlipped.size()),
        "changes beyond the v1 probe window move the digest");
}

// zlib detection: the synthesized fixture is a zlib stream; the same bytes
// re-headed as an IWD-like archive header are not.
void ExpectZlibDetection(const std::vector<std::uint8_t> &fixture)
{
    Expect(DetectZlibStream(fixture.data(), fixture.size()),
        "synthesized fixture is detected as a zlib stream");
    std::vector<std::uint8_t> notZlib = fixture;
    notZlib[0] = 'I'; // e.g. an IWD archive header
    notZlib[1] = 'W';
    Expect(!DetectZlibStream(notZlib.data(), notZlib.size()),
        "non-zlib headers are rejected");
}

// The contract output digest is exactly 64 lowercase hex characters.
bool DigestHexIsCanonical(const char *hex)
{
    if (std::string_view(hex).size() != 64)
        return false;
    for (const char *c = hex; *c; ++c)
        if ((*c < '0' || *c > '9') && (*c < 'a' || *c > 'f'))
            return false;
    return true;
}

void TestSelfTest(const char *fixturePath)
{
    const std::vector<std::uint8_t> fixture = SynthesizeZlibFixture();

    if (!WriteFileBytes(fixturePath, fixture.data(), fixture.size()))
    {
        std::fprintf(stderr, "FAIL: cannot write self-test fixture '%s'\n", fixturePath);
        ++g_failures;
        return;
    }

    std::vector<std::uint8_t> readBack;
    Expect(ReadFileBytes(fixturePath, readBack), "self-test fixture reads back");
    Expect(readBack == fixture, "self-test fixture round-trips byte-identically");

    const db::graph_hash::Digest first = CaptureEnvelope(readBack.data(), readBack.size());
    const db::graph_hash::Digest second = CaptureEnvelope(readBack.data(), readBack.size());
    Expect(first == second, "envelope capture is deterministic");

    ExpectDigestSensitivity(fixture, first);
    ExpectZlibDetection(fixture);

    // Output formatting: exactly the contract lines, hex lowercase.
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(first, hex);
    Expect(DigestHexIsCanonical(hex), "digest hex is 64 lowercase characters");
    std::printf("self_test_graph_sha256=%s\n", hex);

    std::remove(fixturePath);
}

void PrintUsage()
{
    std::fprintf(stderr,
        "Usage:\n"
        "  retail_fastfile_parity_harness --fastfile <path>\n"
        "  retail_fastfile_parity_harness --self-test [fixture-path]\n");
}

} // namespace

int main(int argc, char **argv)
{
    const char *fastfilePath = nullptr;
    bool selfTest = false;
    const char *selfTestPath = "kisakcod-parity-selftest.tmp";

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--fastfile" && i + 1 < argc)
        {
            fastfilePath = argv[++i];
        }
        else if (arg == "--self-test")
        {
            selfTest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                selfTestPath = argv[++i];
        }
        else
        {
            std::fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            PrintUsage();
            return 2;
        }
    }

    if (selfTest)
    {
        TestSelfTest(selfTestPath);
        if (g_failures > 0)
        {
            std::fprintf(stderr, "retail-fastfile-parity-harness self-test: %d failure(s)\n",
                g_failures);
            return 1;
        }
        std::printf("retail-fastfile-parity-harness self-test: all checks passed\n");
        return 0;
    }

    if (!fastfilePath)
    {
        PrintUsage();
        return 2;
    }

    return RunCapture(fastfilePath);
}
