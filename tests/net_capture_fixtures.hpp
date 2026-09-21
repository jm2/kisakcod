// Commercial-reference capture fixtures -- manifest, variable-field spec and
// masked byte-compare (issue #127 / ki-dyqxl, governed by #122 and
// docs/NETWORK_COMPATIBILITY.md).
//
// This subsystem is the certification gate the wire-contract harness was
// built for: production MSG encoders/decoders are byte-compared against
// reference captures taken from the UNMODIFIED commercial binaries pinned in
// #122 (commercial 1.7 and Steam commercial 1.8). Two doctrines are enforced
// here:
//
//   1. FAIL CLOSED. Missing or malformed reference evidence is a blocker to
//      certification -- it never passes and it is never silently skipped
//      (the certification run reports the blocker on stderr and exits with
//      CERTIFICATION_BLOCKED_EXIT, which CMake registers as SKIP_RETURN_CODE
//      so the ctest summary shows a named "Not Run", not a pass).
//
//   2. NO FORK BASELINES. Only captures recorded from the commercial
//      references may live under fixtures/netcaptures/<profile>/. Bytes
//      produced by KisakCOD itself are never accepted as a baseline; the
//      in-tree contracts derive their goldens from the decompiled retail
//      codec instead.
//
// Variable capture fields (challenge values, sequence numbers, qport,
// timestamps, ...) are declared EXPLICITLY in each capture's manifest
// section. A capture whose kind mandates variable declarations but omits
// them is malformed. Comparisons mask exactly the declared spans on BOTH
// sides, so a genuine serialization drift in invariant bytes still fails.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace netcapture
{

// Distinct exit code so CMake's SKIP_RETURN_CODE can classify "reference
// evidence missing" separately from a test failure (1) or a pass (0).
constexpr int CERTIFICATION_BLOCKED_EXIT = 77;

struct VariableField
{
    std::string name;
    std::size_t byte; // offset of the variable span within the capture
    std::size_t len;  // span length in bytes
};

struct CaptureSpec
{
    std::string file;
    std::string kind;
    std::string verify; // "encode" or "decode-reencode"
    std::vector<VariableField> variables;
    // name = value inputs recorded by the operator for "encode" captures
    // (hex or decimal integers, or hex byte strings suffixed _hex).
    std::vector<std::pair<std::string, std::string>> inputs;
    std::string notes;
};

struct CaptureManifest
{
    std::string profile;
    int formatVersion = 0;
    std::string sourceBuild;
    std::string sanitizedBy;
    std::vector<CaptureSpec> captures;
    std::string error; // non-empty <=> malformed
};

// The explicit per-kind rule table. `requiredVars` names the variable
// capture fields every capture of this kind MUST declare (mirrored in
// tests/fixtures/netcaptures/README.md); `engineOnly` kinds are enforced
// only where the production netchan TUs link (Win32 ILP32 engine target).
struct KindRule
{
    const char *kind;
    const char *verify;                  // expected verify mode
    const char *const *requiredVars;     // mandated variable declarations
    std::size_t requiredVarCount;
    bool engineOnly;
};

// Variable capture fields of the codec-level capture kinds. The captures
// pin the production codec over operator-recorded fixed inputs, so the
// spans below document WHERE capture-dependent values sit in the bytes
// even though the recorded inputs make the encoding deterministic.
constexpr const char *kScalarSequenceVars[] = {"sequence", "acknowledge"};
constexpr const char *kUsercmdDeltaVars[] = {"key", "from_hex"};

constexpr KindRule kKindRules[] = {
    {"scalar-sequence", "encode", kScalarSequenceVars, 2, false},
    {"huffman-block", "encode", nullptr, 0, false},
    {"usercmd-delta", "decode-reencode", kUsercmdDeltaVars, 2, false},
};

constexpr std::size_t kKindRuleCount = sizeof(kKindRules) / sizeof(kKindRules[0]);

const KindRule *ruleForKind(const std::string &kind);

// Parse the manifest text. Any structural defect (unknown key, malformed
// span, missing kind, unknown kind, verify/kind mismatch, missing mandated
// variable declarations, duplicate capture file) yields ok==false with a
// precise error; the caller reports it as a certification blocker.
CaptureManifest parseManifest(const std::string &profile, const std::string &text);

// Reference-evidence root: $KISAKCOD_NETCAPTURES_DIR if set (and non-empty),
// otherwise the in-tree fixtures directory. Returns false only if neither
// can be located; the caller reports that as a blocker.
bool fixturesRoot(std::string &outRoot);

// Parse "name@byte:len" (byte/len decimal). Returns false on syntax errors.
bool parseVariableField(const std::string &spec, VariableField &out);

// Hex text (even length, 0..N) to bytes. Returns false on bad syntax.
bool parseHexBytes(const std::string &hex, std::vector<std::uint8_t> &out);

struct MaskedDiff
{
    bool ok;
    std::size_t byte;  // first differing byte (after masking)
    std::size_t bit;   // first differing bit within that byte
    std::string where; // "(variable <name>)" when the drift boundary touches
                       // a declared span, otherwise the invariant offset
};

// Byte-compare expected vs actual after masking the declared variable spans
// on BOTH buffers. A size difference is itself a first difference. `where`
// names a declared variable when the first invariant difference sits at or
// inside a masked span boundary on one side only (a misdeclared variable) --
// those fail too: the declaration must describe the capture faithfully.
MaskedDiff compareMasked(const std::vector<std::uint8_t> &expected,
                         const std::vector<std::uint8_t> &actual,
                         const std::vector<VariableField> &variables);

} // namespace netcapture

// Gate self-tests: manifest parser, variable-field spec parsing, hex input
// parsing and masked byte-compare. Run in the normal suite so a broken gate
// is a test FAILURE, not a silent skip.
void run_capture_subsystem_selftests();

// Round-trip contracts over the linked production codec (all targets).
void run_net_capture_roundtrip_contracts();

// Certification gate: `kisakcod-net-wire-contract-tests capture-certification`.
// Exit 0 = every required profile present, well-formed and byte-verified;
// exit 77 = reference evidence missing/incomplete (BLOCKED, reported);
// exit 1 = evidence present but a verification FAILED (retail drift).
int run_capture_certification();
