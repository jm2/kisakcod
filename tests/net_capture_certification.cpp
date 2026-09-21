// Commercial-reference capture certification runner (issue #127 / ki-dyqxl).
//
// `kisakcod-net-wire-contract-tests capture-certification` runs the
// fail-closed certification gate of docs/NETWORK_COMPATIBILITY.md over the
// reference evidence under fixtures/netcaptures/<profile>/:
//
//   * every required profile must exist and carry a well-formed manifest,
//   * every mandated capture kind must be present,
//   * "encode" captures are re-encoded from the manifest's recorded fixed
//     inputs with the production codec and byte-compared,
//   * "decode-reencode" captures are decoded and re-encoded with the
//     production codec and byte-compared under the declared variable
//     capture fields,
//   * any drift is reported as first differing invariant byte + bit.
//
// Missing/incomplete evidence exits CERTIFICATION_BLOCKED_EXIT (registered
// as the ctest SKIP_RETURN_CODE so the summary shows a named "Not Run").
// Present-but-divergent evidence exits 1: that is retail drift, a FAILURE.
// Only a complete, matching evidence set exits 0 (certification pass).
//
// This run certifies wire format equivalence only; it is not a merge
// approval and does not substitute for the session-layer certification in
// the compatibility document.

#include "net_capture_fixtures.hpp"
#include "msg_wire_test_harness.hpp"

#include <qcommon/msg_mp.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{

using namespace netcapture;

const char *const kRequiredProfiles[] = {"commercial-1.7", "commercial-1.8"};

struct Blocker
{
    std::string profile;
    std::string detail;
};

void reportBlockers(const std::vector<Blocker> &blockers)
{
    std::fprintf(stderr,
                 "\n"
                 "============================================================\n"
                 " COMMERCIAL-REFERENCE CERTIFICATION BLOCKED\n"
                 "============================================================\n"
                 " The wire contracts cannot certify commercial equivalence\n"
                 " without reference evidence. Missing evidence is a blocker\n"
                 " to certification, never a passing or silently skipped\n"
                 " result (docs/NETWORK_COMPATIBILITY.md).\n\n");
    for (const Blocker &b : blockers)
        std::fprintf(stderr, " BLOCKER [%s]: %s\n", b.profile.c_str(), b.detail.c_str());
    std::fprintf(stderr,
                 "\n Capture and manifest procedures:\n"
                 "   tests/fixtures/netcaptures/README.md\n"
                 "============================================================\n\n");
}

bool readFileBytes(const std::string &path, std::vector<std::uint8_t> &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0)
        return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0)
        in.read(reinterpret_cast<char *>(out.data()), size);
    return in.good() || in.eof();
}

bool parseSignedInt(const std::string &text, long long &out)
{
    if (text.empty())
        return false;
    char *end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 0); // base 0: 0x / decimal
    if (end != text.c_str() + text.size())
        return false;
    out = value;
    return true;
}

const std::string *findInput(const CaptureSpec &spec, const char *name)
{
    for (const auto &input : spec.inputs)
        if (input.first == name)
            return &input.second;
    return nullptr;
}

// -- per-kind verification -------------------------------------------------

bool verifyScalarSequence(const CaptureSpec &spec, const std::vector<std::uint8_t> &captured,
                          MaskedDiff &diff, std::string &error)
{
    const std::string *sequenceText = findInput(spec, "sequence");
    const std::string *acknowledgeText = findInput(spec, "acknowledge");
    if (!sequenceText || !acknowledgeText)
    {
        error = "scalar-sequence requires recorded inputs 'sequence' and 'acknowledge'";
        return false;
    }
    long long sequence = 0;
    long long acknowledge = 0;
    if (!parseSignedInt(*sequenceText, sequence) || !parseSignedInt(*acknowledgeText, acknowledge))
    {
        error = "scalar-sequence inputs must be integers (decimal or 0x hex)";
        return false;
    }

    std::uint8_t storage[16];
    msg_t msg;
    MSG_Init(&msg, storage, static_cast<int>(sizeof(storage)));
    MSG_WriteLong(&msg, static_cast<int>(sequence));
    MSG_WriteLong(&msg, static_cast<int>(acknowledge));

    const std::vector<std::uint8_t> encoded(storage, storage + msg.cursize);
    diff = compareMasked(captured, encoded, spec.variables);
    return diff.ok;
}

