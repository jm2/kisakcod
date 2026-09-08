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
//   platform=<derived build/runtime identity of THIS executable>
//   leg=<declared capture-leg identity, when --leg is given and validated>
//   fastfile_path=<path>
//   fastfile_bytes=<decimal size>
//   fastfile_zlib_stream=<0|1>
//   graph_sha256=<64 hex characters>
//
// capture_kind envelope-v2 covers the fast-file envelope identity: size,
// zlib stream header bits, and the FULL file content. (The v1 envelope
// minted only a bounded 1 MiB prefix probe, which let same-size assets
// whose tails differ collide on graph_sha256; v2 hashes every byte and
// bumps both the capture kind and the hash domain so v1 references can
// never silently match v2 captures.) The envelope capture hashes fast-file
// BYTES only — no runtime graph is loaded — so an envelope match is
// envelope consistency, never runtime-graph parity; the parity driver
// reports the result under the capture kind's own name. The full widened
// runtime-graph walk (capture_kind graph-v1) enrolls through this identical
// harness contract and output protocol as the native64 loader path lands;
// the parity driver refuses to compare different capture kinds, so the
// contract cannot silently drift.
//
// CAPTURE IDENTITY is derived, never asserted: `platform` comes from this
// executable's own compile-time target (preprocessor defines are the
// toolchain's statement of what it built) cross-checked against the running
// kernel where the platform can observe it. --leg is an assertion that the
// capture belongs to a named leg; it is accepted ONLY when it matches the
// derived identity, so a Linux-built binary cannot stamp leg=windows-x86
// onto its capture, and a capture cannot be minted under a label its own
// executable contradicts. The parity driver re-verifies the emitted
// identity against the requested --host/--ref triples before minting or
// comparing, so a reference minted by a foreign tree cannot pose as
// another leg's reference.
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
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <sys/utsname.h>
#endif

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

// The leg identity becomes one line of the k=v output protocol, so it must
// be a short printable token: no whitespace, control characters, or
// non-ASCII bytes that could break the line-oriented contract.
bool LegIdentityIsSafe(const std::string_view leg)
{
    if (leg.empty() || leg.size() > 64)
        return false;
    for (const char c : leg)
        if (c <= ' ' || c >= '\x7F')
            return false;
    return true;
}

// Compile-time build identity: the platform this executable was built FOR.
// The preprocessor defines are the toolchain's own record of its target;
// they cannot be changed at runtime, so they anchor the capture identity.
std::string BuildPlatformIdentity()
{
#if defined(_WIN32)
    #if defined(_M_X64) || defined(__x86_64__)
        return "windows-amd64";
    #elif defined(_M_IX86) || defined(__i386__)
        return "windows-x86";
    #elif defined(_M_ARM64) || defined(__aarch64__)
        return "windows-arm64";
    #else
        return "windows-unknown";
    #endif
#elif defined(__APPLE__)
    #if defined(__aarch64__)
        return "darwin-arm64";
    #elif defined(__x86_64__)
        return "darwin-amd64";
    #else
        return "darwin-unknown";
    #endif
#elif defined(__linux__)
    #if defined(__x86_64__)
        return "linux-amd64";
    #elif defined(__aarch64__)
        return "linux-arm64";
    #elif defined(__i386__)
        return "linux-x86";
    #else
        return "linux-unknown";
    #endif
#else
    return "unknown-platform";
#endif
}

#if !defined(_WIN32)
// Runtime kernel identity from uname(3), mapped onto the same vocabulary as
// the build identity. Returns false when the running kernel is not a
// recognized parity-leg platform (the build identity then stands alone).
bool RuntimePlatformIdentity(std::string &out)
{
    utsname info;
    if (uname(&info) != 0)
        return false;
    const std::string_view sysname = info.sysname;
    const std::string_view machine = info.machine;
    if (sysname == "Linux")
        out = "linux-";
    else if (sysname == "Darwin")
        out = "darwin-";
    else
        return false;
    if (machine == "x86_64" || machine == "amd64")
        out += "amd64";
    else if (machine == "aarch64" || machine == "arm64")
        out += "arm64";
    else if (machine == "i386" || machine == "i686")
        out += "x86";
    else
        return false;
    return true;
}
#endif