bool verifyHuffmanBlock(const CaptureSpec &spec, const std::vector<std::uint8_t> &captured,
                        MaskedDiff &diff, std::string &error)
{
    const std::string *payloadHex = findInput(spec, "payload_hex");
    if (!payloadHex)
    {
        error = "huffman-block requires recorded input 'payload_hex'";
        return false;
    }
    std::vector<std::uint8_t> payload;
    if (!parseHexBytes(*payloadHex, payload) || payload.empty())
    {
        error = "huffman-block input 'payload_hex' must be non-empty hex";
        return false;
    }

    std::vector<std::uint8_t> encoded(payload.size() + 64, 0);
    const int encodedSize = MSG_WriteBitsCompress(true, payload.data(), static_cast<int>(payload.size()),
                                                  encoded.data(), static_cast<int>(encoded.size()));
    if (encodedSize <= 0)
    {
        error = "production MSG_WriteBitsCompress rejected the recorded payload";
        return false;
    }
    encoded.resize(static_cast<std::size_t>(encodedSize));

    diff = compareMasked(captured, encoded, spec.variables);
    return diff.ok;
}

bool verifyUsercmdDelta(const CaptureSpec &spec, const std::vector<std::uint8_t> &captured,
                        MaskedDiff &diff, std::string &error)
{
    const std::string *keyText = findInput(spec, "key");
    const std::string *fromHex = findInput(spec, "from_hex");
    if (!keyText || !fromHex)
    {
        error = "usercmd-delta requires recorded inputs 'key' and 'from_hex'";
        return false;
    }
    long long key = 0;
    if (!parseSignedInt(*keyText, key))
    {
        error = "usercmd-delta input 'key' must be an integer";
        return false;
    }
    std::vector<std::uint8_t> fromBytes;
    if (!parseHexBytes(*fromHex, fromBytes) || fromBytes.size() != sizeof(usercmd_s))
    {
        error = "usercmd-delta input 'from_hex' must be exactly 32 bytes (sizeof usercmd_s)";
        return false;
    }

    usercmd_s from{};
    std::memcpy(&from, fromBytes.data(), sizeof(from));

    msg_t reader;
    MSG_InitReadOnly(&reader, const_cast<std::uint8_t *>(captured.data()),
                     static_cast<int>(captured.size()));
    usercmd_s decoded{};
    MSG_ReadDeltaUsercmdKey(&reader, static_cast<int>(key), &from, &decoded);

    std::uint8_t storage[128];
    msg_t reencoded;
    MSG_Init(&reencoded, storage, static_cast<int>(sizeof(storage)));
    MSG_WriteDeltaUsercmdKey(&reencoded, static_cast<int>(key), &from, &decoded);

    if (reencoded.cursize != static_cast<int>(captured.size()))
    {
        error = "re-encoded delta length differs from capture (decoded state may be corrupt)";
        return false;
    }
    const std::vector<std::uint8_t> reencodedBytes(storage, storage + reencoded.cursize);
    diff = compareMasked(captured, reencodedBytes, spec.variables);
    return diff.ok;
}

bool verifyCapture(const CaptureSpec &spec, const std::vector<std::uint8_t> &captured,
                   MaskedDiff &diff, std::string &error)
{
    if (spec.kind == "scalar-sequence")
        return verifyScalarSequence(spec, captured, diff, error);
    if (spec.kind == "huffman-block")
        return verifyHuffmanBlock(spec, captured, diff, error);
    if (spec.kind == "usercmd-delta")
        return verifyUsercmdDelta(spec, captured, diff, error);
    error = "no verification handler for kind";
    return false;
}

// -- subsystem self-tests (run in the normal suite, not only in the gate) --

void selftestVariableFieldParsing()
{
    VariableField field;
    CHECK(parseVariableField("challenge@12:4", field));
    CHECK(field.name == "challenge" && field.byte == 12 && field.len == 4);

    CHECK(!parseVariableField("nocolon", field));
    CHECK(!parseVariableField("@0:4", field));
    CHECK(!parseVariableField("name@:4", field));
    CHECK(!parseVariableField("name@4:", field));
    CHECK(!parseVariableField("name@x:4", field));
    CHECK(!parseVariableField("name@4:y", field));
}