// The capture identity of THIS execution: the build identity, verified
// against the running kernel where the platform can observe it. Returns
// false under foreign execution (e.g. an amd64 build running under CPU
// emulation on an arm64 kernel) — there the executable cannot honestly
// attribute its capture to any leg, and refusing is the fail-closed path.
bool DerivePlatformIdentity(std::string &out)
{
    out = BuildPlatformIdentity();
#if !defined(_WIN32)
    std::string runtime;
    if (RuntimePlatformIdentity(runtime) && runtime != out)
        return false;
#endif
    return true;
}

// Renders the stable output protocol into `out`. `platform` is the derived
// identity (always emitted); `leg` may be null/empty (no --leg given). The
// parity driver verifies both against the requested --host/--ref triples
// before trusting a mint or a compare.
void FormatCapture(
    const char *path,
    const std::vector<std::uint8_t> &bytes,
    const db::graph_hash::Digest &digest,
    const std::string_view platform,
    const std::string_view leg,
    std::string &out)
{
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(digest, hex);
    out.clear();
    out += "capture_kind=envelope-v2\n";
    out += "hash_domain=";
    out += db::graph_hash::kHashDomain;
    out += '\n';
    out += "platform=";
    out += platform;
    out += '\n';
    if (!leg.empty())
    {
        out += "leg=";
        out += leg;
        out += '\n';
    }
    out += "fastfile_path=";
    out += path;
    out += '\n';
    out += "fastfile_bytes=";
    out += std::to_string(bytes.size());
    out += '\n';
    out += "fastfile_zlib_stream=";
    out += DetectZlibStream(bytes.data(), bytes.size()) ? '1' : '0';
    out += '\n';
    out += "graph_sha256=";
    out += hex;
    out += '\n';
}

void EmitCapture(
    const char *path,
    const std::vector<std::uint8_t> &bytes,
    const db::graph_hash::Digest &digest,
    const std::string_view platform,
    const std::string_view leg)
{
    std::string out;
    FormatCapture(path, bytes, digest, platform, leg, out);
    std::fputs(out.c_str(), stdout);
}

int RunCapture(const char *path, const std::string_view leg)
{
    // Identity first: a capture that cannot be attributed is not emitted.
    std::string platform;
    if (!DerivePlatformIdentity(platform))
    {
        std::fprintf(stderr,
            "error: this harness build (%s) is executing on a different"
            " platform; capture identity cannot be established\n",
            BuildPlatformIdentity().c_str());
        return 2;
    }
    // --leg is an assertion about this executable's identity. It is checked
    // against the derived build/runtime identity, never stamped verbatim:
    // a Linux build cannot mint a leg=windows-x86 capture.
    if (!leg.empty() && leg != platform)
    {
        std::fprintf(stderr,
            "error: --leg '%.*s' does not match this executable's platform"
            " identity '%s'; a foreign leg label cannot be stamped onto"
            " this capture\n",
            static_cast<int>(leg.size()), leg.data(), platform.c_str());
        return 2;
    }

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

    EmitCapture(path, bytes, digest, platform, leg);
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

// Protocol contract: the envelope kind is NAMED as envelope (the parity
// driver must never report an envelope match as runtime-graph parity), the
// derived platform identity is always present, and the leg identity line
// appears exactly when --leg declares one.
void ExpectProtocolContract(
    const std::vector<std::uint8_t> &fixture,
    const db::graph_hash::Digest &digest)
{
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(digest, hex);
    Expect(DigestHexIsCanonical(hex), "digest hex is 64 lowercase characters");
    std::printf("self_test_graph_sha256=%s\n", hex);

    std::string protocol;
    FormatCapture("fixture.ff", fixture, digest, "linux-amd64", "linux-amd64", protocol);
    Expect(protocol.find("capture_kind=envelope-v2\n") != std::string::npos,
        "protocol names the envelope capture kind explicitly");
    Expect(protocol.find("\nplatform=linux-amd64\n") != std::string::npos,
        "protocol carries the derived platform identity");
    Expect(protocol.find("\nleg=linux-amd64\n") != std::string::npos,
        "protocol carries the declared leg identity");
    Expect(protocol.find("graph_sha256=" + std::string(hex)) != std::string::npos,
        "protocol carries the capture digest");

    std::string noLeg;
    FormatCapture("fixture.ff", fixture, digest, "linux-amd64", {}, noLeg);
    Expect(noLeg.find("\nplatform=linux-amd64\n") != std::string::npos,
        "platform identity is emitted even without a declared leg");
    Expect(noLeg.find("leg=") == std::string::npos,
        "protocol omits the leg line when no leg is declared");

    // Derived identity on the CI host must resolve and match the runtime
    // kernel (the driver-gates test additionally exercises the refusal
    // paths through the real binary).
    std::string platform;
    Expect(DerivePlatformIdentity(platform), "platform identity resolves on this host");
    Expect(LegIdentityIsSafe(platform), "derived identity is a safe protocol token");
    Expect(LegIdentityIsSafe("linux-amd64"), "triple-shaped leg identities are accepted");
    Expect(!LegIdentityIsSafe(""), "empty leg identities are rejected");
    Expect(!LegIdentityIsSafe("linux amd64"), "leg identities with whitespace are rejected");
    Expect(!LegIdentityIsSafe("leg\ninjected"), "leg identities with newlines are rejected");
    Expect(!LegIdentityIsSafe(std::string(65, 'x')), "overlong leg identities are rejected");
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
    ExpectProtocolContract(fixture, first);

    std::remove(fixturePath);
}

void PrintUsage()
{
    std::fprintf(stderr,
        "Usage:\n"
        "  retail_fastfile_parity_harness [--leg <identity>] --fastfile <path>\n"
        "  retail_fastfile_parity_harness --self-test [fixture-path]\n"
        "--leg asserts the capture-leg identity; it is accepted only when it\n"
        "matches the platform identity this executable derives from its own\n"
        "build/runtime, so a foreign label cannot be stamped onto a capture.\n");
}

struct HarnessOptions
{
    const char *fastfilePath = nullptr;
    std::string leg;
    bool selfTest = false;
    const char *selfTestPath = "kisakcod-parity-selftest.tmp";
};

// Parses argv into `opts`. Returns the process exit code to use when
// parsing fails (2), or 0 to continue into the selected mode.
int ParseHarnessArgs(const int argc, char **argv, HarnessOptions &opts)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--fastfile" && i + 1 < argc)
        {
            opts.fastfilePath = argv[++i];
        }
        else if (arg == "--leg" && i + 1 < argc)
        {
            opts.leg = argv[++i];
            if (!LegIdentityIsSafe(opts.leg))
            {
                std::fprintf(stderr,
                    "error: --leg must be 1-64 printable, non-whitespace characters"
                    " (got %zu bytes)\n",
                    opts.leg.size());
                return 2;
            }
        }
        else if (arg == "--self-test")
        {
            opts.selfTest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                opts.selfTestPath = argv[++i];
        }
        else
        {
            std::fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            PrintUsage();
            return 2;
        }
    }
    return 0;
}

int RunSelfTest(const char *fixturePath)
{
    TestSelfTest(fixturePath);
    if (g_failures > 0)
    {
        std::fprintf(stderr, "retail-fastfile-parity-harness self-test: %d failure(s)\n",
            g_failures);
        return 1;
    }
    std::printf("retail-fastfile-parity-harness self-test: all checks passed\n");
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    HarnessOptions opts;
    const int parseStatus = ParseHarnessArgs(argc, argv, opts);
    if (parseStatus != 0)
        return parseStatus;

    if (opts.selfTest)
        return RunSelfTest(opts.selfTestPath);

    if (!opts.fastfilePath)
    {
        PrintUsage();
        return 2;
    }
    return RunCapture(opts.fastfilePath, opts.leg);
}