void selftestHexParsing()
{
    std::vector<std::uint8_t> bytes;
    CHECK(parseHexBytes("00ff10", bytes));
    CHECK(bytes.size() == 3 && bytes[0] == 0x00 && bytes[1] == 0xFF && bytes[2] == 0x10);
    CHECK(parseHexBytes("", bytes) && bytes.empty());
    CHECK(!parseHexBytes("0", bytes));
    CHECK(!parseHexBytes("zz", bytes));
    CHECK(!parseHexBytes("0g", bytes));
}

void selftestManifestParsing()
{
    const char *good =
        "# comment\n"
        "format_version = 1\n"
        "source_build = commercial 1.7 retail (operator recorded)\n"
        "sanitized_by = operator, 2026-09-20\n"
        "\n"
        "capture = 01-scalar.bin\n"
        "kind = scalar-sequence\n"
        "verify = encode\n"
        "input = sequence = 0x1A2B3C4D\n"
        "input = acknowledge = 517\n"
        "var = sequence@0:4\n"
        "var = acknowledge@4:4\n"
        "notes = netchan message front longs\n";

    CaptureManifest manifest = parseManifest("commercial-1.7", good);
    CHECK(manifest.error.empty());
    CHECK(manifest.formatVersion == 1);
    CHECK(manifest.captures.size() == 1);
    CHECK(manifest.captures[0].kind == "scalar-sequence");
    CHECK(manifest.captures[0].inputs.size() == 2);
    CHECK(manifest.captures[0].variables.size() == 2);

    struct Mutation
    {
        const char *what;
        std::string text;
    };
    std::vector<Mutation> bad;
    bad.push_back({"missing format_version", "source_build = x\nsanitized_by = y\ncapture = f\nkind = scalar-sequence\nverify = encode\nvar = sequence@0:4\nvar = acknowledge@4:4\n"});
    bad.push_back({"missing provenance", "format_version = 1\nsanitize = y\ncapture = f\nkind = scalar-sequence\nverify = encode\nvar = sequence@0:4\nvar = acknowledge@4:4\n"});
    {
        std::string text = good;
        text.replace(text.find("kind = scalar-sequence"), 21, "kind = mystery-kind");
        bad.push_back({"unknown kind", text});
    }
    {
        std::string text = good;
        text.replace(text.find("verify = encode"), 15, "verify = eyeball");
        bad.push_back({"wrong verify mode", text});
    }
    {
        std::string text = good;
        text.replace(text.find("var = acknowledge@4:4"), 21, "var = other@4:4");
        bad.push_back({"missing mandated variable", text});
    }
    {
        std::string text = good;
        text.append("capture = 01-scalar.bin\nkind = scalar-sequence\nverify = encode\nvar = sequence@0:4\nvar = acknowledge@4:4\n");
        bad.push_back({"duplicate capture file", text});
    }
    bad.push_back({"unknown key", "format_version = 1\nmystery_key = 3\n"});

    for (const Mutation &m : bad)
    {
        CaptureManifest broken = parseManifest("commercial-1.7", m.text);
        CHECK(!broken.error.empty());
        (void)m.what;
    }
}

void selftestMaskedCompare()
{
    const std::vector<std::uint8_t> base = {0x01, 0x02, 0x03, 0x04, 0x05};
    const std::vector<VariableField> vars = {{"seq", 1, 2}};

    MaskedDiff diff = compareMasked(base, base, vars);
    CHECK(diff.ok);

    std::vector<std::uint8_t> variableDrift = base;
    variableDrift[1] = 0xAA;
    variableDrift[2] ^= 0xFF;
    diff = compareMasked(base, variableDrift, vars);
    CHECK(diff.ok);

    std::vector<std::uint8_t> invariantDrift = base;
    invariantDrift[3] = 0x07; // low bit flips first (bit 0)
    diff = compareMasked(base, invariantDrift, vars);
    CHECK(!diff.ok);
    CHECK(diff.byte == 3 && diff.bit == 0);

    std::vector<std::uint8_t> boundaryDrift = base;
    boundaryDrift[3] = 0xFF; // first byte after the masked span
    diff = compareMasked(base, boundaryDrift, vars);
    CHECK(!diff.ok);

    std::vector<std::uint8_t> longer = base;
    longer.push_back(0x06);
    diff = compareMasked(base, longer, vars);
    CHECK(!diff.ok);
}

} // namespace

void run_capture_subsystem_selftests()
{
    selftestVariableFieldParsing();
    selftestHexParsing();
    selftestManifestParsing();
    selftestMaskedCompare();
}

int run_capture_certification()
{
    std::string root;
    std::vector<Blocker> blockers;

    if (!fixturesRoot(root))
    {
        Blocker b;
        b.profile = "*";
        b.detail = "no reference-evidence directory compiled in and KISAKCOD_NETCAPTURES_DIR is unset";
        blockers.push_back(b);
        reportBlockers(blockers);
        return CERTIFICATION_BLOCKED_EXIT;
    }

    for (const char *profileName : kRequiredProfiles)
    {
        const std::string profile = profileName;
        const std::string manifestPath = root + "/" + profile + "/MANIFEST.txt";

        std::vector<std::uint8_t> manifestBytes;
        if (!readFileBytes(manifestPath, manifestBytes))
        {
            Blocker b;
            b.profile = profile;
            b.detail = "missing " + manifestPath;
            blockers.push_back(b);
            continue;
        }
        manifestBytes.push_back(0); // NUL-terminate for string construction

        const CaptureManifest manifest = parseManifest(profile,
                                                       reinterpret_cast<const char *>(manifestBytes.data()));
        if (!manifest.error.empty())
        {
            Blocker b;
            b.profile = profile;
            b.detail = "malformed MANIFEST.txt: " + manifest.error;
            blockers.push_back(b);
            continue;
        }

        // Every mandated kind must be represented at least once.
        for (std::size_t r = 0; r < kKindRuleCount; ++r)
        {
            bool present = false;
            for (const CaptureSpec &spec : manifest.captures)
                if (spec.kind == kKindRules[r].kind)
                    present = true;
            if (!present)
            {
                Blocker b;
                b.profile = profile;
                b.detail = std::string("no capture of required kind '") + kKindRules[r].kind + "'";
                blockers.push_back(b);
            }
        }
        if (!blockers.empty())
            continue;

        for (const CaptureSpec &spec : manifest.captures)
        {
            const std::string capturePath = root + "/" + profile + "/" + spec.file;
            std::vector<std::uint8_t> captured;
            if (!readFileBytes(capturePath, captured) || captured.empty())
            {
                Blocker b;
                b.profile = profile;
                b.detail = "capture file missing or empty: " + capturePath;
                blockers.push_back(b);
                continue;
            }

            MaskedDiff diff{};
            std::string error;
            if (!verifyCapture(spec, captured, diff, error))
            {
                if (error.empty())
                {
                    error = "WIRE DRIFT in " + spec.file + " at " + diff.where
                            + " (byte " + std::to_string(diff.byte) + ", bit " + std::to_string(diff.bit)
                            + "): production codec diverges from the commercial reference";
                }
                std::fprintf(stderr, "FAIL [%s] %s: %s\n", profile.c_str(), spec.file.c_str(),
                             error.c_str());
                blockers.push_back(Blocker{profile, spec.file + ": verification failed (see FAIL line)"});
                continue;
            }
            std::fprintf(stdout, "ok   [%s] %s (%s, %zu bytes)\n", profile.c_str(), spec.file.c_str(),
                         spec.kind.c_str(), captured.size());
        }
    }

    if (!blockers.empty())
    {
        // Distinguish "evidence missing/malformed" (BLOCKED -> 77) from
        // "evidence present but divergent" (retail drift -> 1).
        for (const Blocker &b : blockers)
            if (b.detail.find("verification failed") == std::string::npos)
            {
                reportBlockers(blockers);
                return CERTIFICATION_BLOCKED_EXIT;
            }
        std::fprintf(stderr,
                     "\nCOMMERCIAL-REFERENCE CERTIFICATION FAILED: production codec "
                     "diverges from the commercial reference captures above.\n");
        return 1;
    }

    std::fprintf(stdout,
                 "\nCOMMERCIAL-REFERENCE CERTIFICATION PASS: production codec matches all "
                 "reference captures (wire-format equivalence only; not a merge approval).\n");
    return 0;
}
